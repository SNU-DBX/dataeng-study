# 05 · The state word: 32 bits doing five jobs

*Refcount, usage count, and ten flags in one atomic — including the header lock itself.*

`BufferDesc.state` is a single `pg_atomic_uint32`. Packing everything into one word is what lets the
common case — pin an already-resident buffer — be a single compare-and-swap with no lock at all.

```text
  bit  31      30      29      28      27      26      25      24      23      22
      ┌───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
      │PERMA- │CKPT_  │PIN_CNT│JUST_  │IO_    │IO_IN_ │TAG_   │VALID  │DIRTY  │LOCKED │
      │NENT   │NEEDED │WAITER │DIRTIED│ERROR  │PROGRES│VALID  │       │       │       │
      └───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┘
       ◀────────────────────── 10 flag bits ────────────────────────────────────────▶
      ┌───────────────┬──────────────────────────────────────────────────────────────┐
      │ usagecount    │  refcount                                                    │
      │ 4 bits (21-18)│  18 bits (17-0)   — how many backends have this pinned        │
      └───────────────┴──────────────────────────────────────────────────────────────┘

  BM_LOCKED (bit 22) is the buffer header spinlock. It is IN the word it protects.
```

*buf_internals.h:44–77. `BUF_REFCOUNT_BITS 18` + `BUF_USAGECOUNT_BITS 4` + `BUF_FLAG_BITS 10` = 32,
enforced by a `StaticAssertDecl`.*

| Flag | Meaning | Why PA2a cares |
|---|---|---|
| `BM_VALID` | The bytes in the frame are the real contents of the page. | Your seal clears it. Nothing may read a frame without it. |
| `BM_TAG_VALID` | There is a buffer-table entry for this frame's tag. | Your invalidation must clear tag and entry together, or you leak a hash entry pointing at a recycled frame. |
| `BM_DIRTY` | Modified since read; must be written before reuse. | The seal's flush set is exactly the `BM_DIRTY` frames of the sealing pool. |
| `BM_JUST_DIRTIED` | Dirtied *again* while a write was in flight. | Set under the header lock; makes `FlushBuffer` leave the buffer dirty rather than falsely marking it clean. |
| `BM_IO_IN_PROGRESS` | Exclusive right to perform I/O on this frame. | Held by `StartBufferIO`/`TerminateBufferIO` around every read and write. |
| `BM_CHECKPOINT_NEEDED` | Was dirty when this checkpoint began. | The trick that lets a checkpoint write a *fixed* set of pages while the system keeps dirtying more ([§12](12-gather-sort-flush.md)). |
| `BM_PERMANENT` | Not an unlogged relation. | Gates the `XLogFlush` in `FlushBuffer` ([§11](11-wal-rule.md)). |
| `BM_LOCKED` | The header spinlock is held. | See [§10](10-pin-lock-spinlock.md) — this is *not* the content lock, and confusing the two is the single most common source of buffer-manager bugs. |

!!! note "Design note · usagecount is not LRU"

    `BM_MAX_USAGE_COUNT` is 5, four bits wide. Every pin bumps it (capped); every pass of the clock
    sweep decrements it. It is a 3-bit-ish approximation of "recently used" — deliberately *not* a
    timestamp, because a true LRU needs a global ordered structure and therefore a global lock on
    every single buffer hit. The comment in `buf_internals.h` is candid about the tradeoff: a large
    value "would approximate LRU semantics. But it can take as many as `BM_MAX_USAGE_COUNT+1`
    complete cycles of clock sweeps to find a free buffer."

    This is a theme worth internalizing: nearly every "policy" decision in a high-concurrency system
    is really a decision about how much shared mutable state the policy requires.
