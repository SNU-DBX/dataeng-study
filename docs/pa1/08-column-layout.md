# 08 · 컬럼 레이아웃은 카탈로그가 정한다

!!! abstract "목표"
    튜플의 데이터 부분에는 컬럼 이름도, 타입도 적혀 있지 않습니다. 컬럼이 어떤 순서로 놓이고 몇
    바이트이며 어디서 시작할 수 있는지는 카탈로그 `pg_attribute`가 정합니다. 다만 가변 길이 컬럼은
    실제 길이를 값이 직접 들고 있고(10장), 짧은 값은 정렬하지 않고 놓입니다(09장). 이 장이 끝나면
    `pg_attribute`의 세 열 `attnum`, `attlen`, `attalign`이 각각 무엇을 정하는지 말할 수 있어야
    합니다.

[지난 장](./07-tuple-header.md)에서 `t_hoff`가 24라는 것까지 읽었습니다. `lab` 1번 튜플은 전체
48바이트이고, 컬럼 값은 `t_hoff`가 가리키는 24번 바이트부터 끝까지, 24바이트입니다.

```
01 00 00 00 01 00 00 00 cb 04 fb 71 1f 01 00 00 09 6b 69 6d 79 22 00 00
```

이 24바이트에 다섯 컬럼이 들어 있는데, 어디서 끊어야 하는지 바이트만 봐서는 알 수 없습니다. 튜플
안에는 컬럼 정보가 없습니다. 컬럼 정보는 카탈로그에 테이블 단위로 저장되어 있고, 모든 튜플이 그것을
공유합니다.

---

## 1 · `pg_attribute`

테이블의 컬럼 하나가 `pg_attribute`의 행 하나입니다.

```sql title="psql"
select attnum, attname, format_type(atttypid, atttypmod) as type, attlen, attalign
  from pg_attribute
 where attrelid = 'lab'::regclass and attnum > 0
 order by attnum;
```
```
 attnum | attname |  type   | attlen | attalign
--------+---------+---------+--------+----------
      1 | id      | integer |      4 | i
      2 | flag    | boolean |      1 | c
      3 | big     | bigint  |      8 | d
      4 | name    | text    |     -1 | i
      5 | born    | date    |      4 | i
(5 rows)
```

`attrelid`는 02장에서 본 테이블의 oid이고, `attnum > 0`은 사용자가 만든 컬럼만 고르는 조건입니다.
튜플을 읽는 데 필요한 것은 세 열입니다.

| 열 | 정하는 것 |
|---|---|
| `attnum` | **순서**. 값은 튜플 안에 `attnum` 순서대로 놓인다 |
| `attlen` | **길이**. 양수면 고정 길이의 바이트 수, −1이면 가변 길이 |
| `attalign` | **정렬**. 값이 시작할 수 있는 오프셋의 배수 |

`pg_attribute.h`의 주석이 말하듯 `attlen`과 `attalign`은 `pg_type`의 `typlen`, `typalign`을 컬럼마다
복사해 둔 것입니다.

```c title="src/include/catalog/pg_attribute.h:55"
    /*
     * attlen is a copy of the typlen field from pg_type for this attribute.
     * See atttypid comments above.
     */
    int16       attlen;
```
```c title="src/include/catalog/pg_attribute.h:96"
    /*
     * attalign is a copy of the typalign field from pg_type for this
     * attribute.  See atttypid comments above.
     */
    char        attalign;
```

---

## 2 · `attlen`: 고정 길이와 가변 길이

`attlen > 0`인 컬럼은 튜플 안에서 정확히 그만큼의 바이트를 차지합니다. `bool`은 1, `int4`는 4,
`int8`은 8입니다. 바이트 그대로가 값이고, 정수는 little-endian입니다.

`attlen = -1`인 컬럼은 **varlena**(variable-length attribute)입니다. 값마다 길이가 달라서, 튜플 안의
값 자체가 자기 길이를 헤더에 들고 있습니다. `text`, `varchar`, `char(n)`, `numeric`이 여기 속합니다.
그 헤더를 읽는 법은 [10장](./10-varlena.md)에서 다룹니다.

자주 쓰는 타입의 `typlen`과 `typalign`을 봅시다.

```sql title="psql"
select typname, typlen, typalign
  from pg_type
 where typname in ('bool','int2','int4','int8','float4','float8','date',
                   'timestamp','timestamptz','text','varchar','bpchar','numeric')
 order by typlen, typname;
```
```
   typname   | typlen | typalign
-------------+--------+----------
 bpchar      |     -1 | i
 numeric     |     -1 | i
 text        |     -1 | i
 varchar     |     -1 | i
 bool        |      1 | c
 int2        |      2 | s
 date        |      4 | i
 float4      |      4 | i
 int4        |      4 | i
 float8      |      8 | d
 int8        |      8 | d
 timestamp   |      8 | d
 timestamptz |      8 | d
(13 rows)
```

`bpchar`가 `char(n)`입니다. `char(5)`처럼 길이를 정해도 튜플 안에서는 가변 길이 값으로 저장됩니다.

---

## 3 · `attalign`: 값이 시작할 수 있는 자리

`attalign`은 글자 하나입니다.

| `attalign` | 정렬 | 시작 오프셋 | 타입 |
|---|---|---|---|
| `c` | char | 아무 데나 | `bool` |
| `s` | short | 2의 배수 | `int2` |
| `i` | int | 4의 배수 | `int4`, `float4`, `date`, `text`, `varchar`, `numeric` |
| `d` | double | 8의 배수 | `int8`, `float8`, `timestamp` |

CPU가 4바이트 정수를 4의 배수 주소에서, 8바이트 정수를 8의 배수 주소에서 읽어야 빠르기 때문입니다(C
구조체의 패딩과 같은 이유입니다). 그래서 `bool` 뒤에 `int8`이 오면 사이에 패딩이 생깁니다. `lab` 1번
튜플의 데이터 부분은 `01 00 00 00 01 00 00 00`으로 시작합니다. 앞 4바이트는 `id` = 1이고, 뒤
4바이트는 `flag`의 `01` 한 바이트와 패딩 `00 00 00` 세 바이트입니다. 다음 컬럼 `big`을 8의 배수
자리에 놓으려고 비운 것입니다.

`text`처럼 가변 길이인 타입에는 예외가 있습니다. 짧은 값은 `attalign`과 상관없이 정렬하지 않고
놓입니다. 이 예외는 09장 2.1절에서 다룹니다.

[다음 장](./09-null-bitmap-and-alignment.md)에서는 이 정렬 규칙을 실제로 적용해 튜플을 끊어 봅니다.

---

## 4 · 정리

- 튜플의 데이터 부분은 값만 이어져 있고, 컬럼의 순서·길이·정렬은 `pg_attribute`의 `attnum`,
  `attlen`, `attalign`이 정합니다. 가변 길이 컬럼의 실제 길이는 값의 헤더에 있습니다.
- `attlen > 0`은 고정 길이, `-1`은 가변 길이(varlena)입니다.
- `attalign`은 `c`/`s`/`i`/`d`이고 각각 1/2/4/8의 배수 오프셋에서 값이 시작합니다. 짧은 가변 길이
  값은 예외로 정렬하지 않습니다(09장).
