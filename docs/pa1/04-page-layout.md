# 04 · 슬롯 페이지 레이아웃

!!! abstract "목표"
    8 KB 블록 하나가 안에서 어떻게 나뉘어 있는지 이해합니다. 블록은 24바이트 페이지 헤더, 그 바로 뒤에서
    뒤쪽으로 자라는 라인 포인터 배열, 페이지 끝에서 앞쪽으로 자라는 튜플 영역, 그 사이의 빈 공간으로
    나뉩니다. 이 장이 끝나면 페이지 헤더의 세 오프셋만 보고 페이지의 구역을 나눌 수 있고, 페이지가
    정상인지 판단하는 조건을 말할 수 있어야 합니다.

[지난 장](./03-block-io.md)에서 블록 하나, 8192바이트를 읽어 왔습니다. 그 바이트는 PostgreSQL이
**페이지**라고 부르는 형식으로 채워져 있습니다. 블록은 I/O 단위이고 페이지는 그 안의 형식입니다. 이
장에서는 그 형식을 PostgreSQL 헤더 파일과 `lab`의 실제 바이트로 살펴봅니다.

---

## 1 · 페이지는 슬롯 페이지다

PostgreSQL의 페이지 형식은 `bufpage.h` 첫머리의 그림 하나로 요약됩니다.

```c title="src/include/storage/bufpage.h:32"
 * +----------------+---------------------------------+
 * | PageHeaderData | linp1 linp2 linp3 ...           |
 * +-----------+----+---------------------------------+
 * | ... linpN |                                      |
 * +-----------+--------------------------------------+
 * |           ^ pd_lower                             |
 * |                                                  |
 * |             v pd_upper                           |
 * +-------------+------------------------------------+
 * |             | tupleN ...                         |
 * +-------------+------------------+-----------------+
 * |       ... tuple3 tuple2 tuple1 | "special space" |
 * +--------------------------------+-----------------+
 *                                  ^ pd_special
```

네 구역으로 나뉩니다.

| 구역 | 위치 | 내용 |
|---|---|---|
| 페이지 헤더 | 바이트 0 – 23 | `PageHeaderData`. 나머지 구역의 경계가 여기 적혀 있다 |
| 라인 포인터 배열 | 24 – `pd_lower` | 4바이트 항목 `linp1, linp2, …`. 항목마다 튜플 하나의 위치와 길이 |
| 빈 공간 | `pd_lower` – `pd_upper` | 아직 아무것도 없다 |
| 튜플 영역 | `pd_upper` – `pd_special` | 행의 실제 바이트. 끝에서부터 거꾸로 채워진다 |

`pd_special` 뒤의 special space는 인덱스 페이지가 쓰는 자리라 힙 페이지에는 없습니다. 힙 페이지의
`pd_special`은 항상 8192, 즉 페이지 끝입니다.

이런 구조를 **슬롯 페이지(slotted page)** 라고 부릅니다. 튜플을 직접 세지 않고, 앞쪽의 고정 크기
슬롯(라인 포인터)이 튜플을 가리키게 하는 방식입니다. 라인 포인터 배열은 앞에서 뒤로 자라고 튜플
영역은 뒤에서 앞으로 자라서, 둘이 만나면 페이지가 꽉 찬 것입니다.

!!! note "Design note"
    슬롯 없이 튜플을 페이지 앞부터 차례로 붙이면 왜 안 될까요? 튜플은 길이가 제각각이고
    지워지기도 하므로, 바깥에서 행을 가리키는 값이 "페이지 안 몇 번째 바이트"라면 튜플 하나를
    지우거나 옮길 때마다 그 값이 전부 틀려집니다. 슬롯을 두면 튜플이 페이지 안에서 움직여도
    "N번째 슬롯"이라는 이름은 그대로라, 바깥에서는 (블록 번호, 슬롯 번호)만 기억하면 됩니다. 다음
    장에서 실제로 튜플이 옮겨지는데도 슬롯 번호가 유지되는 것을 보게 됩니다.

---

## 2 · 24바이트 페이지 헤더

페이지 헤더는 C 구조체 그대로입니다.

```c title="src/include/storage/bufpage.h:159"
typedef struct PageHeaderData
{
    /* XXX LSN is member of *any* block, not only page-organized ones */
    PageXLogRecPtr pd_lsn;      /* LSN: next byte after last byte of xlog
                                 * record for last change to this page */
    uint16      pd_checksum;    /* checksum */
    uint16      pd_flags;       /* flag bits, see below */
    LocationIndex pd_lower;     /* offset to start of free space */
    LocationIndex pd_upper;     /* offset to end of free space */
    LocationIndex pd_special;   /* offset to start of special space */
    uint16      pd_pagesize_version;
    TransactionId pd_prune_xid; /* oldest prunable XID, or zero if none */
    ItemIdData  pd_linp[FLEXIBLE_ARRAY_MEMBER]; /* line pointer array */
} PageHeaderData;
```
```c title="src/include/storage/bufpage.h:218"
#define SizeOfPageHeaderData (offsetof(PageHeaderData, pd_linp))
```

`LocationIndex`는 `uint16`입니다. 필드를 바이트 오프셋으로 펴면 이렇습니다.

| 바이트 | 필드 | 크기 | 뜻 |
|---|---|---|---|
| 0 | `pd_lsn` | 8 | 이 페이지를 마지막으로 바꾼 WAL 레코드의 위치 |
| 8 | `pd_checksum` | 2 | 페이지 체크섬. 디스크에 쓸 때 계산해 넣는다 |
| 10 | `pd_flags` | 2 | 상태 비트 셋 |
| 12 | `pd_lower` | 2 | 라인 포인터 배열의 끝 = 빈 공간의 시작 |
| 14 | `pd_upper` | 2 | 빈 공간의 끝 = 튜플 영역의 시작 |
| 16 | `pd_special` | 2 | special space의 시작. 힙은 8192 |
| 18 | `pd_pagesize_version` | 2 | 페이지 크기와 레이아웃 버전을 한 값에 |
| 20 | `pd_prune_xid` | 4 | 이 페이지에 치울 튜플이 생겼는지 알려 주는 트랜잭션 ID 힌트 |
| 24 | `pd_linp[]` | 4 × N | 라인 포인터 배열. 여기부터가 페이지 헤더 밖이다 |

`SizeOfPageHeaderData`는 `pd_linp`의 오프셋이므로 **24**입니다. 이 뒤로 계속 쓰는 것은 `pd_lower`,
`pd_upper`, `pd_special` 셋입니다.

### 2.1 · `lab`의 페이지 헤더

01장에서 만든 `lab`, 즉 세 행을 넣고 `CHECKPOINT`까지 한 그 테이블의 파일(`base/16384/132971`) 첫
40바이트를 찍어 봅시다. 페이지 헤더 24바이트에, 그 뒤에 이어지는 라인 포인터 3개(12바이트)까지 같이
봅니다. `hexdump`의 `-n 40`은 처음 40바이트만 찍으라는 옵션입니다.

```sh title="shell"
hexdump -C -n 40 $PGBASE/pgdata/base/16384/132971
```
```
00000000  03 00 00 00 18 21 a4 b7  70 6b 00 00 24 00 70 1f  |.....!..pk..$.p.|
00000010  00 20 04 20 00 00 00 00  d0 9f 60 00 a0 9f 58 00  |. . ......`...X.|
00000020  70 9f 5e 00 00 00 00 00                           |p.^.....|
00000028
```

x86-64는 little-endian이라 여러 바이트짜리 정수는 **낮은 바이트가 먼저** 옵니다. `24 00`은 0x0024,
`70 1f`는 0x1f70입니다. 위 표대로 잘라 읽으면 이렇습니다.

| 바이트 | 원문 | 값 | 필드 |
|---|---|---|---|
| 0 – 7 | `03 00 00 00 18 21 a4 b7` | 3/B7A42118 | `pd_lsn` (앞 4바이트가 상위, 뒤 4바이트가 하위) |
| 8 – 9 | `70 6b` | 0x6b70 | `pd_checksum` |
| 10 – 11 | `00 00` | 0 | `pd_flags` |
| 12 – 13 | `24 00` | 0x0024 = **36** | `pd_lower` |
| 14 – 15 | `70 1f` | 0x1f70 = **8048** | `pd_upper` |
| 16 – 17 | `00 20` | 0x2000 = **8192** | `pd_special` |
| 18 – 19 | `04 20` | 0x2004 | `pd_pagesize_version` = 8192 + 4 |
| 20 – 23 | `00 00 00 00` | 0 | `pd_prune_xid` |
| 24 – 35 | `d0 9f 60 00 a0 9f 58 00 70 9f 5e 00` | | 페이지 헤더 밖. 4바이트씩 라인 포인터 3개다. 다음 장에서 자세히 본다 |

세 오프셋이 페이지를 이렇게 나눕니다.

```
   byte 0        24       36                           8048              8192
   ┌─────────────┬────────┬────────────────────────────┬─────────────────┐
   │ page header │ linp x3│        free space          │  tuples (3)     │
   └─────────────┴────────┴────────────────────────────┴─────────────────┘
                          ^ pd_lower                   ^ pd_upper        ^ pd_special
```

행 3개짜리 테이블이니 라인 포인터도 3개, 24 + 3 × 4 = 36이 `pd_lower`입니다. 튜플 영역은 8048부터
8192까지 144바이트이고, 그 사이 8012바이트가 비어 있습니다.

---

## 3 · 빈 페이지는 이렇게 시작한다

새 페이지는 `PageInit()`이 만듭니다.

```c title="src/backend/storage/page/bufpage.c:42"
PageInit(Page page, Size pageSize, Size specialSize)
{
    PageHeader  p = (PageHeader) page;

    specialSize = MAXALIGN(specialSize);
    …
    /* Make sure all fields of page are zero, as well as unused space */
    MemSet(p, 0, pageSize);

    p->pd_flags = 0;
    p->pd_lower = SizeOfPageHeaderData;      /* ← 24 */
    p->pd_upper = pageSize - specialSize;    /* ← 힙: 8192 */
    p->pd_special = pageSize - specialSize;  /* ← 힙: 8192 */
    PageSetPageSizeAndVersion(page, pageSize, PG_PAGE_LAYOUT_VERSION);
    /* p->pd_prune_xid = InvalidTransactionId;     done by above MemSet */
}
```

전부 0으로 지운 뒤 세 오프셋만 적습니다. 힙은 `specialSize`가 0이라 `pd_upper`와 `pd_special`이 둘 다
8192입니다. 라인 포인터가 하나도 없으니 `pd_lower`는 24이고, 이 상태를 `PageIsEmpty()`가 알아봅니다.

```c title="src/include/storage/bufpage.h:225"
PageIsEmpty(const PageData *page)
{
    return ((const PageHeaderData *) page)->pd_lower <= SizeOfPageHeaderData;
}
```

---

## 4 · 튜플이 들어오면 두 오프셋이 움직인다

튜플 하나를 넣는 함수가 `PageAddItemExtended()`입니다. 핵심만 추리면 이렇습니다.

```c title="src/backend/storage/page/bufpage.c:193"
PageAddItemExtended(Page page, Item item, Size size, OffsetNumber offsetNumber, int flags)
{
    …
    if (offsetNumber == limit || needshuffle)
        lower = phdr->pd_lower + sizeof(ItemIdData);   /* ← 라인 포인터 하나만큼 아래로 */
    else
        lower = phdr->pd_lower;

    alignedSize = MAXALIGN(size);                      /* ← 8의 배수로 올림 */

    upper = (int) phdr->pd_upper - (int) alignedSize;  /* ← 튜플 하나만큼 위로 */

    if (lower > upper)
        return InvalidOffsetNumber;                    /* ← 둘이 만나면 꽉 찬 것 */
    …
    /* set the line pointer */
    ItemIdSetNormal(itemId, upper, size);              /* ← 슬롯에 (위치, 길이) 기록 */
    …
    /* copy the item's data onto the page */
    memcpy((char *) page + upper, item, size);         /* ← 튜플 바이트 복사 */

    /* adjust page header */
    phdr->pd_lower = (LocationIndex) lower;
    phdr->pd_upper = (LocationIndex) upper;

    return offsetNumber;
}
```

새 튜플은 `pd_upper`에서 `MAXALIGN(size)`만큼 **뺀** 자리에 놓입니다. 그래서 첫 튜플이 페이지 끝에
붙고, 다음 튜플은 그 앞에 붙습니다. `MAXALIGN`은 8바이트 단위로 올림하는 매크로라 모든 튜플은
8의 배수 오프셋에서 시작합니다.

`lab`의 세 튜플로 확인해 봅시다. 길이는 다음 장에서 라인 포인터를 풀어 얻는 값입니다.

| 순서 | 튜플 길이 | `MAXALIGN` | `pd_upper` 계산 | 튜플 시작 |
|---|---|---|---|---|
| 1 | 48 | 48 | 8192 − 48 | 8144 |
| 2 | 44 | 48 | 8144 − 48 | 8096 |
| 3 | 47 | 48 | 8096 − 48 | 8048 |

마지막 `pd_upper`가 8048, 2.1절의 페이지 헤더에 적힌 값과 같습니다. 튜플 2는 44바이트인데 48바이트 자리를
차지합니다. 4바이트는 정렬 때문에 비는 패딩입니다.

---

## 5 · 정상 페이지의 조건

디스크에서 읽은 8 KB가 온전한 페이지인지를 PostgreSQL은 `PageIsVerified()`로 봅니다.

```c title="src/backend/storage/page/bufpage.c:94"
PageIsVerified(PageData *page, BlockNumber blkno, int flags, bool *checksum_failure_p)
{
    …
    if (!PageIsNew(page))
    {
        …
        if ((p->pd_flags & ~PD_VALID_FLAG_BITS) == 0 &&
            p->pd_lower <= p->pd_upper &&
            p->pd_upper <= p->pd_special &&
            p->pd_special <= BLCKSZ &&
            p->pd_special == MAXALIGN(p->pd_special))
            header_sane = true;
        …
    }

    /* Check all-zeroes case */
    pagebytes = (size_t *) page;

    if (pg_memory_is_all_zeros(pagebytes, BLCKSZ))
        return true;
    …
```

체크섬 검사(06장)를 빼고 보면 두 갈래입니다.

**보통 페이지.** 세 오프셋이 순서대로여야 합니다.

```
pd_lower <= pd_upper <= pd_special <= 8192
```

여기에 `pd_lower`는 24 이상이라는 조건이 사실상 붙습니다. 페이지 헤더보다 앞에서 라인 포인터가 시작할 수는
없기 때문입니다. 이 부등식이 뜻하는 것은 하나입니다. 라인 포인터 배열 [24, `pd_lower`)과 튜플 영역
[`pd_upper`, `pd_special`)이 **겹치지 않고 둘 다 페이지 안에 있습니다**. 이 부등식이 깨진 페이지에서는
라인 포인터가 페이지 밖을 가리킬 수 있습니다.

**전부 0인 페이지.** `PageIsNew()`는 `pd_upper == 0`을 봅니다.

```c title="src/include/storage/bufpage.h:235"
PageIsNew(const PageData *page)
{
    return ((const PageHeaderData *) page)->pd_upper == 0;
}
```

`PageInit()`을 거친 페이지는 `pd_upper`가 0일 수 없으니, 0이라는 것은 아직 초기화되지 않은
페이지라는 뜻입니다. 지난 장 5절에서 본 그 페이지입니다. 테이블을 늘릴 때 PostgreSQL이 파일에 먼저
0으로 자리를 잡고, 내용은 나중에 씁니다. 그런 페이지는 8192바이트가 **전부** 0일 때만 정상입니다.
`pd_upper`가 0인데 다른 바이트가 0이 아니라면 그것은 깨진 페이지입니다.

!!! danger "Trap"
    전부 0인 페이지는 에러가 아닙니다. 부등식으로 보면 `pd_lower`가 24보다 작아 실패하지만,
    PostgreSQL은 그 전에 `PageIsNew()`로 걸러서 정상으로 칩니다. 라인 포인터가 없으니 행도 없고,
    그냥 지나가면 되는 페이지입니다.

---

## 6 · 정리

- 페이지는 24바이트 페이지 헤더, 라인 포인터 배열(앞에서 뒤로), 빈 공간, 튜플 영역(뒤에서 앞으로)으로
  나뉩니다. 힙 페이지에는 special space가 없어 `pd_special`은 8192입니다.
- 페이지 헤더에서 쓰는 값은 `pd_lower`(바이트 12), `pd_upper`(14), `pd_special`(16)이고, little-endian
  `uint16`입니다.
- 튜플은 `pd_upper`에서 `MAXALIGN(size)`만큼 뺀 자리에 놓이므로 8의 배수 오프셋에서 시작합니다.
- 체크섬 검사를 빼고 보면, 정상 페이지는 24 ≤ `pd_lower` ≤ `pd_upper` ≤ `pd_special` ≤ 8192이거나,
  8192바이트 전부가 0입니다.
