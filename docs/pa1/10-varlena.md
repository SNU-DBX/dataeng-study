# 10 · 가변 길이 값(varlena)

!!! abstract "목표"
    `attlen = -1`인 컬럼의 값이 튜플 안에 어떻게 놓이는지 이해합니다. 값 앞에 붙는 1바이트 또는
    4바이트 헤더에서 전체 길이를 꺼내는 법, 둘을 첫 바이트로 구분하는 법, `text`·`varchar`·
    `char(n)`·`numeric`이 각각 어떻게 보이는지를 살펴봅니다. 이 장이 끝나면 varlena의 첫 바이트를
    보고 헤더 크기와 전체 길이를 말할 수 있어야 합니다.

[지난 장](./09-null-bitmap-and-alignment.md)에서 `lab`의 컬럼 다섯 중 넷은 `attlen`이 정한
길이대로 끊었습니다. `text`인 `name`만은 `attlen`이 −1이라 카탈로그에 길이가 없어서, 1번 튜플
(`name` = 'kim')에서는 `09 6b 69 6d` 4바이트, 3번 튜플(`name` = '한글')에서는 `0f ed 95 9c ea b8 80`
7바이트라는 결과만 받아들이고 넘어갔습니다. 그 4와 7이 어디서 나오는지를 이 장에서 알아봅니다. 가변
길이 값은 자기 길이를 값의 첫머리, 헤더에 들고 있습니다.

---

## 1 · 두 종류의 헤더

varlena 값은 헤더와 데이터로 되어 있고, 헤더에 **헤더를 포함한** 전체 길이가 적혀 있습니다. 헤더는
두 종류로, 짧은 값에는 1바이트 헤더가, 긴 값에는 4바이트 헤더가 붙습니다.

```c title="src/include/varatt.h:111"
typedef union
{
    struct                      /* Normal varlena (4-byte length) */
    {
        uint32      va_header;
        char        va_data[FLEXIBLE_ARRAY_MEMBER];
    }           va_4byte;
    …
} varattrib_4b;
```
```c title="src/include/varatt.h:127"
typedef struct
{
    uint8       va_header;
    char        va_data[FLEXIBLE_ARRAY_MEMBER]; /* Data begins here */
} varattrib_1b;
```

어느 쪽인지는 첫 바이트의 최하위 비트가 정합니다. little-endian 기준의 규칙이 헤더 파일 주석에
있습니다.

```c title="src/include/varatt.h:149"
 * Bit layouts for varlena headers on little-endian machines:
 *
 * xxxxxx00 4-byte length word, aligned, uncompressed data (up to 1G)
 * …
 * xxxxxxx1 1-byte length word, unaligned, uncompressed data (up to 126b)
 *
 * The "xxx" bits are the length field (which includes itself in all cases).
 * …
 * Also, it is not possible for a 1-byte length word to be zero;
 * this lets us disambiguate alignment padding bytes from the start of an
 * unaligned datum.  (We now *require* pad bytes to be filled with zero!)
```

| 첫 바이트 | 헤더 | 길이 필드 | 담을 수 있는 데이터 |
|---|---|---|---|
| 최하위 비트 1 (홀수) | 1바이트 | 상위 7비트 | 126바이트까지 |
| 최하위 비트 0 (짝수) | 4바이트 | 32비트 값의 상위 30비트 | 1 GB까지 |

매크로는 이렇습니다.

```c title="src/include/varatt.h:217"
#define VARATT_IS_1B(PTR) \
    ((((varattrib_1b *) (PTR))->va_header & 0x01) == 0x01)
```
```c title="src/include/varatt.h:225"
#define VARSIZE_4B(PTR) \
    ((((varattrib_4b *) (PTR))->va_4byte.va_header >> 2) & 0x3FFFFFFF)
#define VARSIZE_1B(PTR) \
    ((((varattrib_1b *) (PTR))->va_header >> 1) & 0x7F)
```

계산으로 쓰면 이렇습니다. `b`는 첫 바이트, `w`는 첫 4바이트를 little-endian `uint32`로 읽은
값입니다.

```
1-byte header:  b & 1 == 1      total = b >> 1          (1 .. 127)
4-byte header:  b & 1 == 0      total = w >> 2          (4 .. 1 GB)
```

`total`은 헤더를 포함한 바이트 수입니다. 데이터는 그 뒤 `total − 1` 또는 `total − 4`바이트입니다.
1바이트 헤더의 길이 필드가 0이 될 수 없다는 것(값이 최소 1, 헤더 자신)이 지난 장의 "패딩 0과
구분된다"는 규칙의 근거입니다.

!!! note "Design note"
    헤더가 두 종류인 이유는 짧은 값이 압도적으로 많기 때문입니다. 'kim' 세 글자에 4바이트 헤더와
    정렬 패딩까지 붙이면 값보다 부가 정보가 큽니다. 1바이트 헤더는 그 낭비를 줄이고, 대신 최하위
    비트 하나를 종류 표시에 씁니다. 4바이트 헤더는 하위 2비트를 표시에 쓰므로 길이 필드가 30비트,
    최대 1 GB입니다.

---

## 2 · 실제 값으로

세 종류의 문자열 타입과 `numeric`을 넣은 테이블입니다. 1번 행은 짧은 값만, 2번 행은 `note`에
`'xxx...x'`(x가 200개)라는 긴 값을, 3번 행은 `note`에 빈 문자열을 넣습니다.

```sql title="psql"
create table vl_demo (id int, note text, code varchar(10), tag char(5), price numeric(10,2));
insert into vl_demo values (1, 'abc', 'abc', 'abc', 12.34),
                           (2, repeat('x', 200), 'hello', 'hi', -0.5),
                           (3, '', 'q', 'q', 0);
checkpoint;
select lp, lp_len, t_hoff from heap_page_items(get_raw_page('vl_demo', 0));
```
```
 lp | lp_len | t_hoff
----+--------+--------
  1 |     49 |     24
  2 |    249 |     24
  3 |     40 |     24
(3 rows)
```

카탈로그를 보면 `id`를 뺀 네 컬럼이 전부 `attlen` −1, `attalign` `i`입니다.

```sql title="psql"
select attnum, attname, format_type(atttypid, atttypmod) as type, attlen, attalign
  from pg_attribute
 where attrelid = 'vl_demo'::regclass and attnum > 0
 order by attnum;
```
```
 attnum | attname |         type          | attlen | attalign
--------+---------+-----------------------+--------+----------
      1 | id      | integer               |      4 | i
      2 | note    | text                  |     -1 | i
      3 | code    | character varying(10) |     -1 | i
      4 | tag     | character(5)          |     -1 | i
      5 | price   | numeric(10,2)         |     -1 | i
(5 rows)
```

### 2.1 · 1번 행: 전부 1바이트 헤더

```sql title="psql"
select lp, t_data from heap_page_items(get_raw_page('vl_demo', 0)) where lp = 1;
```
```
 lp |                        t_data
----+------------------------------------------------------
  1 | \x0100000009616263096162630d61626320200f00810c00480d
(1 row)
```

```
offset  0  1  2  3   4  5  6  7   8  9  10 11  12 13 14 15 16 17  18 19 20 21 22 23 24
      ---------------------------------------------------------------------------------
byte    01 00 00 00  09 61 62 63  09 61 62 63  0d 61 62 63 20 20  0f 00 81 0c 00 48 0d
        ^^^^^^^^^^^  ^^^^^^^^^^^  ^^^^^^^^^^^  ^^^^^^^^^^^^^^^^^  ^^^^^^^^^^^^^^^^^^^^
        id = 1       note 'abc'   code 'abc'   tag 'abc  '        price 12.34
                     1-byte hdr   1-byte hdr   1-byte hdr         1-byte hdr
                                               (char(5): padded with 2 spaces)
```

| 컬럼 | 첫 바이트 | 최하위 비트 | 전체 길이 = 첫 바이트 >> 1 | 데이터 |
|---|---|---|---|---|
| `note` text | `09` | 1 | 4 | `61 62 63` = 'abc' |
| `code` varchar(10) | `09` | 1 | 4 | `61 62 63` = 'abc' |
| `tag` char(5) | `0d` | 1 | 6 | `61 62 63 20 20` = 'abc  ' |
| `price` numeric(10,2) | `0f` | 1 | 7 | `00 81 0c 00 48 0d` |

세 가지가 보입니다.

- `text`와 `varchar(10)`은 튜플 안에서 **똑같습니다**. 길이 제한은 값을 넣을 때만 검사하고 저장
  형식에는 흔적이 없습니다.
- `char(5)`는 'abc'를 공백으로 5글자까지 채워 저장합니다. 그래서 헤더 1 + 5 = 6입니다.
- `numeric`도 varlena입니다. 6바이트 안의 형식은 `numeric` 타입의 것이라 여기서 풀지 않습니다.
  헤더에서 길이를 꺼내 바이트를 통째로 넘기면 됩니다.

정렬을 확인하면, 각 값이 앞 값 바로 뒤에 붙어 있습니다. `note`는 오프셋 4, `code`는 8, `tag`는 12,
`price`는 18에서 시작합니다. `price`의 18은 4의 배수가 아니지만 첫 바이트 `0f`가 0이 아니므로
정렬하지 않습니다(지난 장 2.1절). 합계는 4 + 4 + 4 + 6 + 7 = 25이고, `t_hoff` 24를 더하면 49로
`lp_len`과 같습니다.

### 2.2 · 2번 행: 4바이트 헤더

`note`에 `'xxx...x'`(x가 200개)를 넣은 행입니다. 값이 길어서 데이터 처음 24바이트만 봅니다.

```sql title="psql"
select encode(substring(t_data from 1 for 24), 'hex')
  from heap_page_items(get_raw_page('vl_demo', 0)) where lp = 2;
```
```
 020000003003000078787878787878787878787878787878
```

```
offset  0  1  2  3   4  5  6  7   8  9  10 11 12 ...
      ------------------------------------------------
byte    02 00 00 00  30 03 00 00  78 78 78 78 78 ...   (200 bytes of 'x')
        ^^^^^^^^^^^  ^^^^^^^^^^^  ^^^^^^^^^^^^^^^^^^
        id = 2       4-byte hdr   note data
                     of note
```

첫 바이트 `30`은 짝수라 4바이트 헤더입니다. 네 바이트를 little-endian으로 읽으면 `w` = 0x00000330 =
816이고, `total = 816 >> 2 = 204` = 헤더 4 + 데이터 200입니다. `note`가 오프셋 4에서 시작한 것은
4의 배수라 정렬 조건을 이미 만족해서입니다.

그 뒤 세 컬럼은 오프셋 4 + 204 = 208부터 이어집니다.

```sql title="psql"
select encode(substring(t_data from 209 for 40), 'hex')
  from heap_page_items(get_raw_page('vl_demo', 0)) where lp = 2;
```
```
 0d68656c6c6f0d68692020200b7fa18813
```

```
offset  208                214                220
      --------------------------------------------------
byte    0d 68 65 6c 6c 6f  0d 68 69 20 20 20  0b 7f a1 88 13
        ^^^^^^^^^^^^^^^^^  ^^^^^^^^^^^^^^^^^  ^^^^^^^^^^^^^^
        code 'hello'       tag 'hi   '        price -0.5
        1-byte hdr         1-byte hdr         1-byte hdr
```

`substring`이 1부터 세므로 오프셋 208은 `from 209`입니다. `code`는 `0d` → 6바이트('hello'), `tag`는
`0d` → 6바이트('hi   '), `price`는 `0b` → 5바이트입니다. 합계는 4 + 204 + 6 + 6 + 5 = 225이고,
`t_hoff` 24를 더하면 249로 `lp_len`과 같습니다.

### 2.3 · 3번 행: 빈 문자열

```sql title="psql"
select lp, t_data from heap_page_items(get_raw_page('vl_demo', 0)) where lp = 3;
```
```
 lp |               t_data
----+------------------------------------
  3 | \x030000000305710d7120202020070081
(1 row)
```

```
offset  0  1  2  3   4   5  6   7  8  9  10 11 12  13 14 15
      -----------------------------------------------------
byte    03 00 00 00  03  05 71  0d 71 20 20 20 20  07 00 81
        ^^^^^^^^^^^  ^^  ^^^^^  ^^^^^^^^^^^^^^^^^  ^^^^^^^^
        id = 3       |   |      tag 'q    '        price 0
                     |   code 'q'
                     note '' (header only)
```

빈 문자열 `''`은 `03` 한 바이트입니다. `3 >> 1 = 1`, 헤더 하나뿐이고 데이터가 0바이트입니다. NULL과
다릅니다. NULL은 null bitmap에 표시되고 바이트가 없지만(09장), 빈 문자열은 값이 있고 그 값의 길이가
0인 것입니다.

---

## 3 · 헤더 뒤의 바이트

헤더를 벗기고 나면 남는 것은 타입마다 정해진 바이트입니다.

| 타입 | 데이터 바이트 |
|---|---|
| `text`, `varchar(n)` | 문자열 그대로. 인코딩은 데이터베이스 인코딩(보통 UTF-8). 끝에 NUL 없음 |
| `char(n)` | n글자까지 공백으로 채운 문자열 |
| `numeric` | `numeric` 타입의 내부 형식 |

`lab`의 `name` 컬럼으로 돌아가면, `09 6b 69 6d`는 1바이트 헤더(총 4바이트)에 'kim'이고, `0f ed 95
9c ea b8 80`은 1바이트 헤더(총 7바이트)에 '한글'의 UTF-8 여섯 바이트입니다.

---

## 4 · 정리

- varlena는 헤더 + 데이터이고 헤더에 헤더 포함 전체 길이가 있습니다.
- 첫 바이트가 홀수면 1바이트 헤더, `total = b >> 1`(최대 127). 짝수면 4바이트 헤더, `total = w >> 2`.
- `text`와 `varchar`는 저장 형식이 같고, `char(n)`은 공백으로 채워지며, `numeric`은 자기 형식의
  바이트입니다. 빈 문자열은 헤더만 있는 1바이트 값이고 NULL과 다릅니다.
- 1바이트 헤더는 0이 될 수 없습니다. 이것이 정렬 패딩(항상 0)과 구분하는 근거입니다.
