# 09 · 교체 정책 

!!! abstract 목표
    버퍼 풀이 가득 찼을 때 **누구를 내보낼 것인가**를 어떻게 정하는지 이해한다. 버퍼 프레임에 대한 세 갈래의
    공급원(전략 링 → freelist → clock sweep)이 각각 무엇인지, 그리고
    `BufferAccessStrategy`라는 "전략"이 정확히 무엇을 하는 것인지까지 정리한다.

[지난 문서](08-buffer-alloc.md)에서 `GetVictimBuffer()`가 희생양 프레임을 **완전히 비운 상태로** 돌려준다는
것을 보았다. 그런데 그 프레임을 애초에 **누구로 고를 것인가**는 다루지 않고 넘어갔다. 이 장이
그 질문에 답한다.

이 결정은 버퍼 매니저에서 유일하게 "정책"이라 부를 만한 부분이고, 코드도 별도의 파일
`src/backend/storage/buffer/freelist.c`에 따로 있다.

```c title="freelist.c"
/*
 * freelist.c
 *    routines for managing the buffer pool's replacement strategy.
 */
```

---

## 1 · `StrategyGetBuffer()`가 약속하는 것

`GetVictimBuffer()`가 `freelist.c`를 직접 부르는 지점은 단 한 줄이다.

```c title="bufmgr.c"
    /*
     * Select a victim buffer.  The buffer is returned with its header
     * spinlock still held!
     */
    buf_hdr = StrategyGetBuffer(strategy, &buf_state, &from_ring);
    buf = BufferDescriptorGetBuffer(buf_hdr);

    Assert(BUF_STATE_GET_REFCOUNT(buf_state) == 0);

    /* Pin the buffer and then release the buffer spinlock */
    PinBuffer_Locked(buf_hdr);
```

`StrategyGetBuffer()` 함수의 주석을 보면 기대되는 동작에 대해 설명하고 있다.

```c title="freelist.c"
/*
 *  Called by the bufmgr to get the next candidate buffer to use in
 *  BufferAlloc(). The only hard requirement BufferAlloc() has is that
 *  the selected buffer must not currently be pinned by anyone.
 *
 *  To ensure that no one else can pin the buffer before we do, we must
 *  return the buffer with the buffer header spinlock still held.
 */
```

1. **핀이 걸리지 않은 프레임을 고른다** (`refcount == 0`)
2. **헤더 스핀락을 쥔 채로 돌려준다**

스픽락을 쥐어야 하는 것은, 핀이 0이라는 점을 보장하기 위해 필요하다. 스픽락을 놓게 되면 다른 백엔드가 그 프레임을 찾아 핀을 걸어 버릴 수 있기 때문이다. 그래서 확인한 상태를 **핀을 잡을 때까지 이어 가야** 하고, 그러려면 락을 잡고 넘겨줄 수밖에 없다. 이 뒤에 호출되는 `PinBuffer_Locked()`는 "이미 스핀락이 잡혀 있다고 가정하고" 프레임을 핀하는 것이다.

`StrategyGetBuffer()`의 흐름은 다음과 같이 정리된다. 크게 세 갈래의 출처로부터 프레임을 찾아오려고 시도한다. 여기서 비교적 간단한 것부터 먼저 설명한다.

```text
  StrategyGetBuffer(strategy, &buf_state, &from_ring)
        │
        ├─(1)─▶ strategy ring        ...only if a strategy object was passed
        │         hit  ──▶ return (from_ring = true)
        │         miss ──▶ fall through
        │
        ├─(2)─▶ freelist             ...frames that are genuinely unused
        │         hit  ──▶ return
        │         miss ──▶ fall through
        │
        └─(3)─▶ clock sweep          ...evict somebody
                  always returns, or ERROR "no unpinned buffers available"
```

---

## 2 · 첫째 갈래: freelist

### 2.1 · 부팅 시에는 전부 freelist에 들어 있는 채로 시작한다

`StrategyInitialize()`를 보면 시작 상태가 명확하다. [§03](03-buffermanagershmeminit.md)에서 `BufferManagerShmemInit()`이 기술자들을 `freeNext`로 줄줄이 엮어 두는 것을 보았는데, 그 리스트를 통째로 넘겨받는다. 즉 기동 직후에는 모든 프레임이 freelist에 있고, 워크로드가 시작되면서 하나씩 빠져나간다.

```c title="freelist.c"
    /*
     * Grab the whole linked list of free buffers for our strategy. We
     * assume it was previously set up by BufferManagerShmemInit().
     */
    StrategyControl->firstFreeBuffer = 0;
    StrategyControl->lastFreeBuffer = NBuffers - 1;

    /* Initialize the clock sweep pointer */
    pg_atomic_init_u32(&StrategyControl->nextVictimBuffer, 0);
```

### 2.2 · 꺼내기 — 락 없는 사전 검사, 그리고 재검사

```c title="freelist.c"
    /*
     * First check, without acquiring the lock, whether there's buffers in the
     * freelist. Since we otherwise don't require the spinlock in every
     * StrategyGetBuffer() invocation, it'd be sad to acquire it here -
     * uselessly in most cases. That obviously leaves a race where a buffer is
     * put on the freelist but we don't see the store yet - but that's pretty
     * harmless, it'll just get used during the next buffer acquisition.
     */
    if (StrategyControl->firstFreeBuffer >= 0)
    {
        while (true)
        {
            /* Acquire the spinlock to remove element from the freelist */
            SpinLockAcquire(&StrategyControl->buffer_strategy_lock);

            if (StrategyControl->firstFreeBuffer < 0)
            {
                SpinLockRelease(&StrategyControl->buffer_strategy_lock);
                break;
            }

            buf = GetBufferDescriptor(StrategyControl->firstFreeBuffer);

            /* Unconditionally remove buffer from freelist */
            StrategyControl->firstFreeBuffer = buf->freeNext;
            buf->freeNext = FREENEXT_NOT_IN_LIST;

            SpinLockRelease(&StrategyControl->buffer_strategy_lock);

            local_buf_state = LockBufHdr(buf);
            if (BUF_STATE_GET_REFCOUNT(local_buf_state) == 0
                && BUF_STATE_GET_USAGECOUNT(local_buf_state) == 0)
            {
                if (strategy != NULL)
                    AddBufferToRing(strategy, buf);
                *buf_state = local_buf_state;
                return buf;
            }
            UnlockBufHdr(buf, local_buf_state);
        }
    }
```

정상 상태에서 freelist는 거의 항상 비어 있으므로, 매번 스핀락을 잡는
것은 낭비다. 그래서 락 없이 `firstFreeBuffer >= 0`을 한 번 읽어 보고, 비어 있으면 if문 안을 건너 뛰고 바로 클럭
스윕으로 간다. 물론 이로 인해 방금 막 반납된 프레임을 놓칠 가능성도 있지만, 어차피 다음번 버퍼 프레임 할당 때 사용해도 충분하므로 차라리 놓치는 게 낫다는 판단에서 기인한 구현이다. 대신 일단 freelist에 뭔가 있어 보이면 락을 잡고, 그 사이에 freelist의 프레임이 다 떨어져버렸는지 재검사해서 다시금 판단한다.

### 2.3 · 되돌려주기 — `StrategyFreeBuffer()`

```c title="freelist.c"
void
StrategyFreeBuffer(BufferDesc *buf)
{
    SpinLockAcquire(&StrategyControl->buffer_strategy_lock);

    /*
     * It is possible that we are told to put something in the freelist that
     * is already in it; don't screw up the list if so.
     */
    if (buf->freeNext == FREENEXT_NOT_IN_LIST)
    {
        buf->freeNext = StrategyControl->firstFreeBuffer;
        if (buf->freeNext < 0)
            StrategyControl->lastFreeBuffer = buf->buf_id;
        StrategyControl->firstFreeBuffer = buf->buf_id;
    }

    SpinLockRelease(&StrategyControl->buffer_strategy_lock);
}
```

이름만 봐서는 자주 불려야 할 것 같지만, 실제로 프레임이 여기로 들어오는 경우는 많지 않다. 일반적으로 굳이 freelist로 빈 프레임을 다시 반납하지는 않는다. 그래서 처음에 있던 프레임이 빠져나간 뒤에는 대부분 clock sweep을 통해 작동하게 된다. 예외적으로 다음과 같은 경우들에서는 앞으로 사용되지 않는 것이 확실한 프레임들이므로 freelist로 돌려보낸다.
* `DROP TABLE`, `TRUNCATE` 등으로 릴레이션의 페이지를 통째로 버릴 때 (`DropRelationBuffers()` 계열)
* [§08](08-buffer-alloc.md) §1.5에서 본 **버퍼 테이블 삽입 충돌** — 애써 확보한 희생 버퍼를 쓸 일이
  없어졌을 때 "깨끗하고 안 쓰이는 프레임이니 빨리 다시 찾아지도록" 되돌려준다

---

## 3 · 둘째 갈래: 클럭 스윕

### 3.1 · 시곗바늘 — `ClockSweepTick()`

```c title="freelist.c"
static inline uint32
ClockSweepTick(void)
{
    uint32      victim;

    /*
     * Atomically move hand ahead one buffer - if there's several processes
     * doing this, this can lead to buffers being returned slightly out of
     * apparent order.
     */
    victim =
        pg_atomic_fetch_add_u32(&StrategyControl->nextVictimBuffer, 1);

    if (victim >= NBuffers)
    {
        uint32      originalVictim = victim;

        /* always wrap what we look up in BufferDescriptors */
        victim = victim % NBuffers;
        …
    }
    return victim;
}
```

길지 않지만 이것이 어떤 식으로 작동하는지 정확하게 이해해야 한다. 시곗바늘이 가리키는 숫자인 `nextVictimBuffer`는 계속 증가되는 것처럼 보이지만, 버퍼 풀의 프레임 수 상한에 도달하면 다시 0으로 되감아준다. 그러나 되감기로 인한 대기를 최소화하고 있는 패턴이다. `% NBuffers`로 실제 프레임을 얻어갈 수 있기 때문이다. 

**한 바퀴를 넘긴 당사자만** 되감는 경로를 탄다(`victim == 0`). 카운터를 `NBuffers` 아래로 되감으면서
`completePasses`를 올리는데, 둘이 **함께** 갱신되어야 하므로 스핀락이 필요하다.

```c title="freelist.c"
        if (victim == 0)
        {
            expected = originalVictim + 1;

            while (!success)
            {
                /*
                 * Acquire the spinlock while increasing completePasses. That
                 * allows other readers to read nextVictimBuffer and
                 * completePasses in a consistent manner which is required for
                 * StrategySyncStart().
                 */
                SpinLockAcquire(&StrategyControl->buffer_strategy_lock);

                wrapped = expected % NBuffers;

                success = pg_atomic_compare_exchange_u32(&StrategyControl->nextVictimBuffer,
                                                         &expected, wrapped);
                if (success)
                    StrategyControl->completePasses++;
                SpinLockRelease(&StrategyControl->buffer_strategy_lock);
            }
        }
```



### 3.2 · 판정 — 세 가지 경우

`ClockSweepTick()`으로 다음 순서를 받아오고 나면, 우리가 알고 있는 clock sweep 알고리즘이 작동한다. 판정은 다음과 같다.

| `refcount` | `usage_count` | 판정 |
| --- | --- | --- |
| `> 0` | — | **건드릴 수 없다.** 누군가 쓰는 중이므로 그냥 지나간다 |
| `0` | `> 0` | 한 칸 깎고 지나간다 (`-= BUF_USAGECOUNT_ONE`) |
| `0` | `0` | **찾았다.** 이 프레임을 희생양으로 반환 (스핀락을 쥔 채로!) |


```c title="freelist.c"
    /* Nothing on the freelist, so run the "clock sweep" algorithm */
    trycounter = NBuffers;
    for (;;)
    {
        buf = GetBufferDescriptor(ClockSweepTick());

        /*
         * If the buffer is pinned or has a nonzero usage_count, we cannot use
         * it; decrement the usage_count (unless pinned) and keep scanning.
         */
        local_buf_state = LockBufHdr(buf);

        if (BUF_STATE_GET_REFCOUNT(local_buf_state) == 0)
        {
            if (BUF_STATE_GET_USAGECOUNT(local_buf_state) != 0)
            {
                local_buf_state -= BUF_USAGECOUNT_ONE;
                trycounter = NBuffers;
            }
            else
            {
                /* Found a usable buffer */
                if (strategy != NULL)
                    AddBufferToRing(strategy, buf);
                *buf_state = local_buf_state;
                return buf;
            }
        }
        else if (--trycounter == 0)
        {
            UnlockBufHdr(buf, local_buf_state);
            elog(ERROR, "no unpinned buffers available");
        }
        UnlockBufHdr(buf, local_buf_state);
    }
```

```text
                    nextVictimBuffer (atomic, wraps at NBuffers)
                              │
                              ▼
   ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
   │ u2 │ u0 │pin │ u1 │ u5 │ u0 │ u3 │ u0 │pin │ u1 │ u0 │ u2 │
   └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
     hand moves right, one frame per tick ──────────────────────▶ (wraps)

   at each frame:   refcount > 0   ──▶ skip (pinned; can't touch it)
                    usagecount > 0 ──▶ usagecount--, keep going  ("second chance")
                    usagecount = 0 ──▶ EVICT THIS ONE
```

이것이 [§04](04-buffer-state.md)에서 본 `usage_count`가 **실제로 소비되는 유일한 곳**이다.
핀할 때 오르고, 바늘이 지나갈 때 깎인다. 그래서 자주 쓰인 페이지는 바늘이 여러 번 지나가야
비로소 축출 후보가 된다.

여기서 `usage_count`의 상한이 5라는 점(`BM_MAX_USAGE_COUNT`)의 의미가 드러난다. **가장 뜨거운
페이지라도 바늘이 여섯 번 지나가면 축출된다.** 상한이 없으면 한 번 뜨거웠던 페이지가 영영 나가지
않을 수 있고, 상한을 키우면 빈 프레임 하나를 찾는 데 바퀴를 그만큼 더 돌아야 한다. 5는 그 절충이다.

## 4 · 셋째 갈래: 링 버퍼

지금까지 여기서 말하는 "전략"이 무엇인지 정확히 다루지 않았는데, 이제 이를 살펴보기로 한다.

### 4.1 · 배경: 한 번 읽고 말 페이지가 캐시를 쓸어버린다

100GB짜리 테이블을 `SELECT count(*)`로 전체 스캔한다고 하자. 지금까지 본 클럭 스윕대로라면
이 스캔이 읽는 모든 페이지가 버퍼 풀에 자리를 잡고, `usage_count`를 1씩 올리고, 그 과정에서
**다른 세션이 쓰고 있던 뜨거운 페이지들을 전부 밀어낸다.**

문제는 이 스캔이 읽은 페이지를 **다시 볼 일이 거의 없다**는 것이다. 클럭 스윕은 "최근에 쓰였는가"만
보고 "앞으로 또 쓸 것인가"는 모른다. 한 번 훑고 버릴 페이지와 반복해서 쓸 페이지를 구분하지 못한다.

`buffer/README`가 이 문제를 이렇게 적는다.

```text
When running a query that needs to access a large number of pages just once,
such as VACUUM or a large sequential scan, a different strategy is used.
A page that has been touched only by such a scan is unlikely to be needed
again soon, so instead of running the normal clock sweep algorithm and
blowing out the entire buffer cache, a small ring of buffers is allocated
using the normal clock sweep algorithm and those buffers are reused for the
whole scan.
```

해법이 **링 버퍼**다. 작은 프레임 집합을 미리 확보해 놓고 **스캔 내내 그것만 돌려 쓴다.**
그러면 이 스캔이 버퍼 풀에 남기는 흔적이 링 크기만큼으로 제한된다.

`BufferAccessStrategy`가 바로 그 링이다. 이름은 "전략"이지만 실체는 **"내가 재사용할 프레임
번호들을 적어 둔 작은 배열"** 이다.

### 4.2 · 네 가지 전략

```c title="bufmgr.h"
typedef enum BufferAccessStrategyType
{
    BAS_NORMAL,                 /* Normal random access */
    BAS_BULKREAD,               /* Large read-only scan (hint bit updates are
                                 * ok) */
    BAS_BULKWRITE,              /* Large multi-block write (e.g. COPY IN) */
    BAS_VACUUM,                 /* VACUUM */
} BufferAccessStrategyType;
```

| 타입 | 기본 링 크기 | 언제 쓰이나 | 더러워지면 |
| --- | --- | --- | --- |
| `BAS_NORMAL` | 없음 (`NULL`) | 일반적인 경우(대부분) | 해당 없음 |
| `BAS_BULKREAD` | 256KB + α | 큰 순차 스캔 | **링에서 뺀다** |
| `BAS_BULKWRITE` | 16MB | `COPY FROM`, `CREATE TABLE AS SELECT` | 링에 두고 WAL을 flush |
| `BAS_VACUUM` | 2MB (`vacuum_buffer_usage_limit`) | `VACUUM` | 링에 두고 WAL을 flush |

`BAS_NORMAL`이 `NULL`을 돌려준다는 점이 중요하다.

```c title="freelist.c"
    case BAS_NORMAL:
        /* if someone asks for NORMAL, just give 'em a "default" object */
        return NULL;
```

**"전략 없음"이 곧 기본 전략**이다. 그래서 지금까지 본 모든 `if (strategy != NULL)` 분기가
"특수 전략을 쓰는 중인가"를 묻는 것이었다.

### 4.3 · 언제 사용되는가

전략이 자동으로 붙는 대표적인 곳이 힙 순차 스캔이다.

```c title="heapam.c"
    /*
     * If the table is large relative to NBuffers, use a bulk-read access
     * strategy and enable synchronized scanning (see syncscan.c). …
     */
    if (!RelationUsesLocalBuffers(scan->rs_base.rs_rd) &&
        scan->rs_nblocks > NBuffers / 4)
    {
        allow_strat = (scan->rs_base.rs_flags & SO_ALLOW_STRAT) != 0;
        …
    }

    if (allow_strat)
    {
        /* During a rescan, keep the previous strategy object. */
        if (scan->rs_strategy == NULL)
            scan->rs_strategy = GetAccessStrategy(BAS_BULKREAD);
    }
```

문턱은 **테이블 블록 수 > `NBuffers / 4`** 다. 버퍼 풀의 1/4보다 큰 테이블을 훑는다면 "어차피 다
캐시할 수 없는 스캔"이므로, 캐시를 쓸어버리기 전에 링으로 제한한다는 판단이다. `VACUUM`과 `COPY`는 크기와 무관하게 항상 전략을 만든다. 그쪽은 접근 패턴 자체가 "한 번 훑고 끝"임이 확실하기 때문이다.

### 4.5 · 자료구조 — 별도의 공유 메모리가 아님

```c title="freelist.c"
/*
 * Private (non-shared) state for managing a ring of shared buffers to re-use.
 */
typedef struct BufferAccessStrategyData
{
    /* Overall strategy type */
    BufferAccessStrategyType btype;
    /* Number of elements in buffers[] array */
    int         nbuffers;

    /*
     * Index of the "current" slot in the ring, ie, the one most recently
     * returned by GetBufferFromRing.
     */
    int         current;

    /*
     * Array of buffer numbers.  InvalidBuffer (that is, zero) indicates we
     * have not yet selected a buffer for this ring slot. …
     */
    Buffer      buffers[FLEXIBLE_ARRAY_MEMBER];
}           BufferAccessStrategyData;
```

한 가지 링에 대해서 주의해야 할 점은, 공유 메모리에 있는 것이 아니라는 것이다. 이는 각 백엔드들이 개별적으로 할당받아서 갖고 있는 백엔드 로컬 구조체이다. 그렇기 때문에 `StrategyGetBuffer()`가 링을 볼 때는 **락이 전혀 필요 없다.** 자신의 링에서 자기 혼자 읽어오기 때문이다.

```c title="freelist.c"
    /*
     * If given a strategy object, see whether it can select a buffer. We
     * assume strategy objects don't need buffer_strategy_lock.
     */
    if (strategy != NULL)
    {
        buf = GetBufferFromRing(strategy, buf_state);
        if (buf != NULL)
        {
            *from_ring = true;
            return buf;
        }
    }
```

### 4.6 · 링에서 꺼내기

```c title="freelist.c"
static BufferDesc *
GetBufferFromRing(BufferAccessStrategy strategy, uint32 *buf_state)
{
    /* Advance to next ring slot */
    if (++strategy->current >= strategy->nbuffers)
        strategy->current = 0;

    /*
     * If the slot hasn't been filled yet, tell the caller to allocate a new
     * buffer with the normal allocation strategy.  He will then fill this
     * slot by calling AddBufferToRing with the new buffer.
     */
    bufnum = strategy->buffers[strategy->current];
    if (bufnum == InvalidBuffer)
        return NULL;

    /*
     * If the buffer is pinned we cannot use it under any circumstances.
     *
     * If usage_count is 0 or 1 then the buffer is fair game (we expect 1,
     * since our own previous usage of the ring element would have left it
     * there, but it might've been decremented by clock sweep since then). A
     * higher usage_count indicates someone else has touched the buffer, so we
     * shouldn't re-use it.
     */
    buf = GetBufferDescriptor(bufnum - 1);
    local_buf_state = LockBufHdr(buf);
    if (BUF_STATE_GET_REFCOUNT(local_buf_state) == 0
        && BUF_STATE_GET_USAGECOUNT(local_buf_state) <= 1)
    {
        *buf_state = local_buf_state;
        return buf;
    }
    UnlockBufHdr(buf, local_buf_state);

    /*
     * Tell caller to allocate a new buffer with the normal allocation
     * strategy.  He'll then replace this ring element via AddBufferToRing.
     */
    return NULL;
}
```

링 자체는 단순한 원형 배열이고 `current`가 한 칸씩 전진한다. 여기에는 데이터가 직접 담긴 것이 아니라, 버퍼 프레임의 번호들이 담겨있을 뿐이다. 그런데 판정 조건이 `usage_count <= 1`이라는 점이 재미있다. 주석의 논리를 풀면 이렇다.

* **1이 정상값이다.** 내가 지난 바퀴에 이 프레임을 썼으니 `usage_count`가 1로 남아 있을 것이다.
* **0도 괜찮다.** 그 사이에 클럭 스윕 바늘이 지나가며 깎았을 수 있다.
* **2 이상이면 남이 만졌다는 뜻이다.** 내 스캔 말고 누군가 이 페이지를 진짜로 쓰고 있다는 신호이므로,
  **뺏지 않는다.**

즉 링은 다른 세션에게 유용해진 페이지를 양보해주는 방식으로 사용된다. 

`NULL`을 돌려주면 호출자는 freelist/클럭 스윕으로 내려가 새 프레임을 얻고, 그 프레임을 링의
현재 칸에 채워 넣는다.

```c title="freelist.c"
static void
AddBufferToRing(BufferAccessStrategy strategy, BufferDesc *buf)
{
    strategy->buffers[strategy->current] = BufferDescriptorGetBuffer(buf);
}
```

아래와 같이 링이 한 번 채워지고 나면 스캔이 아무리 길어도 **이 네 프레임만 돌아가며 재사용된다.**
버퍼 풀의 나머지는 손대지 않는다.

```text
   scan start                  ring (nbuffers = 4)
   ─────────────────────────────────────────────────────────
   tick 1   current=0   [ -- , -- , -- , -- ]   miss → clock sweep → fill slot 0
   tick 2   current=1   [ 91 , -- , -- , -- ]   miss → clock sweep → fill slot 1
   tick 3   current=2   [ 91 , 34 , -- , -- ]   miss → clock sweep → fill slot 2
   tick 4   current=3   [ 91 , 34 , 12 , -- ]   miss → clock sweep → fill slot 3
   tick 5   current=0   [ 91 , 34 , 12 , 57 ]   HIT  → reuse frame 91   ← steady state
   tick 6   current=1   [ 91 , 34 , 12 , 57 ]   HIT  → reuse frame 34
   ...
```


### 4.7 · 링에서 빠져나가기 — `StrategyRejectBuffer()`

링에는 함정이 하나 있다. **링 안의 프레임이 더러워진 경우**다. 더러운 프레임을 재사용하려면 먼저 디스크에 써야 하고, WAL 규칙(추후 설명) 때문에 WAL을 디스크에 먼저 써야 할 수 있다. 그러면 링을 이용해서 스캔이 얼마 안 돌아갔는데 flush를 기다리느라 대기해야 할 수 있다. `BAS_BULKREAD`는 이 상황에서 **그 프레임을 링에서 버린다.**

```c title="freelist.c"
bool
StrategyRejectBuffer(BufferAccessStrategy strategy, BufferDesc *buf, bool from_ring)
{
    /* We only do this in bulkread mode */
    if (strategy->btype != BAS_BULKREAD)
        return false;

    /* Don't muck with behavior of normal buffer-replacement strategy */
    if (!from_ring ||
        strategy->buffers[strategy->current] != BufferDescriptorGetBuffer(buf))
        return false;

    /*
     * Remove the dirty buffer from the ring; necessary to prevent infinite
     * loop if all ring members are dirty.
     */
    strategy->buffers[strategy->current] = InvalidBuffer;

    return true;
}
```

`InvalidBuffer`로 칸을 비우면, 다음 바퀴에 `GetBufferFromRing()`이 그 칸에서 `NULL`을 돌려주고
클럭 스윕이 새 프레임을 가져다 채운다. 더러워진 페이지는 링 밖에 남아 정상적인 축출 대상이 된다.

!!! note "설계 노트 · 왜 `BAS_BULKREAD`만 거부하는가"
    README가 세 전략의 선택을 각각 다르게 정당화한다.

    ```text
    Hence this strategy works best for scans that are read-only (or at worst
    update hint bits).  In a scan that modifies every page in the scan, like a
    bulk UPDATE or DELETE, the buffers in the ring will always be dirtied and
    the ring strategy effectively degrades to the normal strategy.
    ```

    `BAS_BULKREAD`는 **읽기 전용을 가정**한다. 더러워지는 것은 힌트 비트 갱신 정도의 예외 상황이므로,
    그 예외를 만나면 링에서 빼는 것이 맞다. 다만 모든 페이지를 수정하는 스캔이라면 링이
    매번 비워져 **사실상 일반 전략으로 퇴화**한다는 점도 솔직히 적어 놓았다.

    반면 `BAS_VACUUM`과 `BAS_BULKWRITE`는 **더러워지는 것이 정상**이다. `VACUUM`은 페이지를 정리하고
    `COPY`는 페이지를 채우는 일이니, 더럽다고 링에서 빼면 링이 남아나지 않는다. 그래서 이쪽은
    WAL flush를 감수하고 링에 둔다.

    README가 그 절충의 근거까지 적는다 — 백그라운드 `VACUUM`이 자기 WAL flush 때문에 느려지는 것은
    괜찮지만, `COPY`는 그러면 곤란하므로 링을 16MB로 **크게** 잡아 flush 빈도를 낮췄다는 것이다.
    같은 메커니즘을 워크로드 성격에 따라 다르게 조율한 사례다.

---

## 5 · 세 갈래를 다시 합치면

```text
  StrategyGetBuffer(strategy, &buf_state, &from_ring)
  │
  │ (1) ring — backend-local, no shared state at all
  ├──▶ if (strategy != NULL)
  │        GetBufferFromRing()
  │          slot empty        ──▶ NULL, fall through
  │          pinned            ──▶ NULL, fall through
  │          usagecount >= 2   ──▶ NULL, fall through   (someone else wants it)
  │          otherwise         ──▶ return  (*from_ring = true)
  │
  │ ...wake bgwriter if requested; count this allocation
  │
  │ (2) freelist — spinlock, but usually empty after warmup
  ├──▶ if (firstFreeBuffer >= 0)         ← lock-free probe, may be stale
  │        pop under buffer_strategy_lock
  │        re-check refcount/usagecount under the header lock
  │          usable   ──▶ AddBufferToRing() if strategy; return
  │          not      ──▶ discard, loop
  │
  │ (3) clock sweep — one atomic add per tick
  └──▶ for (;;)
           ClockSweepTick()
             refcount > 0     ──▶ --trycounter (ERROR at 0)
             usagecount > 0   ──▶ usagecount--, trycounter = NBuffers
             else             ──▶ AddBufferToRing() if strategy; return

  every return path hands back the frame with its header spinlock STILL HELD
```

순서가 **싼 것에서 비싼 것으로**이다. 링은 공유 상태를 안 건드리고, freelist는 스핀락 하나면 되고, 클럭 스윕은 프레임들을 순회한다.
