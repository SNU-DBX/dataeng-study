# 02 · 공유 메모리

## 공유 메모리(Shared Memory)란

### 공유 메모리가 필요한 이유

가상 메모리를 사용하는 OS 상에서 모든 프로세스는 자기만의 가상 주소 공간(virtual address space)를 갖기 때문에 서로 다른 프로세스는 서로의 메모리에 접근할 수 없다(isolation). 만약 메모리에 있는 데이터를 공유하고 싶다면 어떻게 해야할까? 시스템 프로그래밍의 기초를 배웠다면 파이프(pipe)를 통해 데이터를 적어넣고 읽는 방식의 Inter-Process Communication(IPC)에 대해 익숙할 것이다. 그러나 이는 끊임없는 데이터 복사를 필요로 하기 때문에, 수많은 프로세스들이 접근해야 하는 버퍼 풀에는 적절하지 않다.

### 공유 메모리

**공유 메모리(shared memory)**는 동일한 물리 메모리 프레임을 여러 가상 주소 공간에 매핑한 것이다. 아래와 같이 두 프로세스 A, B가 공유 메모리 공간에 접근한다면, *약간의 마찰*은 있겠지만 별도의 시스템 콜이나 복사 과정 없이 한 프로세스의 변경 사항을 다른 프로세스가 읽을 수 있다 (원칙적으로는 두 매핑이 꼭 같은 가상 주소로 이어질 필요는 없다). 그러나 PostgreSQL에서는 데이터베이스 프로세스들이 적어도 공유 메모리에 대해서는 같은 가상 주소 매핑을 갖고 있다. 그 이유는 이 프로세스들이 하나의 프로세스로부터 [`fork()`](https://man7.org/linux/man-pages/man2/fork.2.html)되었기 때문이다.

```text
   process A                     process B
   ┌────────────┐                ┌────────────┐
   │ 0x7f…1000  │──┐          ┌──│ 0x7f…3000  │   (virtual addresses may differ!)
   └────────────┘  │          │  └────────────┘
                   ▼          ▼
                  ┌─────────────┐
                  │   frame 42  │                 one physical frame
                  └─────────────┘
```

!!! info "PostgreSQL의 프로세스 모델"

    PostgreSQL은 스레드가 아니라 프로세스를 쓴다. 서버를 시작하면 먼저 **postmaster**라고 불리는 핵심 프로세스 하나가 구동된다. postmaster의 역할은 직접 데이터베이스 쿼리를 처리하는 것이 아니라, 포트를 열어 두고 기다리다가 클라이언트 연결이
    들어올 때마다 [`fork()`](https://man7.org/linux/man-pages/man2/fork.2.html)해서 자식 프로세스를
    하나씩 만들고, 그 프로세스가 해당 연결의 질의를 처리하도록 하는 것이다(process-per-connection 모델). PostgreSQL에서는 이 자식 프로세스를 **백엔드(backend)** 라고 부른다. 이외에도 PostgreSQL은 여러 보조 프로세스들을 사용하는데, 이들도 같은 방식으로 `fork()`된다.

    ```text
    postmaster ── 포트 대기, 질의 처리 안 함
        ├─ backend      (클라이언트 1)   ─┐
        ├─ backend      (클라이언트 2)    │  전부 fork()된 자식
        ├─ backend      (클라이언트 3)    │  → 공유 메모리 매핑을 상속
        ├─ checkpointer                │
        ├─ background writer           │
        ├─ walwriter                   │
        └─ autovacuum launcher        ─┘
    ```

### POSIX 공유 메모리

POSIX에서는 공유 메모리를 **메모리 안에 사는 파일**로 모델링한다
([`shm_overview(7)`](https://man7.org/linux/man-pages/man7/shm_overview.7.html)). 이름을 붙여 객체를
만들고, 파일처럼 크기를 정하고, 그것을 자기 주소 공간에 매핑해 쓴다. 리눅스에서는 `/dev/shm` 아래 파일로 나타나게 된다. 대략적인 흐름은 다음과 같다.

| 순서 | 호출 | 의미 |
|---|---|---|
| 1 | [`shm_open("/name", O_CREAT\|O_RDWR, 0600)`](https://man7.org/linux/man-pages/man3/shm_open.3.html) | 이름 붙은 객체를 생성한다. 파일 디스크립터(fd)가 반환된다. |
| 2 | [`ftruncate(fd, size)`](https://man7.org/linux/man-pages/man2/ftruncate.2.html) | 크기를 정한다. 갓 만든 객체는 크기가 0이라 이 단계가 필수다. |
| 3 | [`mmap(NULL, size, PROT_READ\|PROT_WRITE, MAP_SHARED, fd, 0)`](https://man7.org/linux/man-pages/man2/mmap.2.html) | fd를 이용해 현재 프로세스의 주소 공간에 매핑한다. |
| 4 | [`close(fd)`](https://man7.org/linux/man-pages/man2/close.2.html) | 현재 프로세스로부터 파일을 닫는다. 공유 메모리 매핑 자체는 남아있게 된다. |
| 5 | [`munmap(addr, size)`](https://man7.org/linux/man-pages/man2/munmap.2.html) / [`shm_unlink("/name")`](https://man7.org/linux/man-pages/man3/shm_unlink.3.html) | munmap()은 내 매핑만 해제하고, shm_unlink()는 객체를 지워 회수 가능하게 만든다. |

!!! note "System V"

    같은 일을 하는 더 오래된 API로 [`shmget`](https://man7.org/linux/man-pages/man2/shmget.2.html) /
    [`shmat`](https://man7.org/linux/man-pages/man2/shmat.2.html)이 있다. 파일 경로 대신 정수
    키(key)로 세그먼트를 식별하고, 파일 디스크립터 대신 세그먼트 ID를 쓴다. 이 방식의 장점은 붙어 있는
    프로세스 수를 `shmctl()`로 조회할 수 있다는 것이다.

그런데 PostgreSQL이 실제로 쓰는 것은 위의 `shm_open` 경로가 아닌 **익명 매핑(anonymous
mapping)** 이다. 뒷받침이 되는 파일(backing file)이 없는 익명 메모리를 요청한다. 예를 들면 다음과 같은 형태로 익명 매핑을 만들 수 있다.

```c
ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
/*                                      ↑
 *                                그래서 fd가 -1 */
```

`MAP_ANONYMOUS`는 "이 매핑을 뒷받침할 파일은 없다"는 뜻이다. 그래서 `fd`로 `-1`을 넘기고,
`shm_open`도 `ftruncate`도 필요 없다. 그런데 **이름이 없으면 다른 프로세스가 어떻게 찾아오는가?** 답은 불가능하다는 것이다(일부 예외 존재). 대신 `fork()`로 만들어진 자식은 부모의 주소 공간을 복제하면서 이 매핑도 함께 물려받기 때문에, 익명 공유 매핑된 메모리에 접근할 수 있다. 따라서 PostgreSQL에서는 모든 백엔드와 보조 프로세스가 postmaster의 자식이므로 별도의 어려움 없이 공유 메모리에 접근할 수 있다.

## PostgreSQL의 공유 메모리 관리

이제 PostgreSQL이 공유 메모리를 어떻게 초기화하고 사용하는지 알아보자. 이는 `src/backend/storage/ipc/ipci.c` 파일의 `CreateSharedMemoryAndSemaphores()` 함수에서 이루어진다. PostgreSQL은 익명 `mmap()`으로 충분한 크기의 공유 메모리를 **한 번에** 확보해 둔 뒤, 공유 메모리가 필요한 서브시스템들이 그 안에서 자기 몫을 떼어 가게 한다. 이렇게 하는 이유는 공유 메모리 세그먼트를 나중에 키울 수 없기 때문이다.

**첫번째 단계: 크기 측정 및 세그먼트 생성**

 `CalculateShmemSize()`가 서브시스템마다 정의된 `###ShmemSize()`를 차례로 불러 합한다. 버퍼 풀의 경우 `BufferManagerShmemSize()`가 그 역할을 맡는다. 여기에 약간의 여유를 더하고 8 KB의 배수로 올림한 값이 최종 요청 크기가 된다.

위에서 계산된 크기로 `PGSharedMemoryCreate()`가 익명 mmap 세그먼트를 만든다. 이어지는
`InitShmemAllocation()`이 공유 메모리를 할당해주기 위한 기본적인 할당기를 준비한다. 공유 메모리 할당기는 `freeoffset`을 앞으로 밀기만 하는 **범프 할당기(bump allocator)** 이고
해제 기능이 없다.

**두번째 단계: 공유 메모리 할당**

`CreateOrAttachShmemStructs()`가 서브시스템마다 정의된 `###ShmemInit()`을
차례로 부른다. 버퍼 풀은 `BufferManagerShmemInit()`이다. 각 함수는 `ShmemInitStruct("이름", 크기, &found)`를 호출해 `freeoffset`을 그만큼 전진시키고 그 자리의 포인터를 돌려받는다. 여기서 주의할 점은 첫번째 단계에서 미리 잡아 둔 메모리보다 두번째 단계에서 할당 요청된 메모리가 더 많은 경우 할당이 실패할 수 있다는 점이다(`src/backend/storage/ipc/shmem.c`의 `ShmemInitStruct()` 참고).

```text
  postmaster startup           (postmaster.c)
        │
        ▼
  CreateSharedMemoryAndSemaphores()           (src/backend/storage/ipc/ipci.c)
        │
        ├─ CalculateShmemSize(&numSemas)
        │             size = 100000                        (slop for small stuff)
        │             + PGSemaphoreShmemSize(...)
        │             + BufferManagerShmemSize()   ◀── your pools get counted HERE
        │             + LockManagerShmemSize()
        │             + ... ~40 more subsystems
        │             round up to 8 KB
        │
        ├─ PGSharedMemoryCreate(size)   ── one segment, no growth
        ├─ InitShmemAccess() / InitShmemAllocation()
        │
        └─ CreateOrAttachShmemStructs()         
                      CreateLWLocks()
                      InitShmemIndex()
                      XLOGShmemInit()
                      CLOGShmemInit()
                      BufferManagerShmemInit()   ◀── your pools get built HERE
                      LockManagerShmemInit()
                      ...
```