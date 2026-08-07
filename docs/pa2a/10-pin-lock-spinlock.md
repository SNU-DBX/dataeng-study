# 10 · Pin, content lock, header spinlock — three different things

*Students conflate these every single year. They protect different things for different durations.*

| | Pin (refcount) | Content lock | Header spinlock |
|---|---|---|---|
| **Protects** | The frame's *identity*: this frame still holds this page. | The frame's *bytes*: the 8 KB page image. | The frame's *descriptor*: tag and state word. |
| **Mechanism** | 18-bit counter inside `state`, bumped by CAS. | A full `LWLock` embedded in the descriptor. | `BM_LOCKED` bit inside `state`. |
| **Modes** | Just a count. | Shared (read) / exclusive (write). | Exclusive only. |
| **Duration** | Long — often a whole statement. | Medium — one page operation. | **Dozens of instructions.** No I/O, no `palloc`, no `ereport`, ever. |
| **Acquire** | `PinBuffer`, `IncrBufferRefCount` | `LockBuffer(buf, BUFFER_LOCK_SHARE)` | `LockBufHdr(desc)` |
| **Held with** | Can hold many at once. | Requires a pin. | Requires nothing; excludes everything. |

A pin says *"do not evict this page out from under me."* It says nothing about the contents: two
backends can both hold pins and both read, or one can be modifying under an exclusive content lock
while the other merely holds a pin and reads no bytes. A content lock says *"the bytes are stable
for me right now."* The header spinlock says *"nobody else may look at or change this descriptor for
the next few nanoseconds."*

```c title="bufmgr.c:3067 — PinBuffer, the CAS loop"
static bool
PinBuffer(BufferDesc *buf, BufferAccessStrategy strategy)
{
    ref = GetPrivateRefCountEntry(b, true);

    if (ref == NULL)                       /* first pin by THIS backend */
    {
        ref = NewPrivateRefCountEntry(b);
        old_buf_state = pg_atomic_read_u32(&buf->state);
        for (;;)
        {
            if (old_buf_state & BM_LOCKED)
                old_buf_state = WaitBufHdrUnlocked(buf);   /* someone holds the header */

            buf_state  = old_buf_state;
            buf_state += BUF_REFCOUNT_ONE;

            if (strategy == NULL) {
                if (BUF_STATE_GET_USAGECOUNT(buf_state) < BM_MAX_USAGE_COUNT)
                    buf_state += BUF_USAGECOUNT_ONE;
            } else {
                /* Ring buffers shouldn't evict others from pool. */
                if (BUF_STATE_GET_USAGECOUNT(buf_state) == 0)
                    buf_state += BUF_USAGECOUNT_ONE;
            }

            if (pg_atomic_compare_exchange_u32(&buf->state, &old_buf_state, buf_state))
            {
                result = (buf_state & BM_VALID) != 0;
                break;
            }
        }
    }
    else                                   /* already pinned by us — no atomic at all */
        result = (pg_atomic_read_u32(&buf->state) & BM_VALID) != 0;

    ref->refcount++;
    ResourceOwnerRememberBuffer(CurrentResourceOwner, b);
    return result;
}
```

## `PrivateRefCount`: the same pin, counted twice

The shared `refcount` in the state word counts *backends*, not pins. If one backend pins the same
buffer five times, the shared counter goes up once. The other four are tracked in process-local
memory:

```text
  BACKEND-LOCAL (bufmgr.c:215)                    SHARED (state word)
  ┌────────────────────────────────┐              ┌──────────────────────┐
  │ PrivateRefCountArray[8]        │              │ BufferDesc.state     │
  │   { Buffer 43, refcount 3 }    │─── 1 ───────▶│   refcount += 1      │
  │   { Buffer 91, refcount 1 }    │─── 1 ───────▶│   (once per backend) │
  │   …                            │              └──────────────────────┘
  │ PrivateRefCountHash (overflow) │
  └────────────────────────────────┘
     8 array slots; beyond that, entries spill into a local hash table
     (PrivateRefCountOverflowed). Array entries are evicted round-robin
     by PrivateRefCountClock so a hot buffer can't get stuck in the hash.
```

*Two levels for one reason: speed. Re-pinning a buffer you already hold must not touch shared memory
at all — no atomic, no cache-line bounce. The local array is the fast path.*

## The quiescent point — the fact PA2a is built on

Pins are owned by the `ResourceOwner`, and at every transaction boundary PostgreSQL asserts they are
all gone:

```c title="bufmgr.c:3991, :4060"
void
AtEOXact_Buffers(bool isCommit)
{
    CheckForBufferLeaks();
    AtEOXact_LocalBuffers(isCommit);
}

static void
CheckForBufferLeaks(void)
{
#ifdef USE_ASSERT_CHECKING
    for (i = 0; i < REFCOUNT_ARRAY_ENTRIES; i++)
    {
        res = &PrivateRefCountArray[i];
        if (res->buffer != InvalidBuffer)
        {
            elog(WARNING, "buffer refcount leak: %s", DebugPrintBufferRefcount(res->buffer));
            RefCountErrors++;
        }
    }
    … same walk over PrivateRefCountHash if it overflowed …
    Assert(RefCountErrors == 0);
#endif
}
```

!!! lens "PA2a lens · this is your seal point"

    Read that assertion again, because it is the entire solution to PA2a's hardest problem. At the
    transaction boundary, **this backend holds no pins, holds no content locks, and is not in the
    middle of modifying any page**. Under the single-writer assumption (G1) that means *nobody* is
    standing on any frame in any pool.

    So: `BufferAlloc` notices the threshold and marks the pool `PENDING`; the transaction finishes;
    and at the boundary you flush, invalidate, reset, advance, and bump the generation with no
    protocol at all. No pin counts, no gate, no arbitration. All of that machinery is the
    concurrency bonus, and it exists only because relaxing G1 destroys this guarantee.

    The price is a sizing invariant you must state and defend: a seal cannot happen mid-transaction,
    so the headroom between the threshold and the end of the pool must cover the largest single
    transaction's buffer demand. If it does not, the pool is exhausted and you raise `ERROR` (G9).
    That is not a bug in your implementation — it is the documented failure mode of giving up
    eviction.
