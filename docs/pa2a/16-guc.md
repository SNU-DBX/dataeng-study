# 16 · GUC plumbing

*Adding a knob is four edits. Choosing its context is the only interesting decision.*

Configuration parameters ("GUCs") live in static tables in `src/backend/utils/misc/guc_tables.c`.
Here is the one you are about to sit next to:

```c title="guc_tables.c:2378"
{
    {"shared_buffers", PGC_POSTMASTER, RESOURCES_MEM,
        gettext_noop("Sets the number of shared memory buffers used by the server."),
        NULL,
        GUC_UNIT_BLOCKS
    },
    &NBuffers,
    16384, 16, INT_MAX / 2,          /* boot value, min, max */
    NULL, NULL, NULL                 /* check, assign, show hooks */
},
```

| Context | Changeable | Example |
|---|---|---|
| `PGC_POSTMASTER` | Only at server start | `shared_buffers` — the value determines the size of a shared segment that cannot be resized. |
| `PGC_SIGHUP` | On config reload | `checkpoint_timeout` |
| `PGC_USERSET` | Any time, per session | `work_mem` |

!!! lens "PA2a lens · `buffer_pools` must be `PGC_POSTMASTER`"

    And you should be able to say why in one sentence: the value is read by
    `BufferManagerShmemSize()` during pass 1, before the shared segment exists, and the segment
    cannot be resized afterwards. Any other context would let a running server disagree with its own
    memory layout.

    Checklist for adding it: **(1)** declare the variable (`int buffer_pools;`) next to `NBuffers`
    and extern it in the appropriate header; **(2)** add the table entry with a sane min (2 — one
    pool cannot seal) and a modest max; **(3)** use it in `BufferManagerShmemSize()` *and*
    `BufferManagerShmemInit()` together ([§02](02-shared-memory.md)'s trap); **(4)** add it to
    `postgresql.conf.sample` so the test harness can set it.
