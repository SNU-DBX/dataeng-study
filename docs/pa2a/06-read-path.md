# 06 · "사용자" 입장에서 본 버퍼 풀

!!! abstract 목표
    버퍼 풀 **바깥**에서 버퍼 풀을 사용하는 PostgreSQL 내의 subsystem이 누구인지, 어떤 상황에서 버퍼 풀을 사용하며 어떤 함수를 고르는지 파악한다.

지금까지 버퍼 풀의 구성 요소를 살펴 보았다. 이제 버퍼 풀의 내부를 이해하기에 앞서, 버퍼 풀이 누구에 의해 어떻게 사용되는지를 먼저 이해한다. 버퍼 풀의 외부에 있는 "사용자"(정확히 말해서 이건 사람이 아니라, PostgreSQL 내의 다른 기능을 위한 코드들을 말한다) 입장에서 볼 때, 버퍼 풀이 해 주어야 하는 일은 어떤 데이터 페이지를 제공하는 것이면 충분하다. 그 안에서 어떻게 구현되든 상관없이 말이다. 그러나 반대로 말하면 우리가 버퍼 풀 내부의 구현을 수정하게 되더라도, 외부에 제공하는 것은 동일해야 한다. 

## 1 · 버퍼 풀의 사용자는 누구인가

`src/include/storage/bufmgr.h`를 `#include`하는지를 바탕으로 세어보면, 다음과 같이 사용자를 구별해 볼 수 있다. 크게 다섯 부류다.

| 종류 | 대표 파일 | 무엇을 위해 페이지를 읽는가 |
| --- | --- | --- |
| **접근 방법(access method)** | `access/heap/`, `access/nbtree/`, `access/gist/`, `access/gin/`, `access/brin/`, `access/spgist/`, `access/hash/` | 테이블과 인덱스의 실제 데이터(대부분의 경우) |
| **여타 포크(fork) 관리** | `storage/freespace/freespace.c`, `access/heap/visibilitymap.c` | FSM, 가시성 맵 — 데이터를 관리하기 위한 메타데이터 |
| **복구(recovery)** | `access/transam/xlogutils.c`, `xlogrecovery.c` | WAL 레코드가 가리키는 페이지를 재생하기 위함 |
| **유지보수 명령** | `access/heap/vacuumlazy.c`, `commands/analyze.c`, `commands/dbcommands.c` | VACUUM, ANALYZE, `CREATE DATABASE` |
| **기타** | `commands/sequence.c` | 특수한 경우 |

!!! note "사용자가 적어도 무관한 이유"

    데이터베이스의 상위 계층(executor, planner, parser)은 버퍼 풀을 전혀 모르고, 알 필요도 없다. 데이터를 접근하기 위해서는 접근 방법(대표적으로 heap)을 통해 데이터를 요청하면 그만이다. 실행기(executor)는 예를 들어서 `heap_getnext()`를 부르고 튜플을 받을 뿐, 그 튜플이 어느 프레임에 있는지 알 필요가 없다. 반대 방향도 참이다. 버퍼 풀은 자기가 담고 있는 8KB의 페이지가 평범한 데이터 페이지인지 인덱스 페이지인지 모른다.

## 2 · 버퍼 풀로부터 읽기 위한 세 가지 경로

버퍼 풀로부터 읽거나 쓰기 위한 페이지를 받아오는 함수는 크게 세 가지가 있다. 가장 많이 사용되는 것이 `ReadBuffer()` 계열로, 우리가 보통 생각하는 데이터/인덱스 페이지를 딱 1개 읽고자 할 때 쓰인다. 이와 유사하되 조금 더 특수한 예외적인 경우로서 하나의 블록이 아니라 여러 개의 블록을 접근하고자 할 때는 `ReadBuffer()`를 경유하지 않고 바로 그 아래에 있는 함수들인 `StartReadBuffers(), WaitReadBuffers()`를 **직접** 호출한다. 예를 들어서 우리가 어떤 테이블 하나를 처음부터 끝까지 읽고자 한다면 이런 류의 방식이 사용되며, 이를 `src/backend/storage/aio/read_stream.c`에서 지원하고 있다. 마지막으로 테이블을 확장하기 위해 새로운 빈 페이지가 필요한 상황에 사용하는 것으로서 `ExtendBufferedRel()`계열이 있다.

| 호출자의 상황 | 쓰는 API | 
| --- | --- |
| "블록 번호 **하나**를 지금 알고 있다" | `ReadBuffer()` 계열 |
| "앞으로 읽을 블록 번호들을 **미리 알 수 있다**" | `read_stream`으로부터 직접 호출 |
| "읽고 싶은 블록이 **아직 존재하지 않는다**" | `ExtendBufferedRel()` 계열 |

각 경우에 불리는 함수들을 따라가보면 아래와 같게 되는데, 밑에서 보다 상세히 살펴 볼 것이다.

```text
      B-트리 탐색           순차 스캔 / VACUUM       INSERT가 자리를 못 찾음
      "다음은 456번"         "0번부터 끝까지"         "새 블록이 필요하다"
           │                       │                        │
           ▼                       ▼                        ▼
     ReadBuffer 계열         read_stream 계열        ExtendBufferedRel 계열
           │                       │                        │
           │ 한 블록                 │ 여러 블록                │
           ▼                       ▼                        │
    StartReadBuffer()       StartReadBuffers()              │
       (단수형)                (복수형)                      │
           └───────────┬───────────┘                        │
                       ▼                                    ▼
              StartReadBuffersImpl()            ExtendBufferedRelCommon()
                       │                                    │
                       ▼                                    │
               PinBufferForBlock()                          │
                       │                                    │
                       ▼                                    │
                  BufferAlloc()  ◀── 버퍼 찾기/할당             │
                       │                                    │
                       └─────────────┬──────────────────────┘
                                     ▼
                            GetVictimBuffer()
                            (빈 프레임 공급)
```

위에서 볼 수 있듯 `ReadBuffer()` 계열도 결국 `StartReadBuffer()`를 지난다. 다만 `ReadBuffer()` 계열은 항상 한 블록(`StartReadBuffer()`, 단수형), 읽기 스트림은 여러 블록(`StartReadBuffers()`, 복수형)을 요청한다. 둘 다 같은 `StartReadBuffersImpl()`로 들어간다. 이는 [다음 문서](./07-bufferalloc.md)에서 보다 상세히 살펴 볼 것이다.

## 3 · `ReadBuffer()` 계열 — 블록 하나 읽기

가장 많이 쓰이는 경로다. 본래 Postgres 15 (2022.10월 공개)까지만 해도 이것이 사실상 유일한 경로였다. 현재(18.4버전 기준)는 여러 개의 블록을 한번에 확장하기 위한 `ExtendBufferedRel()` 계열이 분리되어 나왔지만, 여전히 다른 특수 케이스들(예: 디스크로부터 읽지 않고 0으로 채워서 프레임을 받고자 하는 경우)을 처리하기 위한 `ReadBufferExtended()`가 남아있다. 사실 `ReadBuffer()`는 가장 기본이 되는 경우를 처리하기 위한 일종의 껍데기로서, 나머지 인자를 기본값으로 채워 `ReadBufferExtended()`로 넘긴다. 즉 `ReadBufferExtended()`가 무엇인지 이해하는 것만으로도 `ReadBuffer()`는 이해할 수 있다.

```c title="src/backend/storage/buffer/bufmgr.c"
Buffer
ReadBuffer(Relation reln, BlockNumber blockNum)
{
    return ReadBufferExtended(reln, MAIN_FORKNUM, blockNum, RBM_NORMAL, NULL);
}
```

아래 네 가지 경우에 대해서 간략히 정리해보면 다음과 같다.

```c title="src/include/storage/bufmgr.h (편의상 순서 조정)"
extern Buffer ReadBuffer(Relation reln, BlockNumber blockNum);
extern Buffer ReadBufferExtended(Relation reln, ForkNumber forkNum,
                                 BlockNumber blockNum, ReadBufferMode mode,
                                 BufferAccessStrategy strategy);
extern Buffer ReadBufferWithoutRelcache(RelFileLocator rlocator,
                                        ForkNumber forkNum, BlockNumber blockNum,
                                        ReadBufferMode mode, BufferAccessStrategy strategy,
                                        bool permanent);
extern bool   ReadRecentBuffer(RelFileLocator rlocator, ForkNumber forkNum,
                               BlockNumber blockNum, Buffer recent_buffer);
```

| 함수 | 언제 쓰는가 | 18.4 버전 내 호출 지점들 (예시)) |
| --- | --- | --- |
| `ReadBuffer()` | `Relation`이 있고, 메인 포크의 평범한 읽기다 (가장 일반적인 경우). | 66곳 — `heapam.c`, `nbtpage.c`, `gist*.c`, `brin*.c` … |
| `ReadBufferExtended()` | ReadBuffer()로부터 불리거나, 특수한 경우(포크가 메인이 아니거나, 특수 모드나 접근 전략이 필요할 때 등) | 29곳 — `visibilitymap.c`, `freespace.c`, `vacuumlazy.c` … |
| `ReadBufferWithoutRelcache()` | `Relation`이 없으며, 릴레이션 캐시를 못 쓰는 상황 | 2곳 — `xlogutils.c:497`, `dbcommands.c:298` |
| `ReadRecentBuffer()` | "아마 이 프레임에 있을 것이다"라는 추측이 있다 | 1곳 — `xlogutils.c:473` |

우리가 주로 신경써야 하는 것은 `ReadBuffer()`를 위한 경우, 즉 가장 기본적인 인자들로 `ReadBufferExtended()`를 불렀을 때 생기는 일이다. 가장 아래의 두 가지는 데이터베이스를 복구하는 특수한 상황이나, 데이터베이스 자체를 만들기 위한 작업에 쓰인다(당장 이해되지 않아도 무방함). 따라서 일반적인 데이터베이스 작동 과정에서는 사용되지 않는다.

!!! info "PostgreSQL에서 말하는 포크(fork)란?"
    위에서 `MAIN_FORKNUM`과 같이, 포크가 등장했다. 사실 이것은 데이터베이스에서 필수적인 개념은 아니며, PostgreSQL이 선택한 구현의 일부다. PostgreSQL은 **하나의 릴레이션(테이블) 데이터와 이를 위한 메타데이터를 디스크 상에서 여러 개의 파일로 나뉘어 저장하는데, 그 각각을 포크(fork)라고 부른다** (프로세스를
    복제하는 `fork()` 시스템 콜과는 아무 관계가 없고, "한 그루에서 갈라져 나온 가지"라는 뜻에 가깝다.). 실질적으로 의미가 있는 fork는 3가지다: main, fsm, vm. main은 테이블의 데이터이며, fsm과 vm은 메타데이터다.

    ```c title="src/include/common/relpath.h"
    typedef enum ForkNumber
    {
        InvalidForkNumber = -1,
        MAIN_FORKNUM = 0,
        FSM_FORKNUM,
        VISIBILITYMAP_FORKNUM,
        INIT_FORKNUM,
    } ForkNumber;
    ```

    예를 들어서 실제로 데이터베이스를 파일로 저장하게 되면, 다음과 같이 보인다. 파일 이름에 들어간 숫자(`24576`)가 릴레이션 번호(`relNumber`)이고, 뒤에 붙는 접미사가 포크 종류다. 데이터를 담고 있는 메인 포크는 접미사가 없다.

    ```console
    $ ls base/16384/
    24576        ← main : 테이블의 실제 데이터
    24576_fsm    ← fsm  : 각 페이지에 남은 여유 공간
    24576_vm     ← vm   : 각 페이지의 가시성
    ```

    fsm과 vm이 각각 무엇을 의미하는지 현 시점에서 정확히 이해할 필요는 없으나, 그것들을 main 포크의 데이터와는 다르게 취급해야 한다는 점은 이해해야 한다. 그리고 이들은 다행히도 서로 다른 ForkNumber로서 구분이 된다. 그래서 `ReadBuffer()`는 메인 포크밖에 읽지 못하고, fsm, vm을 읽으려면 `ReadBufferExtended()`로 포크 번호를 명시해야 한다.

    #### ⭐️⭐️⭐️ fsm과 vm의 의미

    | 포크 | 파일 | 무엇이 들어 있나 | 없으면 어떻게 되나 |
    |---|---|---|---|
    | `MAIN_FORKNUM` | `24576` | **튜플이 담긴 진짜 데이터 페이지** | 데이터가 없다 |
    | `FSM_FORKNUM` | `24576_fsm` | 페이지마다 "여유 공간이 얼마나 남았나" | INSERT가 느려질 뿐, 정확성은 유지된다 |
    | `VISIBILITYMAP_FORKNUM` | `24576_vm` | 페이지마다 "모두에게 보이나 / 얼어 있나" | VACUUM이 느려질 뿐, 정확성은 유지된다 |
    | `INIT_FORKNUM` | `24576_init` | UNLOGGED 테이블의 "초기 상태" 사본 | UNLOGGED 테이블에만 존재한다 |

    핵심은 뒤의 세 포크는 사실 메인 포크의 데이터에 대한 특성을 요약해서 말해준다. 메인 포크의 페이지
    하나당 FSM은 1바이트를, VM은 2비트를 갖는다. 따라서 원본보다 수천 배 작기 때문에 통째로 버퍼 풀에 올리더라도 크게 문제가 없으며, 설령 없어지더라도 데이터 자체가 손상되는 것은 아니다.

    **FSM(free space map)** 은 `INSERT`로 새로운 데이터를 추가하고 싶을 때, 충분한 자리가 남은 공간을 찾는데 쓰인다. 각 페이지를 1바이트로 표현하게 되는데 1바이트로는 0~255까지밖에 표현할 수 없으므로 8 KB를 256단계로 쪼개
    32바이트 단위로 내림한다(`FSM_CAT_STEP = BLCKSZ / 256`). 이렇게 뭉개도 되는 이유는 이것이 절대적인 정답이 아니라 힌트로 사용되기 때문이다. 설령 FSM이 충분한 자리가 있다고 해서 가보았는데 자리가 부족하면 다시 찾으면 된다.

    **VM(visibility map)** 은 페이지마다 2비트만 쓰기 때문에 4가지 경우(0, 1, 2, 3)만을 표현할 수 있는데 다음과 같은 정보를 각각의 비트에 지정한다.

    * `VISIBILITYMAP_ALL_VISIBLE = 0x01` — 이 페이지의 모든 튜플이 모든 트랜잭션에게 보인다
    * `VISIBILITYMAP_ALL_FROZEN = 0x02` — 그에 더해 모두 얼어 있다(frozen)

    2비트뿐이라 8 KB짜리 VM 페이지 하나가 데이터 페이지 약 32,000개, 즉 약 255 MB의 테이블할 수 있다. 이를 활용하여 VACUUM은 `ALL_VISIBLE`인 페이지를 아예 읽지 않고 건너뛰고, 인덱스 온리 스캔(index-only scan)은 인덱스에서 찾은 튜플이 `ALL_VISIBLE` 페이지에 있다면 힙 페이지를 읽지 않고 인덱스에 있는 값만으로 답한다 (**visibility**에 대해서는 이후 MVCC를 학습한 뒤에 이해할 수 있다).

!!! info "⭐️⭐️⭐️ `Relation`이 없다는 것은?"

    우리가 흔히 '테이블'이라고 부르는 것이 PostgreSQL 내에서 '릴레이션'이라고 불린다. 그리고 여기서 말하는 `Relation`은 릴레이션 캐시(relcache)가 관리하는 큰 구조체로, 해당 테이블에 대한 각종 정보(스키마·통계·인덱스 등)를 들고 있다. 이것을 얻으려면 시스템 카탈로그를 읽을 수 있어야 하는데, **복구** 중에는 카탈로그를 읽을 수
    없다. 카탈로그 자체도 아직 복구되지 않은 릴레이션이기 때문이다. `CREATE DATABASE`라는 명령어도 마찬가지로
    아직 카탈로그에 등록되지 않은 디렉터리를 복사한다.

    그래서 이 두 곳은 `Relation` 대신 `RelFileLocator`(테이블스페이스·DB·릴레이션 OID 세 개)만으로
    페이지를 읽는다. 버퍼 태그에 필요한 것은 사실 이것뿐이므로 아무 문제가 없다.

    `ReadRecentBuffer()`는 복구 전용으로 최적화된 함수다. WAL 재생은 같은 페이지를 연속으로 여러 번 건드리는
    일이 잦아서, 직전에 쓴 `Buffer` 번호를 기억해 두었다가 "그 프레임에 아직 그 페이지가 있으면
    해시 테이블 조회 없이 바로 핀만 잡아라"라고 요청한다. 맞으면 `true`, 아니면 `false`를 돌려주고
    호출자가 정식 경로로 다시 읽는다.

`ReadBufferExtended()`에서 앞의 세 인자인 `Relation reln, ForkNumber forkNum, BlcokNumber blockNum`은 우리가 원하는 데이터를 특정하기 위한 용도다(버퍼 태그를 생각해 보면 된다). 그러면 나머지 두 인자인 `ReadBufferMode mode, BufferAccessStrategy strategy`에 대해 알아보자.

### `ReadBufferMode` — 무슨 목적으로 읽는가

이름과 달리 이것은 "읽기 방식"이 아니라 무슨 목적으로 읽는지, 즉 **"디스크 내용을 신경 쓸 것인가"** 의 선택이다. 우리가 알고 있는 보통의 데이터 읽기는 `RBM_NORMAL`을 쓴다. 위에서 살펴 본 `ReadBuffer()`도 `RBM_NORMAL`을 인자로 넣은 것을 볼 수 있다. 다른 것들은 새로운 빈 버퍼 페이지를 받거나, 아니면 특수한 용도(복구)를 위해 사용된다.

```c title="src/include/storage/bufmgr.h"
typedef enum
{
    RBM_NORMAL,                 /* Normal read */
    RBM_ZERO_AND_LOCK,          /* Don't read from disk, caller will
                                 * initialize. Also locks the page. */
    RBM_ZERO_AND_CLEANUP_LOCK,  /* Like RBM_ZERO_AND_LOCK, but locks the page
                                 * in "cleanup" mode */
    RBM_ZERO_ON_ERROR,          /* Read, but return an all-zeros page on error */
    RBM_NORMAL_NO_LOG,          /* Don't log page as invalid during WAL
                                 * replay; otherwise same as RBM_NORMAL */
} ReadBufferMode;
```

* `RBM_ZERO_AND_LOCK`은 빈 프레임을 확보하고 0으로 채운다. 디스크에 무엇이 있든 상관없다는 뜻이다.
  복구가 전체 페이지 이미지(full-page image)를 덮어쓸 때, 그리고 새 인덱스 페이지를 초기화할 때
  쓴다.
* `RBM_ZERO_ON_ERROR`는 읽되, 읽기가 실패해도 에러를 내지 않고 0으로 채워진 페이지를 준다.
  `zero_damaged_pages`와 짝을 이루는, 손상된 테이블에서 데이터를 건지기 위한 도구다.
* `RBM_NORMAL_NO_LOG`는 복구가 "이 페이지가 유효하지 않다"는 로그를 남기지 않게 한다.

### `BufferAccessStrategy` — cache pollution 예방하기

```c
typedef enum BufferAccessStrategyType
{
    BAS_NORMAL,     /* Normal random access */
    BAS_BULKREAD,   /* Large read-only scan (hint bit updates are ok) */
    BAS_BULKWRITE,  /* Large multi-block write (e.g. COPY IN) */
    BAS_VACUUM,     /* VACUUM */
} BufferAccessStrategyType;
```

`BufferAccessStrategy`는 버퍼 풀이 효율적으로 동작하는 데에 중요한 역할을 한다. 버퍼 풀이 가득찬 상황에서 새로운 페이지를 집어넣고 싶다면, 기존의 페이지 중 하나를 골라서 퇴출해야 한다. 이때 PostgreSQL은 기본적으로 clock-sweep 알고리즘을 적용해서 퇴출할 희생양을 선정한다(`BAS_NORMAL`). 그러나 만약 우리가 커다란 테이블을 처음부터 끝까지 스캔해 나간다고 하면, **딱 한 번 읽고 다시 안 볼 페이지가 버퍼 풀 전체를 밀어낸다.** 그래서 대량 읽기/쓰기 등을 위한 별도의 접근 방법론을 적용할 수 있게 한다. 예를 들어서 대량 읽기를 위한 `BAS_BULKREAD`는 작은 순환 버퍼(ring buffer)를 자기 몫으로 잡고 그 안에서만 재사용한다.


!!! info "링 버퍼는 '따로 할당된 버퍼'가 아니다"

    "ring buffer"라는 이름 때문에 **버퍼 풀과 별개로 떼어 놓은 전용 메모리 영역**을 떠올리기 쉽다.
    그러나 그렇지 않다. 링은 버퍼 풀 상의 **힌트**이지 버퍼 풀 내부의 **별도 구획**이 아니다.

    ```c title="src/backend/storage/buffer/freelist.c"
    typedef struct BufferAccessStrategyData
    {
        BufferAccessStrategyType btype;   /* 어떤 전략인가 */
        int         nbuffers;             /* 링 슬롯의 개수 */
        int         current;              /* 가장 최근에 내준 슬롯 */

        /*
         * Array of buffer numbers.  InvalidBuffer (that is, zero) indicates we
         * have not yet selected a buffer for this ring slot.  For allocation
         * simplicity this is palloc'd together with the fixed fields of the
         * struct.
         */
        Buffer      buffers[FLEXIBLE_ARRAY_MEMBER];   /* ← 실제 페이지가 아닌 '버퍼번호' */
    }           BufferAccessStrategyData;
    ```

    링이 들고 있는 것은 8 KB 페이지가 아니라 **`Buffer` 번호, 즉 정수 몇 개짜리 배열**이다. 게다가 `BufferAccessStrategyData`는 공유 메모리도 아니고 **그 백엔드의 로컬 메모리**에 있다. 다른 백엔드는
    남의 링의 존재를 알 수 없다.

    실제로 링에 들어 있는 프레임은 **여전히 공유 버퍼 풀의 평범한 프레임**이다. 버퍼 테이블에 정상적으로 등록되어 있고, 다른 백엔드가 같은 페이지를 찾으면 핀을 걸 수 있다. "내 링 안에 있다"는 것이 소유권이나 배타성을 뜻하지 않는다.

    그렇다면 링이 실제로 하는 일은 무엇인가. **희생양을 고를 때 링을 먼저 들여다보는 것**
    뿐이다. 전략 없이(`BAS_NORMAL`) 새 프레임이 필요하면 clock-sweep 알고리즘이 풀 전체를 돌며 고르게 되지만, 전략이 지정되어 있으면 **"내가 직전에 쓴 N개 중 하나를 재활용할 수 있는가"** 를 먼저 확인한다.

    그리고 이 요청은 **거절될 수 있다.** `GetBufferFromRing()`은 다음 세 경우에 `NULL`을 돌려주고,
    호출자는 평범한 clock-sweep 알고리즘으로 되돌아간다. 첫번째는 아직 슬롯이 채워지지 않은 평범한 경우다. 이때는 링을 채워줘야 한다. 두번째는 다른 백엔드(혹은 나 자신)이 링 슬롯에 올라온 페이지에 이미 핀을 걸어둔 것으로, 링은 이를 사용해서는 안 된다(이는 REFCOUNT > 0으로 확인된다). 세번째로 USAGECOUNT > 1이라는 것은 누군가 **최근에** 페이지를 접근했다는 것이므로, "딱 한 번 읽고 버릴 페이지"라는 전략의 전제가 깨진 것이다. 

    ```c title="src/backend/storage/buffer/freelist.c"
    bufnum = strategy->buffers[strategy->current];
    if (bufnum == InvalidBuffer)
        return NULL;                       /* ① 아직 링의 이 슬롯을 안 채웠다 */
    …
    if (BUF_STATE_GET_REFCOUNT(local_buf_state) == 0 /* ② 핀이 걸려 있다 */
        && BUF_STATE_GET_USAGECOUNT(local_buf_state) <= 1) /* ③ 남이 최근에 만졌다 */
    {
        *buf_state = local_buf_state;
        return buf;                        /* ②③이 아니면 재활용 가능하므로 사용한다 */
    }
    UnlockBufHdr(buf, local_buf_state);
    return NULL;                            
    ```

## 4 · `ExtendBufferedRel()` 계열 — 아직 없는 블록들

PostgreSQL 16에서 추가된 것으로, 기존의 `ReadBufferExtended()`에서 여러 개의 블록을 추가하기 위한 별도의 함수를 만들어서 분리시킨 것이다(`ReadBufferExtended()`에 backward compatibility를 위한 예외적인 확장 케이스 처리가 남아있으나 이 또한 `ExtendBufferedRel()`을 호출한다). 그리고 `ReadBuffer()`에서와 유사하게, `ExtendBufferedRel()`은 `ExtendBufferedRelBy()`의 특수한 경우로서, 블록의 개수 `N=1`인 경우다. 

| 함수 | 계약 | 대표 호출자 |
| --- | --- | --- |
| `ExtendBufferedRel()` | 블록 **하나**를 붙이고 그 버퍼를 준다 | `nbtpage.c:978`, `gistutil.c:880`, `brin.c:1127`, `sequence.c:368` — 인덱스 초기화가 대부분 |
| `ExtendBufferedRelBy()` | 블록 **N개**를 한 번에 붙인다. 몇 개나 붙었는지 돌려준다 | `hio.c:341` (대량 INSERT/COPY) |
| `ExtendBufferedRelTo()` | "블록 N번이 존재할 때까지" 늘린다 | `freespace.c:641`, `visibilitymap.c:638`, `xlogutils.c:513` |

* **`...By()`** 는 대량 적재를 위한 것이다. 데이터를 한 번에 많이 추가하고 싶을 때, 블록을 한 개씩 붙이면 시스템 상 경합(contention)을 일으킬 수 있으므로 이렇게 여러 개를 붙일 수 있도록 한 것이다. 
* **`...To()`** 는 "끝에 하나 붙여줘"로는 표현할 수 없는 경우를 처리한다. 예를 들어서 *몇 개를 붙여라*가 아니라 *최소 이 정도까지 키워라*에 대응하기 위함이다.
  다. 

!!! note "왜 `ReadBuffer()`계열에서 분리해나왔나"

    원래는 `ReadBuffer(rel, P_NEW)`와 같은 식으로 호출하면 새로운 블록을 한 개 붙여 주었다. 여기서 `P_NEW`는 사실
    `InvalidBlockNumber` 값이었다. 그러나 이렇게 할 경우 여러 개의 블록을 붙이고 싶을 때 상당한 문제가 생긴다. 테이블을 확장하기 위해서는 다른 백엔드와 충돌하지 않도록 **확장 락(extension lock)**을 잡아야 했다. 그런데 과거의 방식대로라면 확장 락을 잡은 채로 `ReadBuffer(P_NEW)`를 여러 차례 호출해야 했는데, `ReadBuffer(P_NEW)`는 보통의 버퍼 풀 작동 방식처럼 희생양 버퍼 프레임을 찾아서 이것을 디스크에 쓴 뒤에 빈 페이지로 가져와야 하기 때문에 오랜 시간이 걸릴 수 있었다.

    그래서 Postgres 16 이후의 `ExtendBufferedRelShared()`는 순서를 뒤집는다. 확장 락을 잡지 않은 채로 희생양 버퍼 프레임들을 필요한 만큼 먼저 확보한 뒤에야 락을 잡는다.

## 5 · Read Stream — 읽을 블록들을 미리 아는 경우

테이블을 순차 스캔(sequential scan)한다면 0, 1, 2, 3, ...번 블록을 차례대로 읽게 된다. 따라서 다음에
무엇을 읽을지 이미 알고 있다. 그렇다면 첫 블록을 처리하는 동안 다음 블록의 I/O를 미리 걸어 둘 수 있다. 이 패턴을 위한 API가 `read_stream.c`다. 호출자는 블록 번호를 하나씩 돌려주는 콜백을 등록하고, 버퍼를 하나씩 꺼내 쓴다.

그런데 Read Stream은 `ReadBuffer()`를 호출하지 않는다. 이는 블록 한 개를 위한 함수이기 때문이다. 대신에 이들은 `StartReadBuffers()`를 직접 바로 호출한다. 우선 지금은 이 정도까지만 알아두자.

## 6 · `Buffer`를 반환

`ReadBuffer()` 및 `ExtendBufferedRel()`은 버퍼 1개를 직접 반환하고, 여러 개의 버퍼를 반환해야 하는 경우에는 인자를 통해 반환하게 된다. 어찌 되었든 버퍼 풀의 사용자 입장에서는 사실은 정수형 숫자인 `Buffer`값을 받아서 다음과 같이 페이지를 찾아서 사용하게 된다.

```c title="예시"
Buffer buf = ReadBuffer(rel, 42);   /* buf 는 그냥 int */
Page   page = BufferGetPage(buf);   /* 메모리 주소가 된다 */
```

호출자가 아는 것은 이 숫자뿐이고, 나머지 API는 전부 이 숫자를 인자로 받는다. 다만 `Buffer`와 내부 `buf_id`가 1만큼 다르다는 특성을 갖고 있는데, 이에 관해서는 [부록](20-buffer-id.md)을 참고하기 바란다.

`Buffer`를 읽어 온 다음의 전형적인 수명 주기는 이렇다.

```c
Buffer buf = ReadBuffer(rel, blkno);        /* 1. 획득 — 핀이 걸린 상태로 돌아온다  */
LockBuffer(buf, BUFFER_LOCK_EXCLUSIVE);     /* 2. 내용 락                          */
    …페이지 수정…
MarkBufferDirty(buf);                       /* 3. "고쳤다"고 선언                   */
    …WAL 기록…
UnlockReleaseBuffer(buf);                   /* 4. 락 + 핀 반납                     */
```

| 단계 | 함수 | 지키는 약속 |
| --- | --- | --- |
| 획득 | `ReadBuffer*`, `ExtendBufferedRel*` | 반환된 버퍼에는 **이미 핀이 걸려 있다** |
| 고정 | (핀 걸려 있음) | 핀이 있는 동안 이 프레임은 절대 퇴출(evict)되지 않는다 |
| 락 | `LockBuffer()`, `LockBufferForCleanup()` | 페이지 *내용*에 대한 동시성 제어 |
| 표시 | `MarkBufferDirty()` | 이걸 불러야 수정된 내용을 디스크에 쓸 수 있다 |
| 반납 | `ReleaseBuffer()`, `UnlockReleaseBuffer()` | 핀을 놓는다. 버퍼가 바로 퇴출되는 것은 아니다. |

!!! info "핀과 락은 다른 것이다"

    * **핀(pin)** 은 *프레임*에 대한 약속으로, 버퍼 풀 관리 차원에서 의미를 갖는다. 버퍼 풀이 가득차더라도 이 페이지를 퇴출시키지 말라는 표시로서, 여러 백엔드가 동시에
      핀을 걸 수 있다. `ReadBuffer()`가 핀을 건 채로 반환한다.
    * **내용 락(content lock)** 은 *페이지 내용*에 대한 약속이다. 현재 페이지 내용을 읽는/고치는 동안 다른 백엔드가 페이지 내용을 고치지 못하게 만든다(공유락, 배타락이 있다). 버퍼 획득과 별개로 명시적으로  획득해야 한다.
  
    따라서, 핀 없이 락을 잡는 것은 불가능하고(프레임이 사라질 수 있으므로), 락 없이 핀만 잡는 것은
    가능하다(예: 스트림이 미리 확보해 둔 버퍼).

!!! question "생각해 볼 거리"

    * `ReadBuffer()`가 핀이 걸린 버퍼를 돌려주는 대신, 페이지 내용을 호출자의 로컬 버퍼로 복사해서
      돌려주는 설계였다면 무엇이 좋고 무엇이 나쁠까?
    * B-Tree 탐색은 왜 읽기 스트림을 쓸 수 없을까? 
    * `MarkBufferDirty()`를 부르는 것이 호출자의 책임이다. 버퍼 풀이 스스로 "이 페이지가 바뀌었다"를
      알아낼 수는 없을까? 알아내려면 무엇이 필요할까?