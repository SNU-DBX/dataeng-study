
#### 정리 락 대기자

`wait_backend_pgprocno`는 **이 버퍼에 대해 정리 락(cleanup lock)을 얻으려고 기다리고 있는 백엔드**가 누구인지를 적어두는 자리다. 값은 그 백엔드의 `PGPROC` 슬롯 번호이며, 기다리는 사람이 없을 때는 `INVALID_PROC_NUMBER`다. 초기화 루프가 하는 일도 딱 그것으로, 모든 기술자를 "대기자 없음" 상태로 만들어 둔다.

여기서 정리 락이란 다음 두 조건을 **동시에** 만족하는 상태를 말한다.

1. 버퍼 내용 락(content lock)을 배타 모드로 잡고 있을 것
2. 그 버퍼의 공유 핀 카운트가 정확히 `1`일 것 — 즉 **자기 자신 말고는 아무도 이 페이지를 붙잡고 있지 않을 것**

배타 락만으로는 "지금 이 페이지를 쓰는 사람이 나뿐"이 보장되지 않는다. 다른 백엔드가 락은 놓았지만 핀은 유지한 채 페이지 안의 튜플을 가리키는 포인터를 들고 있을 수 있기 때문이다. 그래서 페이지 안에서 튜플의 물리적 위치를 옮기는 작업(대표적으로 `VACUUM`의 페이지 정리)은 배타 락으로는 부족하고, 남들의 핀이 전부 빠질 때까지 기다려야 한다.

문제는 "핀이 다 빠지는" 순간을 기다리는 쪽이 알 방법이 없다는 것이다. 핀을 놓는 것은 다른 백엔드이고, 그들은 자기가 마지막 핀 보유자인지 신경 쓰지 않는다. 그래서 다음과 같이 협조 구조를 만든다.

* 정리 락을 원하는 백엔드가 `LockBufferForCleanup()`에서 핀 카운트가 1이 아님을 확인하면, 버퍼 상태에 `BM_PIN_COUNT_WAITER` 플래그를 세우고 `wait_backend_pgprocno`에 자기 번호를 적은 뒤, 내용 락을 풀고 잠든다.
* 나중에 누군가 `UnpinBuffer()`로 핀을 놓다가 `BM_PIN_COUNT_WAITER`가 켜져 있고 핀 카운트가 1로 떨어진 것을 발견하면, 플래그를 지우고 `wait_backend_pgprocno`가 가리키는 백엔드에게 신호를 보내 깨운다.

즉 이 필드는 **"내가 마지막 핀을 놓는 사람이 되면 누구를 깨워야 하는가"** 를 적어둔 쪽지다. 조건 변수나 대기 큐를 따로 두지 않고 기술자 안의 정수 하나로 해결한 것이다.

??? question "핀을 기다리는 백엔드가 여럿일 수 있는데, 하나만 저장해도 되는가?"

    "한 버퍼에 대한 정리 락 대기자는 동시에 최대 한 명"이라는 것이 이 설계의 **불변식**이다. 그리고 이 불변식은 버퍼 매니저가 아니라 **한 층 위의 릴레이션 락**이 보장한다.

    버퍼 내용 락이 보장해 주지는 못한다는 점에 주의하자. `LockBufferForCleanup()`은 대기자로 등록한 직후 `LockBuffer(buffer, BUFFER_LOCK_UNLOCK)`으로 **내용 락을 놓고** 잠든다. 락을 쥔 채 기다리면, 핀을 놓아 주어야 할 바로 그 백엔드가 페이지를 다시 읽으려다 락에 걸려 아무도 진행하지 못하기 때문이다. 따라서 A가 자는 동안 B가 같은 버퍼의 배타 락을 얻는 것 자체는 가능하다.

    그럼에도 실제로 그런 일이 없는 이유는 블로킹 정리 락을 요청하는 작업들이 전부 릴레이션 수준에서 이미 직렬화되어 있기 때문이다. `VACUUM`은 자기 자신과 충돌하는 `ShareUpdateExclusiveLock`을 잡으므로 한 테이블에 하나뿐이고, B-tree·GIN·해시 인덱스의 페이지 삭제도 모두 그 VACUUM 안에서 일어난다. WAL 재생 중의 정리 락은 스타트업 프로세스 혼자 요청한다. 소스 주석도 *"That's impossible with the current usages due to table level locking, but better be safe"* 라고 적고 있다.

    그래서 `LockBufferForCleanup()`은 이미 `BM_PIN_COUNT_WAITER`가 켜져 있으면 조용히 줄을 서는 대신 `elog(ERROR, "multiple backends attempting to wait for pincount 1")`로 중단한다. 정상 동작에서는 도달할 수 없는 경로이고, 위 불변식이 깨졌다는 뜻이기 때문이다.

    참고로 `ConditionalLockBufferForCleanup()`은 대기자로 등록하지 않는다. 조건을 즉시 만족하지 못하면 그냥 `false`를 반환하므로 이 논의와 무관하다. HOT 프루닝처럼 "되면 하고 아니면 넘어가는" 작업들이 이쪽을 쓴다.

??? info "`wait_backend_pgprocno`는 플래그가 켜져 있을 때만 유효하다"

    깨우는 쪽(`UnpinBuffer()`)은 `BM_PIN_COUNT_WAITER` 플래그만 끄고 이 필드는 그대로 둔다. 즉 대기가 끝난 뒤에도 옛 대기자의 번호가 남아 있고, 다음 대기자가 와서 덮어쓴다. 이 필드는 플래그와 **한 쌍으로만** 의미를 가진다.

    그래서 플래그를 끄는 코드는 항상 주인을 함께 확인한다.

    ```c
    if ((buf_state & BM_PIN_COUNT_WAITER) != 0 &&
        bufHdr->wait_backend_pgprocno == MyProc->pgprocno)
        buf_state &= ~BM_PIN_COUNT_WAITER;
    ```

    깨어난 직후(다른 신호로 깨어났을 수도 있다)와, 대기 도중 취소·에러로 죽을 때의 뒷정리(`UnlockBuffers()`) 두 곳에서 같은 패턴이 쓰인다. 확인 없이 껐다가는 그 사이 새로 등록한 다른 백엔드의 대기를 지워 영원히 재우게 된다.

??? info "핀(pin)과 락(lock)"

    둘은 다른 것이다. **핀**은 "이 페이지를 버퍼 풀에서 쫓아내지 마라"는 참조 카운트이고, **락**은 "이 페이지의 내용을 읽는/쓰는 중이다"라는 동시성 제어다. 핀을 잡은 채 락만 놓는 상황이 흔하기 때문에, 정리 락처럼 페이지를 물리적으로 재배치하는 작업은 락과 핀 두 가지를 모두 봐야 한다. 자세한 내용은 버퍼 상태를 다루는 문서에서 이어진다.



#### 버퍼 테이블

`InitBufTable()`이 만드는 공유 해시 테이블이다. 하는 일은 단 하나, **`BufferTag` → `buf_id` 변환**이다. 앞서 두 식별자를 구분할 때 말한 그 사전이 여기서 만들어진다.

```c title="src/backend/storage/buffer/buf_table.c"
typedef struct
{
    BufferTag   key;            /* Tag of a disk page */
    int         id;             /* Associated buffer ID */
} BufferLookupEnt;

void
InitBufTable(int size)
{
    HASHCTL     info;

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

엔트리는 `{태그, 버퍼 번호}` 한 쌍이 전부다. 주목할 점은 세 가지다.

* **크기가 고정되어 있다.** `ShmemInitHash()`에 초기 크기와 최대 크기를 같은 값(`size`)으로 넘긴다. 공유 메모리에 잡힌 해시 테이블은 나중에 늘릴 수 없기 때문에, 처음부터 최대치를 확보해 두는 수밖에 없다.
* **파티션되어 있다.** `HASH_PARTITION`과 `num_partitions = NUM_BUFFER_PARTITIONS`(128)로 테이블을 128조각으로 나눈다. 테이블 전체를 하나의 락으로 보호하면 모든 백엔드의 페이지 조회가 그 락 하나에 직렬화되기 때문이다. 대신 각 파티션마다 LWLock이 하나씩 있고, 자기가 건드릴 파티션만 잠근다.
* **해시 코드를 미리 계산해서 넘긴다.** 어느 파티션을 잠글지 알려면 먼저 해시 값을 알아야 하는데, 잠근 뒤에 다시 계산하는 것은 낭비다. 그래서 호출자가 `BufTableHashCode(&tag)`로 해시 코드를 구하고 → `BufMappingPartitionLock(hashcode)`로 락을 잡고 → 그 해시 코드를 `BufTableLookup()`에 함께 넘기는 순서로 쓴다.\

#### 그림 정리

지금까지 나온 조각들을 페이지 하나를 요청하는 흐름 위에 얹으면 다음과 같다.

```
"이 페이지 주세요" (BufferTag)
        │
        ├─▶ 버퍼 테이블에서 조회            … 있으면 끝. 핀만 잡고 그 버퍼를 쓴다
        │     (tag → buf_id, 파티션 락)
        │
        └─▶ 없으면 담을 자리를 구해야 한다 — StrategyGetBuffer()
              ├─ 프리리스트에 남아 있으면 머리에서 하나  (firstFreeBuffer, freeNext)
              └─ 없으면 클럭 스윕으로 희생자 선정        (nextVictimBuffer)
                    │
                    └─▶ 자리를 구했으면 버퍼 테이블에 새 태그를 등록하고,
                        희생 버퍼의 옛 태그는 지운다
```

여기서 꼭 구분해야 할 것은 **두 가지 "찾기"가 서로 다른 자료구조를 쓴다**는 점이다.

| | 질문 | 자료구조 |
| --- | --- | --- |
| 조회 | "이 페이지가 메모리에 있는가?" | 버퍼 테이블(해시) |
| 확보 | "없으면 어느 칸에 담을까?" | 프리리스트 + 클럭 스윕 |