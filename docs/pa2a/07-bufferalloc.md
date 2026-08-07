# 07 · Inside `BufferAlloc()`

*Look up. Miss. Get a victim. Insert. Handle the collision. Four phases, two lock acquisitions.*

Read this function twice: once for the happy path, once for the concurrency. Its structure is
dictated entirely by one decision — **the victim is acquired without holding the partition lock** —
which is fast, and which makes a race with another backend unavoidable, and which is why the
function is 170 lines instead of 40.

```c title="bufmgr.c:2000 — phase 1, the lookup"
static pg_attribute_always_inline BufferDesc *
BufferAlloc(SMgrRelation smgr, char relpersistence, ForkNumber forkNum,
            BlockNumber blockNum, BufferAccessStrategy strategy,
            bool *foundPtr, IOContext io_context)
{
    BufferTag   newTag;
    uint32      newHash;
    LWLock     *newPartitionLock;
    int         existing_buf_id;

    /* Make sure we will have room to remember the buffer pin */
    ResourceOwnerEnlarge(CurrentResourceOwner);
    ReservePrivateRefCountEntry();

    InitBufferTag(&newTag, &smgr->smgr_rlocator.locator, forkNum, blockNum);
    newHash = BufTableHashCode(&newTag);
    newPartitionLock = BufMappingPartitionLock(newHash);

    /* see if the block is in the buffer pool already */
    LWLockAcquire(newPartitionLock, LW_SHARED);
    existing_buf_id = BufTableLookup(&newTag, newHash);
    if (existing_buf_id >= 0)
    {
        buf = GetBufferDescriptor(existing_buf_id);

        /* Found it. Pin it so no one can steal it from the pool. */
        valid = PinBuffer(buf, strategy);

        /* Can release the mapping lock as soon as we've pinned it */
        LWLockRelease(newPartitionLock);

        *foundPtr = true;
        if (!valid)
            *foundPtr = false;   /* someone else's read is still in flight */
        return buf;
    }
    LWLockRelease(newPartitionLock);
    … continues below …
```

!!! note "Design note · the lock is held across lookup *and* pin"

    That three-line sequence — `BufTableLookup`, `PinBuffer`, `LWLockRelease` — is the load-bearing
    invariant of the whole subsystem. If you released the partition lock before pinning, another
    backend could pick your frame as its victim in the window between, evict the page, and read a
    different block into it. You would then pin, and be holding a frame containing someone else's
    page under your tag.

    The pin is what makes the frame un-stealable (see `InvalidateVictimBuffer` in
    [§09](09-clock-sweep.md): it refuses any frame with `refcount != 1`). So the rule is: **the
    partition lock covers the interval from "this tag maps to frame N" until "frame N is pinned."**

```c title="bufmgr.c:2066 — phases 2–4, the miss"
    /*
     * Acquire a victim buffer. Somebody else might try to do the same, we
     * don't hold any conflicting locks. If so we'll have to undo our work
     * later.
     */
    victim_buffer  = GetVictimBuffer(strategy, io_context);   /* ← §09 */
    victim_buf_hdr = GetBufferDescriptor(victim_buffer - 1);

    LWLockAcquire(newPartitionLock, LW_EXCLUSIVE);
    existing_buf_id = BufTableInsert(&newTag, newHash, victim_buf_hdr->buf_id);
    if (existing_buf_id >= 0)
    {
        /*
         * Got a collision. Someone has already done what we were about to do.
         * We'll just handle this as if it were found in the pool in the
         * first place.  First, give up the buffer we were planning to use.
         */
        UnpinBuffer(victim_buf_hdr);
        StrategyFreeBuffer(victim_buf_hdr);      /* back on the freelist */

        existing_buf_hdr = GetBufferDescriptor(existing_buf_id);
        valid = PinBuffer(existing_buf_hdr, strategy);
        LWLockRelease(newPartitionLock);
        *foundPtr = valid;
        return existing_buf_hdr;
    }

    /* Need to lock the buffer header too in order to change its tag. */
    victim_buf_state = LockBufHdr(victim_buf_hdr);

    Assert(BUF_STATE_GET_REFCOUNT(victim_buf_state) == 1);
    Assert(!(victim_buf_state & (BM_TAG_VALID | BM_VALID |
                                 BM_DIRTY | BM_IO_IN_PROGRESS)));

    victim_buf_hdr->tag = newTag;
    victim_buf_state |= BM_TAG_VALID | BUF_USAGECOUNT_ONE;
    if (relpersistence == RELPERSISTENCE_PERMANENT || forkNum == INIT_FORKNUM)
        victim_buf_state |= BM_PERMANENT;

    UnlockBufHdr(victim_buf_hdr, victim_buf_state);
    LWLockRelease(newPartitionLock);

    /* Buffer contents are currently invalid. Caller will do the read. */
    *foundPtr = false;
    return victim_buf_hdr;
}
```

Note what `BufferAlloc` returns on a miss: a frame that is **pinned, tagged, and empty**.
`BM_TAG_VALID` is set but `BM_VALID` is not. The tag is published in the table *before* the data
exists, deliberately — the header comment in `buf_init.c:34` explains why: "the buffer has to be
available for lookup BEFORE an IO begins. Otherwise a second process trying to read the buffer will
allocate its own copy and the buffer pool will become inconsistent." A second backend that wants the
same block will find the tag, pin the frame, see `!BM_VALID`, and wait on the I/O condition variable
rather than issuing a duplicate read.

??? question "Trace the collision case. Backend A and backend B both miss on block 42. Who wins, and what does the loser do with the frame it already grabbed?"

    Both call `GetVictimBuffer` concurrently and get *different* free frames — say A gets frame 10,
    B gets frame 77. Both then contend for the same partition lock in `LW_EXCLUSIVE`. Say A wins:
    its `BufTableInsert` returns −1 (success), it tags frame 10 and proceeds to read block 42 into
    it. B then acquires the lock, calls `BufTableInsert`, and gets back `10` — the entry already
    exists. B takes the collision branch: it unpins frame 77, hands it back with
    `StrategyFreeBuffer`, pins frame 10 instead, and returns it as a hit. If A's read has not
    completed, B's `PinBuffer` returns `valid = false`, so `*foundPtr = false` and B goes on to wait
    for A's I/O rather than starting its own.

    The cost of losing is one wasted `GetVictimBuffer` — possibly including a page write, if the
    victim was dirty. That is the price of not holding the partition lock across victim selection,
    and it is judged worth paying because collisions are rare and the lock is extremely hot.

!!! lens "PA2a lens · where your threshold check goes"

    `BufferAlloc` is where you notice the pool is filling up, because it is where frames are handed
    out. Your version replaces `GetVictimBuffer()` with a sequential no-evict allocator — bump
    `next_free_slot`, or raise `ERROR` if the pool is exhausted (G9) — and checks the allocation
    ratio against the seal threshold.

    But you must **not seal here.** At this point in the function the calling backend holds a pin on
    a frame in the very pool it would be sealing, and usually an exclusive content lock on it too.
    Sealing on the spot means flushing and invalidating the frame you are standing on. Mark the pool
    `PENDING` and carry on; the seal runs at the transaction boundary, where `AtEOXact_Buffers`
    already guarantees no pins are held ([§10](10-pin-lock-spinlock.md)).
