# 03 · `BufferManagerShmemInit()`, line by line

*Four arrays and a loop. This is the function you will rewrite first.*

The whole buffer pool is four parallel arrays, all indexed by the same integer, all carved out of
shared memory by four calls to `ShmemInitStruct`.

```c title="src/backend/storage/buffer/buf_init.c:68 — near-complete"
void
BufferManagerShmemInit(void)
{
    bool        foundBufs, foundDescs, foundIOCV, foundBufCkpt;

    /* Align descriptors to a cacheline boundary. */
    BufferDescriptors = (BufferDescPadded *)
        ShmemInitStruct("Buffer Descriptors",
                        NBuffers * sizeof(BufferDescPadded),
                        &foundDescs);

    /* Align buffer pool on IO page size boundary. */
    BufferBlocks = (char *)
        TYPEALIGN(PG_IO_ALIGN_SIZE,                       /* ← note the over-allocation */
                  ShmemInitStruct("Buffer Blocks",
                                  NBuffers * (Size) BLCKSZ + PG_IO_ALIGN_SIZE,
                                  &foundBufs));

    /* Align condition variables to cacheline boundary. */
    BufferIOCVArray = (ConditionVariableMinimallyPadded *)
        ShmemInitStruct("Buffer IO Condition Variables",
                        NBuffers * sizeof(ConditionVariableMinimallyPadded),
                        &foundIOCV);

    CkptBufferIds = (CkptSortItem *)
        ShmemInitStruct("Checkpoint BufferIds",
                        NBuffers * sizeof(CkptSortItem), &foundBufCkpt);

    if (foundDescs || foundBufs || foundIOCV || foundBufCkpt)
    {
        Assert(foundDescs && foundBufs && foundIOCV && foundBufCkpt);
        /* note: this path is only taken in EXEC_BACKEND case */
    }
    else
    {
        for (i = 0; i < NBuffers; i++)              /* ← the init loop */
        {
            BufferDesc *buf = GetBufferDescriptor(i);

            ClearBufferTag(&buf->tag);
            pg_atomic_init_u32(&buf->state, 0);
            buf->wait_backend_pgprocno = INVALID_PROC_NUMBER;
            buf->buf_id = i;
            pgaio_wref_clear(&buf->io_wref);        /* ← new in PG 16+; inert under io_method=sync */

            /* Initially link all the buffers together as unused. */
            buf->freeNext = i + 1;

            LWLockInitialize(BufferDescriptorGetContentLock(buf),
                             LWTRANCHE_BUFFER_CONTENT);
            ConditionVariableInit(BufferDescriptorGetIOCV(buf));
        }
        GetBufferDescriptor(NBuffers - 1)->freeNext = FREENEXT_END_OF_LIST;
    }

    /* Init other shared buffer-management stuff */
    StrategyInitialize(!foundDescs);          /* ← builds the hash table + clock sweep state */

    WritebackContextInit(&BackendWritebackContext, &backend_flush_after);
}
```

| Array | Element | What it holds |
|---|---|---|
| `BufferBlocks` | 8192 B | **The data.** A flat `char[]` of `NBuffers × BLCKSZ`. This is the buffer pool proper. |
| `BufferDescriptors` | `BufferDescPadded` | **The metadata.** Tag, buf_id, atomic state word, freelist link, content lock. Padded to a cache line so two backends touching adjacent descriptors don't false-share. |
| `BufferIOCVArray` | cond. var. | One condition variable per frame, slept on by backends waiting for someone else's in-progress I/O to finish. |
| `CkptBufferIds` | `CkptSortItem` | Scratch space for the checkpointer's sort ([§12](12-gather-sort-flush.md)). In shared memory only so that a checkpoint never has to `palloc` at a bad moment. |

And the matching size function — note that it is *not* a literal transcription of the init function;
the `PG_CACHE_LINE_SIZE` and `PG_IO_ALIGN_SIZE` terms are slack for the alignment the init function
performs:

```c title="src/backend/storage/buffer/buf_init.c:162 — complete"
Size
BufferManagerShmemSize(void)
{
    Size        size = 0;

    /* size of buffer descriptors */
    size = add_size(size, mul_size(NBuffers, sizeof(BufferDescPadded)));
    /* to allow aligning buffer descriptors */
    size = add_size(size, PG_CACHE_LINE_SIZE);

    /* size of data pages, plus alignment padding */
    size = add_size(size, PG_IO_ALIGN_SIZE);
    size = add_size(size, mul_size(NBuffers, BLCKSZ));

    /* size of stuff controlled by freelist.c  ── the hash table lives in here */
    size = add_size(size, StrategyShmemSize());

    size = add_size(size, mul_size(NBuffers,
                                   sizeof(ConditionVariableMinimallyPadded)));
    size = add_size(size, PG_CACHE_LINE_SIZE);

    /* size of checkpoint sort array in bufmgr.c */
    size = add_size(size, mul_size(NBuffers, sizeof(CkptSortItem)));

    return size;
}
```

The buffer *table* is not allocated here. It is allocated one level down, inside
`StrategyInitialize()`, which is a slight historical oddity — the hash table is not really a
"strategy" concern:

```c title="src/backend/storage/buffer/freelist.c:474 — trimmed"
void
StrategyInitialize(bool init)
{
    /*
     * Since we can't tolerate running out of lookup table entries, we must be
     * sure to specify an adequate table size here.  The maximum steady-state
     * usage is of course NBuffers entries, but BufferAlloc() tries to insert
     * a new entry before deleting the old.  In principle this could be
     * happening in each partition concurrently, so we could need as many as
     * NBuffers + NUM_BUFFER_PARTITIONS entries.
     */
    InitBufTable(NBuffers + NUM_BUFFER_PARTITIONS);

    StrategyControl = (BufferStrategyControl *)
        ShmemInitStruct("Buffer Strategy Status",
                        sizeof(BufferStrategyControl), &found);

    if (!found)
    {
        SpinLockInit(&StrategyControl->buffer_strategy_lock);

        /* Grab the whole linked list of free buffers ... */
        StrategyControl->firstFreeBuffer = 0;
        StrategyControl->lastFreeBuffer = NBuffers - 1;

        /* Initialize the clock sweep pointer */
        pg_atomic_init_u32(&StrategyControl->nextVictimBuffer, 0);
        …
    }
}
```

??? question "That comment says the table is sized `NBuffers + NUM_BUFFER_PARTITIONS`, not `NBuffers`. Why can the table ever hold more entries than there are frames?"

    Because `BufferAlloc` *inserts before it deletes*. Look ahead to [§07](07-bufferalloc.md): the
    victim buffer's new tag is inserted into the table (`BufTableInsert`) before the victim's old
    tag is removed (`InvalidateVictimBuffer` → `BufTableDelete`). For that window, one frame has two
    entries. With 128 partitions, up to 128 backends can be in that window at once — hence the
    headroom. A hash table that runs out of entries in shared memory cannot grow, so it would be a
    hard `ERROR`; the fix is to over-size it up front.

!!! lens "PA2a lens"

    This is your primary work surface. You will size and initialize `buffer_pools ×` the descriptor
    and block arrays (one contiguous `ShmemInitStruct` each, not one per pool), add a per-pool
    metadata struct — state, `next_free_slot`, generation — and give each pool its own buffer table.

    Two details from the code above that will bite if you skip them: **(1)** the "Buffer Blocks"
    allocation over-allocates by `PG_IO_ALIGN_SIZE` and then `TYPEALIGN`s — preserve that when you
    multiply by `buffer_pools`, or direct I/O alignment breaks. **(2)** `buf->freeNext` and the
    whole freelist become dead under no-eviction (G9) — but the field is still read by
    `StrategyFreeBuffer()`, so decide deliberately whether you are deleting the freelist or leaving
    it inert.
