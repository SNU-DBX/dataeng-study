# 07 · 버퍼 풀의 읽기 요청 처리 과정

!!! abstract 목표
    읽기 요청이 버퍼 매니저 내에서 어떤 단계를 거쳐 내려가는지, 그리고 공유 버퍼 풀에서 핵심적인 역할을 하는 `BufferAlloc()`이 무슨 일을 하는지 이해한다.

[지난 문서](./06-read-path.md)에서 `ReadBuffer()` 계열과 읽기 스트림이 모두 `StartReadBuffer(s)()`와
`WaitReadBuffers()`를 거친다는 것을 보았다. 이 두 함수는 사실 비동기 I/O를 위해
만들어진 구조이나, 우리의 주 관심사는 동기 I/O이기 때문에 이에 집중한다.

그래서 이 장에서는 먼저 동기 I/O 모드(`io_method = sync`)에서 한 개의 블록을 읽어들이기 위한 `ReadBuffer()` 한 번이 어떻게 처리되는지를 끝까지 따라간다. 그러고 나서
비동기 I/O와 읽기 스트림을 바탕으로 이런 식의 설계가 이루어진 이유를 살펴본다. 그 다음으로 버퍼 풀의 핵심이라고 할 수 있는 `BufferAlloc()`을 다룬다.
---

## 1 · `ReadBuffer()` 하나가 읽히기까지

### 1.1 · `ReadBuffer_common()`의 마지막 — 시작하고, 곧바로 기다린다

`ReadBuffer()` → `ReadBufferExtended()` → `ReadBuffer_common()`으로 내려오면 끝에 이런 코드가 있다.

```c title="bufmgr.c"
    /*
     * Signal that we are going to immediately wait. If we're immediately
     * waiting, there is no benefit in actually executing the IO
     * asynchronously, it would just add dispatch overhead.
     */
    flags = READ_BUFFERS_SYNCHRONOUSLY;
    if (mode == RBM_ZERO_ON_ERROR)
        flags |= READ_BUFFERS_ZERO_ON_ERROR;
    operation.smgr = smgr;
    operation.rel = rel;
    operation.persistence = persistence;
    operation.forknum = forkNum;
    operation.strategy = strategy;

    if (StartReadBuffer(&operation, &buffer, blockNum, flags))
        WaitReadBuffers(&operation);

    return buffer;
```
여기서 보면 **`StartReadBuffer()` 바로 다음 줄에서 `WaitReadBuffers()`를 부른다.** 시작해 놓고 다른 일을 하다가 나중에 기다리는 것이 아니라, 시작하자마자 기다린다. 그러니 `ReadBuffer()`에 관한 한
  이 두 단계로 나뉜 것은 아무 이득이 없다.

특히, 애초에 코드가 아예 `flags = READ_BUFFERS_SYNCHRONOUSLY;` 플래그를 통해 무조건 동기적으로 수행하도록 만든다. 주석에서도 *"there is no benefit in actually executing the IO asynchronously, it would just add dispatch overhead."*라는 점이 명시되어 있다. 만약 우리가 PostgreSQL에서 비동기 I/O를 사용하도록 설정하더라도, `ReadBuffer()`계열로 들어 온 이상 동기 I/O가 강제된다.

한 가지 눈여겨 보아야 할 점은 `StartReadBuffer()`의 반환 값에 `if`를 걸어서, `true`일 때에만 `WaitReadBuffers()`을 호출하도록 했다는 것이다. `StartReadBuffer()` → `StartReadBuffersImpl()`을 따라가 보면, 버퍼 풀에서 원하던 블록을 찾았을 때는(buffer hit) `false`를 반환하고, 그렇지 않은 경우 `did_start_io = true;`로 지정한 뒤 이 값을 반환하여 기다리도록 만든다. 즉 이 반환값의 의미는 "기다려야 하는가"에 있다.  

### 1.2 · `StartReadBuffer()` — 프레임을 확보한다

`StartReadBuffer()`(단수형)와 `StartReadBuffers()`(복수형)는 둘 다 껍데기이고, 실제 내용은
`StartReadBuffersImpl()`에 있다. 한 블록짜리 요청이 들어오면 `actual_nblocks == 1`일 수밖에 없으므로, 루프가 여러 번 돌지 않고 한 번만에 끝나게 될 것이다.

```c title="bufmgr.c"
for (int i = 0; i < actual_nblocks; ++i)     /* actual_nblocks == 1 */
{
    bool        found;

    buffers[i] = PinBufferForBlock(operation->rel, operation->smgr,
                                   operation->persistence,
                                   operation->forknum,
                                   blockNum + i,
                                   operation->strategy, &found);

    if (found)
    {
        if (i == 0)
        {
            *nblocks = 1;
            …
            return false;          /* ← buffer hit. WaitReadBuffers는 부를 필요 없다 */
        }
        …
    }
    …
}
```

이 흐름만 보았을 때, `PinBufferForBlock()`가 바로 우리가 찾고자 하는 버퍼 프레임을 찾아서 반환해 주거나(`found == true`), 빈 프레임만을 잡아서 반환해주게 됨(`found == false`)을 알 수 있다. 위에서 언급했 듯이, 프레임을 찾았다면 `false`를 원래의 함수로 반환하기 때문에 굳이 I/O를 기다리지 않는다. 

### 1.3 · 동기 I/O에서는 여기서 I/O를 시작하지 않는다

읽어야 하는 경우, 위 루프를 빠져나온 뒤 `io_method`에 따라 갈린다. 비동기 I/O는 여기서 바로 `AsyncReadBuffers()`를 불러서 I/O 요청을 발행한다. 정상적으로 시작되었다면 `did_start_io`는 `true`가 될 것이다. 그러나 동기 I/O의 경우(`else`절 하단), 별도로 I/O를 하는 것은 없고, 낯선 `smgrprefetch()`가 보인다. 이것은 이름만 본다면 데이터를 미리 읽어오라는 요청 같은데, 실제로 데이터베이스 프로그램 입장에서 볼 때는 I/O가 되는 것이 아니라, 운영체제로 하여금 OS 페이지 캐시(page cache)에 해당되는 블록을 미리 읽어와 달라고 힌트를 보내는 것이다. 결과적으로는, 동기 I/O일 때 `ReadBuffer()` 경로로 오게 된다면 `smgrprefetch()` 또한 부르지 않고 바로 `did_start_io = true;`한 뒤 그 값을 반환하게 된다.

```c title="bufmgr.c"
if (io_method != IOMETHOD_SYNC)
{
    did_start_io = AsyncReadBuffers(operation, nblocks);   /* 비동기: 여기서 발행 */
    operation->nblocks = *nblocks;
}
else
{
    operation->flags |= READ_BUFFERS_SYNCHRONOUSLY;

    if (flags & READ_BUFFERS_ISSUE_ADVICE)
        smgrprefetch(operation->smgr, operation->forknum, blockNum, actual_nblocks);

    /*
     * Indicate that WaitReadBuffers() should be called. WaitReadBuffers()
     * will initiate the necessary IO.
     */
    did_start_io = true;        /* ← I/O를 시작하지 않고 "시작했다"고 답한다 */
}
```

#### ⭐️⭐️⭐️ `smgrprefetch()`는 읽기가 아니라 힌트다

`else`절에 남은 `smgrprefetch()`는 데이터를 읽어 오지 않는다. 대신, 운영체제에게 *"이 파일의 이 부분을 곧 읽을 것 같으니 미리 준비해 두라"* 고 **귀띔(advice)** 하는 것뿐이다. 함수를 따라 내려가 보면 정체가 드러난다. `smgrprefetch()` → `mdprefetch()` → `FilePrefetch()` → **`posix_fadvise(POSIX_FADV_WILLNEED)`**과 같은 순서다.

```c title="fd.c"
returnCode = posix_fadvise(VfdCache[file].fd, offset, amount,
                           POSIX_FADV_WILLNEED);
```
`posix_fadvise()`는 파일 데이터에 대한 접근 패턴을 미리 선언하는 [시스템 콜](https://man7.org/linux/man-pages/man2/posix_fadvise.2.html)로서, `POSIX_FADV_WILLNEED`는 지정된 데이터가 곧 접근될 것임을 의미한다. 이 시스템 콜 자체가 I/O를 수행하는 대신에(nonblocking), 운영체제가 알아서 해당 블록을 **OS 페이지 캐시**로 끌어올린다. 이렇게 되면 나중에 실제로 읽기를 위한 시스템 콜을 부르더라도 이미 운영체제 메모리 내에 I/O가 되어 있어서, 디스크로부터 그때 읽어오지 않아도 된다.

!!! info "OS 페이지 캐시 — 버퍼 풀 밑에 있는 또 하나의 캐시"

    PostgreSQL이 `preadv()`로 파일을 읽으면, 그 데이터는 디스크에서 곧장 버퍼 풀로 오지 않고, 운영체제 커널이 관리하는 캐시를 한 번 거친다.

    ```text
        PostgreSQL 버퍼 풀        ← shared_buffers, 우리가 접근할 수 있는 메모리
              ▲
              │  preadv()  = 커널 캐시에서 버퍼 풀로 복사
              │
        OS 페이지 캐시             ← 커널이 남는 RAM으로 알아서 관리 (사용자 접근 불가)
              ▲
              │  디스크 I/O (많은 시간 소요)
              │
           디스크
    ```

    리눅스는 남는 메모리를 놀리지 않고 최근에 읽은 파일 내용을 페이지 캐시(page cache)에
    보관한다. 그래서 `preadv()`는 캐시 히트(운영체제 커널 메모리에서 버퍼 풀로 8 KB를 복사하고 끝)거나 캐시 미스(운영체제 커널이 디스크에 요청하고, 그동안 사용자 프로세스는 기다려야 한다)가 된다.

    `posix_fadvise(WILLNEED)`는 이 미스를 미리 처리해 두려는 시도로서, "당장 읽어 달라"가 아니라
    "나중에 읽을 테니 그때까지 캐시에 올려 두라"이므로 호출한 프로세스는 잠들지 않고 계속 진행할 수
    있다. 

#### 왜 동기 I/O만 따로 길을 냈나

사실 비동기 경로로 보내도 큰 차이는 없겠지만, 가능한 한 예상치 못한 문제를 예방하고자 하는 차원에서 확실하게 나누어 기존의 동기적인 I/O 스타일로 처리하도록 한 것이라고 주석에서 설명하고 있다.

```c title="bufmgr.c"
    /*
     * The reason we have a dedicated path for IOMETHOD_SYNC here is to
     * de-risk the introduction of AIO somewhat. It's a large architectural
     * change, with lots of chances for unanticipated performance effects.
     *
     * Use of IOMETHOD_SYNC already leads to not actually performing IO
     * asynchronously, but without the check here we'd execute IO earlier than
     * we used to. Eventually this IOMETHOD_SYNC specific path should go away.
     */
```

### 1.4 · `WaitReadBuffers()` — I/O를 위한 함수를 호출한다

이제 `ReadBuffer_common()`의 `WaitReadBuffers(&operation)`이 불린다. 그 안에서는 다음과 같은 `while()` 루프가 존재한다. 동기 I/O의 경우에는 `io_wref`가 비어 있으므로 첫 번째 `if`를 건너뛰게 되고, `nblocks_done`이 0이라 `break`도 하지 않는다. 그래서 곧장 **`AsyncReadBuffers()`** 로 간다. 그 안에서 실제 읽기가 일어나고,
다음 회차에 `nblocks_done == nblocks`가 되어 루프를 빠져나온다.

```c title="bufmgr.c"
while (true)
{
    if (pgaio_wref_valid(&operation->io_wref))
    {
        …발행된 I/O가 있으면 완료를 기다린다…
        ProcessReadBuffersResult(operation);      /* nblocks_done 전진 */
    }

    if (operation->nblocks_done == operation->nblocks)
        break;                                    /* 다 읽었다 */

    CHECK_FOR_INTERRUPTS();

    AsyncReadBuffers(operation, &ignored_nblocks_progress);   /* ← I/O 발행 */
}
```

주목할 점은 동기 I/O만을 위한 코드가 한 줄도 없다는 것이다(애초에 함수 이름부터가 Async다). 이 루프는 원래 읽기가 미완료되어서 부분적으로만 읽힌 경우(partial read) 재시도하도록 만들어진 것인데, 그냥 자연스럽게 동기 I/O도 여기서 처리할 수 있도록 구현해 두었다.

```c title="bufmgr.c:1652"
    /*
     * In the case of IOMETHOD_SYNC, we start - as we used to before the
     * introducing of AIO - the IO in WaitReadBuffers(). This is done as part
     * of the retry logic below, no extra code is required.
     *
     * This path is expected to eventually go away.
     */
```

### 1.5 · `AsyncReadBuffers()` — 실제 I/O가 수행된다

이 부분은 **여러 개**의 블록을 **비동기적**으로 처리할 수 있도록 구성되어 있기 때문에 상당히 복잡해 보인다. 그렇기 때문에 아주 중요한 부분들에 대해서만 언급하고 넘어간다. 대략 다음의 3단계로 구성되어 있다고 보면 된다.

**① 읽을 권리를 얻는다.**

`ReadBuffersCanStartIO()`는 결국 `StartBufferIO()`를 부르는데, 이 함수는 버퍼 프레임에 `BM_IO_IN_PROGRESS`라는 플래그 값을 표기한다. 이는 "내가 이 블록을 읽는 중"이라는 배타적 표식이다. 반대로, 만약 이 값이 이미 적혀 있었다면 다른 백엔드가 읽었다는 것이다.

```c title="bufmgr.c:1855"
if (!ReadBuffersCanStartIO(buffers[nblocks_done], false))
{
    /* Someone else has already completed this block, we're done. */
    operation->nblocks_done += 1;
    *nblocks_progress = 1;
    pgaio_io_release(ioh);
    …
    pgBufferUsage.shared_blks_hit += 1;      /* ← 히트로 계산한다 */
}
```

**② I/O 목적지가 될 버퍼들을 모은다.**

우리가 위에서 미리 `PinBufferForBlock()`을 통해 준비해 두었던 버퍼들을 여기서 I/O의 목적지로 모아서 준비한다. 물론, 우리가 관심을 갖고 있는 단일 블록의 경우에는 루프가 돌아가지 않는다. 여러 개의 블록을 읽을 때에는 이것이 필요하다.

```c title="bufmgr.c"
io_pages[0] = BufferGetBlock(buffers[nblocks_done]);
io_buffers_len = 1;

for (int i = nblocks_done + 1; i < operation->nblocks; i++)
{
    if (!ReadBuffersCanStartIO(buffers[i], true))    /* nowait = true */
        break;
    io_pages[io_buffers_len++] = BufferGetBlock(buffers[i]);
}
```

**③ I/O 요청을 발행한다.**

몇 가지 준비 과정을 거쳐서, `smgrstartreadv()`를 최종적으로 호출한다. 동기 I/O라면 여기서  `preadv()`가 실행되고 디스크를 읽고 돌아온다. 원래는 I/O가 완료된 후에 약간의 후처리(버퍼를 유효(`BM_VALID`)로 표시하고 `BM_IO_IN_PROGRESS`를 내려야 함)가 필요한데, 이는 사전에 등록해 놓았던 콜백 함수를 통해 수행하게 된다.

```c title="bufmgr.c"
pgaio_io_get_wref(ioh, &operation->io_wref);              
pgaio_io_set_handle_data_32(ioh, (uint32 *) io_buffers, io_buffers_len);
pgaio_io_register_callbacks(ioh, PGAIO_HCB_SHARED_BUFFER_READV, flags);
pgaio_io_set_flag(ioh, ioh_flags);

smgrstartreadv(ioh, operation->smgr, forknum, blocknum,
               io_pages, io_buffers_len);                 /* 실제 I/O */
```

### 1.6 · I/O 수행 관련 경로를 정리

```text
  ReadBuffer(rel, 42)
    └ ReadBufferExtended → ReadBuffer_common
         │
         ├─ StartReadBuffer(&op, &buf, 42, READ_BUFFERS_SYNCHRONOUSLY)
         │    └ StartReadBuffersImpl()
         │         └ PinBufferForBlock()  ──▶ BufferAlloc()   
         │              │
         │              ├ 히트  ─────────────▶ return false   (여기서 끝)
         │              └ 미스  ─────────────▶ return true    (I/O는 아직 안 함)
         │
         └─ WaitReadBuffers(&op)          ← true를 받은 경우에만
              └ AsyncReadBuffers()
                   ├ StartBufferIO()        BM_IO_IN_PROGRESS 획득
                   ├ I/O 종착지 블록 모으기      io_pages[]
                   └ smgrstartreadv()  ──▶  preadv()    실제 디스크 읽기
                        └ 콜백: BM_VALID 설정, TerminateBufferIO()
```

---

## 2 · ⭐️⭐️⭐️ 왜 `Start`와 `Wait`로 나뉘어 있나

!!! warning "비동기 I/O"
    이 절은 위와 같이 **단일 블록**의 **동기 I/O**에는 과도하게 복잡해 보이는 읽기 경로가 만들어진 배경을 설명한다. 이해가 꼭 필요한 부분은 아니므로 3절로 넘어가도 무방하다.

### 2.1 · 벡터화된 읽기

디스크에서 연속된 블록 16개를 읽어야 한다고 하자. 개별 블록을 읽는 방식이라면 `pread()`를 16번 불러야 한다. 그러나 이 블록들이 연속되어 있다면 **`preadv()` 한 번**으로 끝낼 수 있다. 이를 위해서는 다음과 같은 과정을 거쳐야 한다.

1. 16개의 버퍼 프레임을 **모두 먼저** 확보한다 → 그래야 I/O 목적지 배열을 만들 수 있다
2. **그 다음에** 한 번의 I/O를 발행한다

이렇게 버퍼 메모리 확보 단계와 I/O 단계가 분리될 수밖에 없다.

### 2.2 · 비동기 I/O

비동기 I/O(`io_method`가 `worker`나 `io_uring`)에서는 `StartReadBuffers()`가
**그 자리에서 `AsyncReadBuffers()`를 불러 I/O를 발행하고 돌아온다.** 호출자는 그동안 다른 일을 할 수
있고, 정말 데이터가 필요해질 때 `WaitReadBuffers()`를 부른다.

| `io_method` | I/O를 발행하는 곳 | `WaitReadBuffers()`가 하는 일 |
| --- | --- | --- |
| `sync` | **`WaitReadBuffers()` 안** | I/O 수행 + 완료 처리 |
| `worker` | `StartReadBuffers()` 안 | I/O 워커가 끝나기를 기다림 |
| `io_uring` | `StartReadBuffers()` 안 | 완료 큐를 확인/대기 |

### 2.3 · 읽기 스트림의 사용 패턴

그런데 위에서 보았듯 `ReadBuffer()`는 `Start` 직후에 `Wait`를 부른다. 겹침이 생길 틈이 없다.
읽기 스트림이라는 별도의 구조가 필요한 이유는 이를 제대로 활용하기 위함이다. 예를 들면 아래와 같다. 스트림은 데이터 소비자가 첫 블록을 처리하는 동안 뒤쪽 블록의 I/O가 이미 진행되도록 만든다. 

```text
  ReadBuffer()                    read_stream
  ────────────                    ──────────────────────────────────
  Start(블록 42)                  Start(블록 0~15)   ─┐
  Wait()          ← 즉시           Start(블록 16~31)  │ 미리 여러 개 발행
  사용                            Start(블록 32~47)  ─┘
                                  Wait()  → 0~15 사용    ← 그동안 뒤쪽 I/O 진행
                                  Wait()  → 16~31 사용
                                  Start(블록 48~63)      ← 빈 자리를 다시 채움
                                  Wait()  → 32~47 사용
                                  …
```

### 2.4 · 배치는 잘릴 수 있다

단, 여러 블록을 한 번에 다루게 되면서 생긴 복잡함이 하나 있다. **요청한 만큼 처리된다는 보장이 없다는 것이다.**

```c title="bufmgr.c"
    /*
     * Otherwise we already have an I/O to perform, but this block can't be
     * included as it is already valid.  Split the I/O here. …  We'll leave
     * this buffer pinned, forwarding it to the next call, avoiding the need
     * to unpin it here and re-pin it in the next call.
     */
    actual_nblocks = i;
    break;
```

예를 들어서 16개의 블록을 요청했는데 5번째가 이미 버퍼 풀에 유효하게 있다면, 이번 연산은 **0~4번 블록만** 처리할 수 있다. 그 이유는 `preadv()` 하나가 **연속된 구간**만 읽을 수 있어 중간에 구멍을 낼 수 없기 때문이다. 5번 블록의 프레임에는 이미 유효한 내용이 들어 있기 때문에 이를 뛰어넘고 `preadv()`를 할 수 없다. 다만 5번 프레임을 다음 호출로 넘겨서 버퍼를 전달(forward)한다.

---

## 3 · 공유 버퍼 풀로부터 버퍼 할당 받기

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

이제 본론이다. 이 함수는 중요하기 때문에 여기에 코드 대부분을 포함하여 설명한다.

#### 준비 · 태그 → 해시코드 → 파티션 락

`BufferAlloc()`이 답해야 하는 첫 번째 질문은 **"이 블록이 지금 버퍼 풀에 있는가"** 다. `NBuffers`개의
기술자를 전부 훑을 수는 없으니, 이를 위한 **버퍼 테이블(buffer table)** 이라는 공유 해시 테이블이
따로 있다. 구조는 놀랄 만큼 단순하다.

```c title="buf_table.c:27"
/* entry for buffer lookup hashtable */
typedef struct
{
    BufferTag   key;            /* Tag of a disk page */
    int         id;             /* Associated buffer ID */
} BufferLookupEnt;
```

키는 [§03](03-buffermanagershmeminit.md)에서 본 `BufferTag`이고, 값은 `buf_id` 하나다. **"어느 페이지가
몇 번 프레임에 있는가"** 그 이상도 이하도 아니다.

조회를 하려면 세 단계를 거친다.

```c
    /* create a tag so we can lookup the buffer */
    InitBufferTag(&newTag, &smgr->smgr_rlocator.locator, forkNum, blockNum);

    /* determine its hash code and partition lock ID */
    newHash = BufTableHashCode(&newTag);
    newPartitionLock = BufMappingPartitionLock(newHash);
```

1. **태그를 만든다** — `RelFileLocator` + 포크 번호 + 블록 번호. 이것이 데이터베이스 전체에서 페이지
   하나를 유일하게 지목한다.
2. **해시코드를 구한다** — 태그를 32비트 정수 하나로 뭉갠다.
3. **그 해시코드로 락을 고른다** — 아래에서 설명한다.

해시코드를 미리 계산해 변수에 담아 두는 데는 이유가 있다. 락을 고르는 데 필요하고, 나중에 조회할 때
`hash_search_with_hash_value()`에 그대로 넘겨 **다시 계산하지 않기** 위해서다.

##### 파티션 락 — 락 하나로는 버틸 수 없다

공유 해시 테이블 하나를 수백 개의 백엔드가 두드리는데 락이 하나뿐이라면, 버퍼 풀 조회 전체가 그 락
하나에 줄을 서게 된다. 그래서 PostgreSQL은 **해시 공간을 128조각으로 나누고 락도 128개**를 둔다.

```c title="lwlock.h:93"
#define NUM_BUFFER_PARTITIONS  128
```

```c title="buf_internals.h:193"
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

해시코드의 나머지 연산으로 파티션을 고르고, 미리 만들어 둔 락 배열에서 해당 락을 집는다. 서로 다른
페이지를 찾는 두 백엔드는 **거의 항상 다른 파티션에 떨어지므로 서로를 막지 않는다.**

!!! danger "함정 — 락은 프레임이 아니라 '태그'에 딸려 있다"
    가장 헷갈리기 쉬운 지점이다. **`buf_id` 3번 프레임을 보호하는 락 같은 것은 없다.** 락을 정하는
    것은 프레임 번호가 아니라 **태그**다.

    그래서 같은 프레임이라도 담고 있는 페이지가 바뀌면 **그것을 보호하는 파티션 락도 바뀐다.**
    프레임 하나의 내용을 A 페이지에서 B 페이지로 교체하려면 원칙적으로 **두 개의 락**이 필요하다는
    뜻이고, 15.2가 실제로 그렇게 했다. 18이 그 구조를 어떻게 피했는지는 Phase 5 뒤의 비교 표에서 다시 본다.

##### 버퍼 테이블 API 세 개

`buf_table.c`가 제공하는 것은 세 함수뿐이고, 파일 머리 주석이 규칙을 못 박는다 —
*"the routines in this file do no locking of their own. The caller must hold a suitable lock."*
**락을 잡는 것은 `BufferAlloc()`의 책임이다.**

| 함수 | 계약 | 필요한 락 |
| --- | --- | --- |
| `BufTableLookup(tag, hash)` | 있으면 `buf_id`, 없으면 `-1` | SHARED 이상 |
| `BufTableInsert(tag, hash, buf_id)` | 넣었으면 `-1`, **이미 있으면 넣지 않고 그 `buf_id`** | EXCLUSIVE |
| `BufTableDelete(tag, hash)` | 지운다 (없으면 에러) | EXCLUSIVE |

`BufTableInsert()`의 저 계약 — **"이미 있으면 넣지 않고 그 id를 돌려준다"** — 이 Phase 4의 전부다.
조회와 삽입이 한 번의 원자적 연산으로 합쳐져 있어서, 락을 두 번 잡지 않아도 된다.

??? info "해시 테이블은 왜 `NBuffers`보다 크게 잡을까"
    ```c title="freelist.c:488"
    /*
     * Since we can't tolerate running out of lookup table entries, we must be
     * sure to specify an adequate table size here.  The maximum steady-state
     * usage is of course NBuffers entries, but BufferAlloc() tries to insert
     * a new entry before deleting the old.  In principle this could be
     * happening in each partition concurrently, so we could need as many as
     * NBuffers + NUM_BUFFER_PARTITIONS entries.
     */
    InitBufTable(NBuffers + NUM_BUFFER_PARTITIONS);
    ```

    정상 상태의 항목 수는 `NBuffers`개지만, 태그를 교체하는 **과도기에는 옛 항목과 새 항목이 잠깐
    함께 존재**할 수 있다. 그런 백엔드가 파티션마다 하나씩 있을 수 있으므로 128개를 더 잡아 둔다.

    다만 이 주석이 묘사하는 "insert a new entry before deleting the old"는 **15.2 시절의 동작**이다.
    18에서는 옛 항목을 `GetVictimBuffer()` 안의 `InvalidateVictimBuffer()`가 **먼저** 지우므로 이런
    과도기가 없다. 여유분이 남아 있는 것은 안전한 쪽으로 기울인 결과다.

#### Phase 1 · 조회 — 버퍼 테이블에 있는가

```c title="bufmgr.c:2013"
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

**맨 앞의 두 줄이 중요하다.** 핀을 기록할 자리를 *미리* 확보한다. 나중에 자리가 없어서 `palloc`이
실패하면, 이미 핀을 잡아 놓은 뒤라 되돌릴 수 없기 때문이다. **실패할 수 있는 일을 먼저 해 두는**
패턴이고, 임계 구역을 다루는 코드에서 자주 보게 된다.

락은 `LW_SHARED`로 잡는다. 조회만 할 것이므로 여러 백엔드가 동시에 같은 파티션을 읽어도 된다.
그리고 결과는 둘 중 하나다 — `existing_buf_id >= 0`(히트)이거나 `-1`(미스)이다.

#### Phase 2 · 히트 — 이미 있는 경우

```c title="bufmgr.c:2027"
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

**핀을 잡자마자 파티션 락을 놓는다.** 이 순서가 핵심이다. 핀이 걸린 순간부터는 아무도 이 프레임을
희생양으로 가져갈 수 없으므로([§10](10-pin-lock-spinlock.md)), 해시 테이블을 계속 붙잡고 있을 이유가
없다. 파티션 락은 128개뿐이라 한 백엔드가 오래 쥐고 있으면 같은 파티션에 속하는 모든 조회가 멈춘다.

**핀이 락을 대신한다**는 것이 이 함수 전체를 관통하는 아이디어이고, Phase 3에서 한 번 더 나온다.

##### `*foundPtr`은 "히트"가 아니다

여기서 반드시 짚어야 할 것이 있다. **버퍼 테이블에서 찾았는데도 `*foundPtr = false`가 될 수 있다.**
`PinBuffer()`의 반환값 `valid`는 "핀을 잡았는가"가 아니라 **"`BM_VALID`가 켜져 있는가"** 이고,
태그는 등록되어 있지만 아직 유효하지 않은 프레임이 실제로 존재하기 때문이다. 주석의 세 경우다.

| | 상황 |
| --- | --- |
| (a) | 다른 백엔드가 **지금 읽는 중**이다 |
| (b) | 이전 읽기 시도가 **실패**했다 |
| (c) | 누군가 `StartReadBuffers()`만 부르고 아직 `WaitReadBuffers()`를 부르지 않았다 |

그러니 `*foundPtr`의 정확한 의미는 히트/미스가 아니라 **"이 버퍼를 바로 써도 되는가 / 읽기가
필요한가"** 다. §1.2의 `found` 변수가 그대로 이 값이었다는 점을 떠올려 보자.
(c)의 경우 — 남이 읽기를 시작만 해 둔 프레임 — 도 자연스럽게 "읽어야 함"으로 흘러가고, 실제 중복은
`AsyncReadBuffers()`의 `StartBufferIO()` 실패 분기에서 걸러진다. 각 층이 자기 몫만 판단하고
넘기는 구조다.

#### Phase 3 · 미스 — 희생 버퍼를 얻어온다

```c title="bufmgr.c:2062"
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

**왜 락을 놓는가?** `GetVictimBuffer()`가 하는 일이 무겁기 때문이다. 클럭 스윕이 풀 전체를 돌 수도
있고(§09), 고른 희생자가 더러우면 `FlushBuffer()` → **`XLogFlush()` + `smgrwrite()`** 까지 한다.
파티션 락을 쥔 채로 WAL 플러시와 디스크 쓰기를 하면, 그 파티션에 속한 모든 조회가 멈춘다.
전체 태그의 1/128이다.

대가는 주석에 적힌 그대로다 — *"Somebody else might try to do the same."* 락을 놓았으니 다른
백엔드가 같은 블록에 대해 똑같은 일을 할 수 있다.

#### Phase 4 · 삽입 실패 — 남이 먼저 넣었다

```c title="bufmgr.c:2078"
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

`BufTableInsert()`의 계약이 이 코드를 가능하게 한다 — **"없으면 넣고 −1을 반환, 이미 있으면 넣지
않고 그 `buf_id`를 반환"**. 조회와 삽입이 한 번의 원자적 연산으로 합쳐져 있어서, 락을 두 번 잡지
않아도 된다.

왜 이런 일이 생기는가? **Phase 3에서 파티션 락을 놓았기 때문이다.** 그 사이에 다른 백엔드가 같은
블록을 요청했고, 나보다 먼저 희생 버퍼를 구해 삽입까지 끝냈다. 락을 놓아서 얻은 확장성의 대가를
여기서 치르는 셈이다.

처리는 세 단계다.

1. **`UnpinBuffer()`** — 내가 애써 구한 희생 버퍼를 포기한다.
2. **`StrategyFreeBuffer()`** — freelist에 돌려준다.
3. **Phase 2의 코드를 그대로 반복한다** — 남이 넣어 둔 프레임을 핀 잡고 돌아간다.

주석의 *"remaining code should match code at top of routine"* 이 3번의 중복을 **의도적인 것**으로
표시한다. 함수로 빼지 않고 복사해 둔 것이다.

!!! info "왜 `StrategyFreeBuffer()`까지 부르나"
    희생 버퍼는 이미 **비워진 상태**로 왔다 — 태그도 없고, 더럽지도 않고, 유효하지도 않다. 그냥
    핀만 풀고 두면 클럭 스윕이 다시 만날 때까지 놀게 된다. freelist에 넣어 두면 다음 요청이 즉시
    가져다 쓴다. 애써 비운 프레임을 낭비하지 않겠다는 것이다.

#### Phase 5 · 삽입 성공 — 이름을 붙이고 마무리

```c title="bufmgr.c:2129"
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

두 개의 `Assert`가 `GetVictimBuffer()`의 계약을 명시한다.

* `refcount == 1` — 나만 핀을 잡고 있다.
* 네 플래그가 **모두 꺼져 있다** — 태그도 없고, 유효하지도 않고, 더럽지도 않고, I/O 중도 아니다.

즉 **희생 버퍼는 완전히 비워진 채로 도착한다.** 15.2에서는 `BufferAlloc()`이 옛 태그를 직접
지웠지만, 18에서는 그 일이 `GetVictimBuffer()` 안의 `InvalidateVictimBuffer()`로 옮겨갔다.
`BufferAlloc()`은 "빈 프레임에 이름을 붙이는 일"만 한다.

새 버퍼의 `usage_count`가 **1**로 시작한다는 점도 여기서 확인된다(`BUF_USAGECOUNT_ONE`).
방금 들어온 페이지에 한 번의 유예를 주는 것이다.

!!! danger "함정 — 파티션 락이 태그 대입까지 이어지는 이유"
    `BufTableInsert()`가 끝난 시점에 해시 테이블은 이미 "이 태그 → 이 `buf_id`"를 가리킨다. 그러나
    **프레임의 `tag` 필드는 아직 비어 있다.** 이 어긋난 창을 다른 백엔드가 들여다보면 안 된다.

    그래서 파티션 락을 삽입부터 태그 대입까지 **놓지 않는다.** 조회하려면 같은 파티션 락을
    `LW_SHARED`로 잡아야 하는데, 우리가 `LW_EXCLUSIVE`로 쥐고 있으므로 아무도 들어오지 못한다.
    `UnlockBufHdr()` → `LWLockRelease()`의 **순서**도 그래서 중요하다. 반대로 하면 창이 열린다.

#### 15.2와 무엇이 달라졌나

| | 15.2 | 18.4 |
| --- | --- | --- |
| 희생자 비우기 | `BufferAlloc()`이 직접 | `GetVictimBuffer()`가 미리 |
| 락 | **옛 파티션 + 새 파티션 동시에** | **새 파티션만** |
| 태그 교체 | 하나의 "rename" 연산 | 무효화 → 삽입, 두 단계 |
| 경쟁 처리 | rename 안에서 | `BufTableInsert()` 충돌 분기 |

15.2가 두 파티션 락을 동시에 잡아야 했던 이유는 **옛 태그를 지우는 일과 새 태그를 넣는 일이
한 함수 안에 있었기** 때문이다. 두 태그는 서로 다른 파티션에 속할 수 있으므로 락 순서 문제까지
생긴다. 18은 그것을 분리하고, 그 사이 프레임이 **이름 없는 상태**로 있는 동안은 **핀**이 지켜
준다. 락 대신 핀으로 보호 구간을 만든 것이다. (자세한 것은 §09.)

!!! info "낡은 주석 하나"
    `BufferAlloc()`의 머리 주석에는 *"io_context is passed as an output parameter…"* 라고 적혀
    있지만, 실제 시그니처는 `IOContext io_context`로 **값 입력**이다. 코드가 바뀌면서 주석이 따라오지
    못한 흔적이다. PostgreSQL 소스라고 해서 주석을 무조건 믿을 일은 아니다.

## 5 · 전체 흐름 다시 보기

§1.6의 그림에 `BufferAlloc()`의 내부(§4)를 채워 넣은 것이다.

```text
ReadBuffer(rel, 42)
  └ ReadBufferExtended → ReadBuffer_common
       ├ blockNum == P_NEW ?  ── yes ─▶ ExtendBufferedRel()      [§06 §14]
       ├ RBM_ZERO_* ?         ── yes ─▶ PinBufferForBlock + ZeroAndLockBuffer
       └ StartReadBuffer(&op, &buf, 42, READ_BUFFERS_SYNCHRONOUSLY)
            └ StartReadBuffersImpl(nblocks=1, allow_forwarding=false)
                 ├ PinBufferForBlock(42, &found)
                 │    └ BufferAlloc()
                 │       P1 BufTableLookup      (SHARED 락)
                 │       P2  └ 히트 ─▶ PinBuffer → 락 해제 → return
                 │       P3 미스 ─▶ 락 해제 → GetVictimBuffer()      [§09]
                 │       P4 BufTableInsert (EXCLUSIVE 락)
                 │           └ 충돌 ─▶ Unpin + StrategyFreeBuffer → 히트 처리
                 │       P5 LockBufHdr → tag 대입 → BM_TAG_VALID → 락 해제
                 │
                 ├ found == true  ─▶ return false   (WaitReadBuffers 불필요)
                 └ io_method == sync ─▶ return true (I/O는 아직 시작 안 함)

  └ WaitReadBuffers(&op)            ← StartReadBuffer가 true를 준 경우만
       while (nblocks_done < nblocks)
            └ AsyncReadBuffers()
                 ├ ReadBuffersCanStartIO → StartBufferIO (BM_IO_IN_PROGRESS)
                 ├ 이웃 블록 모으기 → io_pages[]
                 └ smgrstartreadv()  ── sync면 여기서 preadv()  ★
                      └ 콜백: BM_VALID 설정, TerminateBufferIO
```

## 6 · 정리

* **`ReadBuffer()`는 `Start` 직후에 곧바로 `Wait`를 부른다.** 그래서 결과적으로 15.2와 똑같은
  동기 읽기다. `READ_BUFFERS_SYNCHRONOUSLY` 플래그가 그 사실을 코드로 선언한다.
* **`io_method = sync`에서 실제 I/O는 `WaitReadBuffers()` 안에서** 일어난다. 부분 읽기 재시도
  루프에 얹혀 가므로 sync 전용 코드가 따로 없다.
* **`Start`/`Wait`로 나뉜 이유는 벡터화와 비동기 I/O 때문**이고, 그 구조를 제대로 쓰는 것은 읽기
  스트림뿐이다. 그 대가로 **배치가 잘릴 수 있다**(`actual_nblocks = i; break;` 와 전달된 버퍼).
* **`BufferAlloc()`의 구조는 "파티션 락 없이 희생자를 얻는다"는 결정이 전부 지배한다.** 조회 →
  (락 놓고) 희생자 → 삽입 → 충돌 처리 → 태그 대입.
* **`*foundPtr`은 "읽기가 필요한가"이지 "히트인가"가 아니다.**

!!! lens "PA2a 렌즈"
    PA2a가 손대는 곳은 **Phase 3와 Phase 5** 다. 희생자를 어디서 가져오는가, 그리고 프레임에 이름을
    붙이는 규칙이 무엇인가. Phase 1·2(조회·히트)와 Phase 4(충돌)는 그대로 두어야 한다 — 버퍼 테이블의 의미가
    바뀌면 §06의 모든 호출자가 깨진다.

    **배치 잘림**을 반드시 기억해 두자. 여러 풀을 다루게 되면 "한 배치가 두 풀에 걸치는" 상황을
    막아야 하는데, PostgreSQL이 이미 `actual_nblocks = i; break;` 라는 도구를 갖고 있다.
    새 메커니즘을 만들 필요 없이 그것을 쓰면 된다.

!!! question "생각해 볼 거리"

    * Phase 4의 충돌은 얼마나 자주 일어날까? 어떤 워크로드에서 잦아지고, 그때 낭비되는 것은
      무엇인가?
    * `BufferAlloc()`이 파티션 락을 **쥔 채로** `GetVictimBuffer()`를 부르도록 바꾸면 Phase 4가
      통째로 사라진다. 코드는 훨씬 짧아진다. 그런데도 그렇게 하지 않는 이유는?
    * `StartBufferIO()`가 `BM_IO_IN_PROGRESS`를 세우는 데 실패했을 때, 왜 그것을 "히트"로
      집계할까? 이 집계가 거짓말이 되는 경우는 없을까?

## 앞으로 볼 것

| | 다룰 내용 |
| --- | --- |
| [§08](08-tag-table-lock.md) | 버퍼 테이블과 파티션 락 — `BufTableLookup`/`BufTableInsert`의 속 |
| [§09](09-clock-sweep.md) | `GetVictimBuffer()`와 클럭 스윕 — Phase 3의 내부 |
| [§10](10-pin-lock-spinlock.md) | 핀, 내용 락, 헤더 스핀락 — 이 모든 것을 안전하게 만드는 층 |
| [§19](19-read-stream.md) | 읽기 스트림 — `StartReadBuffers()`의 진짜 사용자 |
