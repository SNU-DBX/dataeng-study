# 02 · Shared memory, in two passes

*Size everything, create one segment, then carve it up. The order matters.*

PostgreSQL uses a **process-per-connection** model: each client gets its own `fork()`ed backend
process. Those processes must all see the same buffer pool, so the pool cannot live on any one
process's heap. It lives in a single System V/POSIX shared memory segment created by the postmaster
*before* any backend exists, and inherited across `fork()`.

That constraint produces the two-pass structure. The segment has to be created at one fixed size, so
every subsystem must declare its appetite *before* anything is allocated:

```text
  postmaster startup           (postmaster.c:1004)
        │
        ▼
  CreateSharedMemoryAndSemaphores()                       ipci.c:200
        │
        ├─ PASS 1 ── CalculateShmemSize(&numSemas)        ipci.c:89
        │             size = 100000                        (slop for small stuff)
        │             + PGSemaphoreShmemSize(...)
        │             + BufferManagerShmemSize()   ◀── your pools get counted HERE
        │             + LockManagerShmemSize()
        │             + ... ~40 more subsystems
        │             round up to 8 KB
        │
        ├─ PGSharedMemoryCreate(size)   ── one segment, one shot, no growth
        ├─ InitShmemAccess() / InitShmemAllocation()
        │
        └─ PASS 2 ── CreateOrAttachShmemStructs()          ipci.c:268
                      CreateLWLocks()
                      InitShmemIndex()
                      XLOGShmemInit()
                      CLOGShmemInit()
                      BufferManagerShmemInit()   ◀── your pools get built HERE
                      LockManagerShmemInit()
                      ...
```

*The two passes. Pass 1 asks "how much?", pass 2 says "here, take it." The two must agree, and
nothing checks that they do until you overrun and corrupt a neighbour.*

```c title="src/backend/storage/ipc/ipci.c:89 — pass 1, trimmed"
Size
CalculateShmemSize(int *num_semaphores)
{
    Size        size;
    …
    size = 100000;
    size = add_size(size, PGSemaphoreShmemSize(numSemas));
    size = add_size(size, hash_estimate_size(SHMEM_INDEX_SIZE,
                                             sizeof(ShmemIndexEnt)));
    …
    size = add_size(size, BufferManagerShmemSize());   /* ← ipci.c:116 */
    size = add_size(size, LockManagerShmemSize());
    …  ~40 more subsystems …

    /* might as well round it off to a multiple of a typical page size */
    size = add_size(size, 8192 - (size % 8192));
    return size;
}
```

```c title="src/backend/storage/ipc/ipci.c:268 — pass 2, trimmed"
static void
CreateOrAttachShmemStructs(void)
{
    /* LWLocks first: InitShmemIndex needs them */
    CreateLWLocks();
    InitShmemIndex();
    …
    CLOGShmemInit();
    CommitTsShmemInit();
    SUBTRANSShmemInit();
    MultiXactShmemInit();
    BufferManagerShmemInit();                        /* ← ipci.c:295 */
    …
}
```

!!! note "Design note · why a size function at all"

    Because shared memory cannot be `realloc`'d. A modern allocator would just grow the heap; a
    shared segment inherited by `fork()` into dozens of processes at a fixed virtual address cannot
    move. So every shared structure in PostgreSQL comes in a matched pair — `XxxShmemSize()` and
    `XxxShmemInit()` — and the two are kept consistent by discipline, not by the compiler. This is a
    recurring pattern in systems code: when you cannot express an invariant in the type system, you
    express it in the naming convention and hope.

!!! danger "Trap · the pass-1/pass-2 mismatch"

    Your first PA2a bug will probably be this one. You will change `BufferManagerShmemInit()` to
    allocate `buffer_pools ×` the arrays and forget the matching multiplication in
    `BufferManagerShmemSize()`. The server may still start — `CalculateShmemSize()` adds 100 KB of
    slop and rounds up — and then die much later with `"out of shared memory"` from an unrelated
    subsystem, or worse, not die at all and corrupt whatever was allocated after you. Change the two
    functions in the same edit, always.

## Where else is this called from?

Two more call sites matter, and both are one line:

| Site | When | Why you care |
|---|---|---|
| `postmaster.c:1004` | Normal server startup | The ordinary path. |
| `postmaster.c:3202` | Crash restart, inside `PostmasterStateMachine` | After a backend crashes, the postmaster tears down and *re-creates* shared memory. Your pool state is re-initialized from scratch here — which is correct, because the on-disk state is whatever the last successful flush left, and recovery replays WAL forward from there. |
