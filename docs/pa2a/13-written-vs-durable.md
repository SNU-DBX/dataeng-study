# 13 · Written ≠ durable: `smgrwrite` and the sync queue

*A trap that will look like a bug in your buffer pool but is a bug in your assumptions.*

`FlushBuffer` calls `smgrwrite()`, and `smgrwrite()` does **not** `fsync`. It issues the `pwrite` and
then calls `RegisterSyncRequest()`, which posts an entry to a queue that **the checkpointer** drains
— the checkpointer being the process that actually calls `fsync`, once, in bulk, at the end of a
checkpoint.

```text
   FlushBuffer
       └─ smgrwrite ──▶ mdwrite ──▶ FileWrite (pwrite)      data in OS page cache
                            └─────▶ register_dirty_segment
                                        └─▶ RegisterSyncRequest(...)
                                                 │
                                    ┌────────────┴──────────────┐
                                    │  checkpointer's queue     │
                                    └────────────┬──────────────┘
                                                 │  ProcessSyncRequests()
                                                 ▼        (checkpointer only)
                                            fsync(fd)  ──▶ actually durable

   NORMAL SERVER:  checkpointer exists, queue is drained, all is well.
   UNDER G10:      no checkpointer ⇒ queue is never drained ⇒ NOTHING IS EVER FSYNCED.
```

*Where durability actually happens. Not in the buffer manager.*

!!! danger "Trap · the disappearing fsync"

    Assumption G10 turns the checkpointer off. So in your build, every sync request your flush posts
    goes into a queue with no reader. Your pages reach the OS page cache and stop there. Everything
    *looks* right — `pg_buffercache` shows clean frames, your observability functions report a
    completed seal — until a crash-recovery test loses committed data and you spend two days looking
    for the bug in your invalidation logic.

    `ProcessSyncRequests()` is not an escape hatch either: `InitSync()` (`sync.c:124`) only builds
    the `pendingOps` table when the process *is* the checkpointer.

    **PA2a is mostly insulated** — the WAL is fsynced by `XLogFlush` regardless, so recovery can
    replay forward and no committed transaction is lost. But you should understand this now, because
    in PA2b the flush worker must call `smgrimmedsync()` per (relation, fork) after writing a pool's
    dirty frames, and the reason will be this paragraph.

The honest takeaway, which generalizes far beyond PostgreSQL: **a successful `write()` is a statement
about the page cache, not about the disk.** Durability requires a separate, explicit, expensive
call, and every storage system draws the line between "written" and "durable" somewhere. Knowing
where your system draws it is the difference between a database and a cache.
