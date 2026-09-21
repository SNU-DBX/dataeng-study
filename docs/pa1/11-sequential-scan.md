# 11 · 순차 스캔

!!! abstract "목표"
    PostgreSQL의 순차 스캔이 테이블을 훑을 때, 부를 때마다 튜플 하나만 돌려주고 다음에 부르면 그
    자리부터 잇는 방식을 살펴봅니다. 이 장이 끝나면 스캔이 호출 사이에 무엇을 기억해 두는지, 한 번의
    호출이 페이지와 라인 포인터를 어떻게 훑는지 말할 수 있어야 합니다.

`select * from lab`처럼 테이블을 처음부터 끝까지 읽는 것을 순차 스캔이라고 합니다. 순차 스캔은 결과를
한꺼번에 만들지 않습니다. 튜플이 하나 더 필요할 때마다 불려서 하나씩 돌려줍니다.

PostgreSQL에는 이 일을 하는 함수가 두 벌 있습니다. 보통의 `SELECT`는 페이지를 읽자마자 튜플이 있는
라인 포인터 번호를 먼저 모아 두는 `heapgettup_pagemode()`를 씁니다. 이 장에서는 라인 포인터를 하나씩
보며 돌려주는 `heapgettup()`을 따라갑니다. 이어서 돌려주는 방식은 둘이 같고, `heapgettup()` 쪽이 한
단계씩 보기 쉽습니다.

---

## 1 · 호출 사이에 기억하는 것

다음 호출에서 이어 읽으려면 어디까지 읽었는지가 남아 있어야 합니다. 스캔은 이 값들을 들고 있습니다.

| 필드 | 뜻 |
|---|---|
| `rs_inited` | 스캔을 시작했는지 |
| `rs_cblock` | 지금 보고 있는 블록 번호 |
| `rs_coffset` | 마지막으로 돌려준 튜플의 라인 포인터 번호 |
| `rs_startblock`, `rs_nblocks` | 훑을 범위. 시작 블록과 테이블의 블록 수 |

페이지에 라인 포인터가 몇 개 남았는지는 기억하지 않습니다. 필요할 때마다 05장 2절의
`PageGetMaxOffsetNumber()`로 다시 셉니다.

---

## 2 · 새 페이지를 시작할 때와 이어 갈 때

한 번의 호출은 둘 중 한 곳에서 시작합니다. 처음이거나 앞 페이지를 다 썼으면 새 페이지의 1번 라인
포인터부터 봅니다.

```c title="src/backend/access/heap/heapam.c:739"
heapgettup_start_page(HeapScanDesc scan, ScanDirection dir, int *linesleft,
                      OffsetNumber *lineoff)
{
    …
    *linesleft = PageGetMaxOffsetNumber(page) - FirstOffsetNumber + 1;  /* ← 라인 포인터 개수 */

    if (ScanDirectionIsForward(dir))
        *lineoff = FirstOffsetNumber;                                   /* ← 1번부터 */
    …
```

이미 튜플을 돌려준 페이지라면 지난번에 돌려준 번호의 다음부터 봅니다.

```c title="src/backend/access/heap/heapam.c:770"
heapgettup_continue_page(HeapScanDesc scan, ScanDirection dir, int *linesleft,
                         OffsetNumber *lineoff)
{
    …
    if (ScanDirectionIsForward(dir))
    {
        *lineoff = OffsetNumberNext(scan->rs_coffset);               /* ← 지난번 번호 + 1 */
        *linesleft = PageGetMaxOffsetNumber(page) - (*lineoff) + 1;  /* ← 남은 개수 */
    }
    …
```

`dir`은 스캔 방향이고 앞으로 읽을 때 1입니다. 두 함수 모두 `lineoff`(다음에 볼 라인 포인터 번호)와
`linesleft`(이 페이지에서 남은 개수)를 채워 줍니다.

---

## 3 · 한 번의 호출

`heapgettup()`에서 필요한 부분만 추리면 이렇습니다.

```c title="src/backend/access/heap/heapam.c:900"
heapgettup(HeapScanDesc scan,
           ScanDirection dir,
           int nkeys,
           ScanKey key)
{
    …
    if (likely(scan->rs_inited))                                           /* ← 이미 시작한 스캔이면(두 번째 호출부터) */
    {
        /* continue from previously returned page/tuple */
        …
        page = heapgettup_continue_page(scan, dir, &linesleft, &lineoff);  /* ← 어디부터 이어 갈지 구한다: rs_coffset + 1번과 남은 개수 */
        goto continue_page;                                                /* ← 블록을 새로 가져오지 않고 아래 for 로 바로 간다 */
    }

    /*
     * advance the scan until we find a qualifying tuple or run out of stuff
     * to scan
     */
    while (true)                                                           /* ← 첫 호출이거나, 페이지를 다 쓰고 돌아왔을 때 */
    {
        heap_fetch_next_buffer(scan, dir);                                 /* ← 다음 블록을 가져온다 */

        /* did we run out of blocks to scan? */
        if (!BufferIsValid(scan->rs_cbuf))
            break;                                                         /* ← 블록이 더 없으면 튜플 없이 끝 */
        …
        page = heapgettup_start_page(scan, dir, &linesleft, &lineoff);     /* ← 새 페이지: 1번부터, 남은 개수는 라인 포인터 수 */
continue_page:                                                             /* ← 이어 갈 때는 위의 goto 로 여기로 뛰어온다. 여기부터는 새 페이지와 똑같다 */
        …
        for (; linesleft > 0; linesleft--, lineoff += dir)                 /* ← lineoff 번부터 남은 라인 포인터를 하나씩 본다 */
        {
            …
            ItemId      lpp = PageGetItemId(page, lineoff);

            if (!ItemIdIsNormal(lpp))
                continue;                                                  /* ← NORMAL 이 아니면 건너뛴다 */

            tuple->t_data = (HeapTupleHeader) PageGetItem(page, lpp);      /* ← page + lp_off */
            tuple->t_len = ItemIdGetLength(lpp);                           /* ← lp_len */
            …
            scan->rs_coffset = lineoff;                                    /* ← 돌려준 번호를 기억 */
            return;                                                        /* ← 튜플 하나를 찾으면 바로 돌아간다. 나머지는 다음 호출이 잇는다 */
        }
        …                                                                  /* ← 이 페이지에 NORMAL 이 더 없으면 while 처음으로 */
    }
    …
}
```

흐름은 이렇습니다.

1. 이미 시작한 스캔이면 이어 갈 자리를 구해 곧바로 `for` 루프로 들어갑니다.
2. 처음이면 `while` 루프가 첫 블록을 가져와 새 페이지를 시작합니다.
3. `for` 루프는 라인 포인터를 하나씩 봅니다. NORMAL이 아니면 건너뛰고(05장 6절), NORMAL이면 그 튜플의
   위치와 길이를 담고 번호를 `rs_coffset`에 적은 뒤 돌아갑니다.
4. 페이지 끝까지 NORMAL을 못 찾으면 `for`가 끝나고 `while`로 돌아가 다음 블록을 가져옵니다. 다음
   블록이 없으면 스캔이 끝납니다.

돌려주는 것은 튜플의 위치와 길이뿐입니다. 튜플을 컬럼으로 끊는 일(09장)은 튜플을 받은 쪽에서 컬럼
값이 필요할 때 합니다.

---

## 4 · `lab`으로 따라가 보기

`lab`은 블록이 하나이고, 라인 포인터 1, 2, 3번이 모두 NORMAL입니다(05장). 호출마다 이렇게 흘러갑니다.

| 호출 | 시작 | 돌려주는 것 | 호출 뒤 |
|---|---|---|---|
| 1 | 새 페이지: 1번부터, 남은 개수 3 | 1번 튜플 | `rs_coffset` = 1 |
| 2 | 이어서: 2번부터, 남은 개수 2 | 2번 튜플 | `rs_coffset` = 2 |
| 3 | 이어서: 3번부터, 남은 개수 1 | 3번 튜플 | `rs_coffset` = 3 |
| 4 | 이어서: 4번부터, 남은 개수 0. 다음 블록도 없다 | 없음 | 스캔 끝 |

UNUSED나 REDIRECT처럼 NORMAL이 아닌 라인 포인터가 섞여 있으면, `for` 루프가 그 번호를 `continue`로
건너뛰고 다음 NORMAL을 찾습니다.

---

## 5 · 정리

- 순차 스캔은 부를 때마다 튜플 하나만 돌려주고, 다음 호출에서 그 자리부터 잇습니다.
- 호출 사이에 기억하는 것은 스캔을 시작했는지(`rs_inited`), 지금 블록(`rs_cblock`), 마지막으로 돌려준
  라인 포인터 번호(`rs_coffset`), 그리고 훑을 범위입니다.
- 한 번의 호출은 새 페이지면 1번부터, 이어 가면 `rs_coffset + 1`부터 라인 포인터를 보고, NORMAL이
  아니면 건너뜁니다. 페이지를 다 쓰면 다음 블록으로 넘어가고, 범위 끝에 닿으면 끝납니다.
