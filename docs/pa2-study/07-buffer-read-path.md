# 07 · 버퍼 풀의 읽기 요청 처리 과정

!!! abstract 목표
    읽기 요청이 버퍼 매니저 내에서 어떤 단계를 거쳐 내려가는지 이해한다.

[지난 문서](./06-buffer-from-user-perspective.md)에서 `ReadBuffer()` 계열과 읽기 스트림이 모두 `StartReadBuffer(s)()`와
`WaitReadBuffers()`를 거친다는 것을 보았다. 이 두 함수는 사실 비동기 I/O를 위해
만들어진 구조이나, 우리의 주 관심사는 동기 I/O이기 때문에 이에 집중한다.

그래서 이 장에서는 먼저 동기 I/O 모드(`io_method = sync`)에서 한 개의 블록을 읽어들이기 위한 `ReadBuffer()` 한 번이 어떻게 처리되는지를 끝까지 따라간다. 그러고 나서
비동기 I/O와 읽기 스트림을 바탕으로 이런 식의 설계가 이루어진 이유를 살펴본다.
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
    이 절은 위와 같이 **단일 블록**의 **동기 I/O**에는 과도하게 복잡해 보이는 읽기 경로가 만들어진 배경을 설명한다. 이해가 꼭 필요한 부분은 아니므로 뒤로 넘어가도 무방하다.

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

## 3 · 정리

* **`ReadBuffer()`는 `Start` 직후에 곧바로 `Wait`를 부른다.** 그래서 결과적으로 15.2와 똑같은 동기
  읽기다. `READ_BUFFERS_SYNCHRONOUSLY` 플래그가 그 사실을 코드로 선언한다.
* **`io_method = sync`에서 실제 I/O는 `WaitReadBuffers()` 안에서** 일어난다. 부분 읽기 재시도 루프에
  얹혀 가므로 sync 전용 코드가 따로 없다.
* **`smgrprefetch()`는 읽기가 아니라 커널에 대한 귀띔**(`posix_fadvise`)이고, `ReadBuffer()`
  경로에서는 아예 실행되지 않는다.
* **`Start`/`Wait`로 나뉜 이유는 벡터화와 비동기 I/O 때문**이고, 그 구조를 제대로 쓰는 것은 읽기
  스트림뿐이다. 그 대가로 **배치가 잘릴 수 있다**(`actual_nblocks = i; break;` 와 전달된 버퍼).

그런데 이 장에서 정작 **버퍼를 찾고 확보하는 일**은 통째로 건너뛰었다. `PinBufferForBlock()`이
프레임을 돌려주었고, 우리는 그것이 히트인지 미스인지만 보았을 뿐이다. 그 안에서 무슨 일이
일어나는지가 다음 장의 내용이다.

!!! question "생각해 볼 거리"

    * `io_method = sync`인데도 `AsyncReadBuffers()`라는 이름의 함수가 I/O를 수행한다. 이름을 고치지 않고 둔 이유는 무엇일까?
    * 읽기 스트림이 블록 0~15를 요청했는데 5번이 이미 유효했다. 잘라서 0~4만 읽고, 5번은 핀을 잡은 채 넘긴다. 여기서 핀을 풀어버리지 않는 이유가 뭘까?

## 앞으로 볼 것

| | 다룰 내용 |
| --- | --- |
| [§08](08-buffer-alloc.md) | `BufferAlloc()`과 `GetVictimBuffer()` — 프레임을 찾고 비워 오는 층 |
| §11(준비 중) | 핀, 내용 락, 헤더 스핀락 |
| [§20](20-read-stream.md) | 읽기 스트림 — `StartReadBuffers()`의 진짜 사용자 |
