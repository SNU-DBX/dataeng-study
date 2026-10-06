# 04 · 버퍼 상태 및 스핀락

!!! abstract 목표
    버퍼 상태에 대한 정보가 어떻게 32비트로 표현되는지, 그리고 여러 백엔드가 이를 동시에 접근할 때 어떻게 일관성을 유지하는지 이해한다.

[지난 문서](./03-buffermanagershmeminit.md)에서 `BufferDescriptors`, 즉 버퍼 기술자를 보았다. 그때는 단순히 태그와 ID만을 살펴보았는데, 이번에는 버퍼 기술자가 담고 있는 버퍼 프레임에 대한 중요한 사실 정보를 살펴본다. 이 사실들 중 상당수가 **단 하나의 32비트 정수** 안에 함께 들어 있다. 

---

## 1 · 버퍼 기술자의 생김새 (복습)

```c title="buf_internals.h"
typedef struct BufferDesc
{
    BufferTag   tag;            /* ID of page contained in buffer */
    int         buf_id;         /* buffer's index number (from 0) */

    /* state of the tag, containing flags, refcount and usagecount */
    pg_atomic_uint32 state;

    int         wait_backend_pgprocno;  /* backend of pin-count waiter */
    int         freeNext;       /* link in freelist chain */

    PgAioWaitRef io_wref;       /* set iff AIO is in progress */
    LWLock      content_lock;   /* to lock access to buffer contents */
} BufferDesc;
```

이번에는 `state`를 중심으로 살펴본다. 이 변수는 `pg_atomic_uint32`라는 자료형으로 선언되어 있다. 이것은 그 자체로서 버퍼 기술자(헤더)에 대한 락이 되기도 한다. 프레임 내용물을 보호하기 위한 `content_lock`과는 별개다.

---

## 2 · 32비트가 나누어 맡는 일

`state`는 `pg_atomic_uint32` 하나다. 이는 32비트의 unsigned integer라고 생각하면 되는데 사실 이것을 평범한 정수로 사용하는 것이 아니라 32비트짜리 비트들의 배열로 쓴다고 이해하면 된다. 특히 PostgreSQL에서 이 상태를 '원자적으로(atomically)' 바꾸기 위해 특별한 자료형을 사용하지만 내부 자체는 특별한 것이 없다.

크게 세 개의 영역으로 구분되어 있고, 이 셋을 합치면 32비트가 된다.

```c title="buf_internals.h"
#define BUF_REFCOUNT_BITS 18
#define BUF_USAGECOUNT_BITS 4
#define BUF_FLAG_BITS 10

StaticAssertDecl(BUF_REFCOUNT_BITS + BUF_USAGECOUNT_BITS + BUF_FLAG_BITS == 32,
                 "parts of buffer state space need to equal 32");
```

```text
  bit  31      30      29      28      27      26      25      24      23      22
      ┌───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
      │PERMA- │CKPT_  │PIN_CNT│JUST_  │IO_    │IO_IN_ │TAG_   │VALID  │DIRTY  │LOCKED │
      │NENT   │NEEDED │WAITER │DIRTIED│ERROR  │PROGRES│VALID  │       │       │       │
      └───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┘
       ◀────────────────────── 10 flag bits ─────────────────────────────────────────▶

  bit          21 - 18                    17 - 0
      ┌───────────────────┬──────────────────────────────────────────────────────────┐
      │ usagecount        │ refcount                                                 │
      │ 4 bits            │ 18 bits  — how many backends have this buffer pinned      │
      └───────────────────┴──────────────────────────────────────────────────────────┘

  BM_LOCKED (bit 22) is the buffer header spinlock.
  It lives INSIDE the very word it protects.
```

### 2.1 · 플래그 10개

가장 먼저 제일 높은 10개의 비트는 여러 종류의 플래그로 사용된다. 지금 당장은 전부 이해가 되지 않을 수 있지만, 이후에 버퍼 풀을 직접 보다 보면 발견하게 된다.

| 플래그 | 의미 |
| --- | ---  |
| `BM_VALID` | 프레임의 바이트가 실제 페이지 내용이다 |
| `BM_TAG_VALID` | 이 태그에 대한 버퍼 테이블 엔트리가 존재한다 |
| `BM_DIRTY` | 읽어 온 뒤 수정되었다. 재사용 전 기록 필요 |
| `BM_JUST_DIRTIED` | 쓰기가 진행되는 **도중에 또** 더러워졌다 |
| `BM_IO_IN_PROGRESS` | 이 프레임에 I/O를 수행할 **배타적 권리**를 보유 |
| `BM_IO_ERROR` | 직전 I/O가 실패했다 |
| `BM_PIN_COUNT_WAITER` | 누군가 단독 핀을 기다리고 있다 |
| `BM_CHECKPOINT_NEEDED` | 이번 체크포인트 시작 시점에 더러웠다(지금 이해할 필요 없음) |
| `BM_PERMANENT` | unlogged 릴레이션이 아니다(지금 이해할 필요 없음) |
| `BM_LOCKED` | **헤더 스핀락이 잡혀 있다**  |

### 2.2 · `usagecount` — 4비트

usage count란 버퍼 풀에서 최근에 자주 사용되지 않은 버퍼 프레임을 축출하는 데에 사용하는 정보다. 이에 대해서는 이후에 교체 정책(replacement policy)을 살펴 볼 때 이해할 수 있다. 상한은 5로 되어 있다. 물론 4비트는 0(0000)에서 15(1111)까지를 표현할 수 있기 때문에 15까지 올릴 수도 있을 것이다.

```c title="buf_internals.h"
#define BM_MAX_USAGE_COUNT  5
```

### 2.3 · `refcount` — 18비트

지금 이 프레임을 몇 개의 백엔드가 핀(pin)하고 있는지다. 핀이란 버퍼 프레임을 어떤 식으로든 사용하겠다는 선언과 같으므로, 이것이 0보다 크면 프레임이 축출되지 않는다. 시스템이 허용하는 최대 백엔드의 개수보다는 크거나 같아야 한다.

```c title="buf_internals.h"
StaticAssertDecl(MAX_BACKENDS_BITS <= BUF_REFCOUNT_BITS,
                 "MAX_BACKENDS_BITS needs to be <= BUF_REFCOUNT_BITS");
```

모든 백엔드가 동시에 같은 버퍼를 핀해도 넘치지 않을 만큼은 되어야 한다는 뜻이다.

위의 값들에 접근하기 위한 매크로가 정의되어 있다. 이를 버퍼 풀 내에서 많이 보게 될 것이다. 여기서 각각의 시작 비트 위치를 고려하여 `BUF_REFCOUNT_ONE`이 `1`이고 `BUF_USAGECOUNT_ONE`이 `1U << 18`로 되어 있다.

```c title="buf_internals.h"
#define BUF_REFCOUNT_ONE 1
#define BUF_REFCOUNT_MASK ((1U << BUF_REFCOUNT_BITS) - 1)
#define BUF_USAGECOUNT_ONE (1U << BUF_REFCOUNT_BITS)
#define BUF_USAGECOUNT_SHIFT BUF_REFCOUNT_BITS

#define BUF_STATE_GET_REFCOUNT(state) ((state) & BUF_REFCOUNT_MASK)
#define BUF_STATE_GET_USAGECOUNT(state) \
    (((state) & BUF_USAGECOUNT_MASK) >> BUF_USAGECOUNT_SHIFT)
```

---

## 3 · 32비트 워드(word)로 표현한 이유

이에 대해서는 소스 주석이 직접 답한다.

```c title="buf_internals.h"
/*
 * Combining these values allows to perform some operations without locking
 * the buffer header, by modifying them together with a CAS loop.
 */
```

가장 흔한 연산을 생각해 보자. 이미 버퍼 풀에 있는 페이지를 핀하는 것이다. 이때 해야 할 일은
`refcount`를 1 올리고, `usagecount`를 1 올리고, `BM_VALID`가 켜져 있는지 확인하는
것이다. 필드 세 개가 바뀌어야 한다.

만약 이 것들이 별도의 변수들로 만들어져 있다면, 락을 통해서 세 개를 한번에 원자적으로 바꿀 수 있게 해야 한다. 원자적이라는 것은 작업이 쪼갤 수 없는 하나의 최소 단위로 실행되어서, **전부 실행되거나 아예 안 되거나** 둘 중 하나인 것을 말한다. 여기서는 값을 바꾼다면 세 개가 모두 바뀌거나, 아니면 전부 안 바뀐 상태여야 한다. 누군가가 그 중간 상태(예: 하나만 바뀐 상태)를 보게 되면 안 된다.

그런데 셋을 한 32비트 워드 내에 넣어두게 되면 한 번에 바꾸는 연산이 가능해진다. 위에 주석에서 말하는 "CAS 루프"가 무엇인지는 아래를 보면 확인할 수 있다.

!!! info "Compare-and-Swap(CAS)란"
    CAS는 말 그대로 비교 후 교체하는 방식이다. 인자로 '기대되는 기존 값(expected old value)'와 '새롭게 넣고 싶은 값(new value)'를 받는다. 현재의 값이 expected old value와 일치하는지 **비교**한 후, 일치하면 new value를 써넣고, 성공한 것으로 반환한다. 만약 일치하지 않다면 실패한다. 

    이렇게 함으로써, 본인이 데이터를 읽은 시점과, 이를 변경하고자 하는 시점 사이에 변동이 생겼으면 하던 일을 취소 후 재시도할 수 있게 해준다. 

```c title="bufmgr.c"
old_buf_state = pg_atomic_read_u32(&buf->state);
for (;;)
{
    if (old_buf_state & BM_LOCKED)
        old_buf_state = WaitBufHdrUnlocked(buf);

    buf_state = old_buf_state;

    /* increase refcount */
    buf_state += BUF_REFCOUNT_ONE;

    if (strategy == NULL)
    {
        /* Default case: increase usagecount unless already max. */
        if (BUF_STATE_GET_USAGECOUNT(buf_state) < BM_MAX_USAGE_COUNT)
            buf_state += BUF_USAGECOUNT_ONE;
    }
    ...
    if (pg_atomic_compare_exchange_u32(&buf->state, &old_buf_state, buf_state))
    {
        result = (buf_state & BM_VALID) != 0;
        ...
        break;
    }
}
```
`PinBuffer()`의 핵심 루프다.

* 먼저 `old_buf_state`를 원자적으로 읽어온다.
* 이후에 내가 읽어온 값을 바탕으로 여러가지 작업을 한다. 어차피 나의 로컬 정보기 때문에 마음대로 수정해도 된다.
* `pg_atomic_compare_exchange_u32(&buf->state, &old_buf_state, buf_state)`를 통해 우리가 쓰고자 하는 위치(`&buf->state`)에 기존의 값(`&old_buf_state`)이 있기를 기대하며 새로운 값(`buf_state`)을 써넣는 CAS 연산을 시도한다.
* 이것이 실패했다는 것은 다른 백엔드가 그 사이에 워드를 바꿨다는 뜻이므로, 갱신된 값(`old_buf_state`에 CAS가 자동으로 채워 준다)으로 처음부터 다시 계산한다.

!!! info "PostgreSQL의 atomic 자료형 및 연산"
    여기서 사용되는 `pg_atomic_read_u32()`나 `pg_atomic_compare_exchange_u32()`을 위해서는 `pg_atomic_uint32` 자료형을 써야 한다는 점을 잊지 말자. 사실 이것은 별 게 아니라, `volatile`로 선언된 정수에 불과하다. atomic 연산은 각 컴퓨터 아키텍처에서 지원해줘야하는데, 우리는 x86 아키텍처를 사용하기 때문에, `src/include/port/atomics/arch-x86.h`를 살펴보기를 바란다.

---

## 4 · `LockBufHdr()`와 `UnlockBufHdr()`

### 4.1 · 왜 락이 필요한가 — CAS만으로 안 되는 경우

위에서 CAS 하나로 핀을 잡는 것을 보았다. 이런 식으로 CAS 루프를 사용한다면 얼마든지 버퍼 상태를 변화시킬 수 있을 것이다. 그런데 버퍼 상태의 플래그 중에는 `BM_LOCKED`가 있었다. 이것은 버퍼 헤더 락으로서, 실질적으로 이 버퍼 상태에 대한 접근을 잠그는 락으로 작동한다. 이것이 반드시 필요한 이유가 뭘까?

**첫째, `state` 하나로 끝나지 않는 연산이 있다.** 예를 들어 희생양 버퍼를 무효화(invalidate)하기 위해서는 여러 정보들을 다 지워야 한다. `BM_TAG_VALID`와 `BM_VALID`를 내리고, `refcount`를 확인하는 것까지는 어떻게 된다고 하자. 그런데 `tag` 정보까지 지워줘야 하는데, 이는 32비트 내에 있지 않은 별개의 정보다. 그래서 `state`와 `tag`를 함께 다뤄야 하는 순간, 별도의 상호 배제가 필요하다. 

**둘째, "읽고 → 판단하고 → 바꾸는" 사이가 벌어지면 안 되는 경우가 있다.** 플래그를 조사하여 판단하고, 이에 따라서 여러 곳을 손봐야 하는 경우 CAS로 표현하기 어렵다.

```c
    for (;;)
    {
        buf_state = LockBufHdr(buf);
        if (!(buf_state & BM_IO_IN_PROGRESS))
            break;
        UnlockBufHdr(buf, buf_state);
        if (nowait) return false;
        WaitIO(buf);
    }

    /* Check if someone else already did the I/O */
    if (forInput ? (buf_state & BM_VALID) : !(buf_state & BM_DIRTY))
    {
        UnlockBufHdr(buf, buf_state);
        return false;                       
    }

    buf_state |= BM_IO_IN_PROGRESS;
    UnlockBufHdr(buf, buf_state);
```

### 4.2 · 락이 자기가 지키는 워드 안에 있다

보통 우리가 생각하는 락이라면 별도의 변수로 구현되어 있을 법한데, 여기서는 하나의 플래그로 구현되어 있다. 이 플래그가 켜져있으면(=1) 락이 걸린 것이다.

```c title="buf_internals.h"
#define BM_LOCKED  (1U << 22)  /* buffer header is locked */
```

락 비트가 `state` 안, 22번 비트에 있다. 락이 자기가 보호하는 데이터의 일부다. 만약 별도의 락 변수를 두면 기술자가 4바이트 커지고, 락 획득과 상태 읽기가 **두 번의 메모리 접근**이 된다. 락 비트를 안에 넣으면 **락을 잡는 원자적 연산 한 번이 상태 읽기까지 겸한다.** 이게 정확히 `LockBufHdr()`의 동작이다.

### 4.3 · `LockBufHdr()` — 잠그면서 동시에 읽는다

```c title="bufmgr.c"
uint32
LockBufHdr(BufferDesc *desc)
{
    SpinDelayStatus delayStatus;
    uint32      old_buf_state;

    Assert(!BufferIsLocal(BufferDescriptorGetBuffer(desc)));

    init_local_spin_delay(&delayStatus);

    while (true)
    {
        /* set BM_LOCKED flag */
        old_buf_state = pg_atomic_fetch_or_u32(&desc->state, BM_LOCKED);
        /* if it wasn't set before we're OK */
        if (!(old_buf_state & BM_LOCKED))
            break;
        perform_spin_delay(&delayStatus);
    }
    finish_spin_delay(&delayStatus);
    return old_buf_state | BM_LOCKED;
}
```

**`pg_atomic_fetch_or_u32(&desc->state, BM_LOCKED)`** 이 한 줄이 워드의 **이전 값 전체를 읽어 오는 일**(`fetch`)과, 22번 비트를 **켜는 일**(`or`)을 동시에 수행한다. 그리고 반환값은 **바꾸기 전의 값**이다. 그래서 그 값의 22번 비트를 보면 락이 이미 잡혀있었는지 아닌지를 알 수 있다. 꺼져 있었다면 내가 방금 켠 것이므로 락을 획득한 것이고, 켜져 있었다면 이미 남이 잡고 있는데 내가 다시 켠 것(영향을 미치지는 않음)이므로 실패다.

**`perform_spin_delay(&delayStatus)`** 만약 실패했으면 기다린다. 여기서 수행하는 `perform_spin_delay()`는 처음에는 CPU에 `pause` 명령을 주며 짧게 돌다가, 경합이 길어지면 점진적으로 대기 시간을 늘려가다가, 최대 한도를 넘어서면 다시 최소 대기 시간으로 줄이도록 되어 있다.

**`return old_buf_state | BM_LOCKED`** 이 함수는 "**내가 락을 잡은 시점의 워드 값**"에 락을 켜서 돌려준다. 이를 통해 버퍼 상태까지를 한번에 돌려줄 수 있다는 큰 장점이 있다. 이를테면 사용하는 패턴은 다음과 같다. 락을 잡은 뒤의 수정이 전부 **로컬 변수 위에서** 일어난다는 점에 주목하자.

```c
buf_state = LockBufHdr(bufHdr);        /* lock + snapshot */

/* ... inspect and modify buf_state freely, in a local variable ... */
buf_state &= ~(BM_VALID | BM_DIRTY | BM_TAG_VALID);
buf_state &= ~BUF_USAGECOUNT_MASK;

UnlockBufHdr(bufHdr, buf_state);       /* write back + unlock, in one store */
```

### 4.4 · `UnlockBufHdr()` — 풀면서 동시에 쓴다

```c title="buf_internals.h"
static inline void
UnlockBufHdr(BufferDesc *desc, uint32 buf_state)
{
    pg_write_barrier();
    pg_atomic_write_u32(&desc->state, buf_state & (~BM_LOCKED));
}
```

훨씬 간단해 보이지만, 중요한 역할을 한다.

**`pg_atomic_write_u32(..., buf_state & ~BM_LOCKED)`** 여기서는 락 비트를 끈 값을 한번에 써넣음으로써, 내가 변경한 내용의 반영과 락 해제가 한번의 저장 명령으로 일어난다. 여기서의 특징은 기존의 내용을 확인하지 않은 채 덮어쓰기 해버린다는 것이다. 이는 그 동안 그 누구도 버퍼 상태를 바꾸지 않았다는 데에 기반하고 있다.

⭐️⭐️⭐️ **`pg_write_barrier()`** 현대 CPU와 컴파일러는 성능을 위해 메모리 쓰기의 순서를 재배열한다. 락 아래에서 `tag`를 바꾸고 나서 락을 푸는 코드를 썼더라도, 하드웨어가 **락 해제를 먼저 내보내고 태그 쓰기를 나중에** 내보낼
수 있다. 그러면 다른 백엔드가 "락은 풀렸는데 태그는 아직 옛것"인 상태를 볼 수 있다. 그래서 이 함수는 "**이 지점 이전의 모든 쓰기가 이 지점 이후의 쓰기보다 먼저 보이게 하라**"는 지시다. 락 해제라는 쓰기 앞에 장벽을 세워서, 락 아래에서 한 모든 작업이 **락이 풀린 것으로 보이는 순간에는 이미 완료되어 보이도록** 만든다.

### 4.5 · 락 없이 건드리려면 반드시 CAS여야 한다

원칙적으로 락 없이 상태를 변경하는 것은 반드시 CAS여야 한다. 심지어 atomic add와 같은 연산을 써도 안 된다. 아래와 같은 주석이 존재한다.

```c title="buf_internals.h"
/*
 * On the other hand, updating of state without holding
 * buffer header lock is restricted to CAS, which ensures that BM_LOCKED flag
 * is not set.  Atomic increment/decrement, OR/AND etc. are not allowed.
 */
```

`pg_atomic_fetch_add_u32(&state, BUF_REFCOUNT_ONE)`은 그 자체로 원자적이다. 그런데도 해서는 안 되는 이유에 대해서는 아래 예시를 보자. A가 헤더 락을 잡고 있는 상황에서, B가 원자적 연산을 통해서 증가시키더라도, A는 자신의 로컬 데이터를 바탕으로 다시 상태를 덮어쓰기 하기 때문에 B의 연산은 반영되지 않는다.

```text
Backend A (holds header lock)           Backend B (unlocked fetch_add)
--------------------------------------  --------------------------------------
buf_state = LockBufHdr(buf)
   → local snapshot: refcount = 1
   (works on its local copy...)
                                        pg_atomic_fetch_add_u32(&state, 1)
                                           → shared word: refcount = 2
UnlockBufHdr(buf, buf_state)
   → blind write of local copy
   → shared word: refcount = 1   ← B's increment is GONE
```

따라서 CAS에서 "내가 읽은 값이 그대로일 때만" 쓰며, CAS 전에 `BM_LOCKED`를 확인하는 것이 중요하다.

정리하면 `state`를 다루는 길은 정확히 두 갈래다.

| | 헤더 락을 잡고(`BM_LOCKED`) | 락 없이 |
| --- | --- | --- |
| 허용되는 연산 | 로컬 사본에서 자유롭게 적용 | CAS만 가능 |
| 선행조건 | `LockBufHdr()`로 획득 | `BM_LOCKED`가 꺼져 있음을 확인 필요 |
| 마무리 | `UnlockBufHdr()`의 덮어쓰기 | CAS 성공 시 성공, 실패 시 재시도 |
| `state` 바깥 필드 | 함께 다룰 수 있음 (`tag` 등) | 불가 |
| 쓰이는 곳 | 무효화, flush 판정, 축출 | 핀/언핀 (가장 흔한 경로) |

### 4.6 · 스핀락이므로 따르는 규율

헤더 락은 본질적으로 스핀락이다. 기다리는 쪽이 `while()` 루프 같은 데에서 **CPU를 태우며** 도는 락이라는 뜻이다. LWLock처럼 대기 큐에 들어가 잠드는 것이 아니다. 그래서 다음과 같은 규칙을 지켜야 한다.

- **아주 짧게 잡아야 한다.** 수십 개 명령어 정도. 지금까지 본 사용 패턴이 전부 "잠그고, 로컬
  변수 몇 줄 고치고, 푼다"인 이유다.
- **락 안에서 I/O를 하면 안 된다.** 디스크를 기다리는 시간은 아주 길 수 있다. 이 동안 다른 백엔드들이 CPU를 태운다.
- **락 안에서 메모리를 할당하면 안 된다.** 그 사이에 에러가 나면 스핀락을 해제할 수 없게 된다.
- **락 안에서 에러를 던지면 안 된다.** 위와 같은 이유다.
- **락 안에서 다른 락을 잡으면 안 된다.** 교착상태(deadlock)을 일으킬 수 있다.
- 
!!! danger "예외적으로, 핀을 잡고 있으면 `tag`는 락 없이 읽어도 된다"
    
    ```c title="buf_internals.h"
    /*
     * An exception is that if we have the buffer pinned, its tag can't change
     * underneath us, so we can examine the tag without locking the buffer header.
     */
    ```

    핀은 축출을 막는다. 축출되지 않으면 태그가 바뀔 수 없다. 그래서 **핀이 곧 태그의 안정성을
    보장**한다. 다만 조건을 정확히 지켜야 한다. 핀을 **이미 잡고 있어야** 한다. 

---

## 5 · 정리

- 버퍼 기술자는 프레임의 **내용이 아니라 사실들**을 담고, 캐시 라인 하나에 들어가도록 설계되었다.
- `state` 32비트는 **플래그 10 + `usagecount` 4 + `refcount` 18**로 쪼개져 있으며, 셋을 한 워드에 담은 목적은 **가장 흔한 연산(핀)을 CAS 한 번으로** 끝내기 위해서다.
- `BM_LOCKED`는 **자기가 보호하는 워드 안에 든 스핀락**이다. `LockBufHdr()`는 `fetch_or` 하나로
  잠그면서 동시에 스냅샷을 가져오고, `UnlockBufHdr()`는 쓰기 장벽 뒤의 저장 하나로 갱신과 해제를
  함께 끝낸다.
- `UnlockBufHdr()`의 blind write 때문에, **락 없이 `state`를 바꾸는 유일한 합법적 방법은 `BM_LOCKED`를 확인한 뒤의 CAS**다. 원자적 증가조차 허용되지 않는다.