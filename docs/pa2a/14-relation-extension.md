# 14 · The second door: relation extension

*Frames are acquired in two places, not one. Miss the second and your pool silently overruns.*

[§06](06-read-path.md) said `BufferAlloc` is the single entry point for shared frames. That is true
for *reads*. Appending a new block to a table is not a read — the block does not exist yet, so there
is nothing to look up and nothing to read in. That path is `ExtendBufferedRelShared()`, and it calls
`GetVictimBuffer()` directly, **in a loop**, before `BufferAlloc` ever gets a say.

```c title="bufmgr.c:2605 — ExtendBufferedRelShared, trimmed"
static BlockNumber
ExtendBufferedRelShared(BufferManagerRelation bmr, ForkNumber fork,
                        BufferAccessStrategy strategy, uint32 flags,
                        uint32 extend_by, BlockNumber extend_upto,
                        Buffer *buffers, uint32 *extended_by)
{
    LimitAdditionalPins(&extend_by);

    /* Acquire victim buffers for extension WITHOUT holding the extension lock.
       Writing out victim buffers is the most expensive part of extending. */
    for (uint32 i = 0; i < extend_by; i++)
    {
        buffers[i] = GetVictimBuffer(strategy, io_context);   /* ← the second door */
        buf_block  = BufHdrGetBlock(GetBufferDescriptor(buffers[i] - 1));
        MemSet(buf_block, 0, BLCKSZ);        /* new buffers are zero-filled */
    }

    if (!(flags & EB_SKIP_EXTENSION_LOCK))
        LockRelationForExtension(bmr.rel, ExclusiveLock);

    first_block = smgrnblocks(bmr.smgr, fork);
    …
    /* Insert buffers into buffer table, mark as IO_IN_PROGRESS.
       This needs to happen BEFORE we extend the relation, because as soon as
       we do, other backends can start to read in those pages. */
    for (uint32 i = 0; i < extend_by; i++)
    {
        InitBufferTag(&tag, &bmr.smgr->smgr_rlocator.locator, fork, first_block + i);
        hash = BufTableHashCode(&tag);
        partition_lock = BufMappingPartitionLock(hash);

        LWLockAcquire(partition_lock, LW_EXCLUSIVE);
        existing_id = BufTableInsert(&tag, hash, victim_buf_hdr->buf_id);
        … collision handling, then smgrzeroextend() the file …
    }
}
```

!!! danger "Trap · the path that isn't reachable from `BufferAlloc`"

    If you put your sequential allocator and your threshold check *only* in `BufferAlloc`, then any
    `INSERT` that extends a table will acquire frames through `ExtendBufferedRelShared` without
    touching your accounting at all. The pool fills past the threshold, no seal is ever triggered,
    and the pool eventually exhausts — reported as an error in a code path you never edited.

    Worse, it is intermittent: it only appears once a table grows, so a test suite that only reads
    and updates in place will pass. The autograder's bulk-load test will not.

    Both call sites need the same treatment. Note also that this loop grabs `extend_by` frames at
    once — your exhaustion check must handle a *batch* request, not just one frame at a time.

??? question "Why does this function acquire all its victim buffers *before* taking the relation extension lock?"

    Because `GetVictimBuffer` can be arbitrarily slow — it may have to write out a dirty page, which
    may in turn force a WAL flush ([§11](11-wal-rule.md)). The extension lock is a heavyweight
    relation-level lock that serializes *every* backend inserting into that table. Doing the
    expensive, unpredictable work outside the lock and holding it only for the `smgrnblocks` +
    table-insert + `smgrzeroextend` sequence is what lets concurrent bulk loads scale. The comment
    says so directly. Same instinct as the checkpointer's sort: figure out what actually costs time,
    and make sure it does not happen while holding something hot.
