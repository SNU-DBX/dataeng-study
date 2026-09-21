# 05 · 라인 포인터

!!! abstract "목표"
    4바이트 라인 포인터 하나가 어떤 비트로 이루어졌는지, 번호는 어떻게 매기는지, 그것으로 튜플에
    어떻게 닿는지 이해합니다. 그리고 라인 포인터의 네 가지 상태가 무엇이고 언제 바뀌는지를 실제
    페이지에서 살펴봅니다. 이 장이 끝나면 페이지 헤더 뒤의 4바이트를 손으로 풀어 튜플의 위치와 길이를
    말할 수 있어야 합니다.

[지난 장](./04-page-layout.md)의 페이지 헤더 24바이트 바로 뒤에 라인 포인터가 있었습니다. `lab`
페이지에서는 `d0 9f 60 00`, `a0 9f 58 00`, `70 9f 5e 00` 세 개였습니다. 이 장에서는 그 4바이트를
풀어 봅니다.

---

## 1 · 4바이트에 세 필드

라인 포인터는 `ItemIdData`라는 이름의 32비트 비트 필드입니다.

```c title="src/include/storage/itemid.h:25"
typedef struct ItemIdData
{
    unsigned    lp_off:15,      /* offset to tuple (from start of page) */
                lp_flags:2,     /* state of line pointer, see below */
                lp_len:15;      /* byte length of tuple */
} ItemIdData;
```

```
bit   31                  17 16 15 14                   0
     ┌──────────────────────┬─────┬──────────────────────┐
     │ lp_len (15 bits)     │flags│ lp_off (15 bits)     │
     └──────────────────────┴─────┴──────────────────────┘
```

| 필드 | 비트 | 뜻 |
|---|---|---|
| `lp_off` | 0 – 14 | 튜플이 시작하는 바이트 오프셋. 페이지 처음부터 센다 |
| `lp_flags` | 15 – 16 | 상태. 아래 네 값 중 하나 |
| `lp_len` | 17 – 31 | 튜플의 바이트 길이 |

15비트는 0 – 32767이라 8 KB 페이지의 어느 오프셋이든 담깁니다. `bufpage.h`의 주석이 페이지를
32 KB까지만 지원하는 이유로 "lp_off/lp_len are 15 bits"를 드는 것도 이 때문입니다.

상태 값은 넷입니다.

```c title="src/include/storage/itemid.h:38"
#define LP_UNUSED       0       /* unused (should always have lp_len=0) */
#define LP_NORMAL       1       /* used (should always have lp_len>0) */
#define LP_REDIRECT     2       /* HOT redirect (should have lp_len=0) */
#define LP_DEAD         3       /* dead, may or may not have storage */
```

필드를 꺼내는 매크로는 이름 그대로입니다.

```c title="src/include/storage/itemid.h:59"
#define ItemIdGetLength(itemId) \
   ((itemId)->lp_len)
…
#define ItemIdGetOffset(itemId) \
   ((itemId)->lp_off)
…
#define ItemIdGetFlags(itemId) \
   ((itemId)->lp_flags)
…
#define ItemIdIsNormal(itemId) \
    ((itemId)->lp_flags == LP_NORMAL)
```

---

## 2 · 번호는 1부터

라인 포인터 배열 `pd_linp[]`는 바이트 24에서 시작하고, 항목 하나가 4바이트입니다. PostgreSQL은 이
항목에 **1부터** 번호를 매기고 `OffsetNumber`라고 부릅니다.

```c title="src/include/storage/off.h:27"
#define FirstOffsetNumber       ((OffsetNumber) 1)
#define MaxOffsetNumber         ((OffsetNumber) (BLCKSZ / sizeof(ItemIdData)))
```

N번 라인 포인터는 배열의 N − 1번째 칸입니다.

```c title="src/include/storage/bufpage.h:245"
PageGetItemId(Page page, OffsetNumber offsetNumber)
{
    return &((PageHeader) page)->pd_linp[offsetNumber - 1];
}
```

바이트로는 `24 + (N − 1) × 4`입니다. 1번은 24, 2번은 28, 3번은 32입니다.

페이지에 라인 포인터가 몇 개인지는 `pd_lower`가 말해 줍니다. 배열이 24에서 `pd_lower`까지이므로
개수는 그 길이를 4로 나눈 것입니다.

```c title="src/include/storage/bufpage.h:373"
PageGetMaxOffsetNumber(const PageData *page)
{
    const PageHeaderData *pageheader = (const PageHeaderData *) page;

    if (pageheader->pd_lower <= SizeOfPageHeaderData)
        return 0;
    else
        return (pageheader->pd_lower - SizeOfPageHeaderData) / sizeof(ItemIdData);
}
```

`lab`은 `pd_lower`가 36이니 (36 − 24) / 4 = 3개입니다.

---

## 3 · `lab`의 라인 포인터 세 개 풀기

바이트 24부터 12바이트를 4바이트씩 끊어 봅시다. little-endian이니 네 바이트를 거꾸로 이어 붙이면
32비트 값이 됩니다.

| 번호 | 바이트 | 원문 | 32비트 값 |
|---|---|---|---|
| 1 | 24 – 27 | `d0 9f 60 00` | 0x00609fd0 |
| 2 | 28 – 31 | `a0 9f 58 00` | 0x00589fa0 |
| 3 | 32 – 35 | `70 9f 5e 00` | 0x005e9f70 |

32비트 값 `w`에서 세 필드를 꺼내는 산술은 이렇습니다.

```
lp_off   =  w        & 0x7fff
lp_flags = (w >> 15) & 0x3
lp_len   = (w >> 17) & 0x7fff
```

1번 `w = 0x00609fd0`을 풀면:

- `lp_off` = 0x9fd0 & 0x7fff = 0x1fd0 = **8144**
- `lp_flags` = (0x00609fd0 >> 15) & 3 = 193 & 3 = **1** = `LP_NORMAL`
- `lp_len` = 0x00609fd0 >> 17 = **48**

세 개 모두 풀면 이렇습니다.

| 번호 | `lp_off` | `lp_flags` | `lp_len` |
|---|---|---|---|
| 1 | 8144 | 1 (NORMAL) | 48 |
| 2 | 8096 | 1 (NORMAL) | 44 |
| 3 | 8048 | 1 (NORMAL) | 47 |

같은 값을 PostgreSQL에게 물어서 맞춰 볼 수 있습니다. `pageinspect`의 `heap_page_items()`는 라인
포인터를 이 세 열로 보여 줍니다(다음 장에서 자세히). 01장에서 설치한 `pageinspect`를 먼저 켜 둡시다.

```sql title="psql"
create extension pageinspect;
select lp, lp_off, lp_flags, lp_len from heap_page_items(get_raw_page('lab', 0));
```
```
 lp | lp_off | lp_flags | lp_len
----+--------+----------+--------
  1 |   8144 |        1 |     48
  2 |   8096 |        1 |     44
  3 |   8048 |        1 |     47
(3 rows)
```

손으로 푼 값과 같습니다.

---

## 4 · 라인 포인터에서 튜플로

NORMAL 라인 포인터의 `lp_off`는 페이지 안 바이트 오프셋이고, 튜플은 거기서 `lp_len`바이트입니다.

```c title="src/include/storage/bufpage.h:355"
PageGetItem(const PageData *page, const ItemIdData *itemId)
{
    Assert(page);
    Assert(ItemIdHasStorage(itemId));

    return (Item) (((const char *) page) + ItemIdGetOffset(itemId));
}
```

`lab` 페이지 전체를 그리면 이렇습니다.

```
   byte 0        24  28  32  36                     8048    8096    8144    8192
   ┌─────────────┬───┬───┬───┬──────────────────────┬───────┬───────┬───────┐
   │ page header │lp1│lp2│lp3│      free space      │tuple 3│tuple 2│tuple 1│
   └─────────────┴─┬─┴─┬─┴─┬─┴──────────────────────┴───────┴───────┴───────┘
                   │   │   │                        ▲       ▲       ▲
                   │   │   │                        │       │       │
                   │   │   └────────────────────────┘       │       │
                   │   │                                    │       │
                   │   └────────────────────────────────────┘       │
                   │                                                │
                   └────────────────────────────────────────────────┘

   lp1 = (off 8144, len 48)   lp2 = (off 8096, len 44)   lp3 = (off 8048, len 47)
```

라인 포인터는 앞에서 뒤로 1, 2, 3이고 튜플은 뒤에서 앞으로 1, 2, 3입니다. 지난 장 4절에서 본
대로, 먼저 들어온 튜플일수록 페이지 끝에 가깝습니다.

튜플이 있는 자리 [`lp_off`, `lp_off + lp_len`)은 반드시 튜플 영역 [`pd_upper`, `pd_special`) 안에
있고, `lp_off`는 8의 배수입니다. `PageAddItemExtended()`가 `MAXALIGN`한 자리에만 튜플을 놓기
때문입니다. 8144, 8096, 8048 모두 8로 나누어떨어집니다.

---

## 5 · 네 가지 상태

`lab`의 라인 포인터는 전부 NORMAL입니다. 나머지 세 상태는 행을 지우거나 고친 뒤에 나타납니다.
네 행짜리 테이블로 직접 만들어 봅시다.

```sql title="psql"
create table lp_demo (id int, v text);
insert into lp_demo values (1, 'one'), (2, 'two'), (3, 'three'), (4, 'four');
select lp, lp_off, lp_flags, lp_len from heap_page_items(get_raw_page('lp_demo', 0));
```
```
 lp | lp_off | lp_flags | lp_len
----+--------+----------+--------
  1 |   8160 |        1 |     32
  2 |   8128 |        1 |     32
  3 |   8088 |        1 |     34
  4 |   8048 |        1 |     33
(4 rows)
```

행 하나를 지우고 하나를 고쳐 봅시다.

```sql title="psql"
delete from lp_demo where id = 2;
update lp_demo set v = 'THREE' where id = 3;
select lp, lp_off, lp_flags, lp_len from heap_page_items(get_raw_page('lp_demo', 0));
```
```
 lp | lp_off | lp_flags | lp_len
----+--------+----------+--------
  1 |   8160 |        1 |     32
  2 |   8128 |        1 |     32
  3 |   8088 |        1 |     34
  4 |   8048 |        1 |     33
  5 |   8008 |        1 |     34
(5 rows)
```

아직 아무 상태도 안 바뀌었습니다. `DELETE`는 2번 튜플을 그 자리에 둔 채 표시만 해 두고, `UPDATE`는
3번을 고치는 대신 새 튜플을 5번 슬롯에 넣었습니다. 지운 것과 고친 것의 옛 버전을 실제로 치우는 것은
`VACUUM`입니다.

```sql title="psql"
vacuum lp_demo;
select lp, lp_off, lp_flags, lp_len from heap_page_items(get_raw_page('lp_demo', 0));
```
```
 lp | lp_off | lp_flags | lp_len
----+--------+----------+--------
  1 |   8160 |        1 |     32
  2 |      0 |        0 |      0
  3 |      5 |        2 |      0
  4 |   8120 |        1 |     33
  5 |   8080 |        1 |     34
(5 rows)
```

이제 세 상태가 다 보입니다.

| 번호 | 상태 | 뜻 |
|---|---|---|
| 2 | `LP_UNUSED` (0) | 빈 슬롯. 튜플이 없고 `lp_off`, `lp_len` 모두 0. 다음 `INSERT`가 재사용할 수 있다 |
| 3 | `LP_REDIRECT` (2) | 이 슬롯의 행은 같은 페이지의 **5번 슬롯**으로 옮겨졌다. `lp_off`가 바이트 오프셋이 아니라 슬롯 번호다 |
| 1, 4, 5 | `LP_NORMAL` (1) | 튜플이 있다 |

REDIRECT의 `lp_off`가 다른 뜻이 되는 것은 매크로 주석에도 적혀 있습니다.

```c title="src/include/storage/itemid.h:74"
/*
 *      ItemIdGetRedirect
 * In a REDIRECT pointer, lp_off holds offset number for next line pointer
 */
#define ItemIdGetRedirect(itemId) \
   ((itemId)->lp_off)
```

4번과 5번을 다시 보면 `lp_off`가 8048 → 8120, 8008 → 8080으로 **바뀌었습니다**. `VACUUM`이 지운
튜플의 빈자리를 없애려고 남은 튜플을 페이지 끝 쪽으로 밀어 붙였기 때문입니다. 튜플은 움직였지만
슬롯 번호 4, 5는 그대로입니다. 이것이 지난 장 Design note에서 말한 "슬롯 번호는 유지된다"는 것입니다.

네 번째 상태 `LP_DEAD`는 튜플은 치웠지만 슬롯을 아직 비우지 못한 상태입니다. 인덱스가 있는
테이블에서 인덱스 정리를 미루면 볼 수 있습니다.

```sql title="psql"
create table lp_demo2 (id int primary key, v text);
insert into lp_demo2 values (1, 'one'), (2, 'two'), (3, 'three'), (4, 'four');
delete from lp_demo2 where id = 2;
vacuum (index_cleanup off) lp_demo2;
select lp, lp_off, lp_flags, lp_len from heap_page_items(get_raw_page('lp_demo2', 0));
```
```
 lp | lp_off | lp_flags | lp_len
----+--------+----------+--------
  1 |   8160 |        1 |     32
  2 |      0 |        3 |      0
  3 |   8120 |        1 |     34
  4 |   8080 |        1 |     33
(4 rows)
```

2번이 `LP_DEAD` (3)입니다. 인덱스가 아직 이 슬롯을 가리키고 있을 수 있어서 재사용하지 않고 남겨
둔 것입니다. 인덱스까지 정리하는 보통의 `VACUUM`을 한 번 더 돌리면 UNUSED가 됩니다.

```sql title="psql"
vacuum lp_demo2;
select lp, lp_off, lp_flags, lp_len from heap_page_items(get_raw_page('lp_demo2', 0));
```
```
 lp | lp_off | lp_flags | lp_len
----+--------+----------+--------
  1 |   8160 |        1 |     32
  2 |      0 |        0 |      0
  3 |   8120 |        1 |     34
  4 |   8080 |        1 |     33
(4 rows)
```

정리하면 이렇습니다.

| `lp_flags` | 이름 | 튜플이 있나 | `lp_off`의 뜻 |
|---|---|---|---|
| 0 | `LP_UNUSED` | 없다 | 0 |
| 1 | `LP_NORMAL` | **있다** | 튜플의 바이트 오프셋 |
| 2 | `LP_REDIRECT` | 없다 | 행이 옮겨 간 슬롯 번호 |
| 3 | `LP_DEAD` | 없다 | 0 |

!!! danger "Trap"
    `lp_off`와 `lp_len`은 NORMAL일 때만 바이트 오프셋과 길이입니다. REDIRECT의 `lp_off`는 슬롯
    번호(위에서는 5)라서 그 값을 오프셋으로 읽으면 페이지 헤더 한가운데를 읽게 됩니다.

---

## 6 · 훑을 때는 NORMAL만 본다

테이블을 처음부터 끝까지 읽는 순차 스캔이 페이지 하나를 처리하는 코드를 봅시다. 라인 포인터를 1번부터
끝까지 돌면서 NORMAL이 아니면 건너뜁니다.

```c title="src/backend/access/heap/heapam.c:506"
page_collect_tuples(HeapScanDesc scan, Snapshot snapshot,
                    Page page, Buffer buffer,
                    BlockNumber block, int lines,
                    bool all_visible, bool check_serializable)
{
    int         ntup = 0;
    OffsetNumber lineoff;

    for (lineoff = FirstOffsetNumber; lineoff <= lines; lineoff++)  /* ← 1 .. pd_lower 기준 개수 */
    {
        ItemId      lpp = PageGetItemId(page, lineoff);
        HeapTupleData loctup;
        bool        valid;

        if (!ItemIdIsNormal(lpp))                                   /* ← UNUSED, REDIRECT, DEAD 는 통과 */
            continue;

        loctup.t_data = (HeapTupleHeader) PageGetItem(page, lpp);   /* ← page + lp_off */
        loctup.t_len = ItemIdGetLength(lpp);                        /* ← lp_len */
        …
```

`lines`는 2절의 `PageGetMaxOffsetNumber()`가 돌려준 개수입니다. 이 루프가 남기는 것이 `page +
lp_off`에서 `lp_len`바이트, 즉 튜플 하나의 바이트 범위입니다. 그 안이 어떻게 생겼는지는
07장부터 살펴봅니다.

---

## 7 · 정리

- 라인 포인터는 4바이트 하나에 `lp_off`(15비트), `lp_flags`(2비트), `lp_len`(15비트)입니다.
  little-endian으로 읽은 32비트 값에서 `w & 0x7fff`, `(w >> 15) & 3`, `(w >> 17) & 0x7fff`로 꺼냅니다.
- 번호는 1부터이고 N번은 바이트 `24 + (N − 1) × 4`에 있습니다. 개수는 `(pd_lower − 24) / 4`입니다.
- NORMAL이면 `page + lp_off`부터 `lp_len`바이트가 튜플이고, 그 범위는 [`pd_upper`, `pd_special`)
  안에 있으며 `lp_off`는 8의 배수입니다.
- UNUSED, REDIRECT, DEAD에는 튜플이 없습니다. REDIRECT의 `lp_off`는 슬롯 번호입니다. 순차 스캔은
  NORMAL만 봅니다.
- `VACUUM`은 튜플을 페이지 안에서 옮기지만 슬롯 번호는 바꾸지 않습니다.
