---
icon: lucide/clipboard-list
---

# PA2 Phase 2 — 데이터 풀을 채우고 통째로 내려쓰기

**선행 문서**: [PA2 Phase 1](phase1.md)

**제출물**: `pa2.patch` + `NOTES.md` + Step 2-4의 그래프 이미지(3개 이내)

---

## 시작하기 전에

### 코스 레포 갱신

Phase 2의 로컬 채점기와 `NOTES.md` 양식(Phase 2 질문이 추가됨)을 받는다. Phase 2에는 제공 코드가 없으므로 소스 트리(`PostgreSQL/postgres-18/`)는 바뀌지 않고, 지금까지 작업한 코드는 그대로 남는다.

`NOTES.md`에는 Phase 1 답을 써 두었으므로 그대로 `git pull`하면 거부되거나 충돌이 난다. 그래서 **답을 사본으로 옮겨 두고, 양식을 원래대로 되돌린 뒤 pull하고, 답을 다시 붙여 넣는다.**

```bash
cd ~/2026-2-data-engineering
cp pa2/NOTES.md ~/NOTES-phase1-backup.md     # 1. 답을 사본으로
git checkout -- pa2/NOTES.md                  # 2. 양식을 원래대로
git pull --no-rebase                          # 3. 갱신
```

4. 새 `pa2/NOTES.md`의 Phase 1 질문 아래에 `~/NOTES-phase1-backup.md`의 답을 다시 붙여 넣는다.

작업을 로컬에 커밋해 두었다면 2가 아무것도 되돌리지 못하므로, 3에서 `CONFLICT (content): Merge conflict in pa2/NOTES.md`가 날 수 있다. 이때는 새 양식을 그대로 받고 merge를 끝낸 뒤 4로 간다.

```bash
git checkout --theirs pa2/NOTES.md
git add pa2/NOTES.md
git commit --no-edit
```

`NOTES.md` 말고 다른 파일에서 충돌이 나면 제공 파일을 고친 것이다. `git merge --abort`로 pull 전 상태로 돌아간 뒤 조교에게 알린다.

### 서버 설정 추가 { #server-settings }

Phase 2부터는 [Phase 1의 가정](phase1.md#assumptions)대로 backend가 하나뿐이도록 병렬 쿼리 worker와 autovacuum을 끈다. 새로 설치하는 경우 [환경 설정](setup.md#db-and-server)에 이미 들어 있다. Phase 1에서 `pgdata`를 만들었다면 다음 세 줄을 덧붙이고 서버를 다시 켠다. 이미 넣었더라도 한 번 더 실행해도 된다(같은 값이 두 번 들어가고, 뒤의 줄이 쓰인다).

```bash
cat >> $PGBASE/pgdata/postgresql.conf <<'EOF'
max_parallel_workers = 0
max_parallel_maintenance_workers = 0
autovacuum = off
EOF
pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log restart
psql -d postgres -c "SELECT name, setting FROM pg_settings WHERE name IN ('io_method', 'max_parallel_workers', 'max_parallel_maintenance_workers', 'autovacuum')"
#                name               | setting
# ----------------------------------+---------
#  autovacuum                       | off
#  io_method                        | sync
#  max_parallel_maintenance_workers | 0
#  max_parallel_workers             | 0
```

채점기는 학생의 `pgdata`를 쓰지 않고 같은 설정으로 서버를 따로 띄운다.

### 실험 되돌리기

각 step의 테스트는 `postgres` 데이터베이스에 테이블을 만든다. 테스트를 처음부터 다시 해 보려면, 그 테스트 끝의 "다시 해 보려면" 블록으로 테스트가 만든 것을 지우고 서버를 테스트 시작 때의 상태로 돌린다. 구현에 버그가 있어 테이블 내용이 이상해졌거나 서버가 제대로 뜨지 않으면, 아래 순서로 한 단계씩 크게 되돌린다.

**1. 테이블만 지운다.** 대부분은 이것으로 충분하다. 테스트에 쓴 이름은 다음과 같다.

```sql
DROP MATERIALIZED VIEW IF EXISTS mv;   -- Step 2-1 Extra
DROP TABLE IF EXISTS t, t2, t3;        -- Step 2-1, 2-2, 2-3
```

**2. 데이터베이스를 새로 만든다.** 따로 만든 데이터베이스(예: `mydb`)가 망가졌을 때 쓴다. [환경 설정](setup.md#db-and-server)에 적은 대로 `DROP DATABASE`, `CREATE DATABASE`는 **default 빌드 서버**로만 한다. 지우려는 데이터베이스에 접속한 채로는 지울 수 없으므로 `postgres`에 접속해서 한다.

```bash
pg_ctl -D $PGBASE/pgdata stop                                   # snudbx 서버 종료
$PGBASE/pgsql-default/bin/pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log start
$PGBASE/pgsql-default/bin/psql -d postgres -c "DROP DATABASE IF EXISTS mydb" -c "CREATE DATABASE mydb"
$PGBASE/pgsql-default/bin/pg_ctl -D $PGBASE/pgdata stop
pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log start   # 다시 snudbx 서버
psql -d mydb -c "CREATE EXTENSION snudbx"                       # 새 데이터베이스에는 확장을 다시 만든다
```

`postgres` 데이터베이스 자체는 지우지 않는다. `postgres`가 망가졌다면 3으로 간다.

**3. `pgdata`를 통째로 새로 만든다.** 서버가 뜨지 않거나, 1·2로 해결되지 않을 때 쓴다. 데이터베이스와 설정이 모두 사라진다(소스 코드와 빌드는 그대로다).

```bash
pg_ctl -D $PGBASE/pgdata stop -m fast       # 떠 있으면 종료. 응답이 없으면 -m immediate
rm -rf $PGBASE/pgdata
```

그다음 [환경 설정 4절](setup.md#db-and-server)의 명령(`initdb`부터 설정 네 줄 덧붙이기까지)을 다시 실행하고, snudbx 서버를 띄워 `psql -d postgres -c "CREATE EXTENSION snudbx"`로 확장을 다시 만든다.

서버가 뜨지 않을 때는 먼저 `$PGBASE/pgdata/log/postgresql.log`의 마지막 부분을 본다. `Assert` 실패나 `PANIC`이 찍혀 있으면 코드의 버그이므로, 코드를 고치고 다시 빌드·설치한 뒤 서버를 띄운다.

---

## 전체 그림

Phase 1에서는 풀을 여러 개 만들기만 했고, 모든 페이지는 여전히 0번 풀에 들어갔다. Phase 2에서는 테이블 데이터를 데이터 풀로 보내고, 데이터 풀이 가득 차면 그 풀을 통째로 디스크에 내려쓴 뒤 다음 데이터 풀로 넘어가게 만든다. 다음과 같은 step으로 나누어서 진행한다.

| Step | 내용 |
|---|---|
| 2-1 | 페이지를 어느 풀에 넣을지 정하기 (라우팅) |
| 2-2 | 모든 풀을 훑어야 하는 함수들 고치기 |
| 2-3 | 교체 없이 채우고, 가득 차면 내려쓴 뒤 다음 풀로 넘어가기 |
| 2-4 | 워크로드를 돌려 일반 버퍼 풀과 비교하고 분석하기 (코드 작성 없음, `NOTES.md`) |

!!! warning "Phase 2는 끝까지 구현한 뒤에 채점한다"
    채점기는 Phase 1의 테스트를 모두 포함하고, Phase 2를 끝까지 구현한 상태를 기준으로 한다. 중간 step에서는 Phase 1 테스트 일부가 실패할 수 있다. 예를 들어 2-1까지만 구현하면 데이터 풀의 페이지가 체크포인트에서 빠지므로, 서버를 재시작하면 테이블 내용 일부가 사라진다. 이것은 2-2에서 해결된다.

---

## Step 2-1. 페이지를 어느 풀에 넣을지 정하기

지금은 모든 페이지가 0번 풀로 간다. 이 step에서는 페이지마다 메타데이터 풀(0번)과 데이터 풀(지금 활성인 풀) 중 어디로 갈지를 정하고, 그 풀 번호를 버퍼를 찾고 할당하는 함수들에 넘긴다.

### 목표

* **요구사항**
    - 아래 규칙대로 풀을 정한다. **일반 테이블(`RELKIND_RELATION`)의 main 포크만** 데이터 풀로 가고, 나머지는 모두 0번 풀로 간다.

    | 경우 | 가는 곳 |
    |---|---|
    | `Relation` 정보가 없는 접근 (`rel == NULL`) | META |
    | main 포크가 아닌 것 (FSM, VM 등) | META |
    | `relkind`가 `RELKIND_RELATION`이 아닌 것 (인덱스, toast, sequence 등) | META |
    | 시스템 카탈로그 (OID가 `FirstNormalObjectId`보다 작은 것) | META |
    | 그 밖의 경우 | DATA |

    - 아래 이름과 시그니처를 그대로 사용한다.

    ```c
    #define META_POOL_ID 0
    typedef enum BufferRoute { ROUTE_META, ROUTE_DATA } BufferRoute;

    BufferRoute GetBufferRoute(Relation rel, ForkNumber forkNum);
    int         BufferRouteGetPoolId(BufferRoute route);
    ```

    - `contrib/snudbx`에 관찰 함수 `snudbx_pool_usage(pool_id integer, rel regclass DEFAULT NULL) → integer`를 추가한다.

### 구현

#### (TODO) `src/include/storage/buf_internals.h` — 선언

아래 선언 내용을 그대로 추가한다. 

```c
#define META_POOL_ID 0
typedef enum BufferRoute { ROUTE_META, ROUTE_DATA } BufferRoute;

BufferRoute GetBufferRoute(Relation rel, ForkNumber forkNum);
int         BufferRouteGetPoolId(BufferRoute route);
```

#### (TODO) `src/backend/storage/snudbx_buffer/bufmgr.c` — 라우팅 함수

* `GetBufferRoute()`는 목표에서 주어진 표의 판정 기준을 그대로 반영한 것이다. `Relation rel`과 `ForkNumber forkNum` 인자를 사용하여 각 기준에 따라서 판정하고, `enum`으로 정의한 두 값(`ROUTE_META, ROUTE_DATA`) 중 하나를 반환하면 된다.
* relation의 종류는 `rel->rd_rel->relkind`, OID는 `RelationGetRelid(rel)`로 얻는다. `FirstNormalObjectId`는 `access/transam.h`에 있으므로 이 헤더를 include시켜주어야 한다.
  
* `BufferRouteGetPoolId()`는 매우 단순한 역할을 하는 함수로, 입력받은 인자 값이 META이면 `META_POOL_ID`를, DATA이면 제어 구조체의 활성 풀 번호(`active_pool`)를 돌려준다.

#### (TODO) `src/backend/storage/snudbx_buffer/bufmgr.c` — 풀 번호 정하고 넘기기

이제 위에서 만든 함수들을 이용하여 실제로 풀 번호를 정해서 넘겨주어야 한다. `GetBufferRoute()`는 `Relation`에 대한 접근이 필요하므로, `Relation`을 볼 수 있는 곳에서 결정하고, 그 아래 함수들에는 인자로 넘긴다.

| 풀을 정하는 곳 | 사용할 `Relation` | 풀 번호를 넘길 곳 |
|---|---|---|
| `PinBufferForBlock()` | `rel` | `BufferAlloc()` → `GetVictimBuffer()` |
| `ExtendBufferedRelShared()` | `bmr.rel` | `GetVictimBuffer()`, 해시 테이블 함수, 파티션 락 |

* `BufferAlloc()`과 `GetVictimBuffer()`는 첫 인자로 `int pool_id`를 받도록 바꾼다. 이렇게 받은 인자는 Step 1-3의 **호출부 수정** 부분에서 `0`을 넘기도록 했던 자리에 넣어주면 된다. `Relation` 없이 들어오는 경로(`ReadBufferWithoutRelcache()` 등)는 `rel`이 `NULL`이므로 따로 고칠 필요가 없다.

#### (TODO) `src/backend/storage/snudbx_buffer/bufmgr.c` — 프리페치 (코드 제공)

`PrefetchBuffer()`도 페이지를 버퍼에 올리므로 풀 번호를 정해서 넘겨야 한다. 그런데 이 함수가 부르는 `PrefetchSharedBuffer()`는 복구 과정(`xlogprefetcher.c`)에서도 쓰는 공용 함수라서 시그니처를 바꿀 수 없다. 복구는 이 과제에서 다루지 않으므로 이 부분은 코드를 그대로 제공한다. 아래 순서대로 따라 한다.

1. 기존 `PrefetchSharedBuffer()`의 이름을 `PrefetchSharedBufferInPool()`로 바꾸고, `static`으로 만들고, 첫 인자로 `int pool_id`를 추가한다. Step 1-3의 **호출부 수정**에서 `0`을 넘기도록 했던 자리(해시 코드, 파티션 락, 해시 테이블 조회)를 모두 `pool_id`로 바꾼다.

    ```c
    /*
     * Implementation of PrefetchBuffer() for shared buffers, in pool pool_id.
     */
    static PrefetchBufferResult
    PrefetchSharedBufferInPool(int pool_id, SMgrRelation smgr_reln,
    						   ForkNumber forkNum, BlockNumber blockNum)
    {
    	/* 원래 PrefetchSharedBuffer()의 몸체를 그대로 가져오면 됨. 0 대신 pool_id를 쓴다. */
    }
    ```

2. 그 바로 아래에 원래 이름의 공용 함수 `PrefetchSharedBuffer()`를 다시 만든다. 이 함수는 0번 풀로 1의 함수를 부르기만 한다.

    ```c
    /*
     * Implementation of PrefetchBuffer() for shared buffers.
     *
     * Callers outside this file have no Relation (recovery prefetching), so the
     * page is looked up in the metadata pool, where such reads place it.
     */
    PrefetchBufferResult
    PrefetchSharedBuffer(SMgrRelation smgr_reln,
    					 ForkNumber forkNum,
    					 BlockNumber blockNum)
    {
    	return PrefetchSharedBufferInPool(META_POOL_ID, smgr_reln, forkNum, blockNum);
    }
    ```

3. `PrefetchBuffer()` 안에서 `PrefetchSharedBuffer()`를 부르던 줄을 다음으로 바꾼다.

    ```c
    		return PrefetchSharedBufferInPool(BufferRouteGetPoolId(GetBufferRoute(reln, forkNum)),
    										  RelationGetSmgr(reln), forkNum, blockNum);
    ```

#### (TODO) `contrib/snudbx/` — `snudbx_pool_usage()`

이제 우리의 버퍼 풀 내부를 확인할 수 있는 함수를 만든다. 이 함수는 `pool_id`번 풀에서 페이지가 들어 있는 프레임의 수를 돌려준다. `rel`을 주면 그 relation의 페이지만 센다(포크는 구분하지 않는다).

`snudbx--1.0.sql` 파일에는 다음과 같이 선언한다. 지금까지의 함수와 달리 `STRICT`를 붙이지 않는다. `STRICT` 함수는 인자 중 하나라도 NULL이면 함수를 부르지 않고 바로 NULL을 돌려주므로, `rel`을 생략한 호출이 항상 NULL이 되기 때문이다.

```sql
CREATE FUNCTION snudbx_pool_usage(pool_id integer, rel regclass DEFAULT NULL)
RETURNS integer
AS 'MODULE_PATHNAME', 'snudbx_pool_usage'
LANGUAGE C;
```

`snudbx.c`에는 아래 뼈대를 추가한다. PostgreSQL에서 사용하는 매크로들이 많이 있기 때문에 이와 같이 코드를 제공하였으나, 의미는 정확히 이해해야 한다. 인자를 꺼내고 결과를 돌려주는 부분은 채워져 있고, 프레임을 세는 루프만 작성하면 된다. include는 파일 위쪽의 기존 include 옆에 넣는다.

```c
#include "access/relation.h"
#include "utils/rel.h"

PG_FUNCTION_INFO_V1(snudbx_pool_usage);

/*
 * Number of frames in pool pool_id that hold a page; if rel is given, only
 * the pages of rel (any fork).
 */
Datum
snudbx_pool_usage(PG_FUNCTION_ARGS)
{
	int			pool_id;
	bool		filter = false;
	RelFileLocator locator;
	int			count = 0;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();
	pool_id = PG_GETARG_INT32(0);
	if (pool_id < 0 || pool_id >= NBufferPools)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("pool_id %d is out of range (0 .. %d)",
						pool_id, NBufferPools - 1)));

	if (!PG_ARGISNULL(1))
	{
		Relation	rel = relation_open(PG_GETARG_OID(1), AccessShareLock);

		locator = rel->rd_locator;
		relation_close(rel, AccessShareLock);
		filter = true;
	}

	/* TODO: pool_id번 풀의 프레임 NBuffers개를 돌면서 count를 센다. */

	PG_RETURN_INT32(count);
}
```

!!! info "각종 매크로의 의미(`fmgr.h`)"
    | 매크로 | 뜻 |
    |---|---|
    | `PG_ARGISNULL(n)` | n번째 인자(0부터)가 NULL인지 |
    | `PG_GETARG_INT32(n)` | n번째 인자를 `int32`로 꺼낸다 |
    | `PG_GETARG_OID(n)` | n번째 인자를 `Oid`로 꺼낸다. `regclass`는 relation의 OID다 |
    | `PG_RETURN_INT32(x)` / `PG_RETURN_NULL()` | 결과를 돌려준다 |

작성해야 하는 루프에서는 각 프레임에 대해 다음을 확인한다.

* 각 프레임에 페이지가 들어 있는지는 버퍼 상태의 `BM_TAG_VALID` 비트로 확인한다. 상태는 `LockBufHdr()`로 헤더를 잠그고 받은 값을 읽은 뒤 `UnlockBufHdr()`로 푼다(이게 무엇인지 잘 모르겠다면 PA2 학습자료 04 버퍼 상태 및 스핀락을 읽어보아야 한다).
* 만약 `filter`가 참이면, 그 페이지가 `rel`의 것인지를 `BufTagMatchesRelFileLocator()`로 `locator`와 비교한다. 이 함수는 원래 PostgreSQL에서 제공하는 함수다(`buf_internals.h`).

### 테스트

`buffer_pools = 3`, `shared_buffers`가 기본값일 때의 예시다.

```sql
CREATE TABLE t (id int PRIMARY KEY, pad text);
INSERT INTO t SELECT g, repeat('x', 100) FROM generate_series(1, 10000) g;
VACUUM t;

SELECT p AS pool_id,
       snudbx_pool_usage(p, 't')      AS t,
       snudbx_pool_usage(p, 't_pkey') AS t_pkey,
       snudbx_pool_usage(p)           AS total
  FROM generate_series(0, snudbx_pool_count() - 1) p;
--  pool_id |  t  | t_pkey | total
-- ---------+-----+--------+-------
--        0 |   4 |     30 |   280
--        1 | 173 |      0 |   173
--        2 |   0 |      0 |     0
```

`t`, `t_pkey` 열은 이 값과 같아야 한다. 0번 풀의 `total`은 그 전에 읽은 시스템 카탈로그의 양에 따라 조금 달라질 수 있다(예: 280 대신 283).

**다시 해 보려면**: 위 테스트를 처음부터 다시 해 보고 싶다면(예: 코드를 고치고 다시 빌드·설치한 뒤), 테스트가 만든 테이블 `t`와 그 인덱스 `t_pkey`를 지운 뒤 처음의 `CREATE TABLE`부터 다시 실행한다. 아래 Extra는 `t`를 쓰므로, Extra를 해 볼 거라면 지우기 전에 한다.

```sql
DROP TABLE IF EXISTS t;   -- 인덱스 t_pkey도 함께 지워진다
```

### Extra Assignment (추가 점수)

만약 이 과제 명세에서 요구하는 대로 구현했다면, 위의 테스트 코드로 만들어진 테이블 t에 대해 다음과 같이 실행하면 이상한 일이 생긴다.

```sql
CREATE MATERIALIZED VIEW mv AS SELECT * FROM t WHERE id <= 100;
SELECT count(*) FROM mv;
--  count
-- -------
--      0
```

이 예시는 위 테스트의 `t`가 남아 있는 상태에서 실행한다. 다시 해 보려면(예: 원인을 고친 뒤 결과가 100으로 나오는지 확인하려면) materialized view를 지우고 `CREATE MATERIALIZED VIEW`부터 다시 실행한다. Extra까지 다 본 뒤에는 `t`도 지운다.

```sql
DROP MATERIALIZED VIEW IF EXISTS mv;   -- 다시 해 볼 때
DROP TABLE IF EXISTS t;                -- 끝낼 때. mv가 남아 있으면 먼저 mv를 지워야 한다
```

원인을 찾아 고치고, `NOTES.md`에 원인을 설명한다. 원인 설명 시에는 MATERIALIZED VIEW가 무엇이고, 이 상황에서 왜 문제가 생기는지 상세히 분석하여야 한다. Gradescope의 자동 채점은 고친 결과가 맞는지(통과/실패)만 표시하고 점수는 주지 않는다. 추가 점수는 수동 채점에서, 자동 채점을 통과했고 `NOTES.md`의 설명이 정확한 이해에 기반할 때 준다. 

---

## Step 2-2. 모든 풀을 훑어야 하는 함수들 고치기

Step 2-1에서 테이블 데이터는 데이터 풀로 가게 되었다. 그런데 버퍼 관리자에는 페이지를 찾고 할당하는 경로 말고도, **버퍼 전체를 처음부터 끝까지 훑는 함수**들이 있다. 이 함수들은 아직 `for (i = 0; i < NBuffers; i++)`, 즉 0번 풀까지만 본다. 이 step에서는 이 함수들이 모든 풀을 보게 만든다.

### 목표

* **요구사항**
    - 아래 표의 함수들이 모든 풀의 프레임을 보게 한다.
    - `BufferSync()`의 몸체를 한 풀만 처리하는 함수로 떼어 낸다. 이름과 시그니처는 아래와 같이 고정한다.

    ```c
    void BufferSyncPool(int pool_id, int flags);
    ```

### 버퍼 전체를 훑는 함수들

평소에 버퍼 관리자를 생각하면 "페이지를 찾고, 없으면 프레임을 하나 골라 읽어 온다"는 경로를 떠올린다. 아래 함수들은 그 경로 밖에서, 특정 relation이나 데이터베이스의 페이지를 **모두** 골라내야 할 때 불린다. 고를 방법이 버퍼 전체를 훑는 것뿐이므로, 0번 풀만 보면 데이터 풀의 페이지를 놓친다.

| 하는 일 | 함수 | 언제 불리나 | 0번 풀만 보면 |
|---|---|---|---|
| dirty 페이지를 모두 디스크에 쓴다 | `BufferSync()` | 체크포인트 (아래 설명) | 데이터 풀의 변경이 디스크에 쓰이지 않는다. 서버를 끄고 다시 켜면 사라진다 |
| relation의 페이지를 쓰지 않고 버린다 | `DropRelationBuffers()` | `VACUUM`이 테이블 끝의 빈 블록을 잘라낼 때 | 잘린 블록이 버퍼에 남는다. 테이블이 다시 커질 때 그 블록을 새로 만들려다 오류가 난다 |
| | `DropRelationsAllBuffers()` | `DROP TABLE`, `TRUNCATE`가 relation 파일을 지울 때 | 지운 파일의 페이지가 버퍼에 남는다 |
| 데이터베이스의 페이지를 버린다 | `DropDatabaseBuffers()` | `DROP DATABASE` | 위와 같다 |
| relation의 dirty 페이지를 디스크에 쓴다 | `FlushRelationBuffers()` | `ALTER TABLE ... SET TABLESPACE`가 파일을 통째로 복사하기 직전 | 복사본에 버퍼에만 있던 변경이 빠진다 |
| | `FlushRelationsAllBuffers()` | 특수한 설정에서 트랜잭션을 커밋할 때 | |
| 데이터베이스의 dirty 페이지를 쓴다 | `FlushDatabaseBuffers()` | 복구 과정 | |
| 사용 중이 아닌 버퍼를 비운다 | `EvictAllUnpinnedBuffers()`, `EvictRelUnpinnedBuffers()` | `pg_buffercache` 확장의 테스트용 함수 | 데이터 풀은 비우지 못한다 |

"버린다"와 "쓴다"가 따로 있는 이유는 다음과 같다. 파일이 지워지거나 잘리는 경우, 그 페이지는 더 이상 쓸 곳이 없으므로 dirty여도 그냥 버린다. 반대로 파일을 버퍼를 거치지 않고 직접 복사하는 경우, 버퍼에만 있는 변경을 먼저 파일에 써 두어야 복사본이 최신이 된다.

!!! info "체크포인트"
    PostgreSQL은 페이지를 고칠 때마다 디스크에 쓰지 않고, dirty 상태로 버퍼에 둔다. 대신 주기적으로(기본 5분), 그리고 **서버를 끌 때** 버퍼의 dirty 페이지를 모두 디스크에 쓴다. 이 작업을 체크포인트라 하고, `CHECKPOINT` 명령으로 직접 부를 수도 있다. 체크포인트가 하는 일은 결국 `BufferSync()`이다.

### 구현

#### (TODO) `src/backend/storage/snudbx_buffer/bufmgr.c` — `BufferSyncPool()`

`BufferSync()`는 dirty 페이지를 고른 뒤 `CkptBufferIds` 배열에 모아 정렬하고, 그 순서대로 쓴다. Step 1-3에서 `CkptBufferIds`를 풀마다 하나씩 두었으므로, 이 과정을 풀 단위로 하는 것이 자연스럽다.

1. 기존 `BufferSync()`의 이름을 `BufferSyncPool()`로 바꾸고, `static`을 떼고, 첫 인자로 `int pool_id`를 추가한다.
2. 몸체에서 프레임을 훑는 첫 루프가 `pool_id`번 풀의 프레임만 돌게 하고, `CkptBufferIds[0]`을 `CkptBufferIds[pool_id]`로 바꾼다.
3. 그 위에 `BufferSync()`를 다시 만든다. 이 함수는 모든 풀에 대해 `BufferSyncPool()`을 부르기만 한다.
4. `BufferSyncPool()`의 선언을 `buf_internals.h`의 `#ifdef SNUDBX` 안, Step 2-1에서 넣은 선언 아래에 추가한다.

!!! info "체크포인트가 활성 풀도 쓴다"
    체크포인트는 아직 채우는 중인 데이터 풀의 dirty 페이지도 쓴다. "가득 차면 한꺼번에 내려쓴다"는 Phase 2의 의도와는 어긋나지만, 쓰지 않으면 위에서 본 것처럼 데이터가 사라지기 때문이다. 이것이 반드시 최적의 설계는 아닐 수 있지만 이 과제에서는 이렇게 진행한다.

#### (TODO) `src/backend/storage/snudbx_buffer/bufmgr.c` — 나머지 함수들

`BufferSync()`를 뺀 나머지 함수들은 루프의 상한만 바꾸면 된다. 각 함수에서 `for (i = 0; i < NBuffers; i++)`(`Evict...` 함수들은 `buf <= NBuffers`)를 찾아 `NBuffers`를 `NTotalBuffers`로 바꾼다. 버퍼 번호는 풀 경계를 넘어 이어지므로 이것만으로 모든 풀을 차례로 돈다.

루프 안에서 페이지를 버릴 때 부르는 `InvalidateBuffer()`는 Step 1-3에서 이미 `BufferPoolIdOf(buf_id)`로 자기 풀의 해시 테이블을 찾도록 고쳤으므로, 루프 안은 고칠 것이 없다.

`FlushRelationsAllBuffers()`와 `FlushDatabaseBuffers()`가 불리는 상황은 이 과제에서 다루지 않는 기능(복구 등)과 관련되어 있으므로, 언제 불리는지까지 이해할 필요는 없다. 같은 방식으로 상한만 바꾼다.

#### (TODO) `src/backend/storage/snudbx_buffer/bufmgr.c` — `FindAndDropRelationBuffers()`

`DropRelationBuffers()`와 `DropRelationsAllBuffers()`는 버릴 블록 수가 적고 그 수를 정확히 알 때, 버퍼 전체를 훑는 대신 이 함수를 부른다. 이 함수는 블록마다 해시 테이블을 찾아보는데, 지금은 0번 풀의 해시 테이블만 본다(`BufTableHashCode(0, ...)` 등). 블록 수를 정확히 아는 경우는 복구 과정에서만 생기므로, 이 함수가 언제 불리는지는 이해할 필요가 없다. 위에서 했던 프리페치에서처럼 같은 방법으로 고친다.

1. 기존 함수의 이름을 `FindAndDropRelationBuffersInPool()`로 바꾸고 첫 인자로 `int pool_id`를 추가한다. 몸체에서 `0`을 넘기던 세 곳을 `pool_id`로 바꾼다.
2. 원래 이름의 함수를 다시 만들어, 모든 풀에 대해 1의 함수를 부르게 한다.
3. 파일 위쪽의 static 함수 선언 목록에 1의 함수 선언을 추가한다.

#### 고치지 않는 것

* **`BgBufferSync()`**: background writer가 주기적으로 부르는 함수로, clock hand보다 조금 앞에 있는 dirty 페이지를 미리 써 둔다. 그래야 backend가 희생 프레임을 고를 때 직접 쓰지 않아도 된다. Step 1-3에서 `StrategySyncStart()`를 0번 풀만 보게 했고, 이 함수가 훑는 `0 .. NBuffers - 1`도 0번 풀의 프레임이므로 둘은 맞물려 있다. Step 2-3부터 데이터 풀은 clock sweep을 쓰지 않으므로 미리 써 둘 대상도 없다. 따라서 0번 풀만 맡으면 된다.
* **`contrib/pg_buffercache`**: 버퍼 전체를 보여 주는 확장이지만 이 과제에서는 쓰지 않는다.

### 테스트

`buffer_pools = 3`, `shared_buffers`가 기본값일 때의 예시다. 먼저 작은 테이블을 만들고 서버를 정상 종료한 뒤 다시 켠다.

```sql
CREATE TABLE t2 (id int, pad text);
INSERT INTO t2 SELECT g, repeat('x', 100) FROM generate_series(1, 2000) g;
SELECT count(*), sum(id) FROM t2;
--  count |   sum
-- -------+---------
--   2000 | 2001000
```

psql을 `\q`로 빠져나온 뒤 서버를 다시 켠다.

```bash
pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log restart
```

이렇게 재시작한 서버에 다시 접속한다.

```bash
psql -d postgres
```

```sql
SELECT count(*), sum(id) FROM t2;
--  count |   sum
-- -------+---------
--   2000 | 2001000
```

Step 2-1까지만 구현한 상태에서는 재시작 후 결과가 이와 다르다. 이어서 테이블을 키웠다가 `VACUUM`으로 잘라내고, 지운다.

```sql
INSERT INTO t2 SELECT g, repeat('x', 100) FROM generate_series(2001, 20000) g;
SELECT pg_relation_size('t2') / 8192 AS blocks, snudbx_pool_usage(1, 't2') AS pool1;
--  blocks | pool1
-- --------+-------
--     345 |   345

DELETE FROM t2 WHERE id > 2000;
VACUUM t2;
SELECT pg_relation_size('t2') / 8192 AS blocks, snudbx_pool_usage(1, 't2') AS pool1;
--  blocks | pool1
-- --------+-------
--      35 |    35

DROP TABLE t2;
SELECT snudbx_pool_usage(1);
--  snudbx_pool_usage
-- -------------------
--                  0

CHECKPOINT;
```

`VACUUM`이 테이블을 35블록으로 잘라내면 1번 풀에서도 잘린 블록이 사라지고, `DROP TABLE` 뒤에는 1번 풀이 빈다(재시작 뒤 다른 테이블을 읽지 않았을 때). 마지막 `CHECKPOINT`는 오류 없이 끝나야 한다.

**다시 해 보려면**: 테스트를 끝까지 했다면 마지막의 `DROP TABLE t2`로 이미 지워졌다. 중간에 멈췄거나 결과가 달랐다면 `t2`를 지운다. 1번 풀이 비는지 보는 마지막 확인은 다른 테이블이 1번 풀에 없을 때만 0이 나오므로, 서버도 다시 켠 뒤 처음의 `CREATE TABLE`부터 다시 실행한다.

```bash
psql -d postgres -c "DROP TABLE IF EXISTS t2"
pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log restart
```

---

## Step 2-3. 교체 없이 채우고, 차면 다음 풀로 넘어가기

지금 데이터 풀은 0번 풀과 똑같이 clock sweep으로 희생 프레임을 골라 페이지를 교체한다. 이 step에서는 데이터 풀에서 **교체를 없앤다.** 데이터 풀은 빈 프레임을 슬롯 순서대로 하나씩 내주기만 한다. 풀이 거의 다 차면 그 풀을 닫고(**seal**) 새 페이지는 다음 데이터 풀에 넣는다. 닫힌 풀은 아무도 그 풀의 페이지를 쓰지 않게 된 뒤에 dirty 페이지를 디스크에 쓰고 비운다(**flush and reset**). 0번 풀은 지금처럼 clock sweep을 쓴다.

### 목표

* **요구사항**
    - **데이터 풀**은 `next_free_slot`(Step 1-2에서 만든 필드)이 가리키는 슬롯부터 차례로 프레임을 내준다. clock sweep, free list, ring(`BufferAccessStrategy`)은 데이터 풀에서 쓰지 않는다.
    - 활성 풀이 `POOL_SEAL_THRESHOLD`만큼 차면 seal하고, 만약 그 풀에 pin이 없다면 디스크에 쓰고 비울 수 있다(flush and reset). 세부 규칙은 아래 [할당 직전의 확인](#alloc-check)에 있다.
    - seal할 때마다 `generation`을 1 늘리고, 다음 데이터 풀(1 → 2 → … → `buffer_pools - 1` → 1)을 활성으로 만든다.
    - 풀 상태에 `POOLSTATE_SEALED`를 추가한다. 상태의 순서는 `READY`, `ACTIVE`, `SEALED`로 고정한다.
    - `SEALED` 풀은 한 번에 **많아야 하나**다. 이를 위해 제어 구조체 `BufferPoolControl`(`buf_internals.h`)의 `active_pool` 다음에 **필드 `int sealed_pool`을 추가한다.** 이 필드에는 `SEALED`인 데이터 풀의 번호를 두고, `SEALED` 풀이 없으면 -1을 둔다. 서버를 켤 때는 -1로 초기화한다. 
    - 다음 이름과 시그니처를 고정하여 사용하므로, 이를 적절한 곳에 선언한다.

    ```c
    typedef struct BufferPoolControl
    {
        int         active_pool;
        int         sealed_pool;                 /* 추가: SEALED인 데이터 풀, 없으면 -1 */
        ...
    } BufferPoolControl;

    #define POOL_SEAL_THRESHOLD   (NBuffers - NBuffers / 10)

    int  PoolGetFrameNoEvict(int pool_id);       /* freelist.c */
    bool PoolShouldSeal(int pool_id);            /* bufmgr.c, 아래 둘도 */
    void SealBufferPool(int pool_id);
    void FlushAndResetBufferPool(int pool_id);
    ```

### 중요한 전제: 동시에 접근하는 backend는 하나 뿐이다

이 step의 설계는 [Phase 1의 가정](phase1.md)대로 **서버를 쓰는 backend가 한 번에 하나**라는 데에 의존하고 있다. psql 세션 하나로만 접속하고, 병렬 쿼리 worker와 autovacuum은 꺼져 있어야 한다([서버 설정](#server-settings)). checkpointer, background writer 같은 보조 프로세스는 작동할 수 있지만, 이들은 페이지를 디스크에 쓰기만 할 뿐 pin을 오래 쥐지 않고 데이터 풀에 새 페이지를 넣지도 않는다. 그래서 여기서는 하나의 backend 자신이 버퍼에 접근하는 순간에 모든 일을 처리한다고 생각하며, 다른 backend를 기다린다거나 하는 일도 없다.

**요약**

| 질문 | 답 |
|---|---|
| 누가 데이터 풀에 새 페이지를 넣나 | 나(backend) 혼자 |
| 누가 seal하고 누가 비우나 | 나(backend) 혼자. 새 프레임을 받기 직전에 확인한다 |
| 풀을 비워도 되는지 어떻게 아나 | 내가 그 풀에 pin한 프레임 수(`PoolPinCount`)가 0이면 아무도 pin하지 않은 것이다 |
| 기다리면 pin이 풀리나 | 아니다. pin을 쥔 것이 나 자신이므로, 지금 쿼리가 끝나야 풀린다. 그래서 어떤 경우에도 기다리지 않는다. 지금 비울 수 없으면 다음 확인으로 미룬다 |

### 버퍼 풀의 전반적 사용 방식

`buffer_pools = 3`이면 공유 버퍼는 풀 3개로 나뉘고, 풀마다 `NBuffers`개의 프레임이 있다. 0번 풀은 Phase 1부터 그대로 clock sweep으로 교체한다. 데이터 풀(1번, 2번)은 이 step부터 **앞에서부터 채우기만** 한다.

```text
 0번 풀 (META)       clock sweep으로 교체. 이 step에서 바뀌지 않는다
 +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
 +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

 1번 풀 (DATA, ACTIVE)                    next_free_slot
 +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 |##|##|##|##|##|##|##|##|##|##|  |  |  |  |  |  |
 +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  0                             ^           ^  ^
                                |           |  NBuffers - 1
                                |           POOL_SEAL_THRESHOLD (90%)
                                다음에 내줄 슬롯

 2번 풀 (DATA, READY)    next_free_slot = 0
 +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
 +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
```

데이터 풀에서 프레임을 받으면 `next_free_slot` 자리를 받고 포인터가 한 칸 오른쪽으로 간다. 포인터 왼쪽은 모두 쓴 프레임이고 오른쪽은 모두 빈 프레임이다. 한 번 쓴 프레임은 페이지가 버려져도(`DROP TABLE` 등) 다시 내주지 않는다. 풀을 통째로 비울 때 포인터가 0으로 돌아간다.

활성 풀은 언제나 데이터 풀 하나다. 새 페이지는 활성 풀에만 들어간다. 활성 풀은 1 → 2 → … → `buffer_pools - 1` → 1 순서로 돈다. 실질적으로는 2개의 풀만을 사용할 것이지만, 여기서는 설계상 제약을 두지 않고 `buffer_pools - 1`개를 사용할 수 있다고 하자.

### 풀의 상태 변화

데이터 풀 하나는 다음 상태를 차례로 돈다.

```text
            앞 풀이 seal되어 이 풀이 활성이 됨
   +-------+ ------------------------------> +--------+
   | READY |                                 | ACTIVE |
   +-------+                                 +--------+
       ^                                          |
       | FlushAndResetBufferPool()                | SealBufferPool()
       | (이 풀에 내 pin이 0일 때)                    | (임계치를 넘었을 때)
       |               +--------+                 |
       +-------------- | SEALED | <---------------+
                       +--------+
```

| 상태 | 뜻 | 페이지 | 새 페이지를 받나 | 페이지를 찾을 수 있나 |
|---|---|---|---|---|
| `READY` | 비어 있다 | 없음. `next_free_slot = 0` | 아니오 | — |
| `ACTIVE` | 지금 사용되는 중 | 있음 | 예 | 예 |
| `SEALED` | 비워지기를 기다리는 중 | 있음. dirty 페이지가 남아 있을 수 있다 | 아니오 | 예 |

seal은 **상태만 바꾸는** 일이다. 디스크에 쓰지도, 비우지도 않는다. `SEALED` 풀의 페이지는 그대로 찾고, 읽고, 고칠 수 있다. 다만 새 페이지를 할당해주지는 않는다. 디스크에 쓰고 비우는 일(`FlushAndResetBufferPool()`)은 그 풀에 내 pin이 하나도 없을 때 한꺼번에 한다.

seal하는 순간 그 풀에 내 pin이 없으면 바로 쓰고 비우는 것으로 진행하기 때문에 `SEALED`는 대개 금방 끝난다. 내가 그 풀의 페이지를 pin하고 있어서 쓰고 비울 수 없을 때에만 `SEALED`로 유지된다.

`SEALED` 풀은 **언제나 많아야 하나**다. 그 번호는 `active_pool`처럼 제어 구조체의 `sealed_pool`에 두고, 없으면 -1이다. `SEALED` 풀이 남아 있는 동안에는 다시 seal하지 않는다. 그래서 어느 순간이든 데이터 풀은 다음 셋으로 나뉜다.

| 풀 | 개수 | 찾는 곳 |
|---|---|---|
| `ACTIVE` | 정확히 하나 | `active_pool` |
| `SEALED` | 0 또는 1 | `sealed_pool` (-1이면 없음) |
| `READY` | 나머지 전부 | — |

### 할당 직전의 확인 { #alloc-check }

풀을 비우려면 그 풀의 프레임을 **아무도 pin하고 있지 않아야 한다.** pin은 "지금 이 페이지를 사용하고 있으니 다른 페이지로 바꾸지 말라"는 표시다(Phase 1의 `refcount`). 다행히도 우리는 backend가 하나뿐이므로, **내가 pin한 것이 없으면 아무도 pin하지 않은 것**이다.

seal과 비우기는 모두 한 곳에서 판단한다. **데이터 풀**의 페이지에 접근하기 **직전**에 `CheckDataPoolSeal()`을 부르고, 이 함수가 다음을 차례로 한다. 0번 풀(META)에 접근할 때는 데이터 풀과 관계가 없으므로 부르지 않는다.

```text
 CheckDataPoolSeal()
   |
   +-- 1. SEALED 풀(sealed_pool)이 있고, 그 풀에 내 pin이 0인가?
   |        예     --> FlushAndResetBufferPool(): 디스크에 쓰고, 비우고, READY
   |                   sealed_pool = -1
   |        아니오 --> 그대로 둔다 (다음 확인 때 다시 본다)
   |
   +-- 2. 활성 풀이 POOL_SEAL_THRESHOLD를 넘었나?
            아니오 --> 끝. 활성 풀에서 할당한다
            예     --> SealBufferPool(): 상태만 바꾼다
                         sealed_pool이 아직 있으면 (pin이 남은 SEALED) --> ERROR
                         활성 풀 = SEALED (sealed_pool), 다음 풀 = ACTIVE, generation + 1
                       방금 seal한 풀에 내 pin이 0인가?
                         예     --> 바로 FlushAndResetBufferPool()
                         아니오 --> SEALED로 남는다. 1에서 나중에 비운다
```

| 상황 | 하는 일 |
|---|---|
| 활성 풀이 임계치 전 | 아무것도 하지 않는다. 활성 풀에서 할당한다 |
| 활성 풀이 임계치를 넘음 | seal한다. 새 페이지는 이제 다음 풀에 들어간다. 그 풀에 내 pin이 없으면 바로 비운다. 대부분 이 경우다 |
| pin이 남은 `SEALED` 풀이 있음 | 그대로 둔다. pin이 다 빠진 뒤의 확인에서 비운다 |
| seal해야 하는데 pin이 남은 `SEALED` 풀이 아직 있음 | `ERROR`를 낸다 |

어떤 경우에도 pin이 빠지기를 **기다리지 않는다.** 내가 쥔 pin은 기다려도 풀리지 않기 때문이다.

임계치는 풀의 90%다. 임계치를 넘으면 곧바로 seal하므로 풀의 마지막 10%는 쓰지 않는다. 이 여유 덕분에, 테이블을 확장할 때처럼 한 번에 프레임 여러 개를 받아도 활성 풀이 넘치지 않는다. 확인할 때 임계치 아래였다면 남은 프레임은 풀의 10% 이상이다. 확장 한 번에 받는 블록 수는 `LimitAdditionalPins()`가 대략 `NBuffers / (최대 backend 수 + 보조 프로세스 수)` 이하로 줄이므로 10%보다 훨씬 작다. 그래서 "남은 프레임이 이번에 받을 수보다 적은가"를 따로 확인하지 않아도 된다.

마지막 `ERROR`는 한 풀에 pin을 쥔 채로, 다음 풀까지도 임계치를 넘길 만큼 더 할당받은 경우다. 예를 들어 커서를 열어 1번 풀의 페이지를 pin해 둔 채로, 같은 트랜잭션에서 아주 큰 테이블에 `INSERT`하면 2번 풀도 임계치를 넘는다. 이때 1번 풀은 아직 `SEALED`라 비울 수 없고, pin을 쥔 것이 나 자신이므로 기다려도 풀리지 않는다. 그 쿼리를 실패시키면 트랜잭션이 끝나면서 pin이 모두 풀린다.`SEALED` 풀을 둘로 늘리지 않는 것이 원칙이다.

#### 사용 사례 1: pin 없이 seal

`buffer_pools = 3`에서 1번 풀이 활성일 때, 쿼리 여러 개가 1번 풀을 채워 가는 모습이다. `■`는 내가 pin한 프레임이다.

```text
 쿼리 A 실행 중       1번 [######■.........]  2번 [................]   임계치 전
 쿼리 A 끝 (pin 0)    1번 [#######.........]  2번 [................]
 쿼리 B 실행 중       1번 [#############■|.]  2번 [................]
                                         ^ 90%
 쿼리 B가 다음 프레임을 받기 직전의 확인: 임계치를 넘음 → seal
   1. 1번 = SEALED, 2번 = ACTIVE, generation + 1     (상태만 바뀜)
   2. 1번에 내 pin이 있는가?
```

`INSERT`처럼 테이블을 늘려 가는 쿼리는 새 페이지를 받을 때 앞 페이지의 pin을 놓는다. 그래서 다음 프레임을 받기 직전에는 대개 pin이 없고, seal한 풀을 바로 비운다.

```text
   pin 0 → 바로 FlushAndResetBufferPool(1번): 디스크에 쓰고, 비우고, READY
                      1번 [................]  2번 [■...............]   쿼리 B는 2번에서 계속
```

#### 사용 사례 2: pin이 남은 채 seal

페이지를 오래 pin하는 쿼리가 있으면, seal한 풀을 바로 비울 수 없다.

```text
 커서 c가 1번의 페이지를 pin  1번 [■#####..........]  2번 [................]
 큰 INSERT, 임계치를 넘음     1번 [■#############|.]  2번 [................]
 다음 프레임을 받기 직전의 확인 → seal:
   1. 1번 = SEALED, 2번 = ACTIVE, generation + 1
   2. 1번에 커서의 pin이 남아 있으므로 비우지 않는다
                              1번 [■#############..]  2번 [####............]
                                   SEALED: 페이지가 남아 있고 찾을 수 있음
                                   새 페이지는 2번으로 간다
 COMMIT (커서 닫힘, pin 0)
 다음에 버퍼에 접근할 때의 확인:
   1번은 SEALED이고 내 pin 0 → FlushAndResetBufferPool(1번)
                              1번 [................]  2번 [####............]
```

#### 실패하는 경우: pin이 남은 SEALED 풀이 있는데 또 seal해야 함

위 상태에서 커서를 닫지 않고 2번 풀까지 임계치를 넘기면, 1번 풀이 아직 `SEALED`인 채로 다시 seal해야 한다.

```text
                              1번 [■#############..]  2번 [##############|.]
                                   SEALED, 내 pin 있음      ACTIVE, 임계치 넘음
 확인: 2번을 seal해야 하는데, sealed_pool = 1번이 아직 남아 있다 → ERROR
```

내가 쥔 pin은 기다려도 풀리지 않으므로, 쿼리를 실패시켜 트랜잭션을 끝낸다. 트랜잭션이 끝나면 pin이 모두 풀리고, 다음 확인에서 1번 풀이 비워진다.

!!! warning "대량 `ROLLBACK`, `DELETE` 뒤에는 `VACUUM`을 한다"
    executor는 스캔이 마지막으로 돌려준 행의 페이지를 다음 행을 돌려줄 때까지 pin해 둔다. 테이블 뒤쪽에 보이는 행이 없는 페이지가 풀 하나 크기 이상 이어져 있으면(큰 `INSERT`를 `ROLLBACK`했거나 큰 범위를 `DELETE`한 직후), 스캔은 그 pin을 쥔 채로 풀 하나 분량 이상을 읽게 되어 위의 `ERROR`가 난다. 이 과제는 autovacuum을 끄므로 `VACUUM`을 직접 해서 죽은 행을 정리하면 다시 읽을 수 있다. 이 경우는 설계상의 한계로 두며, 채점에서도 만들지 않는다.

#### 페이지를 찾는 순서

데이터 페이지를 찾을 때는 다음 순서를 반드시 준수해야 한다. 만약 `SEALED` 풀에 데이터가 올라와 있는데, 이를 확인하지 않고 새 활성 풀에서 데이터를 읽어올 경우, 같은 페이지가 두 풀에 따로 들어가고, 한 쪽에서 고친 내용이 다른 쪽에서는 반영되지 않아서 문제가 생길 수 있다. 다만 `SEALED` 풀에서는 새로운 페이지를 할당하지 않고, 활성 풀에서만 할당한다.

```text
 BufferAlloc(데이터 페이지)
   |
   +-- 1. 활성 풀의 해시 테이블 ------------------ 있음 --> 그 프레임을 pin
   |        | 없다면?
   +-- 2. SEALED 풀(sealed_pool)의 해시 테이블 -- 있음 --> 그 프레임을 pin
   |        | 없다면?
   +-- 3. 활성 풀의 next_free_slot 자리를 받아 디스크에서 읽는다
```

### 구현

#### 구현 순서

본격적으로 구현을 시작하기에 앞서서 어떤 순서로 구현해 나갈지를 먼저 생각한다. 필요한 변수나 함수를 미리 선언하고, 초기화한다. 그 다음 구현해야 할 부분들인 (1) 버퍼 할당 정책 변경 (clock sweep -> bump allocation 스타일로 변경) (2) sealing 관련 판단을 위해 필요한 정보(내가 현재 지금 풀에 둔 pin의 개수)를 추적하기 위한 변수 및 방법 구축 (3) sealing 및 flush & reset 함수 구현 (4) 페이지 할당 직전의 검사 부분 구현(여기서 (3)에서 만든 함수들을 호출하면 된다) (5) 페이지를 찾는 순서에 따라 BufferAlloc() 내 로직 추가 등 순서로 진행한다.

#### (TODO) `src/include/storage/buf_internals.h` — 선언

1. `PoolState`의 `POOLSTATE_ACTIVE` 다음에 `POOLSTATE_SEALED`를 추가한다.
2. `BufferPoolControl`의 `active_pool` 다음에 `int sealed_pool;`을 추가한다.
3. Step 2-2에서 넣은 `BufferSyncPool()` 선언 아래에 위 목표의 매크로와 함수 네 개를 선언한다.

#### (TODO) `src/backend/storage/snudbx_buffer/buf_init.c` — 초기화

`BufferPoolCtl`을 처음 만들 때 `active_pool`, `generation`과 함께 `sealed_pool`을 -1로 초기화한다.

#### (TODO) `src/backend/storage/snudbx_buffer/freelist.c` — 교체 없는 할당

* **`PoolGetFrameNoEvict(pool_id)`**: `pool_id`번 데이터 풀의 `next_free_slot`을 읽어, `NBuffers`보다 작으면 1 늘리고 그 슬롯의 `buf_id`(`pool_id * NBuffers + slot`)를 돌려준다. 다 썼으면 -1을 돌려준다.
    * 여기서 `next_free_slot`은 `pg_atomic_uint32`이고, "읽고, 1을 증가시켜서 쓰는" 두 단계를 한번에(atomically) 수행하는 것이 중요하다. 이 과제는 backend가 하나뿐이라 당장은 일어나지 않지만 일반적으로 주의해야 하는 패턴이다.
    * 따라서 슬롯을 받는 일을 compare-and-swap(`pg_atomic_compare_exchange_u32()`), 즉 CAS로 한 번에 한다. CAS에 대해서 먼저 이해하고, CAS에 인자를 어떻게 넣어주어야 하는지 위 함수의 사용례나 정의 등을 통해 파악한 뒤 구현하라. CAS는 읽은 값이 그 사이에 바뀌었으면 실패하고 현재 값을 다시 알려 주므로, 그 값으로 다시 시도하는 while문 기반 패턴이 필요하다.
* **`StrategyGetBuffer()`**: 맨 앞(`*from_ring = false;` 다음)에서 `pool_id`가 0이 아니면 `PoolGetFrameNoEvict()`로 프레임을 받는다. -1이면 `elog(ERROR, ...)`를 낸다. 함수 설명에 있는 대로 프레임의 헤더 락을 잡은 채(`LockBufHdr()`) 돌려준다. 이 함수가 기존에 어떻게 버퍼를 반환하는지를 잘 보고 따라하면 된다. 우리의 경우 링 버퍼는 사용하지 않는다.
* **`StrategyFreeBuffer()`**: 데이터 풀의 프레임이면 free list에 넣지 않고 바로 돌아간다. `DROP TABLE` 등으로 버려진 프레임은 풀 전체가 비워질 때까지 다시 쓰지 않는다.

#### (TODO) `src/backend/storage/snudbx_buffer/bufmgr.c` — 풀별 pin 수

seal할 수 있는지 판단하려면 내가 각 풀에 pin한 프레임 수를 알아야 한다. backend마다 따로 갖는 배열 `static int PoolPinCount[MAX_BUFFER_POOLS];`를 `PrivateRefCountArray` 등의 선언 근처에 두고, 다음 두 곳에서 센다.

* `NewPrivateRefCountEntry()`: 어떤 버퍼를 처음 pin할 때 불린다. 그 버퍼가 속한 풀의 수를 1 늘린다.
* `ForgetPrivateRefCountEntry()`: 그 버퍼의 마지막 pin을 풀 때 불린다. 1 줄인다.

같은 버퍼를 여러 번 pin해도 이 두 함수는 한 번씩만 불리므로, 이 수는 "pin한 프레임의 개수"가 된다. `Buffer` 번호는 `buf_id + 1`이라는 점에 주의한다.

#### (TODO) `src/backend/storage/snudbx_buffer/bufmgr.c` — seal, flush & reset 함수 구현

* **`PoolShouldSeal(pool_id)`**: `next_free_slot`이 `POOL_SEAL_THRESHOLD` 이상이면 참을 돌려준다.
* **`SealBufferPool(pool_id)`**: 활성 풀 `pool_id`를 seal한다. 상태만 바꾸고, 디스크에 쓰거나 비우지 않는다.
    1. `sealed_pool`이 -1이 아니면 `ereport(ERROR, ...)`를 이용하여 에러 메시지를 내야 한다.
    2. 문제가 없다면, 다음 데이터 풀을 정한다. 마지막 풀 다음은 1번풀로 돌아온다. 이 풀은 반드시 `READY`가 되도록 한다(`Assert`로 확인하는 패턴을 넣을 수 있음).
    3. `pool_id`를 `SEALED`, 다음 풀을 `ACTIVE`로 바꾼다. `sealed_pool`을 `pool_id`로, `active_pool`을 다음 풀로 옮기고, `generation`을 1 늘린다. 여러 backend의 접근을 가정하지 않기 때문에 별도의 락과 같은 보호가 불필요하다.
* (**선택**) **`FlushBufferPool(int pool_id)`** (static): 반드시 이 함수를 따로 두어야 하는 것은 아니지만, 이 함수를 아래의 `FlushAndResetBufferPool(pool_id)`에서 호출하는 식으로 사용하면 로직을 단순화하여 구현하는 데에 도움이 된다. 이 함수는 풀의 프레임을 모두 돌면서, 유효하고 **dirty인** 페이지(`BM_VALID | BM_DIRTY`)를 디스크에 쓴다. 쓰는 방법은 `FlushRelationBuffers()`의 루프를 따라 한다. pin하고, content 락을 공유 모드로 잡고, `FlushBuffer()`를 부르고, 풀어 준다. 이 함수는 내 pin이 없는 풀에만 불리므로, 내가 이미 pin했거나 락을 잡은 프레임은 없다. 일반적으로는 하나의 풀이 쓰여지는 동안에 이미 내가 디스크에 쓴 데이터가 다시 dirty될 우려가 있어서 이런 식으로 구현하면 안 되지만, 여기에서는 backend가 우리 뿐이므로 큰 문제가 되지 않는다.
* **`FlushAndResetBufferPool(pool_id)`**: `SEALED` 풀(`pool_id == sealed_pool`)을 디스크에 쓰고 비워 `READY`로 만든다. 내 pin이 남아 있으면 `elog(ERROR, ...)`를 낸다.
    1. `FlushBufferPool(pool_id)`로 dirty 페이지를 모두 쓴다.
    2. 풀의 프레임 중 `BM_TAG_VALID`인 것마다 `InvalidateBuffer()`를 부른다. 헤더 락을 잡은 채 불러야 하고, 이 함수가 락을 풀어 준다. 해시 테이블 항목도 이 함수가 지운다. 파일 위쪽 static 함수 선언 목록에 `InvalidateBuffer()`의 선언이 없으면 추가한다.
    3. 개별 풀에 대한 메타데이터, 그리고 버퍼 control 구조체에서 초기화해야 할 부분이 있으면 적절히 초기화해준다.

#### (TODO) `src/backend/storage/snudbx_buffer/bufmgr.c` — 할당 직전의 확인 함수 구현

(**선택**) [할당 직전의 확인](#alloc-check)을 static 함수 `CheckDataPoolSeal(void)`로 만든다. 위에서와 마찬가지로 굳이 함수 형태로 구현하지 않아도 되지만, 이렇게 하는 것이 코드의 가독성 향상에 도움을 준다. 위에 있던 pseudocode를 참고하여 다음과 같은 순서로 진행한다. 

1. `sealed_pool`이 -1이 아니고 그 풀에 내 pin이 0이면, `FlushAndResetBufferPool()`로 비운다. 
2. 활성 풀이 `PoolShouldSeal()`이면 `SealBufferPool()`로 seal한다.
3. 2에서 seal했고 방금 seal한 풀에 내 pin이 0이면, 그 풀을 바로 `FlushAndResetBufferPool()`로 비운다.

이 함수를 부르는 곳은 Step 2-1에서 풀 번호를 정한 두 곳이다. 두 곳 모두 데이터 풀 접근(`route == ROUTE_DATA`)일 때만 부른다.

* `PinBufferForBlock()`: `GetBufferRoute()`의 결과를 변수에 받아 두고, `ROUTE_DATA`이면 `CheckDataPoolSeal()`을 부른 뒤 `BufferRouteGetPoolId(route)`로 풀 번호를 정한다.
* `ExtendBufferedRelShared()`: 지금은 함수 맨 앞에서 풀 번호를 정한다. 이것을 `LimitAdditionalPins(&extend_by)` 다음으로 옮기고, 그 바로 앞에서 `ROUTE_DATA`이면 `CheckDataPoolSeal()`을 부른다. 확인한 뒤에 풀 번호를 정하므로, 블록 여러 개를 받는 도중에 풀이 바뀌지 않는다. 위의 10% 여유가 지켜지는지 `Assert(extend_by <= NBuffers - POOL_SEAL_THRESHOLD)`로 확인해 두면 좋다.

#### (TODO) `src/backend/storage/snudbx_buffer/bufmgr.c` — `SEALED` 풀에서 찾기

* `BufferAlloc()`에서 `pool_id`번 풀의 해시 테이블에 페이지가 없을 때, 희생 프레임을 받기(`GetVictimBuffer()`) 전에 다음을 한다:
    * `pool_id`가 데이터 풀이고 `sealed_pool`이 -1이 아니면, `sealed_pool`번 풀의 해시 테이블에서 같은 태그를 찾는다. 찾으면 위쪽의 "Found it" 경로처럼 매핑 락을 잡은 채 `PinBuffer()`로 pin하고, 그 결과를 `*foundPtr`에 넣어 그 프레임을 돌려준다. 해시 값과 매핑 락은 풀마다 다르므로 `sealed_pool`에 대해 따로 구한다.
* 만약 위에서 프레임을 얻지 못했다면 지금의 `pool_id`번에서 새로운 페이지를 할당받으면 된다. 이는 이미 현재의 활성 풀이고, 우리가 위에서 바꿔둔 할당 정책(`StrategyGetBuffer`)에 따라 할당된다.

### 테스트

`buffer_pools = 3`, `shared_buffers = 16MB`(풀마다 2048 프레임)일 때의 예시다. 임계치는 2048의 90%인 1844 프레임이다. 기본값(128MB)으로는 풀 하나가 16384 프레임이라 seal을 보려면 훨씬 큰 테이블이 필요하므로, 테스트하는 동안만 풀을 줄인다. `shared_buffers`는 서버를 다시 켜야 바뀐다.

```bash
psql -d postgres -c "ALTER SYSTEM SET shared_buffers = '16MB'"
pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log restart
psql -d postgres -c "SHOW shared_buffers"
#  shared_buffers
# ----------------
#  16MB
```

`ALTER SYSTEM`은 `postgresql.conf`가 아니라 `$PGBASE/pgdata/postgresql.auto.conf`에 설정을 적고, 이 파일이 `postgresql.conf`보다 나중에 읽힌다. 아래 예시는 이렇게 서버를 막 켠 상태에서 시작한다. 테이블에 약 715블록씩 세 번 넣으면서 활성 풀과 generation을 본다.

```sql
CREATE TABLE t3 (id int, pad text);
SELECT snudbx_active_pool(), snudbx_generation();
--  snudbx_active_pool | snudbx_generation
-- --------------------+-------------------
--                   1 |                 1

INSERT INTO t3 SELECT g, repeat('x', 80) FROM generate_series(1, 50000) g;
SELECT snudbx_active_pool(), snudbx_generation(), snudbx_pool_usage(1, 't3') AS pool1, snudbx_pool_usage(2, 't3') AS pool2;
--  snudbx_active_pool | snudbx_generation | pool1 | pool2
-- --------------------+-------------------+-------+-------
--                   1 |                 1 |   715 |     0

INSERT INTO t3 SELECT g, repeat('x', 80) FROM generate_series(50001, 100000) g;
SELECT snudbx_active_pool(), snudbx_generation(), snudbx_pool_usage(1, 't3') AS pool1, snudbx_pool_usage(2, 't3') AS pool2;
--  snudbx_active_pool | snudbx_generation | pool1 | pool2
-- --------------------+-------------------+-------+-------
--                   1 |                 1 |  1429 |     0

INSERT INTO t3 SELECT g, repeat('x', 80) FROM generate_series(100001, 150000) g;
SELECT snudbx_active_pool(), snudbx_generation(), snudbx_pool_usage(1, 't3') AS pool1, snudbx_pool_usage(2, 't3') AS pool2;
--  snudbx_active_pool | snudbx_generation | pool1 | pool2
-- --------------------+-------------------+-------+-------
--                   2 |                 2 |     0 |   300

SELECT count(*), sum(id) FROM t3;
--  count  |     sum
-- --------+-------------
--  150000 | 11250075000
```

세 번째 `INSERT` 도중 1번 풀이 1844 프레임에 이르러 seal된다. `INSERT`는 테이블을 늘릴 때 앞 페이지의 pin을 놓으므로 1번 풀에 내 pin이 없고, 1번 풀은 바로 디스크에 쓰이고 비워진다. 나머지 300블록은 2번 풀에 들어간다. 1번 풀에 있던 페이지는 디스크에 쓰였으므로 `count`, `sum`이 맞게 나온다.

pin을 쥔 채로 seal하는 경우는 커서로 만들 수 있다.

```sql
BEGIN;
DECLARE c CURSOR FOR SELECT id FROM t3;
FETCH 1 FROM c;
--  id
-- ----
--   1
SELECT snudbx_active_pool(), snudbx_generation(), snudbx_pool_usage(1, 't3') AS pool1, snudbx_pool_usage(2, 't3') AS pool2;
--  snudbx_active_pool | snudbx_generation | pool1 | pool2
-- --------------------+-------------------+-------+-------
--                   1 |                 3 |   602 |     0
```

앞의 `count(*)`가 테이블 2145블록을 읽는 동안 2번 풀도 임계치를 넘어 seal되었다(generation 3). 그 순간에는 스캔이 2번 풀의 페이지를 pin하고 있었으므로 2번 풀은 `SEALED`로 남았다가, 스캔이 다음 페이지로 넘어가며 pin을 놓은 뒤의 확인에서 비워졌다. 1번 풀에는 스캔의 나머지 599블록이 들어갔고, `FETCH`가 블록 0 근처를 다시 읽어 602가 되었다. 이제 커서가 1번 풀의 페이지를 pin하고 있다. 이 상태에서 풀보다 큰 `INSERT`를 한다.

```sql
INSERT INTO t3 SELECT g, repeat('x', 80) FROM generate_series(150001, 300000) g;
SELECT snudbx_active_pool(), snudbx_generation(), snudbx_pool_usage(1, 't3') AS pool1, snudbx_pool_usage(2, 't3') AS pool2;
--  snudbx_active_pool | snudbx_generation | pool1 | pool2
-- --------------------+-------------------+-------+-------
--                   2 |                 4 |  1844 |   901

FETCH 1 FROM c;
--  id
-- ----
--   2
COMMIT;
```

1번 풀이 1844 프레임에 이르러 seal되었지만, 커서의 pin이 남아 있어 비우지 않고 `SEALED`로 두었다(`sealed_pool` = 1). 나머지 901블록은 2번 풀에 들어갔다. 두 번째 `FETCH`는 `SEALED`인 1번 풀에서 페이지를 찾는다. `COMMIT`으로 커서가 닫히면 pin이 모두 풀리고, 다음에 버퍼에 접근할 때 1번 풀이 비워진다.

```sql
SELECT id FROM t3 LIMIT 1;
--  id
-- ----
--   1
SELECT snudbx_active_pool(), snudbx_generation(), snudbx_pool_usage(1, 't3') AS pool1, snudbx_pool_usage(2, 't3') AS pool2;
--  snudbx_active_pool | snudbx_generation | pool1 | pool2
-- --------------------+-------------------+-------+-------
--                   2 |                 4 |     0 |   904
```

`snudbx_*` 함수는 버퍼에 접근하지 않으므로, `COMMIT` 바로 뒤에 부르면 1번 풀이 아직 비워지지 않은 채로 보인다. 마지막으로 서버를 다시 켜도 테이블 내용은 그대로여야 한다.

```bash
pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log restart
psql -d postgres -c "SELECT count(*), sum(id) FROM t3"
#  count  |     sum
# --------+-------------
#  300000 | 45000150000
```

커서를 닫기 전에 `INSERT`를 더 해 2번 풀까지 임계치를 넘기면, 1번 풀이 아직 `SEALED`이므로 `ERROR`가 나야 한다. 실패한 트랜잭션은 `ROLLBACK`되므로 테이블은 그대로다.

```sql
BEGIN;
DECLARE c CURSOR FOR SELECT id FROM t3;
FETCH 1 FROM c;
INSERT INTO t3 SELECT g, repeat('x', 80) FROM generate_series(300001, 450000) g;
INSERT INTO t3 SELECT g, repeat('x', 80) FROM generate_series(450001, 600000) g;
-- ERROR:  all data buffer pools are in use
ROLLBACK;
SELECT count(*), sum(id) FROM t3;
--  count  |     sum
-- --------+-------------
--  300000 | 45000150000
```

**다시 해 보려면**: 이 예시의 숫자(활성 풀, generation, 풀 사용량)는 서버를 막 켠 상태, 즉 활성 풀 1, generation 1에서 시작해야 같게 나온다. 처음부터 다시 해 보려면 `t3`를 지우고 서버를 다시 켠 뒤 `CREATE TABLE t3`부터 실행한다. `shared_buffers`는 16MB로 그대로 둔다.

```bash
psql -d postgres -c "DROP TABLE IF EXISTS t3"
pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log restart
psql -d postgres -c "SELECT snudbx_active_pool(), snudbx_generation()"   # 1 | 1 이어야 한다
```

**끝낼 때**: 테이블을 지우고 `shared_buffers`를 기본값으로 되돌린 뒤 서버를 다시 켠다. 다른 step의 예시는 기본값(128MB)을 기준으로 한다.

```bash
psql -d postgres -c "DROP TABLE IF EXISTS t3" -c "ALTER SYSTEM RESET shared_buffers"
pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log restart
psql -d postgres -c "SHOW shared_buffers"
#  shared_buffers
# ----------------
#  128MB
```

---

## Step 2-4. 워크로드로 설계 비교하기

Step 2-3까지 만든 버퍼 풀은 테스트를 통과한다. 그런데 결과가 맞는 것과 성능이 좋은 것은 다르다. 이 step에서는 같은 워크로드를 **일반 PostgreSQL 버퍼 풀(default 빌드)**과 **우리 버퍼 풀(snudbx 빌드)**에서 돌려 트랜잭션 지연을 비교하고, 우리 설계의 문제와 해법을 `NOTES.md`에 쓴다. 코드는 작성하지 않는다.

### 워크로드

코스 레포의 `pa2/workloads/append_workload`는 테이블 하나에 행을 **덧붙이기만 하는(append-only)** 워크로드다. 로그나 이벤트를 계속 쌓는 상황을 흉내 낸다.

| 항목 | 내용 |
|---|---|
| 트랜잭션 | `INSERT` 하나(행 200개)가 트랜잭션 하나다. 1만 개를 하나씩 차례로 실행한다 |
| 테이블 | 시작할 때 만들고, 끝나면 지운다. 실행 중에는 읽지 않는다 |
| 측정 | 트랜잭션마다 보내고 결과를 받기까지의 시간(지연)을 잰다. 시작 전에 50개를 측정 없이 먼저 돌린다 |
| 접속 | 지금 떠 있는 서버(`PGHOST`, `PGPORT`를 따른다)의 `postgres` 데이터베이스. 다른 데이터베이스는 `-d`로 준다 |
| 출력 | `latency_<빌드>.csv`. 빌드는 서버에 `buffer_pools` 설정이 있는지로 알아낸다. 다른 이름은 `-o`로 준다 |
| 시간과 공간 | 한 번에 10초 정도 걸린다. 실행 중 `pgdata`가 1GB 가까이 커질 수 있다 |

실행 파일만 제공하고 소스는 공개하지 않는다. 실행 중에 `Ctrl-C`로 멈추면 그때까지의 결과를 저장하고 테이블을 지운 뒤 끝난다.

### 실행

두 서버 모두 Step 2-3의 테스트와 같은 설정(`shared_buffers = 16MB`, `buffer_pools = 3`)으로 띄운다. default 빌드는 `buffer_pools`를 모르므로 풀 하나에 16MB를 쓰고, snudbx 빌드는 풀마다 16MB를 쓴다. 두 서버는 같은 `pgdata`를 쓰므로 `shared_buffers`는 한 번만 바꾸면 된다. 실행 파일은 autovacuum이 꺼져 있지 않으면 실행을 거부하고, `shared_buffers`가 16MB가 아니면 경고를 낸다.

```bash
cd ~/2026-2-data-engineering/pa2/workloads

# 1. shared_buffers를 16MB로 (Step 2-3의 테스트를 끝내며 되돌렸다면)
psql -d postgres -c "ALTER SYSTEM SET shared_buffers = '16MB'"

# 2. snudbx 서버에서 실행
pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log restart
./append_workload                       # latency_snudbx.csv

# 3. default 서버에서 실행
pg_ctl -D $PGBASE/pgdata stop
$PGBASE/pgsql-default/bin/pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log start
./append_workload                       # latency_default.csv
$PGBASE/pgsql-default/bin/pg_ctl -D $PGBASE/pgdata stop

# 4. 원래대로: snudbx 서버, shared_buffers 기본값
pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log start
psql -d postgres -c "ALTER SYSTEM RESET shared_buffers"
pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log restart
```

실행하면 서버 정보가 먼저 나온다. 빌드와 설정이 맞는지 확인한다.

```
server:          18.4
build:           snudbx
shared_buffers:  16MB
buffer_pools:    3
running 10000 transactions ...
  1000 / 10000
  ...
wrote 10000 rows to latency_snudbx.csv
```

`latency_*.csv`의 형식은 다음과 같다.

| 열 | 뜻 |
|---|---|
| `txn` | 트랜잭션 번호(1부터) |
| `start_ms` | 첫 트랜잭션을 보낸 때로부터 이 트랜잭션을 보낸 때까지의 시간(ms) |
| `latency_us` | 이 트랜잭션의 지연(µs) |

워크로드는 실행할 때마다 결과가 조금씩 다르다. 빌드마다 두세 번 돌려 경향이 같은지 확인한다(`-o latency_snudbx_2.csv`처럼 이름을 바꾼다). Step 2-3까지의 구현이 틀렸다면 snudbx 서버에서 실행이 실패하거나 결과가 이상하게 나올 수 있다. 이때는 구현부터 고친다.

### 분석 힌트

그래프와 표는 직접 만든다. 도구는 무엇을 써도 된다(Python의 pandas와 matplotlib 등).

* **분포 전체:** 두 빌드의 지연 CDF를 한 그래프에 겹쳐 그린다. 차이가 꼬리에 있다면 x축을 로그 척도로 하거나, y축을 0.99 이상으로 확대한 그래프를 하나 더 그리면 잘 보인다.
* **백분위수:** p50, p95, p99, p99.9, 최댓값을 표로 비교한다. 평균만 보면 놓치는 것이 무엇인지 생각해 본다.
* **시간에 따른 변화:** `start_ms`(또는 `txn`)를 x축, 지연을 y축으로 찍어 본다. 튀는 트랜잭션이 있다면 언제, 얼마나 자주 생기는지, 간격이 일정한지 본다.
* **버퍼 풀 쪽 관찰:** 워크로드 전후에(실행 중이 아니라) 다른 psql로 다음을 조회하면, 그동안 누가 얼마나 읽고 쓰고 교체했는지 알 수 있다. 실행 전에 `SELECT pg_stat_reset_shared('io');`로 통계를 0으로 만든다.

    ```sql
    SELECT backend_type, context, reads, writes, extends, hits, evictions
      FROM pg_stat_io
     WHERE object = 'relation' AND (reads > 0 OR writes > 0 OR extends > 0);
    ```

    snudbx 서버에서는 `snudbx_generation()`도 전후로 비교해 볼 수 있다.

### `NOTES.md`에 답할 질문 (Phase 2)

코스 레포 `pa2/NOTES.md`의 Phase 2 절에 답한다. 표는 `NOTES.md`에 직접 넣는다. 그래프는 이미지 파일(PNG 등)로 저장해 `NOTES.md`와 함께 제출한다. 이미지는 3개 이내로 하고, 답에서 어느 그래프를 말하는지 파일 이름으로 알 수 있게 한다(`NOTES.md`에 링크할 필요는 없다). 여기에서는 각 문항당 4-5 문장 이내로 적는다.

1. 두 빌드의 지연 분포를 CDF 그래프와 백분위수 표(p50, p95, p99, p99.9, 최댓값)로 비교하라. median과 tail에서 각각 어떤 차이가 있는가?
2. snudbx 빌드에는 어떤 문제가 있는가? 그 원인을 Step 2-3의 설계와 연결해 간략히 설명하고, 근거가 된 관찰(그래프, 수치)을 함께 제시하라.
3. 이 문제를 해결하거나 줄일 수 있는 방법을 제안하라. 그 방법이 새로 만드는 비용이나 다른 문제가 있다면 함께 쓴다.


---

## 제출

PA2는 Phase 2까지 끝낸 뒤 **한 번만** 제출한다. Phase 1은 따로 채점하지 않았고(ungraded), 이 제출에서 Phase 1의 항목도 함께 채점한다. Phase 1부터 Phase 2까지의 모든 변경을 담은 패치 하나와, Phase 1·2 질문에 모두 답한 `NOTES.md`, Step 2-4의 그래프 이미지를 Gradescope에 한 번에 올린다.

| 파일 | 내용 |
|---|---|
| `pa2.patch` | 소스 트리의 모든 변경(Phase 1 포함). 이름은 반드시 `pa2.patch` |
| `NOTES.md` | 코스 레포 `pa2/NOTES.md` 양식. Phase 1 질문과 Phase 2 질문에 모두 답한다 |
| 그래프 이미지 | Step 2-4의 그래프. PNG 등, 3개 이내 |

### 패치 만들기

Phase 1과 같은 명령이다. 패치는 소스 트리 안에서 만들되, 파일은 소스 트리 밖(`$PGBASE`)에 저장한다.

```bash
cd $PGBASE/postgres-18
git add -A .
git diff --relative pa2-base > $PGBASE/pa2.patch
```

- 기준이 `pa2-base`이므로 패치에는 Phase 1의 변경과 미리 제공된 코드가 모두 들어간다. Phase 2의 변경만 따로 내지 않는다.
- `git add -A .`의 `.`은 소스 트리 안의 변경만 대상으로 한다는 뜻이다. 새로 만든 파일도 이렇게 해야 패치에 들어간다.
- Step 2-1의 프리페치 코드처럼 문서에서 복사해 넣은 코드도 패치에 들어가야 한다.

패치가 건드릴 수 있는 경로는 Phase 1과 같다. 그 밖의 파일이 포함되면 채점 없이 반려된다.

```
src/include/**
src/backend/utils/init/globals.c
src/backend/utils/misc/guc_tables.c
src/backend/storage/lmgr/lwlock.c
src/backend/storage/snudbx_buffer/**
contrib/snudbx/**
```

### 내기 전에 확인하기

1. **로컬 채점기를 돌린다.** 코스 레포를 갱신했다면 Phase 2의 기초 항목(4.6, 5.1, 6.1)이 추가되어 있다. 모두 통과해야 한다.

    ```bash
    cd ~/2026-2-data-engineering/pa2/local-grader
    ./run_local.sh $PGBASE/postgres-18
    ```

2. **만든 패치가 `pa2-base` 트리에 깨끗이 적용되는지 본다.** Gradescope는 `pa2-base` 상태의 소스 트리에 이 패치를 적용해 채점한다. 위 명령으로 만든 패치라면 대개 문제가 없지만, 손으로 고친 패치는 여기서 걸린다. 지금 작업 중인 트리는 건드리지 않고, `pa2-base` 트리를 임시 디렉터리에 따로 꺼내 확인한다.

    ```bash
    rm -rf /tmp/pa2check && mkdir -p /tmp/pa2check
    git -C ~/2026-2-data-engineering archive pa2-base pa2/PostgreSQL/postgres-18 | tar -x -C /tmp/pa2check
    cd /tmp/pa2check/pa2/PostgreSQL/postgres-18
    git apply --check $PGBASE/pa2.patch && echo "patch OK"
    ```

    `patch OK`가 나오지 않으면 오류에 나온 파일부터 확인하고 패치를 다시 만든다. 확인이 끝나면 `/tmp/pa2check`는 지워도 된다.

3. **각 step의 "테스트" 절 예시를 직접 해 본다.** 로컬 채점기에는 기초 항목만 들어 있고, Gradescope는 훨씬 많은 항목으로 채점한다.

### Gradescope에서 보이는 것

- 점수는 step별로 한 줄씩(Step 1-1 … Step 2-3) 보인다. Step 2-1 extra는 점수 없이 통과 여부만 보인다. 항목별 결과와 실패 이유는 보이지 않는다.
- 패치가 반려되었거나(허용 경로 밖, 적용 실패) 빌드가 실패하면 그 이유가 보인다.
- `NOTES.md`와 이미지는 자동 채점하지 않고 따로 채점한다.
