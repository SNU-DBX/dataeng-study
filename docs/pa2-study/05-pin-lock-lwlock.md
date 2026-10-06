# 05 · 버퍼 핀과 내용 락

!!! abstract 목표
    버퍼 하나를 안전하게 만지기 위해 필요한 보호 장치들이 각각 무엇을 지키고, 어떻게 구현되어 있으며, 버퍼 풀을 쓰는 쪽에서 어떤 패턴으로 나타나는지를 이해한다. 

## 1 · 버퍼 프레임을 위한 보호 장치들

버퍼 프레임 하나에 대해, 서로 **독립적으로** 깨질 수 있는 사실이 세 가지 있다.

```text
   ┌─────────────────────────────────────────────────────────────┐
   │ BufferDesc #43                                              │
   │                                                             │
   │   tag  = (rel 16384, fork main, block 7)   ← 이 프레임의 신원  │
   │   state = flags | usagecount | refcount    ← 이 프레임의 사실  │
   │   content_lock                                              │
   └─────────────────────────────────────────────────────────────┘
                              │
                              ▼
   ┌─────────────────────────────────────────────────────────────┐
   │ BufferBlocks + 43*8192                                      │
   │   8 KB 페이지 이미지                          ← 이 프레임의 내용  │
   └─────────────────────────────────────────────────────────────┘
```

1. **신원(tag)이 바뀔 수 있다.** 내가 43번 프레임에서 `(16384, main, 7)`을 읽고 있는 동안, 다른 백엔드가 43번을 희생양으로 골라 축출하고 전혀 다른 페이지를 읽어 넣을 수 있다. 
2. **내용(content)이 바뀔 수 있다.** 신원은 그대로인데, 다른 백엔드가 그 페이지에 튜플을 삽입하면서 내용이 바뀔 수 있다.
3. **기술자(descriptor/state) 자체가 바뀔 수 있다.** 누군가 `BM_DIRTY`를 내리는 동시에 내가 `refcount`를 올리면, 한쪽의 갱신이 사라질 수도 있다.

이 세가지는 독립적이기 때문에, 하나의 락으로 셋을 다 처리하지 않고 나누어 관리한다.

| | **핀** (refcount) | **내용 락** (content lock) | **헤더 스핀락** (`BM_LOCKED`) |
| --- | --- | --- | --- |
| **지키는 것** | 프레임의 **신원** — 이 프레임은 여전히 이 페이지다(tag) | 프레임의 **바이트** — 8KB 페이지 내용 | 프레임의 **기술자** |
| **구현** | `state` 안 18비트 카운터, CAS로 증감 | 기술자에 있는 `LWLock` | `state` 안 22번 비트 |
| **모드** | 개수뿐 (모드 없음) | 공유(읽기) / 배타(쓰기) | 배타 전용 |
| **유지 기간** | 길다 | 중간 | 짧음 |
| **획득** | `ReadBuffer()`를 통해 핀을 걸어서 받는다 | `LockBuffer(buf, BUFFER_LOCK_SHARE)` | `LockBufHdr(desc)` |
| **선행 조건** | 없음 | **핀을 이미 갖고 있어야 함** | 없음 |
| **대기 방식** | 대기하지 않음 (그냥 카운터 증가) | 세마포어에서 잠듦, 대기 큐 | CPU를 태우며 스핀 |
| **관리 주체** | `ResourceOwner`가 추적, 누수를 검출 | 호출자 책임 | 호출자 책임 |

!!! note "세 층위는 '기간'으로 갈린다"
    동시성 제어 장치의 성격은 대개 "얼마나 오래 잡고 있을 것인가"가 결정한다.

    * 나노초 단위로 잡을 것이라면 대기 큐를 만드는 비용이 락 자체보다 비싸다 → **스핀락**.
    * 마이크로초~밀리초 단위라면 스핀은 CPU 낭비다 → 잠들 수 있는 **LWLock**.
    * 초 단위로, 심지어 트랜잭션 전체에 걸쳐 잡을 것이라면 아무도 기다리지 않고, 다만 남이 함부로 치우지 못하게 한다 → **핀**.

---

## 2 · 핀 — "이 프레임을 치우지 마라"

### 2.1 · 핀이 약속하는 것과 약속하지 않는 것

```text title="src/backend/storage/buffer/README"
Pins: one must "hold a pin on" a buffer (increment its reference count)
before being allowed to do anything at all with it.  An unpinned buffer is
subject to being reclaimed and reused for a different page at any instant,
so touching it is unsafe.
```

**핀이 하나라도 걸려 있는 프레임은 축출되지 않는다.** 그 결과로 따라오는 것이 두 개 더 있다.

* **`tag`가 바뀌지 않는다.** 축출되지 않으면 신원이 바뀔 수 없다.
* **`BufferGetPage()`가 준 주소가 계속 그 페이지를 가리킨다.**

그러나 핀은 **내용의 안정성을 전혀 보장하지 않는다.** 두 백엔드가 동시에 핀을 걸 수 있고, 한쪽이 배타 내용 락을 잡고 페이지를 뜯어고치는 동안 다른 쪽은 핀만 든 채 아무것도 읽지 않고 서 있을 수 있다.

### 2.2 · `PinBuffer()` — CAS 한 번으로 끝내기

핀은 데이터베이스에서 가장 자주 일어나는 사건 중 하나다. 그래서 락을 쓰지 않는다.

```c title="bufmgr.c"
/*
 * Since buffers are pinned/unpinned very frequently, pin buffers without
 * taking the buffer header lock; instead update the state variable in loop of
 * CAS operations. Hopefully it's just a single CAS.
 */
```

```c title="bufmgr.c:3066 — PinBuffer()"
static bool
PinBuffer(BufferDesc *buf, BufferAccessStrategy strategy)
{
    Buffer      b = BufferDescriptorGetBuffer(buf);
    bool        result;
    PrivateRefCountEntry *ref;

    Assert(!BufferIsLocal(b));
    Assert(ReservedRefCountEntry != NULL);       

    ref = GetPrivateRefCountEntry(b, true);

    if (ref == NULL)                             /* 이 백엔드의 이 버퍼에 대한 첫 핀 */
    {
        ref = NewPrivateRefCountEntry(b);

        old_buf_state = pg_atomic_read_u32(&buf->state);
        for (;;)
        {
            if (old_buf_state & BM_LOCKED)
                old_buf_state = WaitBufHdrUnlocked(buf);   

            buf_state = old_buf_state;
            buf_state += BUF_REFCOUNT_ONE;

            if (strategy == NULL)
            {
                /* Default case: increase usagecount unless already max. */
                if (BUF_STATE_GET_USAGECOUNT(buf_state) < BM_MAX_USAGE_COUNT)
                    buf_state += BUF_USAGECOUNT_ONE;
            }
            else
            {
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
    else                                         /* 이미 내가 이 버퍼에 대한 핀을 갖고 있다 */
        result = (pg_atomic_read_u32(&buf->state) & BM_VALID) != 0;

    ref->refcount++;
    Assert(ref->refcount > 0);
    ResourceOwnerRememberBuffer(CurrentResourceOwner, b);
    return result;
}
```

* **`BM_LOCKED` 확인이 CAS보다 먼저 온다.** 헤더 락을 쥔 쪽이 해제할 때 blind write를 하므로, 락이 잡힌 상태의 값을 기준으로 CAS를 성공시켜 봐야 곧 덮여 사라진다. 그러므로 헤더 락이 잡혀 있으면 기다려야 한다.
* 이미 핀을 갖고 있으면 공유 `state`를 **전혀 건드리지 않는다.** `usagecount`도 오르지 않는다. 같은 버퍼를 100번 다시 핀해도 공유 워드에 대한 쓰기는 0번이다.
* **반환값이 `BM_VALID` 여부다.** 핀을 거는 김에 "이 프레임이 사용될 수 있는가"까지 알려 주어, 호출자가 상태를 다시 읽지 않아도 되게 한다.

#### `PrivateRefCount` — 같은 핀을 두 번 센다

공유 `state`의 `refcount`가 세는 것은 **핀의 개수가 아니라 백엔드의 수**다. 한 백엔드가 같은 버퍼를
다섯 번 핀해도 공유 카운터는 1만 오른다. 나머지 넷은 해당 백엔드의 로컬 메모리에서 센다.

```text
  백엔드 로컬 (bufmgr.c:215)                        공유 메모리 (state 워드)
  ┌────────────────────────────────────┐          ┌──────────────────────────┐
  │ PrivateRefCountArray[8]            │          │ BufferDesc.state         │
  │   { Buffer 43, refcount 3 }  ──────┼── +1 ───▶│   refcount               │
  │   { Buffer 91, refcount 1 }  ──────┼── +1 ───▶│   (백엔드당 딱 한 번)      │
  │   …                                │          └──────────────────────────┘
  │ PrivateRefCountHash (오버플로)       │
  └────────────────────────────────────┘
     배열 슬롯 8개. 넘치면 로컬 해시 테이블로 밀려난다
     (PrivateRefCountOverflowed).
```

```c title="bufmgr.c:"
/*
 * shared refcount needs to be modified only once if a buffer is pinned more
 * than once by an individual backend.  It's also used to check that no buffers
 * are still pinned at the end of transactions and when exiting.
 * ...
 * Until no more than REFCOUNT_ARRAY_ENTRIES buffers are pinned at once, all
 * refcounts are kept track of in the array; after that, new array entries
 * displace old ones into the hash table. That way a frequently used entry
 * can't get "stuck" in the hashtable while infrequent ones clog the array.
 */
```

이미 들고 있는 버퍼를 다시 핀하는 일은 흔한데, 그때마다 공유되는 데이터인 버퍼 상태를 변경하려면 많은 비용이 들 수 있다. 그래서 로컬 핀을 따로 관리한다. 부수 효과가 하나 더 있다. "이 버퍼를 내가 핀하고 있는가"를 공유 메모리 접근 없이 물어볼 수 있다는 것이다.

### 2.3 · `PinBuffer_Locked()` — 헤더 락을 이미 쥔 경우

핀을 걸어야 하는데 헤더 락을 이미 쥐고 있는 상황이 있다. 그때 `PinBuffer()`를 부르면
`WaitBufHdrUnlocked()`에서 자기 자신을 기다리며 영원히 스핀한다.

그래서 `PinBuffer_Locked()`는 헤더 락을 별도로 획득하지 않고, 읽어서 상태를 갱신 후 해제한다.

```c title="bufmgr.c"
static void
PinBuffer_Locked(BufferDesc *buf)
{
    Assert(GetPrivateRefCountEntry(BufferDescriptorGetBuffer(buf), false) == NULL);

    /*
     * Since we hold the buffer spinlock, we can update the buffer state and
     * release the lock in one operation.
     */
    buf_state = pg_atomic_read_u32(&buf->state);
    Assert(buf_state & BM_LOCKED);
    buf_state += BUF_REFCOUNT_ONE;
    UnlockBufHdr(buf, buf_state);          /* ← 여기서 스핀락이 풀린다 */

    b = BufferDescriptorGetBuffer(buf);
    ref = NewPrivateRefCountEntry(b);      /* ← 락을 푼 뒤에 로컬 작업 */
    ref->refcount++;
    ResourceOwnerRememberBuffer(CurrentResourceOwner, b);
}
```

이것이 필요한 이유는 함수에 대한 주석의 마지막 문단에 있다.

```c title="bufmgr.c"
/*
 * Note: use of this routine is frequently mandatory, not just an optimization
 * to save a spin lock/unlock cycle, because we need to pin a buffer before
 * its state can change under us.
 */
```

내가 이미 헤더 락을 잡고 있다고 하자. 이 상태에서 어떤 프레임을 사용하기로 결정했다면, 헤더 락을 놓지 않은 채 핀을 할 필요가 있다. 만약 헤더 락을 놓았다가 다시 잡게 된다면 그 사이에 다른 백엔드가 그 프레임을 갈 수 있다.

### 2.4 · `UnpinBuffer()`

`UnpinBuffer()`는 실질적으로 `UnpinBufferNoOwner()`를 호출하게 되므로, 이를 살펴본다.

```c title="bufmgr.c"
static void
UnpinBufferNoOwner(BufferDesc *buf)
{
    ref = GetPrivateRefCountEntry(b, false);
    ref->refcount--;
    if (ref->refcount == 0)                     /* 로컬 카운트가 0이 될 때만 */
    {
        /* I'd better not still hold the buffer content lock */
        Assert(!LWLockHeldByMe(BufferDescriptorGetContentLock(buf)));

        /*
         * Since buffer spinlock holder can update status using just write,
         * it's not safe to use atomic decrement here; thus use a CAS loop.
         */
        old_buf_state = pg_atomic_read_u32(&buf->state);
        for (;;)
        {
            if (old_buf_state & BM_LOCKED)
                old_buf_state = WaitBufHdrUnlocked(buf);
            buf_state = old_buf_state;
            buf_state -= BUF_REFCOUNT_ONE;
            if (pg_atomic_compare_exchange_u32(&buf->state, &old_buf_state, buf_state))
                break;
        }

        /* Support LockBufferForCleanup() */
        if (buf_state & BM_PIN_COUNT_WAITER)
            WakePinCountWaiter(buf);            /* ← 5장의 클린업 락 참고 */

        ForgetPrivateRefCountEntry(ref);
    }
}
```

* **공유 카운터는 로컬 카운트가 0이 될 때만 내린다.** 즉 내가 이 프레임에 대해 가진 핀이 0개가 되어야 공유 refcount를 1 내려야 한다. 당연한 것이다.
* **공유 카운터를 내릴 때에는 CAS 루프를 통해야 한다.** 
* **`Assert(!LWLockHeldByMe(...))`** 핀을 놓기 전에 내용 락을 먼저 놓았는지 검사한다. 락이 핀보다
  먼저 풀려야 한다는 규칙이 있다.

### 2.5 · 핀은 `ResourceOwner`가 추적한다

`PinBuffer()`의 마지막 줄이 `ResourceOwnerRememberBuffer()`이고, `UnpinBuffer()`의 첫 줄이 `ResourceOwnerForgetBuffer()`였다. 즉 핀은 **자원**으로 등록되어 관리된다.

```c title="bufmgr.c"
const ResourceOwnerDesc buffer_pin_resowner_desc =
{
    .name = "buffer pin",
    .release_phase = RESOURCE_RELEASE_BEFORE_LOCKS,
    .release_priority = RELEASE_PRIO_BUFFER_PINS,
    .ReleaseResource = ResOwnerReleaseBufferPin,
    .DebugPrint = ResOwnerPrintBufferPin
};
```

이것이 있어서, 쿼리 중간에 `elog(ERROR)`가 나서 문제가 생기더라도 핀은 자동으로 반납된다. 그리고
트랜잭션 경계에서는 핀이 하나도 남아있지 않도록 검사한다. 

```c title="bufmgr.c"
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
    … PrivateRefCountHash에 대해서도 같은 순회 …
    Assert(RefCountErrors == 0);
#endif
}
```

### 2.8 · 핀에도 한도가 있다

한 백엔드가 가질 수 있는 핀에 대해서는 상한을 부여한다. 일반적으로는 발생하지 않을 일이지만, 여러 버퍼를 한꺼번에 핀하는 읽기 스트림이나 릴레이션 확장과 같은 경우에는 아래와 같이 '공정한 몫'을 정해놓고, 이에 맞게 요청 개수를 스스로 줄이게 한다. 그러나 평범한 단일 블록 `ReadBuffer()`는 이것이 불필요하다.

```c title="bufmgr.c"
MaxProportionalPins = NBuffers / (MaxBackends + NUM_AUXILIARY_PROCS);
```

---

## 3 · ⭐️⭐️⭐️ LWLock

내용 락으로 넘어가기 전에, 그것을 뒷받침하기 위한 PostgreSQL의 도구인 LWLock을 먼저 살펴 본다. 다만 이것을 이해하지 않더라도 사용하는 데에는 문제가 없으므로 넘어가도 괜찮다.

LWLock은 과거에 있던 Reader-writer lock에서 핵심 상태를 보호하기 위해 스핀락을 사용하던 것이, 그 비용이 너무 커짐에 따라 만들어진 lightweight lock이다. 이를 통해 배제 락이 없는 상태에서 공유 락을 기다리지 않고 잡게 해준다.

핵심 원리는 atomic 변수로서 우리가 앞에서 살펴 본 buffer state와 다소간 유사하다. 여기서 핵심은 `LW_VAL_EXCLUSIVE`와 `LW_VAL_SHARED`이다.

```c title="lwlock.c"
#define LW_FLAG_HAS_WAITERS         ((uint32) 1 << 31)
#define LW_FLAG_RELEASE_OK          ((uint32) 1 << 30)
#define LW_FLAG_LOCKED              ((uint32) 1 << 29)   /* 대기 큐 보호용 스핀락 */

#define LW_VAL_EXCLUSIVE            (MAX_BACKENDS + 1)
#define LW_VAL_SHARED               1

#define LW_SHARED_MASK              MAX_BACKENDS
#define LW_LOCK_MASK                (MAX_BACKENDS | LW_VAL_EXCLUSIVE)
```

우리가 LWLock을 획득하고자 `LWLockAcquire()`를 호출하게 되면 그것은 `LWLockAttemptLock()`으로 이어진다. 이 함수에서는 별도의 경합이 없다면 **공유 락 획득을 CAS 한 번**에 수행한다. 여기에도 `while()`이 있지만 그것은 오랜 시간 대기하기 위함은 아니다. 락을 기다리는 것이 아닌, 락을 획득할 수 있는지 없는지 확실하게 판단될 때까지만 재시도한다. 그래서 이 함수 자체는 락을 기다리는 역할을 하지 않고, 이를 호출하는 `LWLockAcquire()`에서 `for(;;)` 루프를 돌면서 기다리게 된다.

```c title="lwlock.c"
static bool
LWLockAttemptLock(LWLock *lock, LWLockMode mode)
{
    old_state = pg_atomic_read_u32(&lock->state);

    while (true)
    {
        desired_state = old_state;

        if (mode == LW_EXCLUSIVE)
        {
            lock_free = (old_state & LW_LOCK_MASK) == 0;   /* 아무도 없어야 */
            if (lock_free)
                desired_state += LW_VAL_EXCLUSIVE;
        }
        else
        {
            lock_free = (old_state & LW_VAL_EXCLUSIVE) == 0;  /* 배타만 없으면 */
            if (lock_free)
                desired_state += LW_VAL_SHARED;
        }

        if (pg_atomic_compare_exchange_u32(&lock->state, &old_state, desired_state))
            return !lock_free;      /* true = 기다려야 한다 */
    }
}
```

기다려야 할 때는 다음과 같은 4단계다.
* 1단계: CAS로 락 획득을 시도한다. 성공하면 좋다.
* 2단계: (실패한 경우) 락의 대기 큐로 들어간다.
* 3단계: 락 획득 시도를 한 번 더 한다. 성공하면 큐에서 제외하고 락을 획득한 걸로 한다.
* 4단계: 이마저 실패하면 세마포어를 통해 sleep한 뒤에, 다시 깨어나서 1단계부터 재시도한다.

```text title="lwlock.c"
 Phase 1: Try to do it atomically, if we succeed, nice
 Phase 2: Add ourselves to the waitqueue of the lock
 Phase 3: Try to grab the lock again, if we succeed, remove ourselves from the queue
 Phase 4: Sleep till wake-up, goto Phase 1
```

3단계에서 재시도하는 것이 흥미로운데, 그 이유는 바로 큐에 들어가는 사이에 락 소유자가 이미 락을 놓아버렸을 때 생길 수 있는 소위 "lost wakeup" 문제 때문이다. 만약 이렇게 재시도를 했는데도 실패했다면, 아직도 락 소유자가 락을 잡고 있다는 뜻이고, 이는 락을 해제할 때 큐에서 우리를 발견해 줄 것이므로 안전하게 깨어날 것을 기대할 수 있다.

!!! danger "LWLock에는 교착(deadlock) 탐지가 없다"
    두 LWLock을 서로 다른 순서로 잡으면 교착상태에 빠질 가능성도 있다. 그러면 서버는 영원히 멈춘 것처럼 보일 수 있다. 서로가 서로를 기다리고 있는 것이다.
    
    그래서 버퍼 매니저는 두 가지 방어를 쓴다. 순서를 고정하거나, 조건부로 락을 잡는 시도를 한다. 순서를 고정하는 것은, 예를 들어서 버퍼 테이블 파티션 락을 두 개 잡아야 할 때는, 반드시 파티션 번호가 작은 것부터 잡는 것이다. 조건부로 잡는 것은, `GetVictimBuffer()`가 희생양의 내용 락을 `LWLockConditionalAcquire()`로 시도하고, 실패하면 그 프레임을 포기하고 다른 희생양을 찾는 것이다.

    또한 LWLock 획득은 엄격한 FIFO가 아니므로 순서가 보존될 것을 기대할 수 없다. 그리고 LWLock을 쥔 백엔드는 취소·종료될 수 없다. 그렇기 때문에 I/O를 LWLock 아래에서 하면 안 된다.

---

## 4 · 내용 락 — "페이지의 내용물이 변하지 않는다"

### 4.1 · `LockBuffer()`

이 함수는 널리 쓰이지만 사실 버퍼 풀 내부보다는 버퍼 풀을 호출하는 입장에서 많이 쓰이게 된다. 이는 버퍼 기술자에 있는 내용 락에 대해 `LWLockAcquire()`를 호출한다.

```c title="bufmgr.c"
void
LockBuffer(Buffer buffer, int mode)
{
    Assert(BufferIsPinned(buffer));                 /* ← 핀이 선행 조건 */
    if (BufferIsLocal(buffer))
        return;                 /* local buffers need no lock */

    buf = GetBufferDescriptor(buffer - 1);

    if (mode == BUFFER_LOCK_UNLOCK)
        LWLockRelease(BufferDescriptorGetContentLock(buf));
    else if (mode == BUFFER_LOCK_SHARE)
        LWLockAcquire(BufferDescriptorGetContentLock(buf), LW_SHARED);
    else if (mode == BUFFER_LOCK_EXCLUSIVE)
        LWLockAcquire(BufferDescriptorGetContentLock(buf), LW_EXCLUSIVE);
    else
        elog(ERROR, "unrecognized buffer lock mode: %d", mode);
}
```
조건부 버전도 있다.

```c title="bufmgr.c"
bool
ConditionalLockBuffer(Buffer buffer)
{
    Assert(BufferIsPinned(buffer));
    if (BufferIsLocal(buffer))
        return true;            /* act as though we got it */

    buf = GetBufferDescriptor(buffer - 1);
    return LWLockConditionalAcquire(BufferDescriptorGetContentLock(buf), LW_EXCLUSIVE);
}
```

### 4.2 · 같은 버퍼에 락을 두 번 잡을 수 없다

```text title="src/backend/storage/buffer/README"
These locks are intended to be short-term: they should not be held for long.
Buffer locks are acquired and released by LockBuffer().
It will *not* work for a single backend to try to acquire multiple locks on
the same buffer.  One must pin a buffer before trying to lock it.
```

핀은 같은 백엔드가 여러 번 겹쳐 잡을 수 있지만, **내용 락은 그렇지 않다.** LWLock은 재진입이 불가능하다. 자기가 배타 락을 쥔 상태에서 다시 공유 락을 요청하면 자기 자신을 기다리며 멈춘다.

### 4.3 · 락을 놓아도 핀은 남는다

README의 접근 규칙 2번에 따르면, 내용 락을 놓더라도 핀을 잡고 있는 한 데이터를 접근할 수 있다. 여기서 말하는 '규칙 5번'에 따르면, 튜플을 물리적으로 제거하기 위해서는 반드시 배제 락을 잡고 핀 카운트가 1임을(즉 다른 백엔드가 핀을 안 잡고 있음) 확인해야 하므로, 내가 핀을 잡고 있다면 다른 백엔드가 튜플을 제거할 수는 없는 것이다.

```text title="src/backend/storage/buffer/README"
2. Once one has determined that a tuple is interesting (visible to the
current transaction) one may drop the content lock, yet continue to access
the tuple's data for as long as one holds the buffer pin.  This is what is
typically done by heap scans, since the tuple returned by heap_fetch
contains a pointer to tuple data in the shared buffer.  Therefore the
tuple cannot go away while the pin is held (see rule #5).
```

---

## 5 · ⭐️⭐️⭐️ 세 보호장치가 함께 쓰이는 곳 — 클린업 락

지금까지 본 세 장치가 한 자리에서 협력하는 지점으로, VACUUM이 튜플을 **물리적으로 제거**할 때가 있다. 

위에서 잠시 살펴 본 대로, 순차 스캔은 내용 락을 놓은 뒤에도 핀만 쥔 채 튜플 포인터를 쓸 수 있다. 그래서 VACUUM이 배타 내용 락만 잡고 페이지를 정리하게 되면, 락을 안 쥔 채 포인터를 들고 있던 스캔이 이상한 내용을 읽을 수도 있다.

```text title="README:72 — 규칙 5"
5. To physically remove a tuple or compact free space on a page, one
must hold a pin and an exclusive lock, *and* observe while holding the
exclusive lock that the buffer's shared reference count is one (ie,
no other backend holds a pin).
```

그래서 역시 위에서 언급한 규칙 5에 따라, **배타 내용 락 + "공유 핀 카운트가 1"** 을 동시에 관찰해야 한다. 즉 여기서만은 핀 카운트가 떨어지기를 기다려야 한다.

```c title="bufmgr.c"
void
LockBufferForCleanup(Buffer buffer)
{
    Assert(BufferIsPinned(buffer));
    CheckBufferIsPinnedOnce(buffer);        /* 내 핀도 정확히 1이어야 한다 */

    for (;;)
    {
        LockBuffer(buffer, BUFFER_LOCK_EXCLUSIVE);   /* ← 내용 락 */
        buf_state = LockBufHdr(bufHdr);              /* ← 헤더 스핀락 */

        if (BUF_STATE_GET_REFCOUNT(buf_state) == 1)  /* ← 핀 카운트 */
        {
            UnlockBufHdr(bufHdr, buf_state);
            return;                                  /* 성공 — 락을 쥔 채 나간다 */
        }

        /* 실패 — 대기자로 등록하고, 내용 락을 풀고, 잠든다 */
        if (buf_state & BM_PIN_COUNT_WAITER)
        {
            UnlockBufHdr(bufHdr, buf_state);
            LockBuffer(buffer, BUFFER_LOCK_UNLOCK);
            elog(ERROR, "multiple backends attempting to wait for pincount 1");
        }
        bufHdr->wait_backend_pgprocno = MyProcNumber;
        PinCountWaitBuf = bufHdr;
        buf_state |= BM_PIN_COUNT_WAITER;
        UnlockBufHdr(bufHdr, buf_state);
        LockBuffer(buffer, BUFFER_LOCK_UNLOCK);      /* ← 반드시 풀고 잔다 */

        ProcWaitForSignal(WAIT_EVENT_BUFFER_PIN);    /* UnpinBuffer가 깨워 준다 */

        /* 깨어나면 대기자 표시를 지우고 처음부터 다시 */
    }
}
```

* **내용 락**을 잡아 새로 들어오는 사람을 막고,
* **헤더 스핀락**을 잡아 핀 카운트를 원자적으로 읽고,
* **핀 카운트**가 1인지 — 즉 나 말고 아무도 이 프레임을 붙잡고 있지 않은지 — 를 확인한다.

그리고 깨워 주는 쪽이 `UnpinBuffer()`에서 보았던 `WakePinCountWaiter()`이다.

```c title="bufmgr.c"
static void
WakePinCountWaiter(BufferDesc *buf)
{
    uint32 buf_state = LockBufHdr(buf);

    if ((buf_state & BM_PIN_COUNT_WAITER) &&
        BUF_STATE_GET_REFCOUNT(buf_state) == 1)
    {
        /* we just released the last pin other than the waiter's */
        int wait_backend_pgprocno = buf->wait_backend_pgprocno;

        buf_state &= ~BM_PIN_COUNT_WAITER;
        UnlockBufHdr(buf, buf_state);
        ProcSendSignal(wait_backend_pgprocno);
    }
    else
        UnlockBufHdr(buf, buf_state);
}
```
