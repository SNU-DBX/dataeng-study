# 07 · 힙 튜플 헤더

!!! abstract "목표"
    라인 포인터가 가리키는 튜플의 첫 23바이트, 힙 튜플 헤더를 읽습니다. 컬럼 값을 꺼내는 데
    필요한 세 필드가 무엇인지, 컬럼 값이 튜플의 몇 번째 바이트에서 시작하는지 이해합니다. 이 장이
    끝나면 튜플 바이트를 보고 컬럼 수, null bitmap의 유무, 데이터 시작 위치를 말할 수 있어야
    합니다.

[05장](./05-line-pointers.md)에서 `lab`의 1번 라인 포인터가 오프셋 8144, 길이 48을 가리켰고,
[06장](./06-reading-a-page-by-hand.md)에서 그 48바이트를 `hexdump`로 꺼냈습니다.

```
00001fd0  a5 39 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |.9..............|
00001fe0  01 00 05 00 02 09 18 00  01 00 00 00 01 00 00 00  |................|
00001ff0  cb 04 fb 71 1f 01 00 00  09 6b 69 6d 79 22 00 00  |...q.....kimy"..|
00002000
```

이것이 `(1, true, 1234567890123, 'kim', '2024-02-29')` 한 행입니다. 앞의 24바이트는 헤더 23바이트와
빈 1바이트이고, 그 뒤가 컬럼 값입니다. 이 장에서는 헤더를 읽어 봅니다.

---

## 1 · 23바이트 고정부

튜플은 `HeapTupleHeaderData`로 시작합니다.

```c title="src/include/access/htup_details.h:153"
struct HeapTupleHeaderData
{
    union
    {
        HeapTupleFields t_heap;
        DatumTupleFields t_datum;
    }           t_choice;

    ItemPointerData t_ctid;     /* current TID of this or newer tuple (or a
                                 * speculative insertion token) */

    /* Fields below here must match MinimalTupleData! */

#define FIELDNO_HEAPTUPLEHEADERDATA_INFOMASK2 2
    uint16      t_infomask2;    /* number of attributes + various flags */

#define FIELDNO_HEAPTUPLEHEADERDATA_INFOMASK 3
    uint16      t_infomask;     /* various flag bits, see below */

#define FIELDNO_HEAPTUPLEHEADERDATA_HOFF 4
    uint8       t_hoff;         /* sizeof header incl. bitmap, padding */

    /* ^ - 23 bytes - ^ */

#define FIELDNO_HEAPTUPLEHEADERDATA_BITS 5
    bits8       t_bits[FLEXIBLE_ARRAY_MEMBER];  /* bitmap of NULLs */

    /* MORE DATA FOLLOWS AT END OF STRUCT */
};
```
```c title="src/include/access/htup_details.h:185"
#define SizeofHeapTupleHeader offsetof(HeapTupleHeaderData, t_bits)
```

`t_choice`는 보통 `HeapTupleFields`이고, 그 안은 트랜잭션 ID 둘과 커맨드 ID 하나입니다.

```c title="src/include/access/htup_details.h:122"
typedef struct HeapTupleFields
{
    TransactionId t_xmin;       /* inserting xact ID */
    TransactionId t_xmax;       /* deleting or locking xact ID */

    union
    {
        CommandId   t_cid;      /* inserting or deleting command ID, or both */
        TransactionId t_xvac;   /* old-style VACUUM FULL xact ID */
    }           t_field3;
} HeapTupleFields;
```

바이트 오프셋으로 펴면 이렇습니다.

| 바이트 | 필드 | 크기 | 뜻 |
|---|---|---|---|
| 0 | `t_xmin` | 4 | 이 행을 넣은 트랜잭션 ID |
| 4 | `t_xmax` | 4 | 이 행을 지우거나 잠근 트랜잭션 ID. 없으면 0 |
| 8 | `t_cid` | 4 | 그 트랜잭션 안에서 몇 번째 명령이었는지 |
| 12 | `t_ctid` | 6 | 블록 번호 4바이트 + 슬롯 번호 2바이트. 이 행 또는 이 행의 새 버전이 있는 자리 |
| 18 | `t_infomask2`{ style="white-space: nowrap" } | 2 | 하위 11비트가 **컬럼 수**, 나머지는 플래그 |
| 20 | `t_infomask` | 2 | 플래그 비트. 그중 **null bitmap 유무**가 여기 있다 |
| 22 | `t_hoff` | 1 | **컬럼 값이 시작하는 오프셋**. 헤더 + bitmap + 패딩 |
| 23 | `t_bits[]` | 가변 | null bitmap. `t_infomask`에 HASNULL이 있을 때만 존재 |

`SizeofHeapTupleHeader`는 `t_bits`의 오프셋이니 **23**입니다. 앞의 18바이트는 트랜잭션과 행 버전에
관한 정보라 컬럼 값을 읽는 데는 쓰이지 않습니다. 값을 꺼내는 데
필요한 것은 18번 바이트부터 시작하는 `t_infomask2`, `t_infomask`, `t_hoff` 세 필드입니다.

---

## 2 · `t_infomask2`: 컬럼 수

하위 11비트가 이 튜플에 저장된 컬럼 수입니다.

```c title="src/include/access/htup_details.h:291"
#define HEAP_NATTS_MASK         0x07FF  /* 11 bits for number of attributes */
```
```c title="src/include/access/htup_details.h:582"
#define HeapTupleHeaderGetNatts(tup) \
    ((tup)->t_infomask2 & HEAP_NATTS_MASK)
```

11비트라 최대 2047이고, 실제 상한은 `MaxHeapAttributeNumber` 1600입니다. 상위 5비트는 다른 용도의
플래그입니다.

---

## 3 · `t_infomask`: null bitmap이 있는가

16비트 전부가 플래그입니다. 튜플을 처음부터 순서대로 끊을 때 필요한 것은 한 비트입니다.

```c title="src/include/access/htup_details.h:190"
#define HEAP_HASNULL            0x0001  /* has null attribute(s) */
```

`HEAP_HASNULL`이 켜져 있으면 헤더 바로 뒤, 23번 바이트부터 null bitmap이 있습니다. 꺼져 있으면
bitmap이 없고 모든 컬럼에 값이 있습니다.

나머지 비트는 이 과정에 쓰이지 않습니다. 그중 `HEAP_XMIN_COMMITTED`, `HEAP_XMAX_INVALID` 같은
비트는 1절의 트랜잭션 필드에 딸린 상태 표시입니다.

---

## 4 · `t_hoff`: 컬럼 값은 여기서 시작한다

`t_hoff`는 튜플 시작에서 첫 컬럼 값까지의 바이트 수입니다. `heap_form_tuple()`이 튜플을 만들 때
이렇게 정합니다.

```c title="src/backend/access/common/heaptuple.c:1151"
    len = offsetof(HeapTupleHeaderData, t_bits);  /* ← 23 */

    if (hasnull)
        len += BITMAPLEN(numberOfAttributes);     /* ← (natts + 7) / 8 바이트 */

    hoff = len = MAXALIGN(len); /* align user data safely */
```

23바이트에 null bitmap 길이를 더하고 8의 배수로 올립니다. bitmap은 컬럼 하나에 1비트씩이라
`(natts + 7) / 8`바이트입니다.

| 컬럼 수 | null 없음 | null 있음 |
|---|---|---|
| 1 – 8 | 23 → **24** | 23 + 1 = 24 → **24** |
| 9 – 16 | 23 → 24 | 23 + 2 = 25 → **32** |
| 17 – 24 | 23 → 24 | 23 + 3 = 26 → **32** |

`lab`은 컬럼이 5개라 어느 경우든 24입니다. 컬럼 값은 항상 튜플 안 8의 배수 오프셋에서 시작하고,
튜플 자체도 페이지 안 8의 배수 오프셋에 놓이므로(04장), 컬럼 값의 정렬을 튜플 시작 기준으로
따져도 페이지 기준과 같습니다. 이 사실이 [09장](./09-null-bitmap-and-alignment.md)에서 쓰입니다.

---

## 5 · `lab`의 튜플 헤더 셋

1번 라인 포인터가 가리키는 튜플의 처음 24바이트를 표대로 잘라서 확인해 봅시다.

```
a5 39 00 00 | 00 00 00 00 | 00 00 00 00 | 00 00 00 00 01 00 | 05 00 | 02 09 | 18 | 00
t_xmin        t_xmax        t_cid         t_ctid              natts   mask    hoff pad
```

| 필드 | 원문 | 값 |
|---|---|---|
| `t_xmin` | `a5 39 00 00` | 14757 |
| `t_xmax` | `00 00 00 00` | 0. 지우지 않았다 |
| `t_cid` | `00 00 00 00` | 0 |
| `t_ctid` | `00 00 00 00 01 00` | (블록 0, 슬롯 1). 자기 자신 |
| `t_infomask2` | `05 00` | 0x0005. 하위 11비트 = **5개 컬럼** |
| `t_infomask` | `02 09` | 0x0902. HASNULL(0x0001) 꺼짐 |
| `t_hoff` | `18` | **24** |
| 23번 바이트 | `00` | bitmap이 없으므로 패딩 |

`pageinspect`로 맞춰 봅시다. `heap_page_items()`는 이 필드들을 열로 보여 줍니다.

```sql title="psql"
select lp, lp_len, t_infomask2, t_infomask, t_hoff, t_bits
  from heap_page_items(get_raw_page('lab', 0));
```
```
 lp | lp_len | t_infomask2 | t_infomask | t_hoff |  t_bits
----+--------+-------------+------------+--------+----------
  1 |     48 |           5 |       2306 |     24 |
  2 |     44 |           5 |       2305 |     24 | 11101000
  3 |     47 |           5 |       2307 |     24 | 11110000
(3 rows)
```

2306은 0x0902로, 손으로 읽은 값과 같습니다. 플래그 이름으로 풀어 주는 함수도 있습니다.

```sql title="psql"
select lp, raw_flags
  from heap_page_items(get_raw_page('lab', 0)),
       lateral heap_tuple_infomask_flags(t_infomask, t_infomask2);
```
```
 lp |                               raw_flags
----+-----------------------------------------------------------------------
  1 | {HEAP_HASVARWIDTH,HEAP_XMIN_COMMITTED,HEAP_XMAX_INVALID}
  2 | {HEAP_HASNULL,HEAP_XMIN_COMMITTED,HEAP_XMAX_INVALID}
  3 | {HEAP_HASNULL,HEAP_HASVARWIDTH,HEAP_XMIN_COMMITTED,HEAP_XMAX_INVALID}
(3 rows)
```

2번과 3번 튜플은 `HEAP_HASNULL`이 켜져 있습니다. 2번은 `name`이 NULL이고 3번은 `born`이 NULL인 행입니다.
그래서 이 둘은 23번 바이트가 패딩이 아니라 null bitmap이고, `t_bits` 열에 그 비트가 보입니다. 2번
튜플의 헤더를 보면 그 바이트가 `17`입니다.

```
00001fa0  a5 39 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |.9..............|
00001fb0  02 00 05 00 01 09 18 17  02 00 00 00 00 00 00 00  |................|
                      ^^^^^ ^^ ^^
                      |     |  └ t_bits, the null bitmap  (17)
                      |     └ t_hoff                     (18)
                      └ t_infomask                       (01 09)
```

`t_infomask`는 0x0901로 HASNULL이 켜져 있고, `t_hoff`는 24입니다. 그리고 `17` = 0b00010111이 bitmap입니다.
이 비트를 어떻게 읽는지는 09장에서 살펴봅니다.

!!! note "Design note"
    헤더가 23바이트인데 `t_hoff`가 24인 이유는 컬럼 값을 8의 배수 자리에서 시작시키기 위해서입니다.
    그래야 첫 컬럼이 8바이트 정수여도 정렬을 맞추려고 패딩을 더 넣을 필요가 없습니다. 1바이트를
    비워 두는 대가로 값 읽기가 단순해집니다.

---

## 6 · 정리

- 튜플은 23바이트 고정 헤더로 시작합니다. 그중 값을 읽는 데 필요한 것은 `t_infomask2`(18),
  `t_infomask`(20), `t_hoff`(22)입니다.
- 컬럼 수 = `t_infomask2 & 0x07FF`.
- `t_infomask & 0x0001`(HASNULL)이 켜져 있으면 23번 바이트부터 `(natts + 7) / 8`바이트의
  null bitmap이 있습니다.
- 컬럼 값은 `t_hoff`에서 시작합니다. `t_hoff`는 23 + bitmap 길이를 8의 배수로 올린 값이고, 컬럼이 8개
  이하면 24입니다.
