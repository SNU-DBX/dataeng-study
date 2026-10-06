# 08 · 버퍼 프레임 할당

!!! abstract 목표
    공유 버퍼 풀의 유일한 할당 경로인 `BufferAlloc()`이 무슨 일을 하는지, 그리고 빈 프레임이 없을 때 `GetVictimBuffer()`가 어떻게 하나를 비워 오는지 이해한다.

[지난 문서](./07-buffer-read-path.md)에서 읽기 요청이 `StartReadBuffer()` → `PinBufferForBlock()`까지 내려오는 것을 보았다. 그런데 정작 **버퍼를 찾고 확보하는 일**은 건너뛰었다. `PinBufferForBlock()`이 프레임을 돌려주었고, 우리는 그것이 히트인지 미스인지만 확인했을 뿐이다.

이 장은 그 안에서 어떤 식으로 버퍼 풀이 프레임을 할당해 주는 지를 다룬다. 이 함수에 버퍼 풀의 핵심 로직이 담겨 있다고 볼 수 있다. `PinBufferForBlock()`의 주된 기능은 `BufferAlloc()`에서 일어나며, `BufferAlloc()`에서도 중요한 역할을 하는 부분이 `GetVictimBuffer()`다. 아래는 함수들의 호출 경로를 보여준다(순서가 아니라 call tree임).

```text
  PinBufferForBlock()
        │
        ▼
   BufferAlloc()            ← §1  "이 페이지가 버퍼 풀에 있는가?"
        │                          있으면 그것을 사용, 없으면 새 프레임에 이름을 붙여서
        │  (미스일 때만)
        ▼
   GetVictimBuffer()        ← §2  "빈 프레임을 하나 다오"
        │                          없으면 하나를 비워서라도
        ▼
   StrategyGetBuffer()            희생양 고르기 (freelist → 클럭 스윕)
```

`BufferAlloc()`은 페이지의 신원(태그)을 다루고, `GetVictimBuffer()`는 신원을 전혀 모른 채 빈 프레임만 공급한다는 점을 염두에 두고 시작해 보자. `GetVictimBuffer()`는 `BufferTag`도 `BlockNumber`도 다루지 않는다.

---

## 1 · `PinBufferForBlock()`과 `BufferAlloc()`

위에서 확인한 바, `StartReadBuffer()`는 `PinBufferForBlock()`를 불러서 버퍼 프레임을 얻어오게 된다. 지금까지는 주로 I/O와 관련된 부분을 설명하였는데, 지금 이 함수 안에서 이루어지는 것이 실질적인 '버퍼 매니저'의 역할이라고 할 수 있다.

`PinBufferForBlock()` 자체는 비교적 간단한 역할만을 수행한다. 각 백엔드의 로컬 버퍼로 가야 할 지 아니면 공유 버퍼 풀로 가야 할 지를 나눈다. 우리의 경우 `BufferAlloc()`이 불리는 경우(공유 버퍼 풀)에만 집중해도 된다. 그리고 나서 몇 가지 통게치를 남기고(`pgBufferUsage.shared_blks_hit++` 등), 반환받은 버퍼 기술자를 `Buffer`로 바꿔서 반환한다.

```c title="bufmgr.c"
    Assert(blockNum != P_NEW);
    …
    if (persistence == RELPERSISTENCE_TEMP)
    {
        bufHdr = LocalBufferAlloc(smgr, forkNum, blockNum, foundPtr);
        if (*foundPtr) pgBufferUsage.local_blks_hit++;
    }
    else
    {
        bufHdr = BufferAlloc(smgr, persistence, forkNum, blockNum,
                             strategy, foundPtr, io_context);
        if (*foundPtr) pgBufferUsage.shared_blks_hit++;
    }
    …
    return BufferDescriptorGetBuffer(bufHdr);
```

### `BufferAlloc()`

이제 본론이다. 이 함수는 버퍼 풀의 핵심 로직을 담고 있기 때문에, 코드 대부분을 따라가며 설명한다.

### 1.1 · 버퍼 테이블 조회 준비

`BufferAlloc()`이 답해야 하는 첫 번째 질문은 **"이 블록이 지금 버퍼 풀에 있는가"** 다. `NBuffers`개의
기술자를 전부 훑을 수는 없으니, 버퍼 해시 테이블, 내지는 버퍼 테이블을 사용한다.

```c title="buf_table.c"
/* entry for buffer lookup hashtable */
typedef struct
{
    BufferTag   key;            /* Tag of a disk page */
    int         id;             /* Associated buffer ID */
} BufferLookupEnt;
```

각 버퍼 엔트리를 보면 위와 같다. 키는 [§03](03-buffermanagershmeminit.md)에서 본 `BufferTag`이고, 값은 `buf_id` 하나다. 어느 페이지(태그)가 몇 번 프레임(buffer id)에 있는지를 저장하고 있는 것이다.

버퍼 테이블 조회를 하려면 다음과 같은 준비 과정이 필요하다.

```c
    /* create a tag so we can lookup the buffer */
    InitBufferTag(&newTag, &smgr->smgr_rlocator.locator, forkNum, blockNum);

    /* determine its hash code and partition lock ID */
    newHash = BufTableHashCode(&newTag);
    newPartitionLock = BufMappingPartitionLock(newHash);

    /* see if the block is in the buffer pool already */
    LWLockAcquire(newPartitinLock, LW_SHARED);
```

1. **태그를 만든다** — `RelFileLocator` + 포크 번호 + 블록 번호. 이는 데이터베이스 전체에서 페이지 하나를 유일하게 구별하는 정보다.
2. **해시코드를 구한다** — 태그를 `uint32` 자료형 정수 하나로 바꾼다.
3. **그 해시코드로 파티션 락을 고르고, 락을 잡는다** — 파티션 락에 대해서는 아래에서 설명한다.

!!! info "해싱을 하는 이유"

    만약 버퍼 테이블이 없었다면, 우리가 원하는 어떤 태그를 갖고 있는 기술자가 있는지, 있다면 어디에 있는지 빠르게 알 방법이 없다. 선형 탐색으로 직접 다 뒤져봐야 한다. 그러나 만약 해싱을 하게 되면, 한 번의 계산만으로 우리가 원하는 태그가 있을 법한 해시 버킷(hash bucket)을 찾아낼 수 있다. 물론 서로 다른 태그가 같은 버킷으로 오게 될 수 있기 때문에, 버킷 내에서 태그를 실제로 비교하면서 우리가 원하는 것과 일치하는지 확인해야 한다. 실제로 여기서 사용되는 PostgreSQL의 dynamic chained hash table에 있는 `hash_search_with_hash_value()` 함수의 일부를 보면, 아래와 같은 버킷 체인 탐색 절차가 보인다. 연결리스트(linked list)의 탐색을 안다면 바로 이해될 것이다.

    ```c title="src/backend/utils/hash/dynahash.c"
    while (currBucket !=NULL)
    {
        if (currBucket->hashvalue == hashvalue &&
            match(ELEMENTKEY(currBucket), keyPtr, keysize) == 0)
            break;
        prevBucketPtr = &(currBucket->link);
        currBucket = *prevBucketPtr;
    }
    ```

#### 파티션 락 — 해시 테이블을 '균등하게' 접근하게 해준다

해시는 대체로 균등하게 나뉘어 접근하게 해주는데, 이는 수십, 수백 개의 백엔드가 버퍼 풀에 접근하는 데 있어서도 유용한 성질로 사용된다. 해시 테이블은 여러 개의 백엔드가 동시 접근할 수 있는 대상이기 때문에 락이 필요하다. 해시테이블에 접근하기 전에는 반드시 락을 획득하고, 접근한 후에는 반드시 락을 해제해야 한다. 그런데 해시테이블이 락을 한 개만 사용한다면, 버퍼 풀에 접근하는 모든 백엔드가 그 락을 획득하기 위해 줄서야 한다.

그래서 PostgreSQL은 **해시 공간을 128조각으로 나누고 락도 128개**를 둔다. 이것이 여기서 말하는 해시 파티션이다. 

```c title="lwlock.h"
#define NUM_BUFFER_PARTITIONS  128
```

```c title="buf_internals.h"
static inline uint32
BufTableHashPartition(uint32 hashcode)
{
    return hashcode % NUM_BUFFER_PARTITIONS;
}

static inline LWLock *
BufMappingPartitionLock(uint32 hashcode)
{
    return &MainLWLockArray[BUFFER_MAPPING_LWLOCK_OFFSET +
                            BufTableHashPartition(hashcode)].lock;
}
```

해시코드를 `NUM_BUFFER_PARTITIONS`로 나눈 나머지를 바탕으로 파티션을 선택하고, 이를 기반으로 락을 잡는다. 예를 들어서, 연속된 두 블록을 각각 다른 백엔드가 동시에 접근하려고 한다고 하자. 반드시 그렇다고는 말할 수 없겠으나, 아주 높은 확률로 이 두 블록의 태그로부터 계산된 해시코드들은 128로 나누었을 때 서로 다른 나머지를 가질 것이다. 따라서 이 두 백엔드는 다른 파티션에 떨어지므로 서로를 막지 않는다.

!!! danger "락은 프레임이 아니라 '태그'에 딸려 있다"
    여기서 착각을 하기 쉬운 지점은, 락이 각 프레임과 연결되어 있으리라는 생각이다. 예를 들어서 `buf_id`가 0부터 1023인 프레임을 하나의 락이 보호한다고 생각할 수 있다. 그러나 여기서 락을 정하는 것은 버퍼 프레임 번호가 아니라 **태그**다.     그래서 같은 프레임이라도 담고 있는 페이지가 바뀌면 그것을 보호하는 파티션 락도 바뀐다.

    이것은 파티션 락이 보호하는 대상이 각 프레임 내에 있는 내용물이 아니라, 버퍼 테이블 그 자체이기 때문이다. 동시에 버퍼 테이블을 여러 백엔드들이 검색하거나 삽입/삭제할 때, 이들이 들고 있는 정보는 태그 뿐이다. 따라서 동시 접근으로부터 보호하는 기준은 태그를 이용한 구분이 될 수밖에 없다. (버퍼 프레임 정보는 바로 우리가 찾고자 하는 대상이므로, 버퍼 테이블에 접근할 때는 알 수가 없다.) 

#### 버퍼 테이블 API (조회, 삽입, 삭제)

`src/backend/storage/buffer/buf_table.c`에서 실질적인 버퍼 테이블 접근을 위해 제공하는 것은 다음의 세 함수이다. 참고로 `buf_table.c` 파일의 머리 주석을 보면, *"the routines in this file do no locking of their own. The caller must hold a suitable lock."*, 즉 버퍼 테이블 API를 부르기 전에 알아서 미리 락을 잡고 와야 한다는 점을 명시하고 있다.

| 함수 | 반환값 | 필요한 락 |
| --- | --- | --- |
| `BufTableLookup(tag, hash)` | 있으면 `buf_id`, 없으면 `-1` | SHARED 또는 EXCLUSIVE |
| `BufTableInsert(tag, hash, buf_id)` | 넣는 데 성공하면 `-1`, **이미 있으면 넣지 않고 그 `buf_id`** | EXCLUSIVE |
| `BufTableDelete(tag, hash)` | 지운다 (없으면 에러) | EXCLUSIVE |

### 1.2 · 조회 — 버퍼 테이블에 있는지 확인

```c title="bufmgr.c"
    /* Make sure we will have room to remember the buffer pin */
    ResourceOwnerEnlarge(CurrentResourceOwner);
    ReservePrivateRefCountEntry();

    /* create a tag so we can lookup the buffer */
    InitBufferTag(&newTag, &smgr->smgr_rlocator.locator, forkNum, blockNum);

    /* determine its hash code and partition lock ID */
    newHash = BufTableHashCode(&newTag);
    newPartitionLock = BufMappingPartitionLock(newHash);

    /* see if the block is in the buffer pool already */
    LWLockAcquire(newPartitionLock, LW_SHARED);
    existing_buf_id = BufTableLookup(&newTag, newHash);
```

태그, 파티션 락 등과 관련해서 떼어 놓고 보았던 준비 과정을 포함하여 다시 한번 조회 과정을 처음부터 살펴 본다. 맨 앞의 두 줄은 지금으로서는 이해가 안 갈 수 있지만, PostgreSQL에서 매우 중요한 절차다. 버퍼 프레임을 핀하게 되면 이를 기록해야 하는데, 기록하기 위한 자리를 미리 확보하는 것이다. 이것을 실제로 핀을 잡고 나서 하지 않는 이유는, 그때 가서 자리를 확보하는 데에 실패하면 이미 핀을 잡아 놓은 뒤라 곤란하기 때문이다.

파티션 락은 `LW_SHARED`로 잡는다. 조회만 할 것이므로 여러 백엔드가 동시에 같은 파티션을 읽어도 된다(shared). 그리고 결과는 둘 중 하나다 — `existing_buf_id >= 0`(히트)이거나 `-1`(미스)이다.

!!! info "메모리 할당 미리 하기"

    위에서 살펴 본 다음 두 줄은 각기 다른 데이터를 위한 자리를 확보하지만, 메모리 할당을 미리 한다는 점에서는 동일하다.

    ```c
    ResourceOwnerEnlarge(CurrentResourceOwner);
    ReservePrivateRefCountEntry();
    ```
    
    먼저, `ReservePrivateRefCountEntry()`에 대해 알아보자. 공유 버퍼 풀의 버퍼 기술자에는 각기 프레임에 몇 개의 백엔드가 핀을 꽂았는지 기록하지만, 이와 별개로 그것과 별개로 **백엔드마다 자기만의 핀 장부**를 따로 들고 있다. 그 이유는 같은 버퍼를 한 백엔드가 여러 번 핀하는 경우가 있는데, 이 때 기술자에 있는 공유되는 핀 카운트를 매번 올리지 않기 위함이고, 백엔드가 자신의 작업을 마쳤을 때 혹시라도 핀을 놓지 않은 프레임이 있는지 검사하기 위한 것이다. 이 장부가 바로 `PrivateRefCountArray`다.

    ```c title="bufmgr.c"
    #define REFCOUNT_ARRAY_ENTRIES 8
    ...
    static struct PrivateRefCountEntry PrivateRefCountArray[REFCOUNT_ARRAY_ENTRIES];
    ```

    재미있게도 크기는 고작 8개다. 만약 8개를 넘어선다면? 이를 처리하기 위한 오버플로 해시 테이블(`PrivateRefCountHash`)로 밀어낸다.

    `ReservePrivateRefCountEntry()`가 하는 일은 이 배열에서 빈 칸 하나를 찾아 `ReservedRefCountEntry`에 담아 두는 것뿐이다. 다음과 같은 경우는 성공적인 경우다.

    ```c title="bufmgr.c"
    /* Already reserved (or freed), nothing to do */
    if (ReservedRefCountEntry != NULL)
        return;

    for (i = 0; i < REFCOUNT_ARRAY_ENTRIES; i++)
    {
        res = &PrivateRefCountArray[i];
        if (res->buffer == InvalidBuffer)
        {
            ReservedRefCountEntry = res;
            return;
        }
    }
    ```

    그러나 빈 칸이 하나도 없으면 시계 바늘(`PrivateRefCountClock`)이 가리키는 칸을 고른 뒤, 그 칸을 다른 곳으로 옮겨 놓고 자리를 비운다. 이 때 '다른 곳'이 바로 이를 저장하기 위해 쓰이는 해시 테이블(`PrivateRefCountHash`)다. 이 과정에서 `hash_search(..., HASH_ENTER, ...)`가 불리는데, 이는 이름과 달리 `HASH_ENTER`를 인자로 넣었기 때문에 해시 삽입 요청이 되며, 삽입 과정에서 메모리 할당을 일으킬 수도 있다. 그런데 시스템에 메모리가 없는 상황이 불운하게 발생할 수도 있으므로 미리 여기서 실행해 두고 넘어가는 것이다.

    `ResourceOwnerEnlarge()`는 조금 더 넓은 맥락에서 이해해야 한다. 단순히 버퍼 핀 뿐만 아니라, 다양한 자원들이 데이터베이스 엔진의 실행 과정에서 할당되는데, 이를 실행 종료 시에 자동으로 반납하기 위해 `ResourceOwner`라는 것이 존재한다. 특히, 실행이 비정상적으로 종료(예: 에러로 도중에 갑자기 중단)되더라도 반납하도록 해 준다. 버퍼 핀도 '자원' 중 하나로서, 핀을 할 때 `ResourceOwnerRememberBuffer()`로 여기에 등록된다.

    ```c title="src/include/storage/buf_internals.h"
    static inline void
    ResourceOwnerRememberBuffer(ResourceOwner owner, Buffer buffer)
    {
        ResourceOwnerRemember(owner, Int32GetDatum(buffer), &buffer_pin_resowner_desc);
    }
    ```

    이쪽도 위와 비슷하게 작은 배열에 삽입하다가, 넘치면 해시 테이블로 보내버리는 구조다. Resource owner에 대해서는 `src/backend/utils/resowner/resowner.c`를 참고하면 된다.

    **그래서 왜 미리 하는가**

    `resowner.c`의 주석이 이유를 그대로 말해 준다.

    ```c title="resowner.c"
    /*
     * This is separate from actually inserting a resource because if we run out
     * of memory, it's critical to do so *before* acquiring the resource.
     */
    ```

    메모리 할당은 실패할 수 있고, PostgreSQL에서 할당 실패는 `elog(ERROR)`로 에러를 일으킨다. 만약 버퍼 핀을 먼저 잡고 나서 기록하려다 여기서 튕기면, **아무도 기억하지 못하는 핀**이
    남는다. 그 버퍼는 계속 핀이 꽂혀 있는 것으로 나타나기 때문에, 작업이 끝나도 반납되지 않고, 축출 대상에서도 제외된다.

    순서를 뒤집으면 이 문제가 통째로 사라진다. 실패할 수 있는 일(자리 확보)을 아직 아무것도 잡지 않은 상태에서 먼저 해치우고, 그 뒤로는 실패할 수 없는 일(이미 확보된 칸에 값을 채우는 것)만 남기는 것이다.


### 1.3 · 히트 — 우리가 원하는 페이지가 이미 버퍼에 있는 경우

```c title="bufmgr.c"
    if (existing_buf_id >= 0)
    {
        BufferDesc *buf;
        bool        valid;

        /*
         * Found it.  Now, pin the buffer so no one can steal it from the
         * buffer pool, and check to see if the correct data has been loaded
         * into the buffer.
         */
        buf = GetBufferDescriptor(existing_buf_id);

        valid = PinBuffer(buf, strategy);

        /* Can release the mapping lock as soon as we've pinned it */
        LWLockRelease(newPartitionLock);

        *foundPtr = true;

        if (!valid)
        {
            /*
             * We can only get here if (a) someone else is still reading in
             * the page, (b) a previous read attempt failed, or (c) someone
             * called StartReadBuffers() but not yet WaitReadBuffers().
             */
            *foundPtr = false;
        }

        return buf;
    }
```

위에서 몇 가지 중요한 포인트들이 있다.

* **핀을 잡자마자 파티션 락을 놓는다.** 핀이 걸린 순간부터는 아무도 이 프레임을
희생양으로 축출하고 가져갈 수 없으므로, 락을 잡고 있을 필요가 없다. 이제 파티션 락을 놓더라도, 이 핀이 유지되는 동안 해시 테이블에서 이 태그에 대한 매핑이 빠져나올 일은 어차피 없다.
* **버퍼 테이블에서 찾았는데도 `*foundPtr = false`가 될 수 있다.**
`PinBuffer()`의 반환값 `valid`는 "핀을 잡았는가"가 아니라 **"`BM_VALID`가 켜져 있는가"**(즉 내용물이 유효한가) 이고, `valid`하지 않은 경우에는 `*foundPtr = false`로 해버린다. 이는 태그가 등록되어 있어서 버퍼 테이블에서 발견할 수 있었음에도 아직 유효하지 않은 상태이기 때문이다. 주석에 따르면 다음과 같은 경우들을 생각해 볼 수 있다.

| | 상황 |
| --- | --- |
| (a) | 다른 백엔드가 지금 디스크로부터 읽는 중이다. |
| (b) | 이전 읽기 시도가 실패했다 |
| \(c) | 누군가 `StartReadBuffers()`만 부르고 아직 `WaitReadBuffers()`를 부르지 않았다 |

이에 따라서 `*foundPtr`의 정확한 의미는 단순히 버퍼 테이블 히트/미스가 아니라 **"이 버퍼를 바로 써도 되는가 / 읽기가 필요한가"** 다. 참고로 `BufferAlloc()`에 인자로 주어지는 `foundPtr`은 [§07](07-buffer-read-path.md) §1.2에서 보았던 `found` 변수와 같다. 

그런데 이렇게 된다면 여러 백엔드가 중복으로 읽기를 시도할 가능성이 있는데, 중복 읽기는 `AsyncReadBuffers()`의 `StartBufferIO()` 에서 걸러지게 되어서, 문제는 발생하지 않는다.

### 1.4 · 미스 — 희생양 버퍼를 얻어온다

```c title="bufmgr.c"
    /*
     * Didn't find it in the buffer pool.  We'll have to initialize a new
     * buffer.  Remember to unlock the mapping lock while doing the work.
     */
    LWLockRelease(newPartitionLock);

    /*
     * Acquire a victim buffer. Somebody else might try to do the same, we
     * don't hold any conflicting locks. If so we'll have to undo our work
     * later.
     */
    victim_buffer = GetVictimBuffer(strategy, io_context);
    victim_buf_hdr = GetBufferDescriptor(victim_buffer - 1);
```

여기서는 실패 시 파티션 락을 놓게 된다. 이 락은 방금 위에서 우리가 '테이블 조회'를 위해 잡았던 락이다. 락에서의 경합을 막기 위해서 락은 정말 필요한 기간 동안만 잡고 있는 게 원칙이다. 우리는 이제 희생양 버퍼를 찾아서(`GetVictimBuffer()`) 거기에 우리가 원하는 페이지를 가져오도록 해야 하는데, 이 과정은 상당히 오래 걸릴 수 있다. 최악의 경우 희생양 버퍼를 `FlushBuffer()`를 통해 디스크 쓰기까지 해야 할 수도 있다.

물론, 주석에 적혀 있듯이, 이 시점에 우리와 정확히 동일한 행동을 하고 있는 다른 백엔드가 있을 수 있다 — *"Somebody else might try to do the same."* 그래서 그런 가능성을 염두에 두고 처리해 나가야 한다.

어쨌거나 이 시점에 `GetVictimBuffer()`를 통해 우리는 깨끗한(내용물이 디스크에 써졌기 때문에 마음대로 사용해도 되는) 버퍼 프레임을 얻었다.

### 1.5 · 신규 버퍼 테이블 추가 실패

```c title="bufmgr.c"
    LWLockAcquire(newPartitionLock, LW_EXCLUSIVE);
    existing_buf_id = BufTableInsert(&newTag, newHash, victim_buf_hdr->buf_id);
    if (existing_buf_id >= 0)
    {
        /*
         * Got a collision. Someone has already done what we were about to do.
         * We'll just handle this as if it were found in the buffer pool in
         * the first place.  First, give up the buffer we were planning to use.
         */
        UnpinBuffer(victim_buf_hdr);

        /*
         * The victim buffer we acquired previously is clean and unused, let
         * it be found again quickly
         */
        StrategyFreeBuffer(victim_buf_hdr);

        /* remaining code should match code at top of routine */
        existing_buf_hdr = GetBufferDescriptor(existing_buf_id);
        valid = PinBuffer(existing_buf_hdr, strategy);
        LWLockRelease(newPartitionLock);
        *foundPtr = true;
        if (!valid)
            *foundPtr = false;
        return existing_buf_hdr;
    }
```

신규 추가를 위해서는 `LW_EXCLUSIVE`로 락을 잡아야 한다. 버퍼 테이블을 변경(추가/삭제)하는 경우에는 배제 락이 필요하고, 단순히 조회만 할 때에는 공유 락으로 충분하다. 

그런데 `BufTableInsert()`를 했을 때 반환되는 값이 `-1`이면 성공적으로 추가된 것인데, 그게 아니라면 이미 누군가가 `existing_buf_id`로 우리가 원하는 태그의 페이지를 넣어두었음을 알 수 있다. 이런 일이 생기는 이유는 위에서 잠시 파티션 락을 놓았기 때문이다. 많은 백엔드들이 동시에 작동할 때에는 어떤 식으로든 다른 백엔드가 끼어들 가능성이 있음을 염두에 두어야 한다. 그 사이에 다른 백엔드가 나와 같은 블록(즉, 같은 태그를 가진 블록)을 위한 요청을 진행했고, 나보다 먼저 희생양 버퍼를 구해서 삽입까지 끝내고 락을 놓은 상태인 것이다. 지금 내가 락을 잡고 있다는 것은 그 다른 백엔드가 이미 락을 놓았음을 의미한다.

이렇게 되면 우리는 우리가 하던 일을 그만두고, 방금 발견한 버퍼 프레임을 마치 조회하다가 발견한 것처럼 하면 된다. 일단 하던 일을 그만두고 원래 대로 하기 위해서는, 희생양 버퍼를 다시 돌려줘야 한다. 그래서 아래의 1, 2단계를 수행해야 하고, 그 다음은 마치 버퍼 히트가 난 것처럼 행동하면 된다.

1. **`UnpinBuffer()`** — 내가 애써 구한 희생 버퍼를 포기한다.
2. **`StrategyFreeBuffer()`** — freelist에 돌려준다.
3. **버퍼 히트 시 코드를 수행한다** — 남이 넣어 둔 프레임을 핀 잡고 돌아간다.

!!! info "왜 `StrategyFreeBuffer()`까지 부르나"
    희생양 버퍼는 이미 **비워진 상태**로 반환되었다. 태그도 없고, 유효하지도 않다. 그래서 핀만 풀게 되면 나중에 clock-sweep 알고리즘에 의해 발견될 때까지는 사용되지 않는다. 그래서 이 함수를 통해 freelist에 돌려 보내 주면, 다음 버퍼 프레임 요청 시 바로 사용할 수 있게 된다.

### 1.6 · 신규 버퍼 테이블 추가 성공

위에서 `existing_buf_id`가 음수로 나왔다면 추가에 성공한 것이다. 이제 이 버퍼 프레임은 비어 있는 상태이며, 우리가 배제 락을 잡고 있는 동안에 버퍼 테이블에 추가되었다. 남은 것은 우리의 몫이다.

```c title="bufmgr.c"
    /*
     * Need to lock the buffer header too in order to change its tag.
     */
    victim_buf_state = LockBufHdr(victim_buf_hdr);

    /* some sanity checks while we hold the buffer header lock */
    Assert(BUF_STATE_GET_REFCOUNT(victim_buf_state) == 1);
    Assert(!(victim_buf_state & (BM_TAG_VALID | BM_VALID | BM_DIRTY | BM_IO_IN_PROGRESS)));

    victim_buf_hdr->tag = newTag;

    victim_buf_state |= BM_TAG_VALID | BUF_USAGECOUNT_ONE;
    if (relpersistence == RELPERSISTENCE_PERMANENT || forkNum == INIT_FORKNUM)
        victim_buf_state |= BM_PERMANENT;

    UnlockBufHdr(victim_buf_hdr, victim_buf_state);

    LWLockRelease(newPartitionLock);

    *foundPtr = false;
    return victim_buf_hdr;
```

여기서 두 개의 `Assert`가 `GetVictimBuffer()`에서 준수해야 하는 상태를 다시 한번 검증한다.

* `BUF_STATE_GET_REFCOUNT(victim_buf_state) == 1` — refcount는 곧 pin count이므로, 나만 핀을 잡고 있다는 뜻이다.
* `!(victim_buf_state & (BM_TAG_VALID | BM_VALID | BM_DIRTY | BM_IO_IN_PROGRESS))` — 버퍼 프레임의 상태를 나타내는 플래그 중 네 가지가 모두 꺼져 있다(태그도 없고, 유효하지도 않고, 더럽지도 않고, I/O 중도 아니다).

즉, 희생양 버퍼는 완전히 비워져 있고, 누구도 이를 채우려고 하지 않고 있어야 하며, 오직 나만이 핀을 잡고 있어야 한다는 것이다.

이제 버퍼 기술자(여기서 `victim_buf_hdr`로 나오는 것이다)의 태그를 채우고, 상태에 적절한 플래그를 올려준다. 그리고나서야 파티션 락을 놓게 된다. 이때까지 락을 잡고 있어야 하는 이유는, 버퍼 테이블에는 "이 태그 → 이 `buf_id`"의 매핑이 추가되어 있지만, 프레임 내의 태그는 아직 채워지지 않았기 때문이다. 이때 누군가가 읽게 되면 해시 테이블에서 미스가 날 것이다. 

??? info "Assert란?"
    C의 표준 `assert()`는 **"여기까지 왔다면 이 조건은 반드시 참이어야 한다"** 를 코드에 적어 두는
    장치다. 조건이 거짓이면 메시지를 찍고 `abort()`로 프로세스를 죽인다. **프로그래머의 가정이 깨졌음**을 즉시 드러내는 용도로 사용된다.

    PostgreSQL은 표준 `assert()`를 그대로 쓰지 않고 `c.h`에서 `Assert()`를 직접 정의한다. 그런데
    정의가 **세 갈래**다.

    ```c title="src/include/c.h"
    #ifndef USE_ASSERT_CHECKING

    #define Assert(condition)  ((void)true)
    ...
    #elif defined(FRONTEND)

    #include <assert.h>
    #define Assert(p) assert(p)
    ...
    #else                       /* USE_ASSERT_CHECKING && !FRONTEND */

    #define Assert(condition) \
        do { \
            if (!(condition)) \
                ExceptionalCondition(#condition, __FILE__, __LINE__); \
        } while (0)
    #endif
    ```

    **첫번째 — assert 없음**: `USE_ASSERT_CHECKING`이 정의되지 않았으면 `Assert()`는
    `((void)true)`, 즉 아무것도 하지 않는 표현식으로 사라진다. 실제로 데이터베이스 엔진을 프로덕션에서 사용하기 위해 빌드할 때에는 이렇게 해야 한다. 반대로 assert를 켜기 위해서는 PostgreSQL을 빌드할 때 `--enable-cassert`를 플래그로 넣어 줘야 한다.

    **두번째 — 프론트엔드**: `psql`, `pg_dump` 같이 PostgreSQL에 접속하여 사용하는 클라이언트 프로그램은 `FRONTEND`가 정의된 채 컴파일되고, 여기서는 그냥 `<assert.h>`의 표준 `assert()`로 넘긴다. 독립 실행 프로그램이므로 죽어도 자기 하나만 죽는다.

    **셋번째 — 백엔드**: 우리가 보고 있는 버퍼 풀 등은 서버 프로세스의 일부이므로 백엔드에 속한다. 표준 `assert()` 대신 `ExceptionalCondition()`을 부르는데, 이 함수가 하는 일을 보면
    왜 따로 만들었는지 알 수 있다.

    ```c title="utils/error/assert.c"
    write_stderr("TRAP: failed Assert(\"%s\"), File: \"%s\", Line: %d, PID: %d\n",
                 conditionName, fileName, lineNumber, (int) getpid());
    ...
    #ifdef HAVE_BACKTRACE_SYMBOLS
        nframes = backtrace(buf, lengthof(buf));
        backtrace_symbols_fd(buf, nframes, fileno(stderr));
    #endif
    ...
    abort();
    ```

    `#condition`은 **전처리기의 문자열화 연산자**로, 조건식을 소스에 적힌 그대로 문자열로 바꾼다.
    그래서 로그에 `failed Assert("buf_state & BM_VALID")`처럼 **깨진 조건이 그대로 찍힌다.** 여기에
    파일·줄 번호·PID, 그리고 가능하면 백트레이스까지 붙는다. 그래서 백엔드에서 `Assert()`의 조건이 걸리게 되면 어느 백엔드 프로세스(PID가 프로세스 ID를 보여준다)가 어느 지점에서 문제를 일으켰는지 로그로 남기게 된다.

    마지막에 `abort()`를 부름으로써 프로세스를 즉시 종료한다. 이상하게 생각될 수도 있겠지만, assert가 깨졌다는 것은 코드의 가정이 깨진 채로 작동한다는 것이기 때문에 손상이 영구적으로 남는 것을 막기 위함이다. 


---

## 2 · `GetVictimBuffer()` — 빈 프레임을 만들어 온다

버퍼 풀의 읽기 경로에서 가장 깊은 곳에 있는, 중요한 함수다. 이 함수는 희생양 버퍼 프레임을 제공하는데, 단순히 프레임을 제공하는 것뿐만 아니라 이를 완전히 비워서 제공한다. 즉 바로 사용할 수 있는 깨끗한 상태로 제공하는 것이 특징이다.

```c title="bufmgr.c"
static Buffer
GetVictimBuffer(BufferAccessStrategy strategy, IOContext io_context)
```

인자를 살펴 보면, 어떤 페이지를 위한 프레임인지에 대해서는 전혀 신경쓰지 않는다. 즉, 이 함수는 순수하게 버퍼 풀 내부에서 빈 프레임을 얻기 위한 동작을 할 뿐이다. 위에서 보았듯 태그를 붙이고 버퍼 테이블에 추가하는 작업은 `BufferAlloc()`에서 이루어진다.

### 2.1 · 전체 구조 — 실패하면 재시도

우선 `BufferAlloc()`에서와 같이 자원을 미리 확보하는 함수들을 부른다. 주석에서 말하고 있는 spinlock은 버퍼 헤더의 스핀락으로, [버퍼 상태 및 스핀락](./04-buffer-state.md) 문서에서 살펴 본 바 있다. 스핀락을 잡았는데 메모리 할당에 실패하게 되면 문제가 되므로 미리 잡는 것이다. 

`BufferAlloc()`에서 이미 한 것을 또 하는 이유는, ["사용자" 입장에서 본 버퍼 풀](./06-buffer-from-user-perspective.md)에서 보았듯 `ExtendBufferedRel` 계열의 함수들이 `BufferAlloc()`을 경유하지 않고 바로 `GetVictimBuffer`로 들어오기 때문이다.

```c title="bufmgr.c"
    /*
     * Ensure, while the spinlock's not yet held, that there's a free refcount
     * entry, and a resource owner slot for the pin.
     */
    ReservePrivateRefCountEntry();
    ResourceOwnerEnlarge(CurrentResourceOwner);

    /* we return here if a prospective victim buffer gets used concurrently */
again:
    …
```

그 다음에는 `again:` 레이블이 나오고, 내려가다 보면 세 번의 `goto again`이 나타난다. 즉, 고른 희생자가 알고 보니 사용할 수 없는 상황인 경우 처음으로 돌아가서 다시 시작하게 된다.(goto문에 대해서는 다음의 [외부문서](https://dojang.io/mod/page/view.php?id=257)를 참고 바란다)

```text
  again:
    ① StrategyGetBuffer()      희생자를 고른다 (헤더 스핀락을 쥔 채 돌아온다)
    ② PinBuffer_Locked()       핀을 잡고 스핀락을 놓는다
    ③ 더러우면    → 내용 락 획득 실패 시 ───────────▶ goto again
                → 전략이 거부 ──────────────────▶ goto again
                → FlushBuffer()로 디스크에 쓴다
    ④ InvalidateVictimBuffer() 실패 ──────────▶ goto again
    ⑤ 검증하고 반환
```

### 2.2 · 희생양 고르기 — `StrategyGetBuffer()`

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

`StrategyGetBuffer()`는 순수하게 희생양 프레임을 **선정**해서 돌려주는 함수다. `src/backend/storage/buffer/freelist.c`에 있는데, 버퍼 교체 정책에 대해서는 [§09](09-replacement-strategy.md)에서 따로 다룬다.** 

주석에서 나와있듯 `StrategyGetBuffer()`는 헤더 스핀락을 쥔 채로 프레임을 돌려준다. 그 이유는 헤더 스핀락을 놓는 순간 다른 백엔드가 그 프레임에 핀을 걸 수 있기 때문이다. 그래서 핀이 걸리지 않은, 마음대로 써도 되는 상태로 유지해야 하며, 이는 `Assert()`를 통해서 재확인된다. 그래서 `PinBuffer_Locked()`는
이미 스핀락이 잡혀 있다고 가정하고 핀을 올린 뒤 스핀락을 풀어 주게 된다.

??? info "⭐️⭐️⭐️ 왜 헤더 스핀락을 놓아주는 것일까?"
    우리가 스핀락을 놓아주었기 때문에, 그 사이에 다른 백엔드가 이 버퍼 프레임에 대해서 핀을 걸 수 있다. 그러면 이 프레임을 재활용하는 것은 적절하지 않다. 이러한 경우에 염두를 두고, `GetVictimBuffer()`의 말미에 호출되는 `InvalidateVictimBuffer()`에서는 다른 백엔드에 의해 핀되거나 수정되었는지를 확인한다. 그렇다면 애초에 스핀락을 계속 쥐고 있지 않는 이유가 무엇일까? 그랬더라면 내가 확보한 희생양을 확실하게 가져갈 수 있는데 말이다.

    그 이유는 스핀락이 갖는 특성에 있다. 스핀락은 대기자가 CPU를 계속 사용하면서 도는 락이기 때문에, CPU 코어를 사실상 마비시키는 것과 같다. 그렇기 때문에 원칙적으로 짧게 잡아줘야 한다. 그리고 스핀락을 잡고 있으면서 잠들 수 있는 경우는 회피해야 한다. 예를 들어서 디스크 읽기를 한다거나, PostgreSQL의 LWLock을 잡으려고 한다거나 하는 경우를 말한다. 또한 만약 에러가 날 경우 스핀락을 해제해 주는 주체가 없다. 다른 락(LWLock)이나 핀은 에러로 인한 종료 시에 반납해주는 `ResourceOwner`가 있지만, 비트 형태로 켜져 있는 스핀락은 별도로 관리되지 못해서 영원히 잡혀있게 된다.

    이와 같은 이유로 여기서는 우선 스핀락을 놓고 진행한 다음 문제가 생길 것처럼 보이는 경우 포기하고 재확인하는 낙관적 동시성 제어를 수행한다. 그러면서도 핀을 잡았기 때문에 스핀락의 부담 없이 안전한 프레임 소유권을 보유할 수 있다.

### 2.3 · 수정되었다면 디스크에 쓴다 — `FlushBuffer()`

여기서 고른 희생양 버퍼 프레임이 더러우면(dirty) 어느 백엔드에 의해서인가 수정되었다는 의미다. 수정되었는지 여부는 `BM_DIRTY`를 확인하여 알 수 있다. 만약 수정된 내용이 있다면 이것은 아직 디스크에 적히지 않았기 때문에, 반드시 디스크에 적어주어야 한다. 

디스크에 적기 전에는 반드시 해당 버퍼의 내용을 보호하기 위한 `content_lock`을 잡아줘야 한다. 내용 락은 버퍼의 내용에 접근할 때 잡는 락으로, 버퍼 풀 내부에서는 거의 잡을 일이 없지만, 외부에서는 버퍼 프레임을 얻고 나서 일반적으로 잡게 된다. 이는 읽고 있는 버퍼의 내용이 갑자기 수정되거나 하는 일을 막기 위함이다. 여기서 내용 락을 잡는 이유는 우리가 디스크에 쓰는 동안에 수정되어서는 안 되기 때문이다. 우리는 내용을 수정하지는 않기 때문에, 공유 락(`LW_SHARED`)을 잡는다.

```c title="bufmgr.c"
    if (buf_state & BM_DIRTY)
    {
        LWLock     *content_lock;

        content_lock = BufferDescriptorGetContentLock(buf_hdr);
        if (!LWLockConditionalAcquire(content_lock, LW_SHARED))
        {
            /*
             * Someone else has locked the buffer, so give it up and loop back
             * to get another one.
             */
            UnpinBuffer(buf_hdr);
            goto again;                       /* ← 포기 ① */
        }
        …  /* ← nondefault strategy에 관한 부분은 일단 생략 */
        /* OK, do the I/O */
        FlushBuffer(buf_hdr, NULL, IOOBJECT_RELATION, io_context);
        LWLockRelease(content_lock);

        ScheduleBufferTagForWriteback(&BackendWritebackContext, io_context,
                                      &buf_hdr->tag);
    }
```

여기서 내용 락을 잡지 못하게 되면 포기하고 `StrategyGetBuffer()`부터 재시도한다. 

다음의 위에서는 생략했던 nondefault 전략의 경우다. 이에 관해서는 [§09](09-replacement-strategy.md)에서 보다 상세히 논의한다. 어쨌거나 여기서는 교체 전략이 있고, 그 전략의 판단(`StrategyRejectBuffer()`)에 의해 결정되는 것으로 이해하면 된다.

```c title="bufmgr.c"
        if (strategy != NULL)
        {
            XLogRecPtr  lsn;

            /* Read the LSN while holding buffer header lock */
            buf_state = LockBufHdr(buf_hdr);
            lsn = BufferGetLSN(buf_hdr);
            UnlockBufHdr(buf_hdr, buf_state);

            if (XLogNeedsFlush(lsn)
                && StrategyRejectBuffer(strategy, buf_hdr, from_ring))
            {
                LWLockRelease(content_lock);
                UnpinBuffer(buf_hdr);
                goto again;                   /* ← 포기 ② */
            }
        }
```

### 2.4 · 프레임의 정체를 무효화한다 — `InvalidateVictimBuffer()`

디스크에 쓰는 것까지 끝났으면 프레임을 무효화해야 한다. 현재 프레임의 내용은 안전하게 디스크에 쓰였기 때문에 마음대로 비워도 된다. 그래서 프레임의 기술자에 적혀있는 정보를 지워야하고, 버퍼 테이블로부터도 지워야 한다. 이를 위해 아래와 같이 `InvalidateVictimBuffer()`를 호출한다.

```c title="bufmgr.c"
    /*
     * If the buffer has an entry in the buffer mapping table, delete it. This
     * can fail because another backend could have pinned or dirtied the
     * buffer.
     */
    if ((buf_state & BM_TAG_VALID) && !InvalidateVictimBuffer(buf_hdr))
    {
        UnpinBuffer(buf_hdr);
        goto again;                           /* ← 포기 ③ */
    }
```

`InvalidateVictimBuffer()` 안이 [1.1](08-buffer-alloc/#bufferalloc)의 `BufferAlloc()`과 대칭적이다.

```c title="bufmgr.c"
    /* have buffer pinned, so it's safe to read tag without lock */
    tag = buf_hdr->tag;

    hash = BufTableHashCode(&tag);              /* ← 옛 태그의 해시 */
    partition_lock = BufMappingPartitionLock(hash);

    LWLockAcquire(partition_lock, LW_EXCLUSIVE);
    buf_state = LockBufHdr(buf_hdr);

    /*
     * If somebody else pinned the buffer since, or even worse, dirtied it,
     * give up on this buffer: It's clearly in use.
     */
    if (BUF_STATE_GET_REFCOUNT(buf_state) != 1 || (buf_state & BM_DIRTY))
    {
        UnlockBufHdr(buf_hdr, buf_state);
        LWLockRelease(partition_lock);
        return false;                           /* ← 실패 */
    }

    ClearBufferTag(&buf_hdr->tag);
    buf_state &= ~(BUF_FLAG_MASK | BUF_USAGECOUNT_MASK);   /* 플래그 전부 끄기 */
    UnlockBufHdr(buf_hdr, buf_state);

    /* finally delete buffer from the buffer mapping table */
    BufTableDelete(&tag, hash);

    LWLockRelease(partition_lock);
    return true;
```
주요한 내용들은 다음과 같다.

* **'옛 태그'의 파티션 락을 배제 락으로 잡는다.** 그 이유는 이를 버퍼 테이블에서 삭제해야 하기 때문이다.
* **누군가가 핀을 걸었거나 더럽혔다면 포기한다.** 여기서 포기하게 되면 다시 시도하게 된다.
* **버퍼 상태의 플래그를 통째로 끈다.** `buf_state &= ~(BUF_FLAG_MASK | BUF_USAGECOUNT_MASK)`를 통해 완전히 꺼준다.
* **버퍼 테이블에서 지운 이후에 파티션 락을 놓는다.**

### 2.5 · 버퍼를 반환한다

```c title="bufmgr.c"
    /* a final set of sanity checks */
#ifdef USE_ASSERT_CHECKING
    buf_state = pg_atomic_read_u32(&buf_hdr->state);

    Assert(BUF_STATE_GET_REFCOUNT(buf_state) == 1);
    Assert(!(buf_state & (BM_TAG_VALID | BM_VALID | BM_DIRTY)));

    CheckBufferIsPinnedOnce(buf);
#endif

    return buf;
```

`Assert()`를 사용하는 빌드에서는 마지막의 몇 가지 사항들을 체크한다. 실제 프로덕션에서는 사용되지 않지만, 이는 개발 과정에서 중요한 의미를 갖는다. 이를 통해 우리는 `GetVictimBuffer()`가 호출자에게 어떤 상태의 버퍼를 돌려주고자 하는지 알 수 있다.
* `refcount == 1`: 핀이 하나 걸려있다
* `CheckBufferIsPinnedOnce(buf)`: 나(백엔드)는 이 버퍼에 핀을 하나만 꽂고 있다
* `BM_TAG_VALID`, `BM_VALID`, `BM_DIRTY`가 모두 꺼져 있다: 실질적으로 백지 상태의 버퍼이며, 버퍼 테이블에도 등록되어있지 않고, 써야 할 내용도 없다

---

## 3 · 전체 흐름 정리

```text
BufferAlloc(smgr, forkNum, 42, strategy, &found)
  │
  │  P1  InitBufferTag → BufTableHashCode → BufMappingPartitionLock
  │      LWLockAcquire(SHARED) → BufTableLookup
  │
  ├─ P2  히트 ─▶ PinBuffer → LWLockRelease → *foundPtr = (valid) → return
  │
  │  P3  미스 ─▶ LWLockRelease
  │              │
  │              └─▶ GetVictimBuffer(strategy, io_context)
  │                    │
  │                    │  again:
  │                    ├─ StrategyGetBuffer()   
  │                    │     └ 모두 핀 ─▶ ERROR "no unpinned buffers available"
  │                    ├─ PinBuffer_Locked()
  │                    ├─ BM_DIRTY ?
  │                    │     ├ 내용 락 실패 ──────────▶ goto again
  │                    │     ├ 전략이 거부 ───────────▶ goto again
  │                    │     └ FlushBuffer()  
  │                    ├─ InvalidateVictimBuffer()   옛 파티션 락 · BufTableDelete
  │                    │     └ 실패 ────────────────▶ goto again
  │                    └─ return  (refcount==1, 플래그 전부 off)
  │
  │  P4  LWLockAcquire(EXCLUSIVE) → BufTableInsert
  │      └ 충돌 ─▶ UnpinBuffer + StrategyFreeBuffer → P2와 동일하게 처리
  │
  └─ P5  LockBufHdr → tag 대입 → BM_TAG_VALID | BUF_USAGECOUNT_ONE
         UnlockBufHdr → LWLockRelease → *foundPtr = false → return
```

## 4 · 정리

* `BufferAlloc()`은 파티션 락 없이 희생자를 얻는다.
* `*foundPtr`은 "읽기가 필요한가"이지 "히트인가"가 아니다.
* `GetVictimBuffer()`는 페이지의 신원을 모른다. 빈 프레임만 공급하고, 이름 붙이는 일은 `BufferAlloc()`이 한다.
* 희생자는 대체 가능하다. 그래서 조금이라도 곤란해지면 기다리지 않고 `goto again` 한다.

!!! question "생각해 볼 거리"

    * `BufferAlloc()`이 파티션 락을 **쥔 채로** `GetVictimBuffer()`를 부르도록 바꾸면 간편할 텐데 그렇게 하지 않는 이유는?
    * `GetVictimBuffer()`의 세 `goto again`은 모두 "이미 한 일을 버리고 처음부터"다. 그중 가장 비싼 것은 어느 것인가?

## 앞으로 볼 것

| | 다룰 내용 |
| --- | --- |
| [§09](09-replacement-strategy.md) | 클럭 스윕과 교체 정책 |
| §11(준비 중) | 핀, 내용 락, 헤더 스핀락 — 세 가지가 어떻게 다른가 |
| §12(준비 중) | `FlushBuffer()`가 지켜야 하는 WAL 규칙 |
