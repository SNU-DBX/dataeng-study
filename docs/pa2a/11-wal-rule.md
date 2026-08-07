# 11 · Dirty pages and the one rule that outranks everything

*`FlushBuffer` is 100 lines. One of them is the reason recovery works.*

Modifying a page is three steps, always in this order: exclusive-lock the buffer, write the WAL
record, mark the buffer dirty and set its LSN. The LSN written into the page header is the position
in the WAL of the last record describing a change to that page. That number is what makes the
write-ahead rule checkable:

```c title="bufmgr.c:4284 — FlushBuffer, the load-bearing half"
static void
FlushBuffer(BufferDesc *buf, SMgrRelation reln, IOObject io_object, IOContext io_context)
{
    /* Claim the exclusive right to do I/O on this frame (sets BM_IO_IN_PROGRESS).
       If someone else beat us to it, there is nothing to do. */
    if (!StartBufferIO(buf, false, false))
        return;
    …
    if (reln == NULL)
        reln = smgropen(BufTagGetRelFileLocator(&buf->tag), INVALID_PROC_NUMBER);

    buf_state = LockBufHdr(buf);
    /* Run PageGetLSN while holding header lock, since we don't have the
       buffer locked exclusively in all cases. */
    recptr = BufferGetLSN(buf);
    buf_state &= ~BM_JUST_DIRTIED;
    UnlockBufHdr(buf, buf_state);

    /*
     * Force XLOG flush up to buffer's LSN.  This implements the basic WAL
     * rule that log updates must hit disk before any of the data-file changes
     * they describe do.
     */
    if (buf_state & BM_PERMANENT)
        XLogFlush(recptr);                    /* ◀── THE RULE */

    bufBlock = BufHdrGetBlock(buf);

    /* Since we have only shared lock on the buffer, other processes might be
       updating hint bits in it, so we must copy the page if we do checksumming. */
    bufToWrite = PageSetChecksumCopy((Page) bufBlock, buf->tag.blockNum);

    smgrwrite(reln, BufTagGetForkNum(&buf->tag), buf->tag.blockNum, bufToWrite, false);

    /* Mark clean (unless BM_JUST_DIRTIED became set) and end BM_IO_IN_PROGRESS. */
    TerminateBufferIO(buf, true, 0, true, false);
}
```

```text
   TIME ────────────────────────────────────────────────────────────────▶

   backend:   LockBuffer(EXCLUSIVE)
              XLogInsert(record)        ──▶ WAL buffer,  returns LSN 0/3A21C8
              PageSetLSN(page, LSN)
              MarkBufferDirty(buf)
              UnlockBuffer()
                                                        ┊
   flusher:                                    FlushBuffer(buf)
                                                  recptr = PageGetLSN = 0/3A21C8
                                                  XLogFlush(0/3A21C8)  ──▶ fsync'd WAL
                                                                          ▲
                                                  smgrwrite(page) ────────┘ only now
   ─────────────────────────────────────────────────────────────────────────
   CRASH HERE and the page may be half-written, but the WAL record that
   describes it is already durable ⇒ redo can reconstruct the page.
   Reverse the two and a crash loses the record for a change already on disk.
```

*Write-ahead logging in one diagram. Every buffer manager on earth has this ordering constraint
somewhere; in PostgreSQL it is those two lines of `FlushBuffer`.*

!!! lens "PA2a lens · you get WAL ordering for free"

    Your seal flushes dirty frames by calling `FlushBuffer()`, which already performs the
    `XLogFlush(lsn)`. You do not need to write any WAL logic, and you should not: the recovery test
    in the autograder exercises exactly this ordering, and it passes if and only if you went through
    `FlushBuffer`. Note the PG 18 signature has two arguments the 15.2 reference does not —
    `io_object` and `io_context`, both for I/O statistics.

??? question "`PageSetChecksumCopy` copies the whole 8 KB page before writing it. Why can't it just compute the checksum in place?"

    Because the flusher holds only a *shared* content lock, and shared does not exclude hint-bit
    updates — `MarkBufferDirtyHint` (bufmgr.c:5430) modifies pages under a shared lock by design,
    since a lost hint bit is harmless. But a byte changing after the checksum is computed and before
    the write completes is *not* harmless: the page would land on disk with a checksum that does not
    match its contents, and would be rejected as corrupt on the next read. So the page is
    snapshotted to private memory, checksummed there, and that copy is written. This is a nice
    example of a weak lock creating an obligation elsewhere.
