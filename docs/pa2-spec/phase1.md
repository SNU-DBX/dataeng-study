---
icon: lucide/clipboard-list
---

# PA2 Phase 1 — 버퍼 풀을 여러 개로 초기화하기

**선행 문서**: [PA2 Setup](setup.md) (개발 환경 준비)

**제출물**: `pa2.patch` + `NOTES.md`

---

## 전체 그림

PA2의 목표는 PostgreSQL의 **버퍼 풀을 여러 개로 나누고, 데이터용 버퍼 풀은 데이터가 주기적으로 디스크에 쓰이게 되는 구조로 만드는 것**이다. 지금의 PostgreSQL은 버퍼 풀이 하나뿐이지만 이것을 메타데이터용 풀 하나와 데이터용 풀 둘, 최소 3개로 늘리면 "한 풀을 통째로 디스크에 내려쓰고 새 풀로 넘어간다"는 방식이 가능해지기 때문이다.

**Phase 1은 초기화까지만 다룬다.** 서버가 시작될 때 N개의 풀이 만들어지도록 하는 것이 전부이고, 어느 풀에 페이지를 넣을지는 아직 정하지 않는다. 다음과 같은 step으로 나누어서 진행한다.

| Step | 내용 |
|---|---|
| 1-1 | `buffer_pools` 설정 추가하기 |
| 1-2 | 풀 제어 구조체와 관찰 함수 |
| 1-3 | 풀마다 자기 자료구조 갖기 |

---

## 가정

과제를 단순하게 만들기 위해 다음을 가정한다. 이 가정을 벗어나는 상황은 구현에서 고려하지 않아도 되고, 채점도 이 범위 안에서만 한다. 이 가정들이 지금 정확히 이해되지 않으면 넘어가도 무방하다.

- **쓰기를 하는 backend는 한 번에 하나다.** 따라서 새로운 락을 추가할 필요가 없다.
- **`io_method = sync`로 서버를 띄운다.** PostgreSQL 18의 비동기 I/O 경로는 신경 쓰지 않아도 된다.
- **`shared_buffers`는 풀 하나의 크기다.** 모든 풀은 같은 수(`NBuffers`)의 프레임을 갖는다.
- **0번 풀은 메타데이터 전용이고, 1번부터가 데이터 풀이다.** 인덱스, 시스템 카탈로그 등은 0번 풀에, 일반 테이블의 데이터는 데이터 풀에 들어간다. `buffer_pools`는 0번 풀을 포함한 전체 풀의 개수다.
- **`initdb`와 `CREATE DATABASE`는 default 빌드 서버로만 한다.** snudbx 빌드 서버에서는 이미 만들어진 데이터베이스만 사용한다.
- **복제(replication)나 스탠바이 서버는 사용하지 않는다.**

!!! info "`CREATE DATABASE`를 snudbx 서버에서 하지 않는 이유"
    `CREATE DATABASE`는 원본 데이터베이스(보통 `template1`)의 모든 페이지를 테이블 정보(`Relation`) 없이 읽어서 새 데이터베이스로 복사한다. 테이블 정보가 없으면 그 페이지가 인덱스인지 테이블 데이터인지 알 수 없으므로 0번 풀로 보내게 된다. 그런데 나중에 같은 테이블을 평소처럼 읽으면 데이터 풀로 가므로, 한 페이지가 두 풀에 따로 들어가 서로 다른 내용을 갖게 될 수 있다. 이를 막으려면 페이지를 찾을 때마다 모든 풀을 뒤져야 하는데, 그 비용이 크기 때문에 대신 이런 경우가 생기지 않는다고 가정한다. 복제를 쓰지 않는 이유도 같다.

---

## Step 1-1. `buffer_pools` 설정 추가하기

버퍼 풀을 실제로 만들기에 앞서, 사용자가 풀의 개수를 지정할 수 있는 설정값을 추가한다. 
우리의 코드 변경사항이 기본 PostgreSQL 빌드에 영향을 미치지 않도록 `#ifdef SNUDBX`를 사용해야 하는 것을 잊으면 안 된다.

### 목표

정수형(Integer) 서버 설정(configuration)값으로 `buffer_pools`를 추가한다(이름을 반드시 이것으로 해야한다).

* **요구사항**
    - 서버를 시작할 때에만 지정 가능하도록 해야 한다(서버 구동 중에는 변경 불가능).
    - 값은 3 이상 `MAX_BUFFER_POOLS`(= 8) 이하이며, 기본값은 3으로 한다(`MAX_BUFFER_POOLS`는 이미 `src/include/storage/lwlock.h`와 `bufmgr.h`에 정의되어 있음).
    - `GUC_NOT_IN_SAMPLE` 플래그를 지정한다.
    - `src/backend/utils/init/globals.c`에 `NBufferPools`를 정의하고, 사용자의 설정값을 받아서 넣도록 한다.

!!! info "실제로 쓰는 풀은 3개"
    이 과제에서는 META 풀 하나와 DATA 풀 둘, 즉 3개만 사용한다. 3보다 큰 값은 나중에 확장할 수 있도록 열어 둔 것이다. 다만 Step 1-2와 1-3에서 할당하는 자료구조는 설정값만큼 만들어져야 하고, 채점도 3보다 큰 값으로 이를 확인한다.

### 구현 내용

#### `src/include/storage/lwlock.h`, `bufmgr.h` — 상한값 정의

`MAX_BUFFER_POOLS`는 **8**로 이미 정의되어 있으므로 별도로 구현하지 않아도 된다. `lwlock.h`는 `bufmgr.h`를 include할 수 없어서, 두 파일에 같은 값으로 각각 정의해 두었다. 값을 바꿀 때는 두 곳을 함께 바꿔야 한다. 여기서 `#ifdef SNUDBX` ~ `#endif`로 인해, 만약 우리가 기본 PostgreSQL을 빌드하게 되면 이 코드는 아예 존재하지 않는 것이나 마찬가지가 된다. 
```c
#ifdef SNUDBX
#define MAX_BUFFER_POOLS 8
#endif
```

#### (TODO) `src/backend/utils/init/globals.c` — 변수 정의

`int NBuffers = 16384;`를 찾아, 그 아래에 `NBufferPools`를 **3**으로 초기화한다. 위치는 크게 문제가 안 되지만 비슷한 것들끼리 함께 있는 게 보기에 편하다.

```c
int			NBuffers = 16384;   /* 원래 있던 줄 */
#ifdef SNUDBX
int			NBufferPools = 3;   /* 추가 */
#endif
```

#### (TODO) `src/include/miscadmin.h` — 변수 선언

`extern PGDLLIMPORT int NBuffers;`가 있는 줄을 찾아, 바로 아래에 같은 형식으로
`NBufferPools`를 선언한다.

```c
extern PGDLLIMPORT int NBuffers;        /* 원래 있던 줄 */
#ifdef SNUDBX
extern PGDLLIMPORT int NBufferPools;    /* 추가 */
#endif
```

!!! info "PGDLLIMPORT란?"
    `PGDLLIMPORT`는 확장 모듈(extension)에서도 이 변수를 참조할 수 있게 하는 표시이다.

#### (TODO) `src/backend/utils/misc/guc_tables.c` — 설정 등록

Step 1-1의 핵심이다. 코드의 형태를 전체적으로 확인한 후, 적절한 위치에 **`buffer_pools`라는 이름으로** 항목을 하나 추가하면 된다. 다른 설정값들이 어떻게 생겼는지를 보면서 각 필드의 의미를 유추하거나, 검색하여 작성한다.

**힌트**: `"shared_buffers"` 항목을 찾아서 스타일을 따라해 본다. 아래에서 16384는 기본값, 16은 최소값, INT_MAX / 2는 최대값이다. `GUC_UNIT_BLOCKS`가 들어가는 자리에는 각종 플래그를 지정할 수 있는데, 우리는 `GUC_NOT_IN_SAMPLE` 플래그 하나만을 사용하기로 한다.

```
    {
        {"shared_buffers", PGC_POSTMASTER, RESOURCES_MEM,
            gettext_noop("Sets the number of shared memory buffers used by the server."),
            NULL,
            GUC_UNIT_BLOCKS
        },
        &NBuffers,
        16384, 16, INT_MAX / 2,
        NULL, NULL, NULL
    },
```

!!! info "`GUC_NOT_IN_SAMPLE`이 필요한 이유"
    `postgresql.conf.sample`은 PostgreSQL을 설치할 때 함께 깔리는 설정 파일 견본으로, 처음에 데이터베이스를 초기화할 때 이 파일을 복사해서 각 데이터베이스별 설정값 파일인 `postgresql.conf`를 만든다.

    문제는 이것이 데이터 파일로 취급되기 때문에 `#ifdef`를 쓸 수 없다는 점이다. 그런데 PostgreSQL은 견본 파일과 실제 설정값 파일의 목록이 일치하는지 검사하게 되어서, 이것이 불일치하면 문제가 될 수 있다. `GUC_NOT_IN_SAMPLE`은 바로 이런 경우를 위해 PostgreSQL이 마련해 둔 표시로, 해당 설정을
    검사 대상에서 제외해 준다.

### 테스트

각각의 빌드를 거쳐 구동한 서버에서는 다음과 같이 각각 다른 출력 결과가 나와야 한다.

**snudbx 서버**

```sql
SELECT vartype, context, min_val, max_val, boot_val
  FROM pg_settings WHERE name = 'buffer_pools';
-- integer | postmaster | 3 | 8 | 3

SELECT pg_settings_get_flags('buffer_pools');
-- {NOT_IN_SAMPLE}

SET buffer_pools = 4;
-- ERROR:  parameter "buffer_pools" cannot be changed without restarting the server
```

**default 서버**

```sql
SELECT count(*) FROM pg_settings WHERE name = 'buffer_pools';
-- 0

SHOW buffer_pools;
-- ERROR:  unrecognized configuration parameter "buffer_pools"
```

---

## Step 1-2. 풀 제어 구조체와 관찰 함수

버퍼 풀이 여러 개가 되고, 동시에 두 개 이상의 풀을 같이 사용하지 않을 때, 반드시 지금 어떤 풀을 쓰고 있는지를 알 수 있어야 한다. 모든 프로세스들이 동일한 값을 공유하면서 보아야 하므로, 공유 메모리 상에 존재해야 한다. 

먼저 여기에서는 버퍼 풀 자체를 여러 개 만들기에 앞서서, 풀을 관리하기 위한 정보를 적어 둘 곳을 만들고, 이것을 외부에서 관찰할 수 있는 기능을 준비한다.

### 목표

목표1: 버퍼 풀을 제어하는 구조체를 정의하고, 공유 메모리를 할당 받아서 여기에 초기화시킨다.

* **요구사항**
    - 아래에서 제공될 요건에 맞추어 버퍼 풀 제어 구조체를 정의한다.
    - `"Buffer Pool Control"`이라는 이름으로 공유 메모리를 할당받는다.
    - 서버가 켜진 직후의 상태는 **활성 풀 1번, 세대(generation) 1**이다. 0번 풀은 인덱스·카탈로그 등을 담는 메타데이터 전용 풀로 항상 사용되고, 1번부터가 테이블 데이터를 담는 데이터 풀이다(Step 1-4에서 다룬다).
  
목표2: `contrib/snudbx` 확장 모듈을 통해 아래 세 함수를 제공한다(상세 내용 아래 참고). 이 함수들은 SQL 인터페이스를 통해 버퍼 풀의 상태를 확인할 수 있게 해 준다.

| SQL 시그니처 | 반환값 |
|---|---|
| `snudbx_pool_count() → integer` | 풀의 개수 |
| `snudbx_active_pool() → integer` | 테이블 데이터가 들어가는 풀의 번호 (Phase 1에서는 항상 1) |
| `snudbx_generation() → bigint` | 1에서 시작하는 카운터. 뒤 Phase에서 풀을 갈아 끼울 때마다 1씩 증가 |

### 구현

#### (TODO) `src/include/storage/buf_internals.h` — 구조체 정의

두 개의 구조체가 필요하다. 먼저, 버퍼 **풀 하나마다** 어떤 상태인지를 관리하기 위한 풀 기술자(Pool Descriptor)를 정의해야 하나, 코드에서 이를 먼저 제공한다.
```c
typedef struct BufferPoolDesc
{
       pg_atomic_uint32 state;         /* a PoolState */
       pg_atomic_uint32 next_free_slot;        /* first slot never handed out */
} BufferPoolDesc;
```

참고로 여기서 `PoolState`로 사용되는 것은 다음과 같이 `enum`으로 정의되어 있다. 추후에 여기에 새로운 상태를 추가할 수도 있지만, 지금은 준비 중인 것과 활성화된 것 두 가지로만 나누어도 충분하다.

```c
typedef enum PoolState
{
    POOLSTATE_READY,    /* empty, available to become active */
    POOLSTATE_ACTIVE,   /* new pages are placed here */
} PoolState;
```

이제 정의해야 하는 것은 풀을 관리하는 제어 구조체다. 여기에는 최소한 세 가지의 멤버변수가 필요하다. 변수의 명칭은 자유롭게 결정해도 되지만, 각각 다음과 같은 이름을 추천한다. '세대 카운터'의 역할이 무엇인지 지금 정확히 모르더라도 일단 만들고 초기화해 놓는다.

  - 현재 활성 풀의 번호 (`int active_pool`)
  - 세대 카운터 (`uint64 generation`)
  - `BufferPoolDesc`의 배열(`BufferPoolDesc pools[FLEXIBLE_ARRAY_MEMBER]`). 이는 각 풀마다 하나씩, 총 **`NBufferPools`개**의 array 형태를 갖는다.

아래와 같이 제공된 빈 칸 내를 채워서 정의한다.
```c
typedef struct BufferPoolControl
{
  // your member variables
} BufferPoolControl;
```

그런데 여기서 `NBufferPools`는 서버가 시작될 때 정해지므로, 코드를 쓰면서 구조체를 정의하는 시점에는 배열의 길이를 알 수 없다. 이때 사용할 수 있는 방법은 구조체의 **마지막 필드**에 길이 없는 배열로 선언해 둔 뒤에, 이 구조체에 충분한 메모리를 사후적으로 런타임 중에 할당함으로써 우리가 원하는 길이를 맞춰주는 방식이다. 이렇게 하면 `sizeof(BufferPoolControl)`은 배열을 **포함하지 않는** 크기가 된다.

PostgreSQL 소스에서 `FLEXIBLE_ARRAY_MEMBER`를 검색하면 같은 방식의 예를 많이 볼 수 있다. 예를 들어서 `src/backend/storage/ipc/procarray.c`에 있는 `pgprocnos[FLEXIBLE_ARRAY_MEMBER];`가 그런 방식의 정의다. `FLEXIBLE_ARRAY_MEMBER`는 사실 아무 값도 없기 때문에 이 배열은 **이름만** 존재하게 된다.

구조체 정의 아래에는 이 구조체를 가리킬 전역 포인터를 선언한다. 이게 있어야 다른 곳에서 이 구조체에 접근가능하다.

```c
extern PGDLLIMPORT BufferPoolControl *BufferPoolCtl;
```

#### (TODO) `src/backend/storage/snudbx_buffer/buf_init.c` — 할당과 초기화

이 파일은 `snudbx_buffer/` 안에 있으므로 `#ifdef SNUDBX`를 사용하지 않고 자유롭게 수정해도 된다.

여기서 해야 할 것들을 소개하면 다음과 같다.

1. **포인터 정의.** 파일 위쪽에 `BufferDescriptors`, `BufferBlocks` 같은 전역 변수가
   정의된 곳이 있다. 이를 흉내내어 `BufferPoolCtl`를 정의한다. 이것이 `BufferPoolControl` 자체가 아닌 그것의 포인터 자료형임에 주의한다.
2. **공유 메모리 크기 계산.** `BufferManagerShmemSize()`는 버퍼 풀이 필요로 하는 공유 메모리의 양을 계산하여 반환한다. 여기에 우리가 새롭게 추가하고자 하는 BufferPoolControl이 차지할 크기를 추가해 주어야 한다. 이 함수는 `+`와 `*` 대신 `add_size()`, `mul_size()`를 쓰는데, 오버플로를 검사해 주는 함수이므로 이 방식을 흉내내면 된다. 단순히 `sizeof(BufferPoolControl)`만으로는 부족하며, `NBufferPools`개의 `BufferPoolDesc`의 크기를 더해주어야 함에 유의한다.
3. **할당.** `BufferManagerShmemInit()`에서 `ShmemInitStruct()`로 영역을 받아
   `BufferPoolCtl`에 넣는다. 다른 버퍼 풀 구성요소들이 어떻게 이를 수행하는지 보고 따라하면 된다. 이름은 정확히 `"Buffer Pool Control"`로 제공하도록 한다. 여기서는 2번에서 제공한 사이즈와 동일한 사이즈를 넣어준다.
4. **초기화.** 제어 구조체가 새로 만들어진 경우에만 값을 채워준다(`found~`와 관련된 처리 방식을 참고하라). 활성 풀은 1, 세대는 1로 두고, 풀마다 있는 `BufferPoolDesc`의 멤버들도 `NBufferPools`개 모두 순회하면서 초기화한다. 항상 사용되는 0번 풀과 활성 풀인 1번 풀은 `PoolState`를 `POOLSTATE_ACTIVE`로, 나머지는 `POOLSTATE_READY`로 주면 되고, `next_free_slot`은 모두 0으로 시작한다.

!!! info "세대를 1에서 시작하는 이유"

    세대가 0이 아니라 **1**에서 시작하는 데에는 이유가 있다. 0을 "아직 아무 세대도
    아님"을 뜻하는 값으로 남겨 두기 위해서이다. 나중에 사용되는 부분이니 지금은 이렇게 해놓고 넘어간다.

#### (TODO) `contrib/snudbx/` — 확장 모듈

이 PostgreSQL 확장 모듈은 C 함수를 SQL에서 부를 수 있게 해 주는 장치이다. 모두 4개의 파일로 이루어지며, `Makefile`과 `snudbx.control`은 새로 만들고, `snudbx--1.0.sql`과 `snudbx.c`는 이미 제공된 파일에 내용을 추가한다.

**`contrib/snudbx/Makefile`**: 이 파일을 만들고 아래 내용을 그대로 복사 붙여넣기

```make
MODULES = snudbx

EXTENSION = snudbx
DATA = snudbx--1.0.sql

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/snudbx
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
```

**`contrib/snudbx/snudbx.control`**: 이 파일을 만들고 아래 내용을 그대로 복사 붙여넣기

```
comment = 'observability functions for the buffer pools'
default_version = '1.0'
module_pathname = '$libdir/snudbx'
relocatable = true
```

**`contrib/snudbx/snudbx--1.0.sql`** — SQL 함수 이름과 C 함수를 연결한다. 제공된 파일에는 세 개 함수 중 하나만 아래와 같이 정의되어 있다. 나머지 둘은 위에서 나왔던 함수 3개의 표를 이용해서 직접 추가한다.

```sql
CREATE FUNCTION snudbx_pool_count()
RETURNS integer
AS 'MODULE_PATHNAME', 'snudbx_pool_count'
LANGUAGE C STRICT;
```

**`contrib/snudbx/snudbx.c`** — 실제 C 함수다. 제공된 파일에는 아래 하나만 정의되어 있다. 나머지 둘을 유사한 방식으로 추가해보라. 반환할 자료형은 `Datum`으로 하면 되며, `src/include/fmgr.h`에 가보면 `PG_RETURN_INT32`를 비롯한 다양한 매크로들이 정의되어 있다. 이를 잘 참고하라.

```c
#include "postgres.h"

#include "fmgr.h"
#include "miscadmin.h"
#include "storage/buf_internals.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(snudbx_pool_count);

Datum
snudbx_pool_count(PG_FUNCTION_ARGS)
{
	PG_RETURN_INT32(NBufferPools);
}
```

몇 가지 주의 사항은 다음과 같다.
- `PG_MODULE_MAGIC`은 파일에 **한 번만** 쓴다.
- `PG_FUNCTION_INFO_V1(...)`은 **함수마다** 하나씩 쓴다.
- `snudbx_generation()`은 내부적으로 C에서는 `uint64`인 값을 SQL 입장에서는 `bigint`로 돌려주는데, SQL에는
부호 없는(unsigned) 정수 타입이 없기 때문이다. 따라서 반환하기 전에 명시적으로 `(int64)`를 이용하여 캐스팅해주어야 한다.

#### 확장 모듈 빌드와 등록

`contrib/snudbx`는 PostgreSQL 전체 빌드에 포함되어 있지 않으므로 따로 빌드한다. 이때 **설치된** snudbx 빌드의 헤더를 기준으로 컴파일되므로, 헤더를 고쳤다면 먼저 snudbx 빌드를 다시 `make install`한 뒤에 빌드해야 한다.

```bash
make -C $PGBASE/build-snudbx -j$(nproc) install       # 서버를 다시 빌드 후 설치한다.
make -C $PGBASE/postgres-18/contrib/snudbx USE_PGXS=1 PG_CONFIG=$PGBASE/pgsql/bin/pg_config install
```

서버를 다시 시작한 뒤, 데이터베이스에 확장 모듈을 한 번 등록한다.

```sql
CREATE EXTENSION snudbx;
```

`.sql` 파일은 등록하는 시점에 한 번만 읽힌다. 함수를 추가하는 등 `.sql` 파일을 고쳤다면 `DROP EXTENSION snudbx;` 후 다시 등록한다.

### 테스트

구조체가 우리가 기대한 대로 제대로 공유 메모리를 확인받았는지 확인한다.

```sql
SELECT name, size FROM pg_shmem_allocations WHERE name = 'Buffer Pool Control';
--         name         | size
-- ---------------------+------
--  Buffer Pool Control |   40
```

여기서 `size`의 구체적인 값은 필드 구성이나 순서 등에 따라 달라질 수 있다. 더욱 중요한 것은 풀 개수에 따라서 달라지는지 여부다. 설정값 파일(`postgresql.conf`)에서 `buffer_pools`를 3, 4, 8로 바꿔 서버를 다시 띄우면서 이 값을 확인해 볼 수 있다.

우리가 만든 관찰함수들도 잘 작동하는지 다음과 같이 사용해 본다.

```sql
SELECT snudbx_pool_count(), snudbx_active_pool(), snudbx_generation();
--  snudbx_pool_count | snudbx_active_pool | snudbx_generation
-- -------------------+--------------------+-------------------
--                  3 |                  1 |                 1
```

---

## Step 1-3. 풀마다 자기 자료구조 갖기

지금까지는 풀의 개수와 상태만 기록했을 뿐, 실제 버퍼 풀은 여전히 하나다. 이 step에서는 버퍼 풀을 이루는 자료구조들을 **풀마다 하나씩** 따로 만든다. 다만 Step 1-3에서는 추가된 풀들을 사용하지 않고, 모든 페이지를 가장 첫번째(즉 0번) 버퍼 풀에만 넣어서 사용하게 된다. 

### 목표

풀마다 아래 일곱 가지를 따로 갖도록 한다.

| 구성요소 | 공유 메모리 이름 | 원래 있는 곳 |
|---|---|---|
| 버퍼 기술자 배열 | `Buffer Descriptors %d` | `buf_init.c` |
| 페이지 프레임(블록) 배열 | `Buffer Blocks %d` | `buf_init.c` |
| I/O CV 배열 | `Buffer IO Condition Variables %d` | `buf_init.c` |
| 체크포인트용 메타데이터 배열 | `Checkpoint BufferIds %d` | `buf_init.c` |
| 버퍼 해시 테이블 | `Shared Buffer Lookup Table %d` | `buf_table.c` |
| 교체 정책 관련 변수 (free list, clock sweep) | `Buffer Strategy Status %d` | `freelist.c` |
| 버퍼 파티션 락 | (공유 메모리 이름 없음) | `lwlock.h` |

* **요구사항**
    - 공유 메모리를 할당받는 것들은 `ShmemInitStruct()`에 위 표에서 제시한 이름을 제공한다. 여기서 `%d`는 풀 번호다. 예를 들어서 0번 풀의 버퍼 기술자 배열은 공유 메모리에 "Buffer Descriptors 0"과 같은 이름으로 할당되어야 한다.
    - **중요**: 각 풀은 `NBuffers`개의 프레임을 갖고, 버퍼 번호(`buf_id`)는 풀 경계를 넘어 하나로 이어진다. 즉 `buf_id = pool_id × NBuffers + slot_id`이다. 이 규칙을 준수하기 위한 아래 세 매크로(`NTotalBuffers`, `BufferPoolIdOf`, `BufferSlotIdOf`)를 `buf_internals.h`에 정의한다. 이름도 반드시 이와 동일하게 해야 한다.
```c
#define NTotalBuffers			(NBuffers * NBufferPools)
#define BufferPoolIdOf(buf_id)	((buf_id) / NBuffers)
#define BufferSlotIdOf(buf_id)	((buf_id) % NBuffers)
```
    - 몇몇 함수들은 버퍼 풀 ID를 인자로 받도록 수정되어야 한다. 이들의 경우 아래에 제공될 함수들의 시그니처를 그대로 따른다.
    - 이 step에서는 우선 0번 풀만 사용하는 것으로 생각하고 구현한다.


### 구현

구현 과정에서 성격이 유사한 것들을 아래와 같이 A, B, C로 분류하여 소개한다. 실제 구현 시 순서 등은 무관하다.

| 분류 | 내용 | 고치는 파일 |
|---|---|---|
| A | 배열 4종과 그 접근 함수 | `buf_internals.h`, `bufmgr.h`, `buf_init.c`, `bufmgr.c` |
| B | 버퍼 해시 테이블, 교체 정책 상태 | `buf_table.c`, `freelist.c`, `buf_internals.h`, `bufmgr.c` |
| C | 버퍼 파티션 락 | `lwlock.h`, `lwlock.c`, `buf_internals.h`, `bufmgr.c` |

`snudbx_buffer/`안에 들어있는 파일들은 자유롭게 수정해도 된다. 하지만 그 밖에 있는 파일(`lwlock.h`, `lwlock.c`, `buf_internals.h`, `bufmgr.h`)은 공용 파일이므로, 기존 줄을 바꿀 때는 `#ifdef SNUDBX` / `#else` / `#endif`로 원래 줄을 남겨 두어야 한다.

#### (TODO) 배열을 풀마다 할당하기 (A)

* 네 배열 `BufferDescriptors`, `BufferBlocks`, `BufferIOCVArray`, `CkptBufferIds`가 전역 변수로 정의된 곳을 찾는다. 지금은 각각 공유 메모리의 배열 하나를 가리키는 포인터인데, 이를 **풀마다 하나씩, 포인터의 배열**로 바꾼다. 여기서는 사용 여부와 무관하게 미리 `MAX_BUFFER_POOLS`개만큼의 포인터를 준비해 둔다. 그러면 `BufferDescriptors[p]`는 `p`번 풀의 기술자 배열의 시작 주소가 된다.

```c
// 예시
BufferDescPadded *BufferDescriptors[MAX_BUFFER_POOLS];
```

* 이것에 대한 실제 공유 메모리가 할당되는 함수로 가서 네 가지 배열을 풀마다 각각 할당한다. `ShmemInitStruct()`에 제공할 이름은 위에 나온 표를 따라 `snprintf()`를 활용하여 만든다. 물론, `BufferManagerShmemSize()`에서 이를 위해 미리 사이즈를 풀 개수만큼 곱해서 늘려줘야한다.

* 버퍼 기술자는 `NTotalBuffers`개 전체를 초기화하되, `freeNext`로 이어지는 free list는 **풀마다 따로** 이어져야 한다. 즉 각 풀의 마지막 슬롯은 다음 풀의 첫 버퍼가 아니라 `FREENEXT_END_OF_LIST`를 가리킨다.

#### (TODO) 선언과 접근 함수 변경 (A)

* 위 전역 변수들의 `extern` 선언은 `buf_internals.h`(`BufferDescriptors`, `BufferIOCVArray`, `CkptBufferIds`)와 `bufmgr.h`(`BufferBlocks`)에 있다. 바꾼 정의에 맞춰 선언도 바꾼다.

* 그다음 이 배열에 접근하는 함수들이 `buf_id`를 (풀 번호, 슬롯 번호)로 나눠 두 단계로 찾아가도록 바꾼다. 위에서 정의한 `BufferPoolIdOf`, `BufferSlotIdOf`를 이용한다.

| 함수 | 위치 |
|---|---|
| `GetBufferDescriptor()` | `buf_internals.h` |
| `BufferDescriptorGetIOCV()` | `buf_internals.h` |
| `BufferGetBlock()` | `bufmgr.h` |
| `BufHdrGetBlock()` | `bufmgr.c` |

* `bufmgr.h`의 `BufferIsValid()`에는 버퍼 번호가 `NBuffers`보다 크지 않은지 확인하는 `Assert`가 있다. 이를 적절히 고친다. 
    - `bufmgr.h`는 `miscadmin.h`를 include하지 않으므로, `NBufferPools`를 쓰려면 `bufmgr.h`에도 `extern` 선언을 추가해야 한다(`bufmgr.h`에 있는 `NBuffers` 선언도 같은 이유로 `miscadmin.h`와 중복되어 있으므로 이를 따라한다고 생각하면 된다).

* `bufmgr.c`의 `AssertNotCatalogBufferLock()`도 assertion 빌드(`--enable-cassert`)에서만 컴파일되는 코드다. 이 함수는 어떤 락의 주소가 `BufferDescriptors` 배열 범위 안에 있는지로 그 락이 버퍼 락인지 판단한다. 이제 이 범위는 풀마다 따로 있으므로, 모든 풀의 범위를 차례로 확인해서 어느 한 풀의 범위 안에라도 있으면 버퍼 락으로 보도록 고친다.

#### (TODO) 풀별 매핑 테이블 (B)

* 해시 테이블을 가리키는 `SharedBufHash`를 풀마다 하나씩 두고(`MAX_BUFFER_POOLS` 크기의 배열), 해시 테이블을 사용하는 모든 함수가 풀 번호를 받아 그 풀의 테이블을 쓰도록 바꾼다(정확히 아래와 같은 인자를 받도록 바꾼다). 해시 테이블 이름은 각각 `"Shared Buffer Lookup Table %d"`이다. 
* `buf_internals.h`의 선언도 함께 바꿔준다.

```c
void   InitBufTable(int pool_id, int size);
uint32 BufTableHashCode(int pool_id, BufferTag *tagPtr);
int    BufTableLookup(int pool_id, BufferTag *tagPtr, uint32 hashcode);
int    BufTableInsert(int pool_id, BufferTag *tagPtr, uint32 hashcode, int buf_id);
void   BufTableDelete(int pool_id, BufferTag *tagPtr, uint32 hashcode);
```

* **이들을 바꾼 뒤에는, 실제로 `bufmgr.c` 내에서 이들을 호출하는 부분도 고쳐야 하는데, 이는 이후에 설명한다.**

#### (TODO) 풀별 교체 정책 (B)

빈 프레임 목록(free list)과 clock sweep 상태를 담는 `BufferStrategyControl`을 풀마다 하나씩 둔다. 각 풀은 **자기가 가진 프레임 안에서만** 빈 프레임을 찾고 교체 대상을 고른다. 대략 다음과 같은 함수들에 주의해서 수정한다.

- `StrategyShmemSize()`, `StrategyInitialize()`: 풀마다 매핑 테이블(`InitBufTable`)과 `"Buffer Strategy Status %d"`를 만든다. 여기서 `firstFreeBuffer`, `lastFreeBuffer`, `nextVictimBuffer` 값의 초기화에 유의한다. 아래의 다른 로직들과 함께 고려하여 결정하여야 한다.
- `ClockSweepTick()`: 풀 번호를 받아 그 풀의 시계 바늘을 돌리고, 그 풀 안의 `buf_id`를 돌려준다. 시계바늘이 가리키는 숫자가 넘쳐서 되감는(wrap around) 경우의 처리에 유의한다.
```c
uint32 ClockSweepTick(int pool_id);
```

- `StrategyGetBuffer()`: 풀 번호를 받아서 이를 바탕으로 처리한다. 다른 부분들이 잘 구현되었다면 이 부분은 다소 기계적인 변경이 된다. 시그니처는 아래와 같다.
```c
BufferDesc *StrategyGetBuffer(int pool_id, BufferAccessStrategy strategy,
                              uint32 *buf_state, bool *from_ring);
```

- `StrategyFreeBuffer()`: 시그니처는 그대로 두고, 버퍼의 `buf_id` 정보를 사용하여 어느 풀의 strategy 구조체를 사용하여 접근할지 정한다.
- `GetBufferFromRing()`: 링 버퍼는 대량 데이터 읽기 등을 위해 사용되는 특수한 경우다. 내가 보려는 현재 칸에 다른 풀의 버퍼가 들어 있으면 쓰지 않는다. 마치 valid하지 않은 버퍼를 보았을 때와 유사하게 처리하면 된다.
```c
BufferDesc *GetBufferFromRIng(int pool_id, BufferAccessStrategy strategy, uint32 *buf_state);
```

- `StrategySyncStart()`, `StrategyNotifyBgWriter()`는 시그니처를 그대로 두고, 0번 풀에 대해서만 동작하도록 한다. 이것에 대해서는 다음 스텝에서 다시 살펴볼 것이다. 

#### (TODO) 풀별 파티션 락 자리 만들기 (C)

매핑 해시 테이블은 동시 접근을 줄이기 위해 128개(`NUM_BUFFER_PARTITIONS`) 구간으로 나뉘고, 구간마다 락이 하나씩 있다. 이 락들은 `MainLWLockArray`라는 하나의 큰 배열 안에 다른 락들과 함께 들어 있으며, 각 종류의 락이 배열의 어디서부터 시작하는지는 아래처럼 정해져 있다(`src/include/storage/lwlock.h`).

```c
#define BUFFER_MAPPING_LWLOCK_OFFSET	NUM_INDIVIDUAL_LWLOCKS
#define LOCK_MANAGER_LWLOCK_OFFSET		\
	(BUFFER_MAPPING_LWLOCK_OFFSET + NUM_BUFFER_PARTITIONS)
#define PREDICATELOCK_MANAGER_LWLOCK_OFFSET \
	(LOCK_MANAGER_LWLOCK_OFFSET + NUM_LOCK_PARTITIONS)
```

풀마다 128개씩 락을 가지려면, 매핑 락 구간 바로 뒤에 오는 `LOCK_MANAGER_LWLOCK_OFFSET`을 그만큼 뒤로 밀어야 한다. 이때 얼마나 밀지는 `buffer_pools`가 아니라 **`MAX_BUFFER_POOLS`**를 기준으로 한다. 즉 `MAX_BUFFER_POOLS × NUM_BUFFER_PARTITIONS`개의 자리를 잡아 둔다.

#### (TODO) 늘어난 락 초기화 (C)

락은 `CreateLWLocks()`에서 `InitializeLWLocks()`를 통해 초기화된다(`src/backend/storage/lmgr/lwlock.c`). 여기서도 위에서 늘린 개수만큼 모두 초기화하도록 바꾼다.

#### (TODO) 풀 번호로 락 찾기

`bufmgr.c` 파일에서 보면 `BufMappingPartitionLock()`과 같은 함수를 이용하여 락을 찾아서 반환한다. 다음 두 함수가 풀 번호를 받아 그 풀의 락 구간에서 락을 찾도록 바꾼다(`src/include/storage/buf_internals.h`).
```c
LWLock *BufMappingPartitionLock(int pool_id, uint32 hashcode);
LWLock *BufMappingPartitionLockByIndex(int pool_id, uint32 index);
```

#### (TODO) 호출부 수정 (A·B·C)

위에서 여러 함수들의 시그니처를 바꾸었다. 이렇게 되면 실제로 해당 함수들을 부르는  `src/backend/storage/snudbx_buffer/bufmgr.c`에서도 변경된 시그니처에 맞게 호출해야 한다. 최대한 찾아서 수정하되, 빌드 과정에서 발생하는 컴파일 오류를 보면서 정확히 어디를 놓쳤는지 확인가능하다. 현재는 어떤 풀을 써야하는지 명확하게 정해져 있지 않기 때문에 다음과 같은 규칙을 사용한다.

| 경우 | 함수 | 넘길 풀 번호 |
|---|---|---|
| 페이지 식별자(태그)로 찾거나 새로 넣을 때 | `PrefetchSharedBuffer()`, `BufferAlloc()`, `ExtendBufferedRelShared()`, `FindAndDropRelationBuffers()`, `GetVictimBuffer()` | `0` (이후 변경되지만 일단 0으로 한다) |
| 이미 프레임을 알고 있을 때 | `InvalidateBuffer()`, `InvalidateVictimBuffer()` | `BufferPoolIdOf(buf_id)` |

참고로 `BufferSync()`(체크포인팅을 위한 함수)는 `CkptBufferIds` 대신 0번 풀의 배열(`CkptBufferIds[0]`)을 쓰면 된다. 

### 테스트

snudbx 서버를 띄우고, 풀마다 영역이 하나씩 생겼는지 확인한다. 아래는 `buffer_pools = 3`, `shared_buffers`가 기본값(128MB)일 때의 결과다.

```sql
SELECT regexp_replace(name, ' [0-9]+$', '') AS name, count(*), min(size) AS size
  FROM pg_shmem_allocations WHERE name ~ ' [0-9]+$' GROUP BY 1 ORDER BY 1;
--              name              | count |   size
-- -------------------------------+-------+-----------
--  Buffer Blocks                 |     3 | 134221824
--  Buffer Descriptors            |     3 |   1048576
--  Buffer IO Condition Variables |     3 |    262144
--  Buffer Strategy Status        |     3 |        28
--  Checkpoint BufferIds          |     3 |    327680
--  Shared Buffer Lookup Table    |     3 |      2896
```

`count`는 `buffer_pools`와 같아야 한다. `Buffer Blocks`의 크기는 `NBuffers × 8192 + 4096`(정렬 여유분)이다.

---

## 제출

패치 파일 하나와 `NOTES.md`를 제출한다. 패치는 소스 트리 안에서 만들되, 파일은 소스 트리 밖(`$PGBASE`)에 저장한다.

```bash
cd $PGBASE/postgres-18
git add -A .
git diff --relative pa2-base > $PGBASE/pa2.patch
```

- `git add -A .`의 `.`은 소스 트리 안의 변경만 대상으로 한다는 뜻이다. 새로 만든 파일(`contrib/snudbx/Makefile` 등)도 이렇게 해야 패치에 들어간다.
- `pa2-base`는 코스 레포에서 PA2 소스 트리를 처음 올린 커밋이다. 따라서 패치에는 미리 제공된 코드도 함께 들어간다.

패치가 건드릴 수 있는 경로는 다음뿐이며, 그 밖의 파일이 포함되면 채점 없이
반려된다.

```
src/include/**
src/backend/utils/init/globals.c
src/backend/utils/misc/guc_tables.c
src/backend/storage/lmgr/lwlock.c
src/backend/storage/snudbx_buffer/**
contrib/snudbx/**
```

---

## `NOTES.md`에 답할 질문

각 질문에 대해 세 문장 내외로 답하라. 코스 레포의 `pa2/NOTES.md`에 질문이 들어 있는 양식이 있으니, 그 파일에 답을 채워 제출한다.

1. `buffer_pools`를 실행 중에 바꿀 수 있게 만들었다면 어떤 문제가 생길지 설명하라.
2. `snudbx_buffer/` 안의 파일에는 `#ifdef SNUDBX`가 필요 없는데 `guc_tables.c`에는
   필요하다. 이 차이는 무엇 때문인가?
3. `BufferManagerShmemSize()`가 돌려준 값보다 `BufferManagerShmemInit()`이 더 많이
   떼어 가면 어떤 일이 생기는가? 당장 문제가 되는가?
4. 제어 구조체를 일반 전역 변수(`static BufferPoolControl ctl;`)로 선언하여 사용하면 왜 안 되는가?