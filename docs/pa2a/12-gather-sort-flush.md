# 12 · Gather, sort, flush — the checkpointer's pattern

*The model for your synchronous seal. Copy the shape; ignore the throttling.*

A checkpoint must write every buffer that was dirty when it started, while the system keeps dirtying
more. `BufferSync()` solves this in three phases, and phase 1 is the clever part.

```c title="bufmgr.c:3344 — phase 1, mark and gather"
static void
BufferSync(int flags)
{
    int mask = BM_DIRTY;

    /* Unless this is a shutdown checkpoint, write only permanent dirty buffers. */
    if (!(flags & (CHECKPOINT_IS_SHUTDOWN | CHECKPOINT_END_OF_RECOVERY |
                   CHECKPOINT_FLUSH_ALL)))
        mask |= BM_PERMANENT;

    /*
     * Loop over all buffers, and mark the ones that need to be written with
     * BM_CHECKPOINT_NEEDED. ... This allows us to write only those pages that
     * were dirty when the checkpoint began, and not those that get dirtied
     * while it proceeds.
     */
    num_to_scan = 0;
    for (buf_id = 0; buf_id < NBuffers; buf_id++)
    {
        BufferDesc *bufHdr = GetBufferDescriptor(buf_id);
        buf_state = LockBufHdr(bufHdr);

        if ((buf_state & mask) == mask)
        {
            buf_state |= BM_CHECKPOINT_NEEDED;

            item = &CkptBufferIds[num_to_scan++];
            item->buf_id    = buf_id;
            item->tsId      = bufHdr->tag.spcOid;
            item->relNumber = BufTagGetRelNumber(&bufHdr->tag);
            item->forkNum   = BufTagGetForkNum(&bufHdr->tag);
            item->blockNum  = bufHdr->tag.blockNum;
        }
        UnlockBufHdr(bufHdr, buf_state);
    }
    if (num_to_scan == 0) return;

    /*
     * Sort buffers that need to be written to reduce the likelihood of random
     * IO. The sorting is also important for ... balancing writes between
     * tablespaces.
     */
    sort_checkpoint_bufferids(CkptBufferIds, num_to_scan);   /* → ckpt_buforder_comparator, :6340 */
    … phase 3: walk the sorted array, SyncOneBuffer() each, throttled by
      CheckpointWriteDelay() and balanced across tablespaces by a min-heap …
}
```

The sort key is `(tablespace, relation, fork, block)` — so the writes leave the buffer manager in
physical file order, turning what would be random 8 KB writes scattered across the pool into long
sequential runs. On spinning disks this was worth an order of magnitude; on NVMe it still helps the
kernel merge requests.

```text
  UNSORTED (buffer-pool order)          SORTED (ckpt_buforder_comparator)
  ┌──────────────────────────┐          ┌──────────────────────────┐
  │ rel 5, blk 900           │          │ rel 5, blk  12           │
  │ rel 9, blk   3           │          │ rel 5, blk  13           │
  │ rel 5, blk  12           │   ──▶    │ rel 5, blk 900           │
  │ rel 9, blk 402           │          │ rel 9, blk   3           │
  │ rel 5, blk  13           │          │ rel 9, blk 402           │
  └──────────────────────────┘          └──────────────────────────┘
        seek, seek, seek…                  one sweep per file
```

*bufmgr.c:6340. Same set of writes, a fraction of the I/O cost. This is "amortize by batching and
ordering" — the most reusable idea in the write path.*

!!! lens "PA2a lens · your seal, in five steps"

    Your seal is `BufferSync` with the throttling removed and the scan restricted to one pool:

    1. **Gather** — walk the sealing pool's frames, collect the `BM_DIRTY` ones into
       `CkptBufferIds`.
    2. **Sort** — `sort_checkpoint_bufferids()`, unchanged. Free performance; no reason to skip it.
    3. **Flush** — `FlushBuffer()` each, which handles WAL ordering ([§11](11-wal-rule.md)).
    4. **Invalidate wholesale** — reset that pool's buffer table, `ClearBufferTag()` every frame,
       zero every `state`, reset `next_free_slot = 0`. The pool goes back *completely empty*.
    5. **Advance** — `active_pool = (active_pool + 1) % buffer_pools`, `generation++`.

    You do *not* need `BM_CHECKPOINT_NEEDED`. Its whole purpose is to fix the set of pages while the
    system keeps dirtying more; your seal runs at a quiescent point with a single writer, so the set
    is already stable. Deleting a mechanism because its precondition changed — and being able to say
    exactly why — is a large part of what this assignment grades.

??? question "The *full* invalidation in step 4 is a deliberately bad idea. What does it cost, and why is it in the spec anyway?"

    It costs every page. A page still in the working set is thrown away at seal time and must be
    re-read from disk on its next access, even though the bytes were sitting right there in a frame
    nobody was using. On a workload with any locality, that is the dominant cost of the design and
    you will measure it.

    It is specified anyway for two reasons. First, it makes the graded invariant crisp and
    machine-checkable: *no valid tag and no dirty frame survives a seal.* Second, it is the
    pathology PA2b exists to fix — Copy-on-Seal copies a still-wanted page forward into the new pool
    instead of re-reading it. You are being asked to build the strawman well enough to feel exactly
    where it hurts.
