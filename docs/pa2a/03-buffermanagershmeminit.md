# 03 · 버퍼 풀 초기화

!!! abstract 목표
    버퍼 풀의 초기화 과정을 이해한다. 

앞 장에서 버퍼 풀의 공유 메모리는 `BufferManagerShmemInit()`를 통해 할당된다는 점을 확인했다. 이 함수는 단순히 메모리를 할당할 뿐만 아니라, 실제로 버퍼 풀을 사용 가능한 상태로 초기화시킨다(`src/backend/storage/buffer/buf_init.c`). 이 코드를 따라가 보면서 어떻게 초기화가 이루어지는지 살펴보자.

### 네 가지 배열 형태의 자료구조 할당

```c title="src/backend/storage/buffer/buf_init.c"
    /* Align descriptors to a cacheline boundary. */
    BufferDescriptors = (BufferDescPadded *)
        ShmemInitStruct("Buffer Descriptors",
                        NBuffers * sizeof(BufferDescPadded),
                        &foundDescs);

    /* Align buffer pool on IO page size boundary. */
    BufferBlocks = (char *)
        TYPEALIGN(PG_IO_ALIGN_SIZE,                       /* ← note the over-allocation */
                  ShmemInitStruct("Buffer Blocks",
                                  NBuffers * (Size) BLCKSZ + PG_IO_ALIGN_SIZE,
                                  &foundBufs));

    /* Align condition variables to cacheline boundary. */
    BufferIOCVArray = (ConditionVariableMinimallyPadded *)
        ShmemInitStruct("Buffer IO Condition Variables",
                        NBuffers * sizeof(ConditionVariableMinimallyPadded),
                        &foundIOCV);

    CkptBufferIds = (CkptSortItem *)
        ShmemInitStruct("Checkpoint BufferIds",
                        NBuffers * sizeof(CkptSortItem), &foundBufCkpt);
```

`BufferDescriptors`, `BufferBlocks`, `BufferIOCVArray`, `CkptBufferIds`가 모두 `ShmemInitStruct()`를 이용하여 할당된다. 이것은 PostgreSQL이 공유 메모리를 할당하기 위해 만든 함수이다(`src/backend/storage/ipc/shmem.c`). 이 함수의 첫번째 인자는 이름이고, 두번째는 사이즈이며, 세번째는 *이미 해당 자료구조가 할당되어 있는지* 여부를 할당받기 위한 변수다. 첫번째나 세번째에 대해서는 그냥 넘어가도 괜찮지만, 인자로 들어가는 사이즈에 대해서는 이해해야 한다. 사이즈는 공통적으로 `NBuffers`라는 변수에 각 자료구조의 크기(`sizeof()`)를 곱하는 형태로 나타난다. 여기서 `NBuffers`는 사용자가 서버 시작 전에 PostgreSQL 설정값을 통해 버퍼 풀의 크기를 지정해 줌에 따라 계산되는 값이다. 

 `ShmemInitStruct()`는 요청받은 만큼의 공유 메모리를 할당한 뒤, 이를 가리키는 포인터를 반환한다(포인터가 뭔지 잘 모른다면 C를 복습해야 한다: [외부링크](https://dojang.io/mod/page/view.php?id=275)). 포인터는 말 그대로 메모리 객체의 주소를 가리키고 있기 때문에, 이후에 프로그램이 이를 알맞은 자료구조로 이해하고 사용할 수 있도록 자료형 변환을 해줘야 한다([외부링크](https://dojang.io/mod/page/view.php?id=496)). 따라서 `(BufferDescPadded *)`와 같은 형 변환이 이루어지고 있는데, `BufferBlocks`의 경우 `(char *)`로의 형 변환이 이루어진다. 이것은 문자열로 쓰겠다는 뜻이 아니라, 이 영역을 의미가 정해지지 않은 바이트 배열로 다루겠다는 뜻이다. 즉 버퍼 블록은 디스크 페이지가 담기는 공간이며 바이트 덩어리로서 준비된다(C 표준에서는 `sizeof(char)==1`이므로 `char *`에 대한 포인터 산술(pointer arithmetic)은 곧 바이트 단위의 연산을 가능하게 한다).

??? info "PostgreSQL 설정하기"
    PostgreSQL의 파라미터 설정은 `postgresql.conf` 파일 내용을 바꾸어 줌으로써 할 수 있다([공식 문서](https://www.postgresql.org/docs/current/config-setting.html#CONFIG-SETTING-CONFIGURATION-FILE) 참고). 버퍼 풀의 크기를 결정하는 파라미터명은 `shared_buffers`로, `shared_buffers = 128MB`와 같이 KB, MB, GB 등을 이용해 설정하면 `NBuffers`는 알아서 계산된다.


### 버퍼 블록과 버퍼 기술자

```c title="src/include/storage/buf_internals.h"
    typedef union BufferDescPadded
    {
        BufferDesc  bufferdesc;
        char        pad[BUFFERDESC_PAD_TO_SIZE];
    } BufferDescPadded;

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

여기에서는 `BufferBlocks`와 `BufferDescriptors`의 역할만을 소개한다. 이 자료구조들에 대한 정의는 버퍼 풀 내부에서만 사용되는 자료구조를 위한 헤더 파일인 `src/include/storage/buf_internals.h`에서 확인 가능하다. `BufferBlocks`는 실제로 데이터가 담기는 버퍼로서, `NBuffers × BLCKSZ`의 크기를 가지므로, `NBuffers`개의 블록들이 연속적으로 나열된 형태를 생각하면 된다. 여기서 `BLCKSZ`는 한 블록의 크기로, 버퍼 페이지의 크기를 말한다. PostgreSQL에서는 8KB, 즉 8192B 크기의 페이지를 기본값으로 한다(1KB, 2KB, 4KB, 8KB, 16KB, 32KB만이 허용된다). 특별한 이유가 없을 경우 페이지 크기를 바꾸지는 않는다. `BufferBlocks`를 할당할 때 이것을 바이트 덩어리라고 설명했는데, 그래서 이를 위한 별도의 자료구조는 없다.

`BufferDescriptors`는 각 버퍼 블록에 대한 메타데이터가 담긴 기술자(descriptor)들의 배열이다. 위의 버퍼 블록에 1:1 대응되므로 `NBuffers`개가 있지만 각각의 크기는 `sizeof(BufferDescPadded)`이다. `BufferDescPadded`는 성능 최적화를 위해 패딩(padding)된 버퍼 기술자를 나타내는 자료구조다. 

??? info "패딩을 하는 이유"
    설령 패딩을 하지 않더라도 버퍼 풀의 정합성에는 문제가 생기지 않는다. 그러나 CPU 캐시의 캐시라인 크기에 정렬되지 않을 경우, 많은 CPU 코어들에서 동시에 접근이 이루어진다면 [false sharing](https://en.wikipedia.org/wiki/False_sharing) 등 성능 저하를 유발할 수 있다. 그래서 여기에서는 64B 길이에 맞추어 버퍼 기술자들이 배치될 수 있도록 패딩을 한다.

```c title="src/backend/storage/buffer/buf_init.c"
    for (i = 0; i < NBuffers; i++)              /* ← the init loop */
    {
        BufferDesc *buf = GetBufferDescriptor(i);

        ClearBufferTag(&buf->tag);
        pg_atomic_init_u32(&buf->state, 0);
        buf->wait_backend_pgprocno = INVALID_PROC_NUMBER;
        buf->buf_id = i;
        pgaio_wref_clear(&buf->io_wref);        /* ← new in PG 16+; inert under io_method=sync */

        /* Initially link all the buffers together as unused. */
        buf->freeNext = i + 1;

        LWLockInitialize(BufferDescriptorGetContentLock(buf),
                            LWTRANCHE_BUFFER_CONTENT);
        ConditionVariableInit(BufferDescriptorGetIOCV(buf));
    }
    GetBufferDescriptor(NBuffers - 1)->freeNext = FREENEXT_END_OF_LIST;
```

버퍼 기술자 내의 변수들 중 버퍼가 담고 있는 내용물에 대한 ID, 버퍼 자체의 ID를 나타내는 두 개의 중요한 멤버 변수들을 알아보자. 

#### 버퍼 태그

`BufferTag tag`는 버퍼에 담겨있는 페이지의 고유한 ID다. `src/include/storage/buf_internals.h`에서 이에 대한 정의를 살펴볼 수 있는데, 현재로서는 이해하기 어려운 `Oid`, `RelFileNumber`, `ForkNumber`, `BlockNumber` 등의 멤버 변수 자료형들이 보인다. 이 자료형들은 사실 특별한 것이 아니라, PostgreSQL이 32비트 정수형(`unsigned int`, `uint32`)에 용도별로 다른 이름을 붙여놓은 것에 가깝다(`ForkNumber`만 예외적으로 `enum`이다). 이름만 다를 뿐 실제로 저장되는 값은 전부 정수이며, 이 정수들이 합쳐지면 디스크 상의 페이지 하나를 고유하게 가리키게 된다.
  

```c
    typedef struct buftag
    {
        Oid         spcOid;         /* tablespace oid */
        Oid         dbOid;          /* database oid */
        RelFileNumber relNumber;    /* relation file number */
        ForkNumber  forkNum;        /* fork number */
        BlockNumber blockNum;       /* blknum relative to begin of reln */
    } BufferTag;
```

각 필드의 의미는 다음과 같다.

| 필드 | 실제 자료형 | 의미 |
| --- | --- | --- |
| `spcOid` | `Oid` (`unsigned int`) | 페이지가 속한 테이블스페이스의 OID |
| `dbOid` | `Oid` | 페이지가 속한 데이터베이스의 OID |
| `relNumber` | `RelFileNumber` | 릴레이션(테이블/인덱스)의 파일 번호 |
| `forkNum` | `ForkNumber` (`enum`) | 포크 종류(main / fsm / vm / init) |
| `blockNum` | `BlockNumber` (`uint32`) | 해당 포크 안에서 몇 번째 블록인지(0부터 시작) |

이렇게 나뉘어 있는 이유는, 이 값들을 순서대로 이어붙이면 그대로 **디스크 상의 물리적 위치**가 되기 때문이다. 앞의 세 값 `spcOid`/`dbOid`/`relNumber`는 파일 경로(예: `base/16384/24576`)를 결정하고, `forkNum`은 그 릴레이션의 어떤 포크인지(데이터 본체인지, 여유 공간 정보를 담은 FSM(free space map)인지, 가시성 맵(visibility map)인지)를 고르며, 마지막으로 `blockNum`이 그 파일 안에서 몇 번째 `BLCKSZ`(기본 8KB) 블록인지를 지정한다. 즉 `BufferTag` 하나면 데이터베이스 전체를 통틀어 단 하나의 페이지를 지목할 수 있다.

이 성질 때문에 `BufferTag`는 단순한 메타데이터가 아니라 **버퍼 풀 조회의 키**로 쓰인다. 어떤 페이지가 필요할 때 PostgreSQL은 먼저 그 페이지의 `BufferTag`를 만들어 버퍼 해시 테이블에서 찾아본다. 지금 살펴보고 있는 초기화 코드에서는 `ClearBufferTag(&buf->tag)`로 모든 태그를 비워두는 작업을 한다.

#### 버퍼 ID

`buf_id`는 이 기술자가 담당하는 버퍼 프레임이 버퍼 풀 안에서 **몇 번째 칸인지**를 나타내는 배열 인덱스다. 버퍼 블록이 커다란 바이트 덩어리로 할당되고, 이것을 `BLCKSZ`씩 나누어서 `NBuffers`개의 블록으로 사용하기 때문에, 각 블록을 부를 ID가 필요하기 때문이다. 초기화 루프에서 `buf->buf_id = i`로, 자기 자신의 인덱스를 그대로 적어 넣는다. 앞서 보았듯 버퍼 블록들은 이름 없는 하나의 커다란 바이트 덩어리(`BufferBlocks`)로 잡혀 있으므로, `i`번 기술자와 `i`번 블록(`BufferBlocks + i * BLCKSZ`)을 이어주는 좌표가 필요하고 그 좌표가 `buf_id`다.

위의 `tag`와는 다른 것을 가리킴에 유의하자. `buf_id`는 메모리 내에서 각 버퍼 블록들을 가리키는 데 사용되기 때문에, 변화하지 않지만, 그 안에 담긴 디스크 페이지는 바뀔 수 있기 때문에 `tag`의 값은 계속 바뀌게 된다(아래 참고).

| | `tag` (`BufferTag`) | `buf_id` (`int`) |
| --- | --- | --- |
| 가리키는 것 | **디스크** 상의 페이지(내용물) | **메모리** 상의 프레임 |
| 값의 범위 | 데이터베이스 전체의 모든 페이지 | `0` ~ `NBuffers - 1` |
| 언제 바뀌나 | 페이지가 들어오고 나갈 때마다 바뀜 | 서버가 구동될 때 정해지고 **절대 바뀌지 않음** |
| 비유 | 사물함에 들어 있는 물건의 이름표 | 사물함 자체에 붙은 번호 |

#### 그외 변수들

`state, content_lock, wait_backend_pgprocno, freeNext`는 중요한 변수이고, `LWLockInitialize()` 및 `ConditionVariableInit()`도 의미가 있지만 그 자체의 메커니즘을 이해해야만 그 쓰임새를 알 수 있기 때문에 각기 관련 문서에서 설명한다. `io_wref`는 비동기적으로 I/O를 할 때 사용되는 변수이지만 우리는 동기적 I/O만을 다룰 것이기 때문에 우선은 생략하고 넘어간다.

### Strategy 초기화

위에서는 페이지를 디스크에서 메모리로 읽어들여서 담는 블록을 위주로 살펴보았다면, 이제 블록을 찾기 위한 수단으로 넘어가자. 어떤 페이지가 필요할 때 그것이 이미 버퍼 풀에 있는지 알아내야 하고, 만약 있다면 그것을 읽으면 되지만, 없다면 빈 블록을 골라서 페이지를 읽어들여야 한다. 그런데 만약 빈 블록조차 없다면 어떻게 해야할까? 이미 버퍼 풀에 있는 블록들 중에서 *제일 덜 중요해 보이는 블록*을 비운 뒤에 그 자리를 이용하는 게 합리적일 것이다. 여기서 *중요*한 블록이 무엇인지를 정하는 방식이 곧 버퍼 캐시 교체 전략(strategy)이고, `BufferManagerShmemInit()`의 마지막에 호출되는 `StrategyInitialize()`가 이를 위한 자료구조들을 준비한다.

??? info "왜 "Strategy"일까?"
    PostgreSQL의 기본 전략은 Clock-sweep이다. 그리고 엄격히 말해서 적용되는 전략은 이것 하나 뿐이다. 그럼에도 불구하고 여기서는 마치 여러 가지 종류의 전략을 사용할 수 있는 것처럼 일반적으로 추상화시켜 두었다. 여기에는 아마 ARC, 2Q 등 다른 교체 전략으로 작동되었던 시기의 영향([관련 문서](https://wiki.postgresql.org/wiki/Multiple_Buffer_Pools#Clock-Sweep_(2005)))이 있을 것으로 보인다. 이와 별개로 `BufferAccessStrategy`가 정의되어 있는데, 이는 많은 데이터를 한번 훑고 지나가는(scan) 류의 접근이 발생할 때(bulk read, bulk write, vacuum) 특정한 조건을 만족하면 적용되는 것으로서, 정해진 수의 버퍼 블록을 돌아가면서(ring buffer) 사용할 때 쓰일 수 있다. 이것은 공유 메모리가 아니라, 각 백엔드의 로컬 메모리에 올라가기 때문에 여기서 따로 초기화하지 않는다.

전략을 위해 필요한 자료구조는 크게 두 가지로, 하나는 **버퍼 테이블**(태그로 버퍼를 찾는 해시 테이블)이고, 다른 하나는 **`BufferStrategyControl`**(교체 정책이 쓰는 공유 상태)이다. 이후에 버퍼 테이블 및 clock-sweep 전략을 상세히 살펴 볼 것이므로 여기에서는 초기화에 집중한다.

```c title="src/backend/storage/buffer/freelist.c — 일부 생략"
void
StrategyInitialize(bool init)
{
    /*
     * Since we can't tolerate running out of lookup table entries, we must be
     * sure to specify an adequate table size here.  The maximum steady-state
     * usage is of course NBuffers entries, but BufferAlloc() tries to insert
     * a new entry before deleting the old.  In principle this could be
     * happening in each partition concurrently, so we could need as many as
     * NBuffers + NUM_BUFFER_PARTITIONS entries.
     */
    InitBufTable(NBuffers + NUM_BUFFER_PARTITIONS);

    StrategyControl = (BufferStrategyControl *)
        ShmemInitStruct("Buffer Strategy Status",
                        sizeof(BufferStrategyControl), &found);

    if (!found)
    {
        SpinLockInit(&StrategyControl->buffer_strategy_lock);

        /* Grab the whole linked list of free buffers ... */
        StrategyControl->firstFreeBuffer = 0;
        StrategyControl->lastFreeBuffer = NBuffers - 1;

        /* Initialize the clock sweep pointer */
        pg_atomic_init_u32(&StrategyControl->nextVictimBuffer, 0);
        …
    }
}
```
#### 버퍼 해시 테이블

제일 먼저 버퍼 해시 테이블을 초기화한다. `InitBufTable()`의 한 개뿐인 인자는 사이즈이다. 바이트 사이즈가 아닌, 몇 개의 해시 엔트리가 필요한지다. 해시 테이블 크기는 `NBuffers`가 아닌, `NBuffers + NUM_BUFFER_PARTITIONS`로 주어지는데, 그 이유는 해시 엔트리를 지우기 전에 넣는 과정으로 인해 순간적으로 버퍼 개수를 넘는 엔트리가 생길 수 있기 때문이다. 파티션 개념에 대해서는 이후 버퍼 테이블에서 다시 살펴볼 것이다.

한 가지 흥미로운 지점은, 위의 자료구조들을 공유 메모리 상에 할당할 때에는 `ShmemInitHash()`를 직접 호출한 뒤에 자료형 변환(예: `(BufferDescPadded *)`)을 통해 알맞은 자료형으로 변환시켰지만 여기에서는 그런 모습이 나타나지 않는다는 것이다. 그 이유는 버퍼 해시 테이블을 가리키는 포인터인 `SharedBufHash`가 이미 `src/backend/storage/buffer/buf_table.c`에서 `static HTAB *SharedBufHash;`와 같이 선언되어 있고, `InitBufTable()` 함수 내에서 `SharedBufHash = ShmemInitHash(...)`와 같이 초기화하고 있기 때문이다. 여기서 `static`은 해시 테이블이 `buf_table.c`가 아닌 다른 파일에서 직접 불리는 것을 방지해 준다(해시 테이블은 `BufTableHashCode()`, `BufTableLookup()`, `BufTableInsert()`, `BufTableDelete()`만을 외부에 노출한다). 

```c title="src/backend/storage/buffer/buf_table.c"
    void
    InitBufTable(int size)
    {
        HASHCTL     info;

        /* assume no locking is needed yet */

        /* BufferTag maps to Buffer */
        info.keysize = sizeof(BufferTag);
        info.entrysize = sizeof(BufferLookupEnt);
        info.num_partitions = NUM_BUFFER_PARTITIONS;

        SharedBufHash = ShmemInitHash("Shared Buffer Lookup Table",
                                    size, size,
                                    &info,
                                    HASH_ELEM | HASH_BLOBS | HASH_PARTITION);
    }
```

#### 버퍼 전략 제어 블록

`BufferStrategyControl`은 교체 정책이 쓰는 값들을 모아둔 공유 메모리상의 구조체 하나다. 배열이 아니라 단 하나뿐이므로, 여기 담긴 값들은 전체 버퍼 풀에 대한 전역적 상태다. 지금은 이런 것들이 있다는 점만 유의하되, `StrategyControl->firstFreeBuffer = 0;`, `StrategyControl->lastFreeBuffer = NBuffers - 1;`과 같이 초기화된다는 점만 알아두자.

```c title="src/backend/storage/buffer/freelist.c"
typedef struct
{
    slock_t     buffer_strategy_lock;   /* 아래 값들을 보호하는 스핀락 */

    pg_atomic_uint32 nextVictimBuffer;  /* clock-sweep의 시곗바늘 */

    int         firstFreeBuffer;        /* 프리 리스트의 머리 */
    int         lastFreeBuffer;         /* 프리 리스트의 꼬리 */

    uint32      completePasses;         /* 시곗바늘이 한 바퀴 돈 횟수 */
    pg_atomic_uint32 numBufferAllocs;   /* 지난 리셋 이후 할당된 버퍼 수 */

    int         bgwprocno;              /* 깨워야 할 bgwriter, 없으면 -1 */
} BufferStrategyControl;
```

### 그외

#### `WritebackContextInit()`

이 함수는 버퍼 풀의 데이터를 디스크에 쓰는 것과 관련된 자료구조를 준비하는 것으로, 각 백엔드가 하나씩만 가지고 있으면 충분하다. 이를 별도로 수정할 필요는 없을 것이다.

### 버퍼 풀을 위한 공유 메모리 크기 산정

[지난 문서](02-shared-memory.md)에서 공유 메모리의 크기를 먼저 산정하여 요청한 뒤, 그 위에다가 각 자료구조들을 위한 메모리를 할당받는다고 설명했었다. 이제 버퍼 풀의 구성 요소를 전반적으로 훑어보았으니 버퍼 풀은 아래와 같이 크기를 산정한다는 것을 이해할 수 있다. 한 가지 살펴보아야 할 점은 중간 중간에 `PG_CACHE_LINE_SIZE`가 들어간다는 것이다. 이는 `ShmemInitStruct()`가 CPU 캐시 라인 단위로 정렬할 수 있도록 크기를 조정하기 때문에 이를 미리 고려하여 여분의 크기가 필요하다는 점을 산입시키는 것이다.

```c title="src/backend/storage/buffer/buf_init.c"
Size
BufferManagerShmemSize(void)
{
    Size        size = 0;

    /* size of buffer descriptors */
    size = add_size(size, mul_size(NBuffers, sizeof(BufferDescPadded)));
    /* to allow aligning buffer descriptors */
    size = add_size(size, PG_CACHE_LINE_SIZE);

    /* size of data pages, plus alignment padding */
    size = add_size(size, PG_IO_ALIGN_SIZE);
    size = add_size(size, mul_size(NBuffers, BLCKSZ));

    /* size of stuff controlled by freelist.c  ── the hash table lives in here */
    size = add_size(size, StrategyShmemSize());

    size = add_size(size, mul_size(NBuffers,
                                   sizeof(ConditionVariableMinimallyPadded)));
    size = add_size(size, PG_CACHE_LINE_SIZE);

    /* size of checkpoint sort array in bufmgr.c */
    size = add_size(size, mul_size(NBuffers, sizeof(CkptSortItem)));

    return size;
}
```