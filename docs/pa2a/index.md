---
icon: lucide/layers
---

# PA2a — Inside the PostgreSQL Buffer Manager

A targeted tour of the code you are about to change: where the buffer pool is born, how a page gets
into it, how PostgreSQL decides which page to throw out, and how pages get back to disk. Every
excerpt is from the pristine PostgreSQL 18.4 tree you were given.

| | |
|---|---|
| **tree** | `postgres-18` @ 18.4 |
| **focus** | `src/backend/storage/buffer/` |
| **assumes** | `io_method = sync` |
| **reading time** | ~90 min + gdb |

---

## 00 · What this guide is — and how to read it

*Not a PostgreSQL tour. A map of exactly the code PA2a rewrites.*

PostgreSQL is roughly 1.3 million lines of C. This guide walks about 900 of them. The selection is
not "the interesting parts" — it is **the parts you will delete, the parts you will bend, and the
parts that will break if you get the first two wrong**.

The three questions that organize everything below are the three you asked implicitly when you read
the assignment:

1. **Where does the buffer pool come from?** Who allocates it, when, out of what, and why is it in
   shared memory rather than the heap? ([§02](02-shared-memory.md)–[§05](05-state-word.md))
2. **What happens on a page request?** `ReadBuffer` down to `BufferAlloc`, the hash table lookup,
   and the clock sweep that finds a victim — the machinery PA2a *deletes*.
   ([§06](06-read-path.md)–[§10](10-pin-lock-spinlock.md))
3. **How do pages get back to disk?** `FlushBuffer`, `BufferSync`, and the checkpointer's
   gather-and-sort — the machinery PA2a *borrows* to implement the seal.
   ([§11](11-wal-rule.md)–[§13](13-written-vs-durable.md))

### Three kinds of marginal note

Three callouts recur, and they mean different things:

!!! note "Design note"

    Why the code is shaped this way — the database-systems reasoning underneath. Read these even if
    you skim the code.

!!! danger "Trap"

    A place where the obvious reading of the code is wrong, or where a plausible PA2a implementation
    will silently corrupt data. These are drawn from real failure modes.

!!! lens "PA2a lens"

    What this specific code becomes in your implementation: kept, deleted, or replaced.
    [§17](17-delta.md) consolidates every lens into one table — but the reasoning lives here, in
    context.

Code excerpts are trimmed. `…` marks elided lines, and `/* ← */` comments are mine, not
PostgreSQL's. Every caption carries the exact `file:line` so you can open the real thing — and you
should, because reading a function in its own file, with its own neighbours, is a skill this
assignment is partly about teaching.

---

## Contents

**Ground**

- [01 · Why a buffer pool exists at all](01-why-a-buffer-pool.md)

**Part I · Where the buffer pool comes from**

- [02 · Shared memory, in two passes](02-shared-memory.md)
- [03 · `BufferManagerShmemInit()`, line by line](03-buffermanagershmeminit.md)
- [05 · The state word](05-state-word.md)

**Part II · The read path, and the machinery you delete**

- [06 · 버퍼 풀을 쓰는 쪽에서 본 API](06-read-path.md)
- [07 · 읽기 연산의 뼈대 — `StartReadBuffers()`에서 `BufferAlloc()`까지](07-bufferalloc.md)
- [08 · The triangle: tag ⇄ hash table ⇄ partition lock](08-tag-table-lock.md)
- [09 · Making room: the clock sweep](09-clock-sweep.md)
- [10 · Pin, content lock, header spinlock](10-pin-lock-spinlock.md)

**Part III · Getting pages back to disk**

- [11 · Dirty pages and the WAL rule](11-wal-rule.md)
- [12 · Gather, sort, flush](12-gather-sort-flush.md)
- [13 · Written ≠ durable](13-written-vs-durable.md)

**Part IV · The edges you will trip over**

- [14 · The second door: relation extension](14-relation-extension.md)
- [15 · LWLocks and condition variables](15-lwlocks.md)
- [16 · GUC plumbing](16-guc.md)

**Part V · Putting it together**

- [17 · The PA2a delta, mapped onto the code](17-delta.md)
- [18 · Lab drills](18-drills.md)

**부록**

- [19 · 읽기 스트림(`read_stream.c`)](19-read-stream.md)
- [20 · 같은 프레임을 부르는 세 가지 이름](20-buffer-id.md)
