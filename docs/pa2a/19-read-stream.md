# 19 · 읽기 스트림(`read_stream.c`)

!!! abstract 목표
    읽기 스트림이 어떤 맥락에서 필요한지, 어떤 자료구조로 이루어져 있는지, 그리고 소비자가 페이지를
    하나씩 받아 가는 동안 스트림이 어떻게 앞을 내다보며 I/O를 발행하는지를 이해한다.

[§06](06-read-path.md)에서 `ReadBuffer()` 경로를 따라가며, 그 경로가 사실상 동기 읽기라는 점을
확인했다. 시작하자마자 곧바로 기다리기 때문이다. 그렇다면 PostgreSQL 18의 비동기 I/O는 대체 누가
쓰는가 — 그 답이 읽기 스트림이다.

이 장은 버퍼 풀 자체를 다루지 않는다. 읽기 스트림은 버퍼 매니저 **위**에 얹힌 계층이고, 결국
`StartReadBuffers()`를 통해 [§06](06-read-path.md)에서 본 `PinBufferForBlock()` → `BufferAlloc()`으로
합류한다. PA2a에서 고쳐야 할 코드는 아니지만, gdb로 순차 스캔을 밟다 보면 반드시 지나가게 된다.

## 왜 필요한가

`ReadBuffer()`를 블록마다 한 번씩 부르는 방식에는 두 가지 손해가 있다. 파일 상단 주석이 그대로
말한다.

> Calling the simple `ReadBuffer()` function for each block is inefficient, because blocks that are
> not yet in the buffer pool require I/O operations that are **small** and might **stall** waiting
> for storage.

* **작다** — 8KB 요청 하나씩 내려간다. 이어진 블록 16개를 읽어도 시스템 콜이 16번이다.
* **멈춘다** — 요청을 낸 백엔드는 그 자리에서 기다린다. 그동안 CPU도 디스크도 논다.

15.2까지 순차 스캔이 그럭저럭 빨랐던 것은 순전히 **커널의 readahead** 덕이었다. 15.2의
`heapam.c`에는 순차 스캔용 프리페치 호출이 아예 없고, 커널이 접근 패턴을 눈치채고 대신 미리
읽어줬을 뿐이다. 그러나 커널의 추측에 기대는 방식은 비트맵 힙 스캔처럼 **순차가 아닌** 접근에서는
전혀 도움이 되지 않는다.

읽기 스트림의 발상은 단순하다. **읽을 블록 번호를 미리 알 수 있는 접근이라면, 그 정보를 데이터베이스가
직접 활용하자**는 것이다. 그래서 두 가지를 한다.

| | 얻는 것 |
| --- | --- |
| **병합**(combining) | 연속된 블록을 `io_combine_limit`까지 묶어 `preadv()` 한 번으로 |
| **겹침**(concurrency) | `max_ios`개의 I/O를 동시에 띄워 두고, 소비자가 앞 버퍼를 처리하는 동안 진행 |

## 어디에서 쓰이는가

| 호출처 | 콜백이 돌려주는 블록 |
| --- | --- |
| `heapam.c` — 순차 스캔 | `heapgettup_advance_block()`이 계산한 다음 블록 |
| `heapam.c` — 비트맵 힙 스캔 | TID 비트맵을 순회하며 나오는 블록 |
| `vacuumlazy.c` | 건너뛰기를 반영한 다음 검사 대상 블록 |
| `analyze.c` | 샘플링으로 뽑힌 블록 |
| `nbtree.c`, `gistvacuum.c`, `spgvacuum.c` | 인덱스 전체 스캔·정리 대상 블록 |
| `bufmgr.c` — `CreateAndCopyRelationData()` | 릴레이션 전체 복사 |

공통점은 **"다음에 읽을 블록을 지금 알 수 있다"** 는 것뿐이다. 순차인지 여부는 조건이 아니다.

## 시작하기

소비자는 스트림을 열면서 **콜백**을 등록한다.

```c title="include/storage/read_stream.h"
typedef BlockNumber (*ReadStreamBlockNumberCB) (ReadStream *stream,
                                                void *callback_private_data,
                                                void *per_buffer_data);

extern ReadStream *read_stream_begin_relation(int flags,
                                              BufferAccessStrategy strategy,
                                              Relation rel,
                                              ForkNumber forknum,
                                              ReadStreamBlockNumberCB callback,
                                              void *callback_private_data,
                                              size_t per_buffer_data_size);
```

콜백은 다음에 읽을 블록 번호를 하나씩 돌려주고, 더 없으면 `InvalidBlockNumber`를 돌려준다. 순차
스캔의 콜백은 이렇게 생겼다.

```c title="heapam.c:288"
static BlockNumber
heap_scan_stream_read_next_serial(ReadStream *stream, void *callback_private_data,
                                  void *per_buffer_data)
{
    HeapScanDesc scan = (HeapScanDesc) callback_private_data;

    if (unlikely(!scan->rs_inited))
        scan->rs_prefetch_block = heapgettup_initial_block(scan, scan->rs_dir);
    else
        scan->rs_prefetch_block = heapgettup_advance_block(scan, scan->rs_prefetch_block,
                                                           scan->rs_dir);
    return scan->rs_prefetch_block;
}
```

`per_buffer_data`는 콜백이 블록마다 딸려 보내고 싶은 정보를 담는 자리다. 비트맵 힙 스캔이
`TBMIterateResult`를 여기 실어 보낸다. 순차 스캔은 쓰지 않으므로 크기가 0이다.

### 플래그

| 플래그 | 뜻 |
| --- | --- |
| `READ_STREAM_DEFAULT` | 기본 |
| `READ_STREAM_MAINTENANCE` | `effective_io_concurrency` 대신 `maintenance_io_concurrency`를 쓴다 (VACUUM 등) |
| `READ_STREAM_SEQUENTIAL` | 순차임을 미리 알림. **프리페치 조언을 끈다** — 리눅스 커널의 readahead가 더 낫기 때문 |
| `READ_STREAM_FULL` | 릴레이션 전체를 읽을 것이므로 초기 ramp-up을 건너뛴다 |
| `READ_STREAM_USE_BATCHING` | AIO 배치 모드 사용. 콜백이 I/O 대기 중 블로킹하지 않는다고 보증해야 한다 |

## 자료구조

핵심은 **두 개의 원형 큐**다. 하나는 핀해 둔 버퍼들, 다른 하나는 진행 중인 I/O들이다. 파일 상단
주석의 예시가 정확하다 — 콜백이 10, 42, 43, 44, 60을 차례로 돌려준 상황:

```text
                     buffers          ios
                     +----+        +--------+
oldest_buffer_index →| 10 |   +----+ 42..44 | ← oldest_io_index
                     | 42 |←--+|   +--------+
                     | 43 |    | +-+ 60..60 |
                     | 44 |    | | +--------+
                     | 60 |←---+-+ |        | ← next_io_index
next_buffer_index →  |    |        +--------+
```

블록 10은 히트라 딸린 I/O가 없다. 42~44는 연속이므로 **하나의 I/O로 병합**되었고, 60은 불연속이라
별도 I/O다. 두 큐가 이렇게 연결되어 있어서, 버퍼를 꺼낼 때 "이 버퍼가 아직 진행 중인 I/O에
속하는가"를 인덱스 비교 한 번으로 판정할 수 있다.

`ReadStream` 구조체에서 눈여겨볼 필드들:

| 필드 | 의미 |
| --- | --- |
| `distance` | **앞을 얼마나 내다볼 것인가.** 적응적으로 변한다 |
| `max_ios` | 동시에 띄울 수 있는 I/O 수 (`effective_io_concurrency` 등에서 결정) |
| `io_combine_limit` | 한 I/O로 묶을 수 있는 최대 블록 수 |
| `pinned_buffers` | 지금 핀해 둔 버퍼 수 |
| `pending_read_blocknum`, `pending_read_nblocks` | **아직 발행하지 않고 키우는 중인** 읽기 |
| `buffered_blocknum` | 콜백에게서 받았지만 아직 쓰지 못한 블록 하나를 되돌려 두는 자리 |
| `seq_blocknum` | 순차 접근 감지용. 다음에 이어질 것으로 예상되는 블록 |
| `fast_path` | 전부 캐시에 있는 스캔용 지름길 |

`pending_read_*`가 이 설계의 요점이다. 콜백에게서 블록 번호를 받아도 **곧바로 발행하지 않는다.**
다음 블록이 이어지는 번호이면 `pending_read_nblocks`를 늘려 키워 나가고, 이어지지 않거나
`io_combine_limit`에 닿았을 때 비로소 `StartReadBuffers()`로 내보낸다.

### 큐 크기

```c title="read_stream.c — read_stream_begin_impl 발췌"
max_pinned_buffers = (max_ios + 1) * io_combine_limit;
…
max_pinned_buffers = Min(strategy_pin_limit, max_pinned_buffers);
max_pinned_buffers = Min(max_pinned_buffers, max_possible_buffer_limit);
max_pinned_buffers = Max(1, max_pinned_buffers);

queue_size = max_pinned_buffers + 1;
```

`max_ios + 1`인 이유는 주석에 있다 — I/O 하나가 끝났을 때 **그 버퍼들이 소비되기를 기다리지 않고**
곧바로 다음 I/O를 시작할 수 있도록 한 벌의 여유를 둔 것이다. `+1`은 소비자가 `per_buffer_data`를
다음 호출 전까지 들고 있을 수 있어서 머리와 꼬리 사이에 틈을 두기 위한 것이다.

핀 개수 상한이 두 번 더 적용된다는 점도 중요하다. 접근 전략(링 버퍼)의 한도와, 버퍼 매니저가 이
백엔드에 허용하는 핀 한도다. 스트림 하나가 버퍼 풀을 독차지하지 못하게 하는 장치다.

## 어떻게 관리되는가

소비자가 부르는 함수는 사실상 하나뿐이다.

```c
while ((buf = read_stream_next_buffer(stream, NULL)) != InvalidBuffer)
{
    … 페이지 처리 …
    ReleaseBuffer(buf);
}
read_stream_end(stream);
```

이 한 번의 호출이 순서대로 세 가지를 한다.

### 1. 가장 오래된 버퍼를 꺼내고, 필요하면 그것만 기다린다

```c title="read_stream.c:790 — read_stream_next_buffer 발췌"
buffer = stream->buffers[oldest_buffer_index];

/* Do we have to wait for an associated I/O first? */
if (stream->ios_in_progress > 0 &&
    stream->ios[stream->oldest_io_index].buffer_index == oldest_buffer_index)
{
    WaitReadBuffers(&stream->ios[io_index].op);
    stream->ios_in_progress--;
    …
    /* Look-ahead distance ramps up rapidly after we do I/O. */
    distance = stream->distance * 2;
    distance = Min(distance, stream->max_pinned_buffers);
    stream->distance = distance;
}
```

**딸린 I/O 하나만** 기다린다. 뒤쪽 I/O들은 진행 중인 채로 둔다. 그리고 기다렸다는 것은 곧 미스가
있었다는 뜻이므로, 그 자리에서 `distance`를 **두 배로** 늘린다.

### 2. 자리가 났으니 앞을 더 채운다

함수 끝에서 `read_stream_look_ahead()`를 부른다. 이 함수가 스트림의 심장이다.

```c title="read_stream.c:429 — read_stream_look_ahead, 뼈대만"
while (stream->ios_in_progress < stream->max_ios &&
       stream->pinned_buffers + stream->pending_read_nblocks < stream->distance)
{
    if (stream->pending_read_nblocks == stream->io_combine_limit)
    {
        read_stream_start_pending_read(stream);      /* 꽉 찼으니 발행 */
        continue;
    }

    blocknum = read_stream_get_block(stream, per_buffer_data);   /* 콜백 호출 */
    if (blocknum == InvalidBlockNumber)
    {
        stream->distance = 0;                        /* 스트림 끝 */
        break;
    }

    /* 이어지는 블록이면 키운다 */
    if (stream->pending_read_nblocks > 0 &&
        stream->pending_read_blocknum + stream->pending_read_nblocks == blocknum)
    {
        stream->pending_read_nblocks++;
        continue;
    }

    /* 안 이어지면 지금까지 모은 것을 발행하고 새로 시작 */
    while (stream->pending_read_nblocks > 0)
    {
        if (!read_stream_start_pending_read(stream) ||
            stream->ios_in_progress == stream->max_ios)
        {
            read_stream_unget_block(stream, blocknum);   /* 되돌려 둔다 */
            return;
        }
    }
    stream->pending_read_blocknum = blocknum;
    stream->pending_read_nblocks = 1;
}
```

루프 조건이 두 가지다 — **I/O 슬롯이 남아 있을 것**, 그리고 **아직 `distance`만큼 못 나갔을 것**.
그 안에서 콜백을 계속 호출하며 연속 블록을 병합해 나간다.

`read_stream_unget_block()`이 재미있는 부분이다. 콜백에게서 블록 번호를 받았는데 한도에 걸려 쓸 수
없게 되면, 콜백을 되돌릴 방법이 없으므로 그 번호를 `buffered_blocknum`에 넣어 두었다가 다음번에
콜백 대신 꺼내 쓴다. 한 칸짜리 밀어내기(pushback) 버퍼다.

### 3. 버퍼를 반환한다

그래서 실제 시간 축은 이렇게 흐른다.

```text
호출 1: [wait(10)] [look-ahead: 42..44 발행, 60 발행] → 버퍼 10 반환
        ─────────── 소비자가 블록 10의 튜플을 훑는 동안 ───────────
                    42..44, 60의 I/O는 커널에서 진행 중
호출 2: [wait(42..44)] [look-ahead: 다음 것 발행]      → 버퍼 42 반환
호출 3: (이미 완료됨, 대기 없음)                        → 버퍼 43 반환
```

**별도의 스레드는 없다.** 제어 흐름은 계속 한 프로세스 안에 있고, 겹침이 생기는 이유는 I/O가 그
프로세스 밖(커널·io_uring·I/O 워커)에서 돌기 때문이다. 소비자가 "일을 끼워 넣는" 것이 아니라,
소비자가 자기 일을 하는 동안 발행해 둔 I/O가 알아서 진행되는 것이다.

## 앞을 얼마나 내다보는가

`distance`는 고정값이 아니라 최근 히트/미스 이력에 따라 움직인다.

| 사건 | `distance` |
| --- | --- |
| 스트림 시작 | `1` (또는 `READ_STREAM_FULL`이면 `io_combine_limit`) |
| I/O를 기다림 (미스) | **× 2** (`max_pinned_buffers`까지) |
| 발행했는데 대기 불필요 (히트) | **− 1** (최소 1) |
| 콜백이 끝을 알림 | `0` |

시작값이 1인 이유는 주석에 있다 — *"When no I/O is necessary, there is no benefit in looking ahead
more than one block."* 전부 캐시에 있는 스캔에서 앞을 내다보는 것은 순수한 낭비다. 그래서 **낙관적으로
시작해서 미스를 만나면 급격히 늘리고, 히트가 이어지면 서서히 줄인다.**

전부 캐시에 있는 경우를 위한 `fast_path`가 따로 있다는 점도 그래서다. 큐 관리와 I/O 큐를 통째로
건너뛰고, 같은 버퍼 슬롯에서 단수형 `StartReadBuffer()`만 부른다.

## `io_method = sync`일 때

PA2a의 가정(G8)이 `io_method = sync`이므로 이 경우를 따로 짚어 둔다. **읽기 스트림은 여전히
사용된다.** `io_method`는 그 아래에서 I/O를 어떻게 수행할지만 정하기 때문이다.

* **병합은 그대로 남는다.** 42~44를 `preadv()` 한 번으로 읽는 이득은 동기 I/O에서도 유효하다.
* **AIO를 통한 겹침은 사라진다.** sync 모드의 `StartReadBuffers()`는 I/O를 시작하지 않고 "기다려야
  함"만 표시하므로, 실제 읽기는 소비 시점의 `WaitReadBuffers()`에서 일어난다.
* **대신 조언(advice) 기반의 유사 비동기가 켜진다.** 스트림이 `smgrprefetch()`(리눅스에서는
  `posix_fadvise(WILLNEED)`)로 커널에게 미리 읽으라고 알린다.

```c title="read_stream.c — read_stream_begin_impl 발췌"
/*
 * Read-ahead advice simulating asynchronous I/O with synchronous calls.
 * Issue advice only if AIO is not used, direct I/O isn't enabled, the
 * caller hasn't promised sequential access (overriding our detection
 * heuristics), and max_ios hasn't been set to zero.
 */
if (stream->sync_mode &&
    (io_direct_flags & IO_DIRECT_DATA) == 0 &&
    (flags & READ_STREAM_SEQUENTIAL) == 0 &&
    max_ios > 0)
    stream->advice_enabled = true;
```

조건에 `READ_STREAM_SEQUENTIAL`이 아닐 것이 들어 있다는 점을 주의하자. **순차 스캔은 조언을 켜지
않는다.** 헤더 주석이 이유를 밝힌다 — *"Explicit advice is known to perform worse than letting the
kernel (at least Linux) detect sequential access."* 즉 sync 모드에서 순차 스캔은 15.2와 비슷하게
커널 readahead에 의존하고, 얻는 것은 병합뿐이다. 반면 비트맵 힙 스캔이나 VACUUM처럼 불규칙한
접근은 조언을 통해 겹침 효과를 얻는다.

## 끝내기와 되돌리기

```c
void read_stream_reset(ReadStream *stream);   /* 소비되지 않은 버퍼를 모두 핀 해제 */
void read_stream_end(ReadStream *stream);     /* reset + pfree */
```

`read_stream_reset()`은 `distance = 0`으로 앞보기를 멈춘 뒤, 남아 있는 버퍼를 전부 꺼내
`ReleaseBuffer()`하고, `distance = 1`로 되돌린다. 스캔 방향이 바뀔 때 이것이 필요하다 — 앞서 읽어 둔
블록들이 전부 반대 방향이라 쓸모없어지기 때문이다. `heap_fetch_next_buffer()`가 정확히 그 처리를
한다.

```c title="heapam.c:646"
if (unlikely(scan->rs_dir != dir))
{
    scan->rs_prefetch_block = scan->rs_cblock;
    read_stream_reset(scan->rs_read_stream);
}
```

## PA2a 관점에서

!!! lens "PA2a 관점"

    읽기 스트림은 **고칠 대상이 아니다.** 다만 두 가지를 기억해 두면 디버깅이 편하다.

    첫째, 순차 스캔을 gdb로 밟으면 `heapgettup()` 아래에 `read_stream_next_buffer()`,
    `read_stream_look_ahead()`, `read_stream_start_pending_read()`가 끼어 있다. 15.2의
    `heapgetpage()` → `ReadBufferExtended()`를 기억하고 있다면 낯설지만, 이들은 전부
    `PinBufferForBlock()` → `BufferAlloc()`으로 합류한다.

    둘째, 스트림은 한 번에 **여러 개의 버퍼를 핀해 둔다**(`max_pinned_buffers`개까지). 버퍼 풀을
    쪼개거나 축출 정책을 바꿀 때, "한 백엔드가 동시에 여러 프레임을 붙잡고 있다"는 사실이 전제를
    깨뜨리지 않는지 확인하라. 스트림 자신도 접근 전략과 백엔드 핀 한도로 스스로를 제한하고 있다.
