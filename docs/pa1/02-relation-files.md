# 02 · PGDATA 안의 테이블 파일

!!! abstract "목표"
    PostgreSQL이 테이블 하나를 디스크의 어떤 파일에 두는지, 그 파일 이름이 어디서 오는지, 그리고 테이블이
    커지면 왜 파일이 여러 개로 갈라지는지 이해합니다. 이 장이 끝나면 테이블 이름에서 출발해 디스크의
    파일들까지 찾아갈 수 있어야 합니다.

PostgreSQL의 테이블은 결국 디스크의 파일입니다. 테이블 하나는 데이터 디렉터리 안의 파일 몇 개이고,
그 파일 이름은 카탈로그에 적혀 있으며, SQL 함수 하나로 물어볼 수 있습니다. 이 장에서는 테이블
이름에서 그 파일까지 가는 길을 PostgreSQL 소스와 함께 따라가 봅니다.

---

## 1 · 테이블은 파일들이다

[01장](./01-environment-setup.md)에서 만든 `lab` 테이블(`id`, `flag`, `big`, `name`, `born` 다섯
컬럼, 세 행)을 11장까지 계속 따라갑니다. 이 테이블이 어느 파일에 있는지 PostgreSQL에게 직접 물어봅시다.

```sql title="psql"
show data_directory;
select pg_relation_filepath('lab');
```
```
 data_directory
----------------
 …/pgdata
(1 row)

 pg_relation_filepath
----------------------
 base/16384/132971
(1 row)
```

두 값을 이어 붙인 `…/pgdata/base/16384/132971`이 `lab`의 파일입니다. 데이터 디렉터리는 `initdb`한
곳이고, 숫자는 서버마다 다르게 나오지만 모양은 같습니다. 실제로 그 자리에 가 보면 파일이 있습니다.

```sh title="shell"
ls -l $PGBASE/pgdata/base/16384/132971*
```
```
-rw------- 1 user user 8192 Sep 17 14:38 .../base/16384/132971
```

이 파일이 **행이 들어 있는 파일**이고, PostgreSQL은 이것을 main fork라고 부릅니다. 테이블을 쓰다
보면 같은 이름에 `_fsm`(free space map), `_vm`(visibility map)이 붙은 보조 파일이 옆에 더 생깁니다
(3절의 큰 테이블에서 볼 수 있습니다). 실제 데이터는 main fork에만 있습니다.

경로의 세 조각은 각각 뜻이 있습니다.

| 조각 | 뜻 | 어디서 오나 |
|---|---|---|
| `base/` | 기본 테이블스페이스. 테이블을 만들 때 위치를 따로 지정하지 않으면 여기다 | 고정 |
| `16384/` | 데이터베이스의 oid. `pg_database`에 있다 | `select oid from pg_database where datname = current_database()` |
| `132971` | 테이블의 relfilenode. `pg_class`에 있다 | `select relfilenode from pg_class where relname = 'lab'` |

이 문자열을 만드는 함수가 `GetRelationPath()`입니다. 기본 테이블스페이스에 있는 보통 테이블이라면
아래 한 줄로 끝납니다.

```c title="src/common/relpath.c:143"
GetRelationPath(Oid dbOid, Oid spcOid, RelFileNumber relNumber,
                int procNumber, ForkNumber forkNumber)
{
    …
    else if (spcOid == DEFAULTTABLESPACE_OID)
    {
        /* The default tablespace is {datadir}/base */
        if (procNumber == INVALID_PROC_NUMBER)
        {
            if (forkNumber != MAIN_FORKNUM)
            {
                sprintf(rp.str, "base/%u/%u_%s",  /* ← _fsm, _vm 는 여기 */
                        dbOid, relNumber,
                        forkNames[forkNumber]);
            }
            else
                sprintf(rp.str, "base/%u/%u",     /* ← main fork: 접미사 없음 */
                        dbOid, relNumber);
        }
        …
```

`forkNames[]`는 같은 파일 위쪽에 있습니다. `main`은 이름표가 있지만 파일 이름에는 붙지 않습니다.

```c title="src/common/relpath.c:33"
const char *const forkNames[] = {
    [MAIN_FORKNUM] = "main",
    [FSM_FORKNUM] = "fsm",
    [VISIBILITYMAP_FORKNUM] = "vm",
    [INIT_FORKNUM] = "init",
};
```

`pg_relation_filepath()`는 이 함수를 SQL에서 부를 수 있게 감싼 것입니다
(`src/backend/utils/adt/dbsize.c:972`). `pg_class`에서 `relfilenode`와 `reltablespace`를 꺼내
`GetRelationPath()`에 넘깁니다. 테이블의 절대 경로는 이 함수의 결과에 데이터 디렉터리를 앞에 붙인
것입니다.

---

## 2 · oid와 relfilenode는 다른 것이다

경로의 세 조각 중 relfilenode는 `pg_class`에서 꺼냈습니다. 그런데 `pg_class`에는 테이블을 가리키는
번호가 둘 있습니다. 테이블의 oid와 relfilenode입니다.

```sql title="psql"
select oid, relfilenode from pg_class where relname = 'lab';
```
```
  oid   | relfilenode
--------+-------------
 132971 |      132971
(1 row)
```

처음에는 둘이 같습니다. 그래서 "oid가 파일 이름"이라고 외우기 쉬운데, 그러면 언젠가 틀립니다.

- **oid**는 카탈로그 안에서 테이블을 가리키는 번호입니다. 테이블이 살아 있는 동안 절대 바뀌지 않습니다.
  `pg_attribute`가 컬럼을 이 테이블에 묶을 때도 oid를 씁니다.
- **relfilenode**는 디스크의 파일 이름입니다. `TRUNCATE`처럼 테이블의 파일을 통째로 새로 만드는
  명령을 거치면 **새 번호**로 바뀝니다. 옛 파일은 지워지고 같은 oid가 새 파일을 가리키게 됩니다.

`lab`은 뒤의 장에서 계속 쓰니 버릴 테이블로 확인해 봅시다.

```sql title="psql"
create table scratch as select 1 as x;
select oid, relfilenode from pg_class where relname = 'scratch';
truncate scratch;
select oid, relfilenode from pg_class where relname = 'scratch';
drop table scratch;
```
```
  oid   | relfilenode
--------+-------------
 133049 |      133049
(1 row)

  oid   | relfilenode
--------+-------------
 133049 |      133052
(1 row)
```

oid는 133049 그대로이고, 파일 이름인 relfilenode만 133049에서 133052로 바뀌었습니다.

!!! danger "Trap"
    relfilenode는 테이블의 수명 동안 바뀔 수 있는 값이라 oid로 계산할 수도, 한 번 보고 외워 둘 수도
    없습니다. PostgreSQL 자신도 파일을 열 때마다 `pg_class`에서 온 값을 씁니다.

---

## 3 · 파일은 1 GB마다 갈라진다

`lab`은 8 KB짜리 파일 하나였습니다. 테이블이 커지면 파일이 하나로 남지 않습니다. 1.2 GB쯤 되는 테이블을
하나 만들어 봅시다. 행이 천육백만 개라 몇십 초 걸립니다.

```sql title="psql"
create table seg_demo as
  select g as id, g % 1000 as grp, md5(g::text) as hash, ((g % 100000) / 7.0)::numeric(10,2) as amount
    from generate_series(1, 16000000) g;
vacuum analyze seg_demo;
checkpoint;
select pg_relation_filepath('seg_demo');
```
```
 pg_relation_filepath
----------------------
 base/16384/133056
(1 row)
```

`vacuum analyze`는 4절에서 볼 통계값을 채워 두려는 것입니다.

```sh title="shell"
ls -l $PGBASE/pgdata/base/16384/133056*
```
```
-rw------- 1 user user 1073741824 Sep 18 16:36 .../base/16384/133056
-rw------- 1 user user  185073664 Sep 18 16:36 .../base/16384/133056.1
-rw------- 1 user user     327680 Sep 18 16:36 .../base/16384/133056_fsm
-rw------- 1 user user      40960 Sep 18 16:36 .../base/16384/133056_vm
```

main fork가 `133056`과 `133056.1` 두 파일이고, `133056_fsm`과 `133056_vm`이 1절에서 말한 보조
파일입니다. 첫 파일은 정확히 1,073,741,824바이트, 즉 1 GiB에서
멈췄고 나머지는 `.1`로 넘어갔습니다. 이 조각 하나를 **세그먼트(segment)** 라고 부릅니다. 테이블이 더
커지면 `.2`, `.3`, … 이 계속 생깁니다.

세그먼트 하나의 크기는 컴파일 시점에 정해진 상수입니다.

```c title="src/include/pg_config.h:649"
#define RELSEG_SIZE 131072       /* ← 세그먼트 하나에 들어가는 블록 수 */
```
```c title="src/include/pg_config.h:32"
#define BLCKSZ 8192              /* ← 블록 하나의 바이트 수. 다음 장에서 다룬다 */
```

131072 × 8192 = 1,073,741,824. 위 `ls` 결과의 첫 파일 크기와 같습니다.

!!! note "Design note"
    세그먼트는 운영체제가 파일 하나의 크기에 한도(흔히 2 GB)를 두던 시절, 그보다 큰 테이블을 담으려고
    만든 것입니다. 지금은 그런 제약이 거의 없지만 기본값은 여전히 1 GB입니다. 그래서 "테이블 하나 =
    파일 여러 개"는 PostgreSQL의 파일을 다루는 코드가 늘 전제하는 규칙입니다.

세그먼트 파일의 이름은 `_mdfd_segpath()`가 만듭니다. 규칙은 "0번은 접미사 없음, 나머지는
`.번호`"가 전부입니다.

```c title="src/backend/storage/smgr/md.c:1681"
_mdfd_segpath(SMgrRelation reln, ForkNumber forknum, BlockNumber segno)
{
    RelPathStr  path;
    MdPathStr   fullpath;

    path = relpath(reln->smgr_rlocator, forknum);         /* ← 1절의 base/<db>/<relfilenode> */

    if (segno > 0)
        sprintf(fullpath.str, "%s.%u", path.str, segno);  /* ← segno 1, 2, … */
    else
        strcpy(fullpath.str, path.str);                   /* ← segno 0 */

    return fullpath;
}
```

---

## 4 · 세그먼트가 몇 개인지는 어떻게 아는가

디렉터리에는 `133056`과 `133056.1`만 있습니다. `133056.2`는 없습니다. 어디까지 열어야 할지는 파일
시스템에 물어보며 알아냅니다. 이 규칙을 정하는 함수가 `mdnblocks()`입니다. PostgreSQL이 "이 테이블은
블록이 몇 개인가"를 셀 때 부르는 함수인데, 세그먼트를 차례로 열어 가며 셉니다.

```c title="src/backend/storage/smgr/md.c:1224"
mdnblocks(SMgrRelation reln, ForkNumber forknum)
{
    …
    for (;;)
    {
        nblocks = _mdnblocks(reln, forknum, v);                      /* ← 이 세그먼트의 블록 수 */
        if (nblocks > ((BlockNumber) RELSEG_SIZE))
            elog(FATAL, "segment too big");                          /* ← 1 GB 를 넘는 조각은 없다 */
        if (nblocks < ((BlockNumber) RELSEG_SIZE))
            return (segno * ((BlockNumber) RELSEG_SIZE)) + nblocks;  /* ← 짧으면 마지막 */

        /*
         * If segment is exactly RELSEG_SIZE, advance to next one.
         */
        segno++;
        …
        v = _mdfd_openseg(reln, forknum, segno, 0);
        if (v == NULL)
            return segno * ((BlockNumber) RELSEG_SIZE);              /* ← 다음 파일이 없으면 끝 */
    }
}
```

루프에서 읽어 낼 규칙은 세 개입니다.

1. **131072블록보다 작은 세그먼트가 마지막입니다.** 어떤 세그먼트가 131072블록보다 작으면 그것이
   마지막이고, 뒤는 보지 않습니다. 앞쪽 세그먼트는 모두 정확히 131072블록입니다.
2. **다음 파일이 없으면 거기서 끝납니다.** `_mdfd_openseg()`가 `NULL`을 돌려주는 것은 에러가
   아닙니다. 테이블이 그만큼이라는 뜻입니다. 단, 0번 세그먼트만은 반드시 있어야 합니다.
3. **세그먼트가 131072블록보다 크면 잘못된 파일입니다.** PostgreSQL은 에러로 처리합니다.

세그먼트 하나의 블록 수는 파일 크기를 8192로 나눈 몫입니다.

```c title="src/backend/storage/smgr/md.c:1873"
_mdnblocks(SMgrRelation reln, ForkNumber forknum, MdfdVec *seg)
{
    off_t       len;

    len = FileSize(seg->mdfd_vfd);        /* ← 파일 크기. 파일 끝의 오프셋을 잰다 */
    …
    /* note that this calculation will ignore any partial block at EOF */
    return (BlockNumber) (len / BLCKSZ);  /* ← 나머지는 버린다 */
}
```

주석이 말하듯 8192로 나누어떨어지지 않고 남는 바이트는 **블록으로 세지 않습니다**. 정상적으로
만들어진 파일의 크기는 항상 8192의 배수라 남는 바이트가 없지만, 계산은 몫만 씁니다.

위 `seg_demo`에 이 규칙을 적용해 봅시다.

| 파일 | 크기 | ÷ 8192 | 판정 |
|---|---|---|---|
| `133056` | 1,073,741,824 | 131072 | 꽉 찼다. 다음 세그먼트를 본다 |
| `133056.1` | 185,073,664 | 22592 | 131072보다 작다. 마지막이다 |
| `133056.2` | 없음 | | 보지 않는다 |

블록 수는 131072 + 22592 = **153664**입니다. `pg_class.relpages`와 맞춰 보면 같은 값이 나옵니다.

```sql title="psql"
select relpages from pg_class where relname = 'seg_demo';
```
```
 relpages
----------
   153664
(1 row)
```

!!! note "Design note"
    `relpages`는 통계값이라 `ANALYZE`나 `VACUUM` 같은 작업이 돌아야 갱신됩니다. 테이블을 막 키운
    직후에는 파일 크기와 어긋날 수 있습니다. 진짜 블록 수는 항상 **파일 크기에서** 옵니다.
    `mdnblocks()`가 카탈로그를 보지 않고 파일 크기를 직접 재는 이유입니다.

---

## 5 · 정리

- 테이블 하나의 행은 `<PGDATA>/base/<dboid>/<relfilenode>` 파일(main fork)에 있습니다. `_fsm`, `_vm`은
  보조 파일입니다.
- `relfilenode`는 파일 이름이고 테이블의 `oid`는 카탈로그 번호입니다. 처음엔 같지만 `TRUNCATE` 뒤에는 달라집니다.
  경로는 매번 `pg_relation_filepath()`로 얻어야 합니다.
- 파일은 131072블록(1 GiB)마다 `.1`, `.2`, … 세그먼트로 갈라집니다. 앞쪽은 전부 꽉 차 있고
  마지막만 짧을 수 있으며, 다음 파일이 없으면 거기가 끝입니다. 블록 수는 파일 크기 ÷ 8192의
  몫입니다.
- PostgreSQL은 세그먼트를 차례로 열어 가며 블록 수를 셉니다. 다음 장에서는 그 세그먼트에서 블록 하나를
  읽어 봅니다.
