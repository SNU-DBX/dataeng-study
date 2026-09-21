# 09 · null bitmap과 정렬

!!! abstract "목표"
    튜플의 데이터 부분을 컬럼 단위로 끊는 규칙 두 가지를 익힙니다. NULL인 컬럼은 null bitmap의
    비트 하나로만 표시되고 바이트를 차지하지 않는다는 것, 그리고 각 컬럼은 `attalign` 경계까지
    패딩을 건너뛴 자리에서 시작하되 1바이트 헤더 varlena만은 예외라는 것입니다. 이 장이 끝나면 `lab`의
    세 튜플을 손으로 끊어 컬럼마다 (시작 오프셋, 길이)를 말할 수 있어야 합니다.

[07장](./07-tuple-header.md)에서 튜플 헤더를, [08장](./08-column-layout.md)에서 카탈로그를 읽었습니다.
이제 둘을 합쳐 `t_hoff`부터 시작하는 데이터를 끊어 봅시다. PostgreSQL이 이 일을 하는 함수가
`heap_deform_tuple()`이고, 이 장에서는 그 함수의 루프를 따라가 봅니다.

```c title="src/backend/access/common/heaptuple.c:1346"
heap_deform_tuple(HeapTuple tuple, TupleDesc tupleDesc,
                  Datum *values, bool *isnull)
{
    …
    bool        hasnulls = HeapTupleHasNulls(tuple);                  /* ← t_infomask & HEAP_HASNULL */
    …
    tp = (char *) tup + tup->t_hoff;                                  /* ← 데이터 시작 */

    off = 0;

    for (attnum = 0; attnum < natts; attnum++)
    {
        …
        if (hasnulls && att_isnull(attnum, bp))                       /* ← 1절: NULL 이면 건너뛴다 */
        {
            …
            continue;
        }
        …
        off = att_pointer_alignby(off, thisatt->attalignby, -1,
                                  tp + off);                          /* ← 2절: varlena 정렬 */
        …
        off = att_nominal_alignby(off, thisatt->attalignby);          /* ← 2절: 고정 길이 정렬 */
        …
        off = att_addlength_pointer(off, thisatt->attlen, tp + off);  /* ← 3절: 길이만큼 전진 */
        …
    }
    …
```

컬럼마다 세 단계입니다. NULL인지 보고, 시작 자리를 맞추고, 길이만큼 나아갑니다. `off`는 `t_hoff`
기준의 오프셋입니다.

---

## 1 · null bitmap: NULL은 바이트를 차지하지 않는다

`t_infomask`에 `HEAP_HASNULL`이 켜진 튜플은 23번 바이트부터 null bitmap이 있습니다. 컬럼 하나에
비트 하나, `attnum` 순서로 0번 비트부터입니다. 비트가 **1이면 값이 있고 0이면 NULL**입니다.

```c title="src/include/access/tupmacs.h:26"
att_isnull(int ATT, const bits8 *BITS)
{
    return !(BITS[ATT >> 3] & (1 << (ATT & 0x07)));
}
```

`ATT >> 3`은 몇 번째 바이트인지, `ATT & 7`은 그 바이트의 몇 번째 비트인지를 나타냅니다. 컬럼 0 – 7은 첫
바이트, 8 – 15는 둘째 바이트에 있습니다.

`lab` 2번 튜플의 bitmap은 `17`이었습니다. 0x17 = 0b00010111입니다. 0번 비트가 가장 오른쪽입니다.

| 비트 | 값 | 컬럼 | |
|---|---|---|---|
| 0 | 1 | `id` | 있음 |
| 1 | 1 | `flag` | 있음 |
| 2 | 1 | `big` | 있음 |
| 3 | 0 | `name` | **NULL** |
| 4 | 1 | `born` | 있음 |
| 5 – 7 | 0 | (없음) | 컬럼이 5개라 안 쓰는 비트 |

`heap_page_items()`의 `t_bits` 열은 같은 바이트를 0번 비트부터 왼쪽에서 오른쪽으로 찍습니다. 2번
튜플이 `11101000`으로 나온 이유입니다. 3번 튜플의 `0f` = 0b00001111은 `11110000`으로 나오고, 4번
비트가 0이니 `born`이 NULL입니다.

NULL인 컬럼은 데이터 부분에 **아무 바이트도 없습니다**. `heap_deform_tuple()`의 루프가 `continue`로
넘어가며 `off`를 건드리지 않는 것은 그 때문입니다. 다음 컬럼이 곧바로 이어집니다.

!!! note "Design note"
    NULL을 특별한 값(예: 모든 비트 1)으로 저장하지 않고 bitmap으로 빼 둔 이유는 두 가지입니다.
    `int4`의 모든 비트 조합이 이미 어떤 정수이므로 NULL로 쓸 값이 남지 않는다는 것, 그리고
    NULL이 많은 행에서 공간이 준다는 것입니다. 그 대신 값을 읽을 때 bitmap을 먼저 봐야 합니다.

---

## 2 · 정렬: 컬럼은 `attalign` 경계에서 시작한다

값이 있는 컬럼은 시작 오프셋을 `attalign` 경계로 올립니다. 위 루프에서 고정 길이 컬럼에 쓰는 것이
`att_nominal_alignby()`입니다.

```c title="src/include/access/tupmacs.h:161"
/*
 * Similar to att_align_nominal, but accepts a number of bytes, typically from
 * CompactAttribute.attalignby to align the offset by.
 */
#define att_nominal_alignby(cur_offset, attalignby) \
    TYPEALIGN(attalignby, cur_offset)
```

`TYPEALIGN(n, off)`는 `off`를 n의 배수로 올리는 매크로입니다. `attalignby`는 `attalign`을 08장 표의
바이트 수(1, 2, 4, 8)로 바꾼 값입니다.

`i`면 4의 배수로, `d`면 8의 배수로, `s`면 2의 배수로 올리고 `c`면 그대로입니다. 올리면서 건너뛴
바이트가 패딩이고, PostgreSQL은 패딩을 **항상 0**으로 채웁니다.

### 2.1 · 예외: 1바이트 헤더 varlena는 정렬하지 않는다

varlena 컬럼(`attlen = -1`)은 `att_pointer_alignby()`를 씁니다. 한 가지가 다릅니다.

```c title="src/include/access/tupmacs.h:125"
/*
 * Similar to att_align_pointer, but accepts a number of bytes, typically from
 * CompactAttribute.attalignby to align the pointer by.
 */
#define att_pointer_alignby(cur_offset, attalignby, attlen, attptr) \
    ( \
    ((attlen) == -1 && VARATT_NOT_PAD_BYTE(attptr)) ? \
    (uintptr_t) (cur_offset) : \
    TYPEALIGN(attalignby, cur_offset))
```

현재 오프셋의 바이트가 패딩이 아니면(`VARATT_NOT_PAD_BYTE`) 정렬하지 않고 그 자리를 그대로 씁니다.
그래도 되는 이유는 주석이 가리키는 `att_align_pointer()`의 설명에 있습니다.

```c title="src/include/access/tupmacs.h:104"
/*
 * att_align_pointer performs the same calculation as att_align_datum,
 * but is used when walking a tuple.  attptr is the current actual data
 * pointer; when accessing a varlena field we have to "peek" to see if we
 * are looking at a pad byte or the first byte of a 1-byte-header datum.
 * (A zero byte must be either a pad byte, or the first byte of a correctly
 * aligned 4-byte length word; in either case we can align safely.  A non-zero
 * byte must be either a 1-byte length word, or the first byte of a correctly
 * aligned 4-byte length word; in either case we need not align.)
 …
 */
```

varlena에는 1바이트 헤더짜리 짧은 값과 4바이트 헤더짜리 긴 값이 있습니다([10장](./10-varlena.md)).
**1바이트 헤더 값은 정렬하지 않고 현재 오프셋에 바로 놓습니다.** 짧은 문자열마다 3바이트씩
패딩을 넣는 낭비를 피하려는 것입니다. 4바이트 헤더 값은 `attalign`대로 정렬합니다.

그러면 읽는 쪽은 어디서 시작하는지 어떻게 알까요? 위 주석을 보면 알 수 있습니다. 현재 오프셋의 바이트를
봅니다. **패딩은 항상 0이고, 1바이트 헤더는 절대 0이 아닙니다**(다음 장에서 보듯 1바이트 헤더는 최하위
비트가 1입니다). 그래서

- 그 바이트가 0이 아니면: 1바이트 헤더이거나, 이미 정렬된 자리에 있는 4바이트 헤더의 첫 바이트입니다.
  어느 쪽이든 정렬 없이 여기서 시작합니다.
- 그 바이트가 0이면: 패딩이거나, 정렬된 자리에 있는 4바이트 헤더의 첫 바이트입니다. 어느 쪽이든
  `attalign` 경계로 올리면 맞는 자리가 나옵니다.

`VARATT_NOT_PAD_BYTE`가 바로 그 검사입니다.

```c title="src/include/varatt.h:221"
#define VARATT_NOT_PAD_BYTE(PTR) \
    (*((uint8 *) (PTR)) != 0)
```

시작 자리가 정해진 뒤, 둘 중 어느 헤더인지는 그 첫 바이트의 최하위 비트로 가립니다(10장).

두 경우를 한 튜플에서 봅시다.

```sql title="psql"
create table pad_demo (flag boolean, word text, essay text);
insert into pad_demo values (true, 'abcd', repeat('y', 150));
checkpoint;
select encode(substring(t_data from 1 for 16), 'hex')
  from heap_page_items(get_raw_page('pad_demo', 0));
```

컬럼 셋을 일부러 이렇게 잡았습니다.

- `flag boolean`: 1바이트짜리를 맨 앞에 두어 다음 컬럼이 오프셋 1, 즉 4의 배수가 아닌 자리에서
  시작하게 만듭니다.
- `word text`에 `'abcd'`: 4글자짜리 짧은 값이라 1바이트 헤더가 붙습니다. 오프셋 1에서 정렬 없이
  바로 시작하는지 보려는 것입니다.
- `essay text`에 `'yyy...y'`(y가 150개): 126바이트를 넘어 4바이트 헤더가 붙습니다. `word`가 끝난
  자리에서 4의 배수까지 패딩이 생기는지 보려는 것입니다.

마지막 `select`는 튜플의 데이터 부분(`t_data`) 처음 16바이트를 16진수로 찍습니다. `essay`는
150바이트라 앞부분만 보면 됩니다.

```
 010b6162636400006802000079797979
```

```
offset  0   1  2  3  4  5   6  7   8  9  10 11  12 ...
      ------------------------------------------------
byte    01  0b 61 62 63 64  00 00  68 02 00 00  79 ...
        ^^  ^^              ^^^^^  ^^^^^^^^^^^
        |   |               |      4-byte header of essay (aligned to 4)
        |   |               two pad bytes (zero)
        |   1-byte header of word: offset 1, NOT aligned
        flag
```

`word`는 `text`라 `attalign`이 `i`인데도 오프셋 1에서 시작했습니다. 첫 바이트 `0b`가 0이 아니므로
정렬하지 않고 그 자리에서 시작했고, `0b`는 홀수라 1바이트 헤더입니다. `word`가 오프셋 6에서 끝난 뒤
`essay`는 4바이트 헤더라 4의 배수인 8까지 패딩 두 바이트(`00 00`)를 두고 시작했습니다. 읽는 쪽에서
오프셋 6의 바이트가 0인 것을 보고 "정렬해야 한다"고 판단하는 것이 `att_pointer_alignby()`입니다.

---

## 3 · 길이만큼 나아간다

시작 자리를 정했으면 컬럼의 길이만큼 `off`를 옮깁니다.

```c title="src/include/access/tupmacs.h:185"
#define att_addlength_pointer(cur_offset, attlen, attptr) \
( \
    ((attlen) > 0) ? \
    ( \
        (cur_offset) + (attlen) \
    ) \
    : (((attlen) == -1) ? \
    ( \
        (cur_offset) + VARSIZE_ANY(attptr) \
    ) \
    …
```

고정 길이면 `attlen`, varlena면 헤더에 적힌 전체 크기(`VARSIZE_ANY`, 헤더 포함)입니다.

---

## 4 · `lab`의 세 튜플 끊기

이제 세 규칙으로 `lab`을 끊어 봅시다. 카탈로그(08장)는 `id` 4/i, `flag` 1/c, `big` 8/d, `name` −1/i,
`born` 4/i입니다. 오프셋은 `t_hoff`(24) 기준입니다.

### 4.1 · 1번 튜플: NULL 없음

```
offset  0  1  2  3   4   5  6  7   8  9  10 11 12 13 14 15  16 17 18 19  20 21 22 23
      -------------------------------------------------------------------------------
byte    01 00 00 00  01  00 00 00  cb 04 fb 71 1f 01 00 00  09 6b 69 6d  79 22 00 00
        ^^^^^^^^^^^  ^^  ^^^^^^^^  ^^^^^^^^^^^^^^^^^^^^^^^  ^^^^^^^^^^^  ^^^^^^^^^^^
        id           flag pad      big                      name         born
```

| 컬럼 | 타입 | `attlen` | `attalign` | 정렬 전 `off` | 정렬 후 | 바이트 | 길이 | 값 |
|---|---|---|---|---|---|---|---|---|
| `id` | integer | 4 | i | 0 | 0 | `01 00 00 00` | 4 | 1 |
| `flag` | boolean | 1 | c | 4 | 4 | `01` | 1 | true |
| `big` | bigint | 8 | d | 5 | **8** (패딩 `00 00 00`) | `cb 04 fb 71 1f 01 00 00` | 8 | 1234567890123 |
| `name` | text | −1 | i | 16 | 16 (`09` ≠ 0, 정렬 안 함) | `09 6b 69 6d` | 4 | 'kim' |
| `born` | date | 4 | i | 20 | 20 | `79 22 00 00` | 4 | 8825 → 2024-02-29 |

끝은 24, `t_hoff` 24를 더하면 튜플 길이 48입니다. `lp_len`과 같습니다. `name`의 첫 바이트 `09`는
1바이트 헤더이고 헤더 포함 길이가 4입니다(10장). `born`은 2000-01-01부터 센 날수입니다.

### 4.2 · 2번 튜플: `name`이 NULL

```
offset  0  1  2  3   4   5  6  7   8  9  10 11 12 13 14 15  16 17 18 19
      -------------------------------------------------------------------
byte    02 00 00 00  00  00 00 00  ff ff ff ff ff ff ff ff  ff ff ff ff
        ^^^^^^^^^^^  ^^  ^^^^^^^^  ^^^^^^^^^^^^^^^^^^^^^^^  ^^^^^^^^^^^
        id           flag pad      big                      born (name is NULL: no bytes)
```

bitmap `17`에서 `name`(비트 3)이 0입니다.

| 컬럼 | 타입 | `attlen` | `attalign` | 정렬 전 | 정렬 후 | 바이트 | 길이 | 값 |
|---|---|---|---|---|---|---|---|---|
| `id` | integer | 4 | i | 0 | 0 | `02 00 00 00` | 4 | 2 |
| `flag` | boolean | 1 | c | 4 | 4 | `00` | 1 | false |
| `big` | bigint | 8 | d | 5 | 8 | `ff ff ff ff ff ff ff ff` | 8 | −1 |
| `name` | text | −1 | i | | | (없음) | 0 | NULL |
| `born` | date | 4 | i | 16 | 16 | `ff ff ff ff` | 4 | −1 → 1999-12-31 |

`name`이 빠지고 `born`이 곧바로 16에 옵니다. 끝 20 + 24 = 44 = `lp_len`.

### 4.3 · 3번 튜플: `born`이 NULL, 한글

```
offset  0  1  2  3   4   5  6  7   8  9  10 11 12 13 14 15  16 17 18 19 20 21 22
      ------------------------------------------------------------------------------
byte    03 00 00 00  01  00 00 00  00 00 00 00 00 00 00 00  0f ed 95 9c ea b8 80
        ^^^^^^^^^^^  ^^  ^^^^^^^^  ^^^^^^^^^^^^^^^^^^^^^^^  ^^^^^^^^^^^^^^^^^^^^
        id           flag pad      big                      name (born is NULL: no bytes)
```

bitmap `0f`에서 `born`(비트 4)이 0입니다.

| 컬럼 | 타입 | `attlen` | `attalign` | 정렬 전 | 정렬 후 | 바이트 | 길이 | 값 |
|---|---|---|---|---|---|---|---|---|
| `id` | integer | 4 | i | 0 | 0 | `03 00 00 00` | 4 | 3 |
| `flag` | boolean | 1 | c | 4 | 4 | `01` | 1 | true |
| `big` | bigint | 8 | d | 5 | 8 | `00 00 00 00 00 00 00 00` | 8 | 0 |
| `name` | text | −1 | i | 16 | 16 | `0f ed 95 9c ea b8 80` | 7 | '한글' |
| `born` | date | 4 | i | | | (없음) | 0 | NULL |

'한글'은 UTF-8로 글자당 3바이트, 여섯 바이트에 헤더 1바이트를 더해 7입니다. 끝 23 + 24 = 47 =
`lp_len`. 마지막 컬럼이 NULL이라 튜플이 홀수 길이로 끝났습니다. 페이지 안에서는 다음 튜플 앞까지
MAXALIGN 패딩이 있지만(04장) 그것은 튜플 길이에 들어가지 않습니다.

`pageinspect`의 `tuple_data_split()`이 같은 일을 해 줍니다. 손으로 끊은 것과 맞춰 봅시다.

```sql title="psql"
select lp, tuple_data_split('lab'::regclass, t_data, t_infomask, t_infomask2, t_bits) as cols
  from heap_page_items(get_raw_page('lab', 0));
```
```
 lp |                                   cols
----+---------------------------------------------------------------------------
  1 | {"\\x01000000","\\x01","\\xcb04fb711f010000","\\x096b696d","\\x79220000"}
  2 | {"\\x02000000","\\x00","\\xffffffffffffffff",NULL,"\\xffffffff"}
  3 | {"\\x03000000","\\x01","\\x0000000000000000","\\x0fed959ceab880",NULL}
(3 rows)
```

패딩은 빠지고 컬럼 바이트만 남습니다. varlena는 헤더 바이트(`09`, `0f`)를 포함한 채로 나옵니다.

!!! danger "Trap"
    정렬은 튜플의 시작이 아니라 **데이터의 시작(`t_hoff`)** 기준으로 세는데, 그 값을 페이지
    기준으로 그대로 써도 됩니다. 07장에서 본 대로 `t_hoff`가 8의 배수이고 튜플 자체가 8의 배수 오프셋에
    있기 때문입니다. 이 둘 중 하나라도 아니었다면 튜플 안 오프셋과 페이지 안 오프셋의 정렬이 달라
    계산이 틀렸을 것입니다.

---

## 5 · 정리

- NULL인 컬럼은 null bitmap의 비트 0으로만 표시되고 데이터 부분에 바이트가 없습니다. 비트 i는
  `bits[i >> 3] & (1 << (i & 7))`이고, 1이 "값 있음"입니다.
- 컬럼은 `attalign` 경계(c 1, s 2, i 4, d 8)로 올린 오프셋에서 시작합니다. 건너뛴 패딩은 0입니다.
- 예외: 1바이트 헤더 varlena는 정렬하지 않습니다. 현재 오프셋의 바이트가 0이 아니면 거기가
  시작이고, 0이면 정렬합니다.
- 길이는 고정 길이면 `attlen`, varlena면 헤더에 적힌 전체 크기입니다.
