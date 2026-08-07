# 01 · Why a buffer pool exists at all

*The layer every textbook draws as a single box, and every DBMS spends a decade tuning.*

A relational table lives on disk as a file of fixed-size *pages* — 8 KB in PostgreSQL (`BLCKSZ`).
Every read and write of a tuple is, physically, a read or write of the page containing it. If each
of those touched the disk, a database would run at the speed of the disk. So the DBMS keeps a
fixed-size in-memory array of page-sized *frames* and a map from *page identity* to *frame*. That
array is the **buffer pool**; the map is the **buffer table**; the pair, plus the policy that
decides what to evict and when to write back, is the **buffer manager**.

```text
      SQL executor                          "give me block 42 of table foo"
            │
            ▼
   ┌───────────────────────────────────────────────────────────────┐
   │  BUFFER MANAGER                                               │
   │                                                               │
   │   buffer table          buffer pool (NBuffers × 8 KB frames)  │
   │   (hash: tag → id)      ┌──────┬──────┬──────┬──────┬──────┐  │
   │   ┌────────────────┐    │ blk  │ blk  │ free │ blk  │ blk  │  │
   │   │ (foo,0,42) → 3 ├───▶│  17  │  99  │      │  42  │   8  │  │
   │   │ (foo,0,17) → 0 │    └──────┴──────┴──────┴──────┴──────┘  │
   │   │      …         │       0      1      2    ▲ 3      4      │
   │   └────────────────┘                          └── buf_id      │
   │                                                               │
   │   descriptors: per frame — tag, refcount, usagecount, flags   │
   └───────────────────────────────────────────────────────────────┘
            │  miss ⇒ read()                    │  dirty ⇒ write()
            ▼                                   ▼
      ┌─────────────────────────────────────────────────┐
      │  base/<dboid>/<relfilenode>   (1 GB segments)   │
      └─────────────────────────────────────────────────┘
```

*The whole subsystem in one picture. Three shared structures — pool, table, descriptors — plus one
policy. Everything in this guide is a detail of this diagram.*

## Why not just let the OS do it?

Your operating system already caches file blocks. Why does a database build a second cache on top of
the first — and knowingly pay for double buffering?

- **The DBMS knows the access pattern; the kernel doesn't.** A sequential scan of a 200 GB table
  should not evict the entire working set. PostgreSQL solves this with *ring buffers*
  (`BufferAccessStrategy`) — a scan is confined to a few hundred frames it recycles among itself.
  The kernel's LRU has no way to know that.
- **Write ordering is a correctness requirement, not a hint.** A page may not reach disk before the
  WAL record describing its change ([§11](11-wal-rule.md)). The kernel will happily write pages back
  in whatever order it likes. The DBMS must be able to say "not yet."
- **Pinning.** A page being modified must not vanish mid-modification. There is no `mmap`-level
  equivalent of "pin this page for the next 40 microseconds."

!!! note "Design note · steal / no-force"

    Textbooks classify buffer managers on two axes. **Steal**: may a dirty page from an
    *uncommitted* transaction be written to disk? **Force**: must all of a transaction's dirty pages
    be written at commit?

    PostgreSQL is **steal / no-force**, like essentially every serious DBMS. Steal means the buffer
    manager can evict whatever it wants and never has to care about transaction state — which is why
    `GetVictimBuffer` ([§09](09-clock-sweep.md)) contains not one word about transactions. No-force
    means commit does not wait for data pages, only for WAL. Both choices are only survivable
    because of the write-ahead log: steal requires *undo* information for recovery, no-force
    requires *redo*. Keep this in mind when you read `FlushBuffer` — the single `XLogFlush()` call
    in it is the entire price of steal.

!!! lens "PA2a lens"

    PA2a keeps steal/no-force intact — you are not touching WAL semantics. What you delete is the
    *replacement policy*: the assumption that the buffer manager can always make room on demand.
    Once a pool cannot evict, "the pool is full" stops being a non-event handled by a clock sweep
    and becomes a lifecycle transition. That single change is the whole assignment.
