# 04 · Three names for the same frame: `buf_id`, `Buffer`, `Block`

*An off-by-one that has confused every person who has ever read this code.*

The same frame is referred to by three different types in three different layers, and they do not
have the same numbering.

```text
   frame index      buf_id      0        1        2        3   …  NBuffers-1
   (internal)                   │        │        │        │
                                ▼        ▼        ▼        ▼
   BufferDescriptors[]      ┌────────┬────────┬────────┬────────┐
                            │ desc 0 │ desc 1 │ desc 2 │ desc 3 │  metadata
                            └────────┴────────┴────────┴────────┘
   BufferBlocks             ┌────────┬────────┬────────┬────────┐
   (char *, flat)           │ 8 KB   │ 8 KB   │ 8 KB   │ 8 KB   │  the page bytes
                            └────────┴────────┴────────┴────────┘
                            ▲
                            └─ BufHdrGetBlock(hdr) = BufferBlocks + buf_id * BLCKSZ

   public handle    Buffer       1        2        3        4
                                 └── Buffer = buf_id + 1,  so 0 == InvalidBuffer

   local (temp) buffers use NEGATIVE Buffer values: -1, -2, -3, …
```

*The +1. `Buffer` is the handle every caller outside `bufmgr.c` holds. Zero is reserved for
`InvalidBuffer`, so the public numbering is one-based, and negative values mean process-local temp
buffers.*

```c title="bufmgr.c:72 · buf_internals.h:334, :344"
/* bufmgr.c:72 — address arithmetic straight into the flat array */
#define BufHdrGetBlock(bufHdr) \
        ((Block) (BufferBlocks + ((Size) (bufHdr)->buf_id) * BLCKSZ))
#define BufferGetLSN(bufHdr)    (PageGetLSN(BufHdrGetBlock(bufHdr)))

/* buf_internals.h:334 */
static inline BufferDesc *
GetBufferDescriptor(uint32 id)
{
    return &(BufferDescriptors[id]).bufferdesc;
}

/* buf_internals.h:344 — the +1 */
static inline Buffer
BufferDescriptorGetBuffer(const BufferDesc *bdesc)
{
    return (Buffer) (bdesc->buf_id + 1);
}
```

You will see `GetBufferDescriptor(buffer - 1)` scattered through `bufmgr.c`; that is the inverse
conversion. And `PrivateRefCount` — per-backend, non-shared bookkeeping we meet in
[§10](10-pin-lock-spinlock.md) — is keyed by `Buffer`, not `buf_id`.

!!! lens "PA2a lens · why one contiguous array matters"

    The VISTA research prototype this assignment distills keeps a separate base pointer per pool,
    because each pool is its own 1 GB huge page. You are not doing that — plain shared memory, one
    contiguous array of `NBuffers × buffer_pools` frames, numbered
    `buf_id = pool × NBuffers + slot`.

    The payoff is large and easy to miss: `BufHdrGetBlock`, `GetBufferDescriptor`, the `Buffer`
    handle, `PrivateRefCount`, and `ResourceOwner` all keep working **completely unmodified**. Pool
    and slot become derived quantities — `buf_id / NBuffers` and `buf_id % NBuffers` — and no code
    outside the buffer manager ever learns that pools exist. Any design where a `Buffer` handle
    stops being a global index will cost you a week.
