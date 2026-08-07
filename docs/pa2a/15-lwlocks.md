# 15 · LWLocks and condition variables

*What the lock does and does not promise — and how to wait for something that isn't a lock.*

PostgreSQL has three tiers of mutual exclusion, and the buffer manager uses all three:

| Tier | Cost | Waits by | Used for |
|---|---|---|---|
| **Spinlock** (`BM_LOCKED`) | ~ns | Busy-spinning | Descriptor field updates. Never held across anything that can block. |
| **LWLock** | ~100ns uncontended | Semaphore sleep, FIFO queue | Partition locks, content locks. Shared/exclusive, no deadlock detection. |
| **Heavyweight lock** (`lmgr`) | ~µs | Full wait graph | Relation and tuple locks. Deadlock *detection*. Not used inside the buffer manager. |

```c title="lwlock.c:786 — the comment that matters"
/*
 * Internal function that tries to atomically acquire the lwlock in the passed
 * in mode.
 *
 * This function will not block waiting for a lock to become free - that's the
 * caller's job.
 *
 * Returns true if the lock isn't free and we need to wait.
 */
static bool
LWLockAttemptLock(LWLock *lock, LWLockMode mode)
```

Three properties of LWLocks that surprise people:

- **No deadlock detection.** Acquire two LWLocks in inconsistent orders and the server hangs
  forever, with no error. This is why `GetVictimBuffer` uses `LWLockConditionalAcquire` for the
  content lock ([§09](09-clock-sweep.md)) and retries rather than blocking: the lock ordering there
  cannot be guaranteed, so blocking is not an option.
- **Acquisition is not strictly FIFO.** `LWLockAcquire` (`lwlock.c:1180`) attempts the lock, queues
  itself if that fails, then *re-attempts* before sleeping. A newly arriving backend can therefore
  acquire a free lock ahead of a queued waiter. Waiters do not starve in practice, but do not build
  a protocol whose correctness depends on queue order.
- **Interrupts are held off while held.** A backend holding an LWLock cannot be cancelled or
  terminated until it releases. Hence: never do I/O of unbounded duration under one.

## Condition variables: waiting for a condition, not a lock

Sometimes you need to wait for a *predicate* — "the I/O on this frame has finished", "the next pool
has been recycled" — rather than for exclusive access. That is `condition_variable.c`, and the usage
pattern is the classic one:

```c title="condition_variable.c:56, :96, :282 — the idiom"
/* WAITER — note the loop. A wakeup is a HINT, not a guarantee. */
ConditionVariablePrepareToSleep(cv);      /* enqueue BEFORE testing */
while (!condition_is_true())
    ConditionVariableSleep(cv, WAIT_EVENT_XXX);
ConditionVariableCancelSleep();

/* SIGNALLER */
set_the_condition();
ConditionVariableBroadcast(cv);
```

!!! note "Design note · why the `while`, always"

    Enqueue-then-test, and re-test after every wake, is not defensive style — it is required.
    Between your test and your sleep, the signaller may run and broadcast to an empty queue; you
    would then sleep forever on a condition that is already true.
    `ConditionVariablePrepareToSleep` exists precisely to close that window: you are in the queue
    before you look. And because `Broadcast` wakes *everyone*, several waiters may find the
    condition false again by the time they run. `WaitIO()` (`bufmgr.c:5959`) is the in-tree example
    — read it as the canonical use.

!!! lens "PA2a lens"

    The **base** assignment needs none of this. Single writer, seal at a quiescent point — there is
    nobody to wait for and nothing to arbitrate; that is exactly what G1 buys you. This section is
    here for two reasons: the **bonus** (concurrent writers) needs a buffer-pool lock plus an
    arrival gate built from a condition variable, and **PA2b** needs backpressure — a seal that must
    wait for the flush worker to finish recycling the next pool. If you find yourself reaching for a
    condition variable in the base, stop and re-read [§10](10-pin-lock-spinlock.md): you have
    probably put your seal in the wrong place.
