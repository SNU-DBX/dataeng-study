# 09 · Making room: `GetVictimBuffer` and the clock sweep

*The most famous 40 lines in the file — and the ones PA2a deletes outright.*

When the lookup misses, someone has to produce an empty frame. `GetVictimBuffer()` is that someone,
and it is a small state machine with three `goto again` retry points.

```c title="bufmgr.c:2345 — GetVictimBuffer, structure only"
static Buffer
GetVictimBuffer(BufferAccessStrategy strategy, IOContext io_context)
{
    ReservePrivateRefCountEntry();
    ResourceOwnerEnlarge(CurrentResourceOwner);

again:
    /* Returned with its header spinlock still held! */
    buf_hdr = StrategyGetBuffer(strategy, &buf_state, &from_ring);
    Assert(BUF_STATE_GET_REFCOUNT(buf_state) == 0);

    PinBuffer_Locked(buf_hdr);          /* pin, then release the spinlock */
    CheckBufferIsPinnedOnce(buf);

    if (buf_state & BM_DIRTY)
    {
        content_lock = BufferDescriptorGetContentLock(buf_hdr);

        /* CONDITIONAL — an unconditional acquire here can deadlock:
           two backends splitting btree pages, each holding the other's victim. */
        if (!LWLockConditionalAcquire(content_lock, LW_SHARED))
        {
            UnpinBuffer(buf_hdr);
            goto again;
        }

        if (strategy != NULL) { /* ring buffer: would this write force a WAL flush? */
            if (XLogNeedsFlush(lsn) && StrategyRejectBuffer(strategy, buf_hdr, from_ring)) {
                LWLockRelease(content_lock);  UnpinBuffer(buf_hdr);  goto again;
            }
        }

        FlushBuffer(buf_hdr, NULL, IOOBJECT_RELATION, io_context);   /* ← §11 */
        LWLockRelease(content_lock);
        ScheduleBufferTagForWriteback(&BackendWritebackContext, io_context, &buf_hdr->tag);
    }

    /* Remove the old tag from the hash table — may fail if someone
       pinned or dirtied it while we were writing. */
    if ((buf_state & BM_TAG_VALID) && !InvalidateVictimBuffer(buf_hdr))
    {
        UnpinBuffer(buf_hdr);
        goto again;
    }

    /* postcondition: pinned once, no tag, not valid, not dirty */
    return buf;
}
```

The postcondition is the contract: a frame pinned exactly once by us, with `BM_TAG_VALID`,
`BM_VALID` and `BM_DIRTY` all clear. That is what `BufferAlloc` asserts on receipt.

## The replacement policy itself

`StrategyGetBuffer()` tries three sources in order: the strategy ring, the freelist, and finally the
clock sweep.

```c title="freelist.c:314 — the clock sweep loop"
    /* Nothing on the freelist, so run the "clock sweep" algorithm */
    trycounter = NBuffers;
    for (;;)
    {
        buf = GetBufferDescriptor(ClockSweepTick());

        local_buf_state = LockBufHdr(buf);

        if (BUF_STATE_GET_REFCOUNT(local_buf_state) == 0)
        {
            if (BUF_STATE_GET_USAGECOUNT(local_buf_state) != 0)
            {
                local_buf_state -= BUF_USAGECOUNT_ONE;   /* second chance */
                trycounter = NBuffers;
            }
            else
            {
                /* Found a usable buffer */
                if (strategy != NULL) AddBufferToRing(strategy, buf);
                *buf_state = local_buf_state;
                return buf;                              /* spinlock still held! */
            }
        }
        else if (--trycounter == 0)
        {
            /* Everything is pinned. Better to fail than spin forever. */
            UnlockBufHdr(buf, local_buf_state);
            elog(ERROR, "no unpinned buffers available");
        }
        UnlockBufHdr(buf, local_buf_state);
    }
```

```text
                    nextVictimBuffer (atomic, wraps at NBuffers)
                              │
                              ▼
   ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
   │ u2 │ u0 │pin │ u1 │ u5 │ u0 │ u3 │ u0 │pin │ u1 │ u0 │ u2 │
   └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
     ↓hand moves right, one frame per tick ────────────────────▶ (wraps)

   at each frame:   refcount > 0  ──▶ skip (pinned; can't touch it)
                    usagecount > 0 ──▶ usagecount--, keep going  ("second chance")
                    usagecount = 0 ──▶ EVICT THIS ONE

   ClockSweepTick() is one pg_atomic_fetch_add_u32 — no lock in the common case.
   trycounter resets to NBuffers on every decrement, so the loop only gives up
   after a full pass in which NOTHING was decremented and NOTHING was free.
```

*freelist.c:108, :314. The clock is an LRU approximation whose entire shared state is one atomic
counter. That, not its hit rate, is why it wins.*

!!! note "Design note · why not real LRU?"

    A true LRU list must be reordered on *every buffer hit* — that is a doubly-linked list mutation
    under a global lock, on the hottest path in the system, at every page access. On a 32-core
    machine that lock becomes the database. The clock sweep gets approximately the same eviction
    quality for one atomic increment and no shared list at all.

    This tradeoff repeats everywhere in systems: **an approximate policy with cheap synchronization
    beats an exact policy with expensive synchronization**, once you have enough concurrency. PA2a
    is a radical version of the same move — the cheapest possible allocation policy (bump a pointer)
    at the cost of no eviction at all.

```c title="bufmgr.c:2277 — InvalidateVictimBuffer, trimmed"
static bool
InvalidateVictimBuffer(BufferDesc *buf_hdr)
{
    Assert(GetPrivateRefCount(BufferDescriptorGetBuffer(buf_hdr)) == 1);

    tag  = buf_hdr->tag;                /* safe: we hold a pin */
    hash = BufTableHashCode(&tag);
    partition_lock = BufMappingPartitionLock(hash);

    LWLockAcquire(partition_lock, LW_EXCLUSIVE);
    buf_state = LockBufHdr(buf_hdr);

    /* If somebody else pinned the buffer since, or even worse, dirtied it,
       give up on this buffer: It's clearly in use. */
    if (BUF_STATE_GET_REFCOUNT(buf_state) != 1 || (buf_state & BM_DIRTY))
    {
        UnlockBufHdr(buf_hdr, buf_state);
        LWLockRelease(partition_lock);
        return false;                   /* → caller does `goto again` */
    }

    ClearBufferTag(&buf_hdr->tag);
    buf_state &= ~(BUF_FLAG_MASK | BUF_USAGECOUNT_MASK);
    UnlockBufHdr(buf_hdr, buf_state);

    BufTableDelete(&tag, hash);          /* tag cleared BEFORE table entry removed */
    LWLockRelease(partition_lock);
    return true;
}
```

!!! lens "PA2a lens · this whole section evaporates"

    Under G9 (no eviction, ever), `GetVictimBuffer`, `StrategyGetBuffer`, `ClockSweepTick`, the
    freelist, `StrategyRejectBuffer`, and the bgwriter are all replaced by roughly this:

    ```c
    slot = pool->next_free_slot++;
    if (slot >= NBuffers) ereport(ERROR, ... "buffer pool exhausted");
    return pool_id * NBuffers + slot;
    ```

    But do not skim `InvalidateVictimBuffer` on your way past. Its two-step — *clear the tag under
    the header lock, then delete the table entry, both under the partition lock* — is exactly the
    ordering your wholesale seal-time invalidation must preserve. Get the order wrong and a
    concurrent lookup can find a table entry pointing at a frame whose tag has already been cleared,
    or a frame whose tag still says "block 42" with no entry to match.

??? question "Why does `InvalidateVictimBuffer` bail out if the buffer became dirty, even though `GetVictimBuffer` just flushed it?"

    Because the flush was performed under a *shared* content lock, which does not exclude hint-bit
    updates — and because between `FlushBuffer` returning and the partition lock being acquired,
    another backend could have pinned the frame (it is still tagged and still in the table, so it is
    still findable) and dirtied it. Bailing out and retrying is cheap; the alternative is discarding
    someone's modification. Note the general shape: **re-verify under the lock what you checked
    outside it.** That pattern is everywhere in this file, and it is the single most transferable
    habit in the guide.
