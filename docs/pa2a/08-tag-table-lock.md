# 08 · The triangle: tag ⇄ hash table ⇄ partition lock

*A page's identity, where that identity is stored, and what protects it.*

A frame's identity is its `BufferTag` — five integers that uniquely name a block on disk without
consulting any catalog:

```c title="buf_internals.h:106"
typedef struct buftag
{
    Oid           spcOid;       /* tablespace oid */
    Oid           dbOid;        /* database oid */
    RelFileNumber relNumber;    /* relation file number */
    ForkNumber    forkNum;      /* main / fsm / vm / init */
    BlockNumber   blockNum;     /* block within the fork */
} BufferTag;
```

!!! note "Design note · why no table OID"

    The tag names a *file*, not a *relation*. The comment above the struct explains: "It's possible
    that the backend flushing the buffer doesn't even believe the relation is visible yet (its xact
    may have started before the xact that created the rel)." A buffer manager that had to look a
    relation up in `pg_class` to know where to write a page would deadlock against itself, because
    reading `pg_class` requires... reading buffers. Physical identity all the way down. This is a
    general principle in storage-layer design: the layer below the catalog cannot depend on the
    catalog.

The map from tag to `buf_id` is a plain shared hash table in `buf_table.c` — 160 lines, and the
file's own header is the most important part:

```c title="buf_table.c:6 — the file header comment"
 * Note: the routines in this file do no locking of their own.  The caller
 * must hold a suitable lock on the appropriate BufMappingLock, as specified
 * in the comments.  We can't do the locking inside these functions because
 * in most cases the caller needs to adjust the buffer header contents
 * before the lock is released (see notes in README).
```

```c title="buf_table.c:78, :90, :118, :148 — the whole API"
uint32 BufTableHashCode(BufferTag *tagPtr);
       /* → get_hash_value(SharedBufHash, tagPtr).  Computed BEFORE locking,
          because the caller needs it to know WHICH partition lock to take. */

int    BufTableLookup(BufferTag *tagPtr, uint32 hashcode);
       /* → buf_id, or -1.  Caller holds the partition lock at least SHARED. */

int    BufTableInsert(BufferTag *tagPtr, uint32 hashcode, int buf_id);
       /* → -1 on success, or the EXISTING buf_id if the tag was already there.
          Caller holds the partition lock EXCLUSIVE. */

void   BufTableDelete(BufferTag *tagPtr, uint32 hashcode);
       /* elog(ERROR, "shared buffer hash table corrupted") if absent.
          Caller holds the partition lock EXCLUSIVE. */
```

A single lock over one hash table would serialize every buffer lookup in the system. So the table is
**partitioned**: 128 independent lock stripes, chosen by the low bits of the hash.

```c title="lwlock.h:93 · buf_internals.h:193"
/* lwlock.h:93 — NB: must be a power of 2 */
#define NUM_BUFFER_PARTITIONS  128
#define BUFFER_MAPPING_LWLOCK_OFFSET   NUM_INDIVIDUAL_LWLOCKS

/* buf_internals.h:193 */
static inline uint32 BufTableHashPartition(uint32 hashcode)
{
    return hashcode % NUM_BUFFER_PARTITIONS;
}

static inline LWLock *BufMappingPartitionLock(uint32 hashcode)
{
    return &MainLWLockArray[BUFFER_MAPPING_LWLOCK_OFFSET +
                            BufTableHashPartition(hashcode)].lock;
}
```

```text
   tag (spc, db, rel, fork, blk)
        │
        │ BufTableHashCode()          ← must be computed before you can lock
        ▼
   hashcode  0x8f3a21b7
        │
        ├──── % 128 ──▶ partition 55 ──▶ MainLWLockArray[OFFSET + 55]
        │                                       │
        │                                  LW_SHARED    → BufTableLookup
        │                                  LW_EXCLUSIVE → BufTableInsert / Delete
        ▼
   hash bucket ──▶ BufferLookupEnt { BufferTag key; int id; }
                                                    │
                                                    ▼
                                            BufferDescriptors[id]
```

*Why the hash code is a parameter everywhere. Hashing is not cheap, and the caller needs the hash to
pick the lock — so it is computed once and threaded through every call rather than recomputed inside
the table routines.*

!!! lens "PA2a lens · per-pool tables"

    You are giving each pool its own buffer table and its own replicated set of 128 partition locks:
    `BufTableLookup(pool_id, &tag, hash)`, and a `BUFFER_MAPPING_LWLOCK_OFFSET_POOL` stride into
    `MainLWLockArray`.

    For PA2a alone a single global table would work and would be simpler. Per-pool is chosen for two
    reasons. **(1)** PA2b needs it unconditionally — Copy-on-Seal leaves the same page resident in
    two pools at once, which a global tag→id map cannot represent. Landing it now makes PA2a → PA2b
    a strict refinement instead of a rewrite. **(2)** It makes your invalidation trivial: sealing
    resets the whole table for that pool in one call, instead of walking every frame and issuing
    `BufTableDelete` per tag.
