# 18 · Lab drills — do these before you write a line

*Reading this guide is worth about a third of what running these is worth.*

Build the pristine tree first, with assertions on. `--enable-cassert` is not optional for this
assignment: half the invariants discussed above are enforced by `Assert` and compiled out otherwise.

```bash title="shell — a debug build of the untouched tree"
./configure --prefix=$HOME/pgdev --enable-debug --enable-cassert CFLAGS="-O0 -g3"
make -j$(nproc) && make install
$HOME/pgdev/bin/initdb -D $HOME/pgdata
# pin the assumptions this course makes:
cat >> $HOME/pgdata/postgresql.conf <<'EOF'
shared_buffers = 16MB          # 2048 frames — small enough to watch
io_method = sync               # G8
autovacuum = off               # G10
EOF
$HOME/pgdev/bin/pg_ctl -D $HOME/pgdata -l log start
psql -c 'CREATE EXTENSION pg_buffercache;'
```

## Drill 1 — watch a miss become a hit

Break on `BufferAlloc`, run a query touching one small table twice, and confirm with your own eyes
that the second execution takes the `existing_buf_id >= 0` branch.

```text title="gdb — attach to the backend, not the postmaster"
psql -c 'SELECT pg_backend_pid();'      # → 48231
gdb -p 48231
(gdb) break BufferAlloc
(gdb) continue
# in psql: SELECT count(*) FROM t;
(gdb) print newTag
(gdb) finish
(gdb) print *foundPtr
```

## Drill 2 — see the pool with your own SQL

`pg_buffercache` exposes the descriptor array as a view. This is the shape your `obs_*` functions
will take, so study it as an interface design, not just a tool:

```sql title="psql"
SELECT relfilenode, relblocknumber, isdirty, usagecount, pinning_backends
FROM   pg_buffercache
WHERE  relfilenode IS NOT NULL
ORDER  BY usagecount DESC LIMIT 20;

-- how full is the pool?
SELECT count(*) FILTER (WHERE relfilenode IS NOT NULL) AS used,
       count(*) FILTER (WHERE isdirty)                 AS dirty,
       count(*)                                        AS total
FROM   pg_buffercache;
```

## Drill 3 — make the clock sweep visible

With 2048 frames, load a table larger than the pool and scan it repeatedly. Watch `used` saturate,
then watch which relations survive. Then re-run with a table that fits. The difference between the
two is the entire value of a replacement policy — and it is what your no-evict pool gives up.

## Drill 4 — trigger the failure mode you are about to institutionalize

Set `shared_buffers = 128kB` (16 frames — the minimum) and run a query with a large sort or a wide
join. You are looking for `ERROR: no unpinned buffers available` from `freelist.c:353`. That message
is the stock server's version of pool exhaustion; under G9, yours becomes the *normal* way a
badly-sized pool fails. Knowing what it looks like now saves you an afternoon later.

## Drill 5 — prove the WAL rule to yourself

Break on `XLogFlush` with a condition, and on `smgrwrite`. Dirty a page, force a flush
(`CHECKPOINT;`), and confirm the order of the two breakpoints. Then read `FlushBuffer` once more and
locate the exact line that guarantees it.

??? question "Self-check: can you answer all seven of these without looking?"

    1. Why must `BufferManagerShmemSize` and `BufferManagerShmemInit` be edited together?
    2. Why is the partition lock held across both the lookup *and* the pin?
    3. What exactly does a pin protect, and what does it *not*?
    4. Why does `BufferAlloc` publish the tag before the data is read in?
    5. Which single line of `FlushBuffer` implements write-ahead logging?
    6. Name the two functions that can acquire a shared frame, and why the second one exists.
    7. Why can a seal not happen inside `BufferAlloc`, and what property of the transaction boundary
       makes it safe there?

    If any of these is fuzzy, the section it comes from is the one to re-read — not the whole guide.

---

!!! info "Source of record"

    All line numbers refer to the unmodified PostgreSQL 18.4 tree at `agents/dbcourse/postgres-18`
    (tag: *Stamp 18.4*). Files worth having open alongside this guide:
    `src/backend/storage/buffer/README` — the in-tree design document, authoritative on locking
    order; `buf_internals.h` — every struct and macro used above; and `bufmgr.c`'s own header
    comments, which are unusually good.

    Written for PA2a. PA2b resumes from the base implementation described here, not from the
    concurrency bonus.
