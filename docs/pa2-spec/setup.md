---
icon: lucide/wrench
---

# PA2 Setup — 개발 환경 준비하기

이 문서는 PA2 전체에 걸쳐 쓰는 소스 트리, 빌드, 서버 실행 방법을 다룬다. Phase 1을 시작하기 전에 끝까지 따라 해 두어야 한다.

---

## 1. 소스 받기와 `PGBASE` 바꾸기

PA2의 소스는 PA1에서 받은 코스 레포(`2026-2-data-engineering`)의 `pa2/` 폴더에 있다. 레포를 이미 받아 두었다면 최신 내용을 받고, 아니라면 새로 받는다.

```bash
cd ~/2026-2-data-engineering && git pull         # 이미 받아 둔 경우
# 또는
cd ~ && git clone https://github.com/snu-dbxlab/2026-2-data-engineering.git
```

`pa2/` 폴더는 다음과 같이 구성되어 있다. 빌드 결과물, 설치본, 데이터는 모두 `pa2/PostgreSQL/` 아래에 만들어지며 git에는 올라가지 않는다.

```
pa2/
├── local-grader/        로컬 채점기 (7절)
└── PostgreSQL/          ← PGBASE
    ├── postgres-18/     PostgreSQL 소스 트리 (여기를 고친다)
    ├── build-default/   default 빌드 폴더          ┐
    ├── build-snudbx/    snudbx 빌드 폴더            │
    ├── pgsql-default/   default 빌드 설치본         │ 2·4절에서 만든다
    ├── pgsql/           snudbx 빌드 설치본          │
    └── pgdata/          데이터 (로그는 pgdata/log/) ┘
```

PA1에서는 `PGBASE`가 `$HOME/PostgreSQL`이었다. PA2에서는 이것을 `pa2/PostgreSQL`로 바꾼다. PA1에서 `.bashrc`에 넣은 `PATH`와 `LD_LIBRARY_PATH`는 `$PGBASE/pgsql`을 가리키므로, `PGBASE`만 바꾸면 그대로 PA2의 snudbx 빌드를 가리키게 된다.

```bash
sed -i 's#^export PGBASE=.*#export PGBASE="$HOME/2026-2-data-engineering/pa2/PostgreSQL"#' ~/.bashrc
source ~/.bashrc
echo $PGBASE
```

레포를 다른 위치에 받았다면 경로를 그에 맞게 바꾼다. `.bashrc`에 `PGBASE`, `PATH`, `LD_LIBRARY_PATH` 줄이 없다면 PA1 환경 설정 문서를 따라 먼저 추가한다.

!!! tip "PA1 환경으로 돌아가기"
    `PGBASE`를 원래 값으로 되돌리면 `PATH`와 `LD_LIBRARY_PATH`도 다시 PA1의 빌드를 가리킨다.

    ```bash
    sed -i 's#^export PGBASE=.*#export PGBASE="$HOME/PostgreSQL"#' ~/.bashrc
    ```

    그다음 **새 터미널을 연다.** 이미 열려 있는 터미널에서 `source ~/.bashrc`를 하면 `PATH` 앞에 경로가 하나 더 붙을 뿐 이전 경로도 남아 있어서 헷갈리기 쉽다. 새 터미널에서 `echo $PGBASE`와 `which pg_ctl`로 확인한다.

    전환하기 전에는 떠 있는 서버를 먼저 종료한다(`pg_ctl -D $PGBASE/pgdata stop`). PA1과 PA2의 서버는 같은 포트(5432)를 쓰므로 동시에 띄울 수 없다. `PGBASE`를 바꾼 뒤에 종료하려면 `$PGBASE` 대신 이전 데이터 디렉터리의 전체 경로를 써야 하므로, 바꾸기 전에 종료해 두는 편이 쉽다.

    PA2로 다시 돌아올 때는 위의 PA2용 `sed` 명령을 실행하고 같은 방법으로 확인한다.

소스 트리(`postgres-18`)에는 PostgreSQL 18.4 대비 다음과 같은 변경 사항이 반영되어 있다. 버퍼 매니저를 수정하면서도, 원하면 기본 PostgreSQL의 버퍼 매니저를 빌드하여 사용할 수 있도록 가장 기본적인 세팅을 제공한다.

| 준비된 것 | 설명 |
|---|---|
| `src/backend/storage/snudbx_buffer/` | `src/backend/storage/buffer/`를 그대로 복사해 놓은 폴더로, 앞으로 이 폴더의 내용을 주로 수정하게 된다. |
| `--enable-snudbx-buffer` | configure할 때 이 옵션을 켜면 `buffer/` 대신 `snudbx_buffer/`가 컴파일되어, 변경한 버퍼 매니저를 사용할 수 있다. |
| `SNUDBX` 매크로 | 위 옵션을 켜면 자동으로 정의된다. |

이 밖에 각 step에서 "제공된다"고 한 코드도 이미 들어 있다.

---

## 2. 빌드 방법

빌드는 두 번 한다. `pgsql-default`에는 기본 PostgreSQL로 빌드한 서버가, `pgsql`에는 우리 설정(`--enable-snudbx-buffer`)을 적용한 서버가 설치된다. 빌드 폴더는 소스 트리 밖(`build-*`)에 둔다. 필요한 패키지는 PA1에서 설치한 것과 같다.

```bash
# (1) default 빌드
mkdir -p $PGBASE/build-default && cd $PGBASE/build-default
$PGBASE/postgres-18/configure --prefix=$PGBASE/pgsql-default --enable-cassert --enable-debug
make -j$(nproc) && make install
```

```bash
# (2) snudbx 빌드
mkdir -p $PGBASE/build-snudbx && cd $PGBASE/build-snudbx
$PGBASE/postgres-18/configure --prefix=$PGBASE/pgsql --enable-cassert --enable-debug \
               --enable-snudbx-buffer
make -j$(nproc) && make install
```

설치가 끝나면 `PATH`에 있는 것이 snudbx 빌드인지 확인한다.

```bash
which pg_ctl      # $PGBASE/pgsql/bin/pg_ctl 이어야 한다
```

### `--enable-cassert`는 필수

PostgreSQL 내부에는 `Assert`로 표현된 자기 검사가 아주 많은데, 이 옵션 없이 빌드하면 그 검사들이 통째로 컴파일에서 빠진다. 채점 서버는 이 옵션으로 빌드하므로, 옵션 없이 통과하던 코드가 채점에서 실패할 수 있다.

확인 방법:

```sql
SHOW debug_assertions;   -- on 이어야 한다
```

---

## 3. 가장 중요한 규칙: `#ifdef SNUDBX`

`src/backend/storage/snudbx_buffer/`에 있는 모든 파일은 `--enable-snudbx-buffer`일 때만 컴파일되므로, 제약 없이 수정해도 된다.

그러나 그 밖의 경로에 있는 파일을 수정하거나 새로운 내용을 추가할 때는 아래와 같은 전처리 지시문을 사용해야 한다.

```c
#ifdef SNUDBX
    /* 추가하는 내용 */
#endif
```

기존 줄을 고쳐야 한다면, 원래 줄을 지우지 말고 이렇게 둘 다 남겨야 한다.

```c
#ifdef SNUDBX
    /* 바꾼 버전 */
#else
    /* 원래 줄 그대로 */
#endif
```

이렇게 하는 이유는 "**원래 PostgreSQL은 멀쩡한데 내 코드만 문제인가**"를 즉시 확인할 수 있어야 하기 때문이다. `--enable-snudbx-buffer` 없이 빌드하면 `SNUDBX`가 정의되지 않으므로, 이 블록들은 컴파일에서 빠져 아예 없는 코드처럼 된다. 즉 언제든 기본 상태의 PostgreSQL로 돌아갈 수 있다. 반대로 옵션을 주면 블록들이 모두 포함된다.

---

## 4. DB 만들기와 서버 띄우기

`initdb`는 **default 빌드**로 실행한다. 우리가 바꾼 코드가 클러스터 생성에 영향을 주지 않게 하기 위해서다. 서버를 띄워 사용할 때는 **snudbx 빌드**를 쓴다. 데이터가 디스크에 저장되는 형식은 바뀌지 않으므로, 서로 다른 빌드로 만든 데이터 디렉터리도 그대로 읽을 수 있다. 그래서 아래 코드 예시에서 보면 `pgdata`라는 폴더에 있는 데이터베이스를 두 개의 서로 다른 빌드가 이용하고 있다.

새 데이터베이스를 만드는 `CREATE DATABASE`(`createdb`)도 default 빌드 서버에서만 한다. 만약 데이터베이스를 삭제하고 싶을 때에도(`DROP DATABASE`) 마찬가지다.

default 빌드는 `PATH`에 없으므로 `$PGBASE/pgsql-default/bin/`을 붙여 실행하고, snudbx 빌드는 이름만으로 실행한다.

```bash
# 데이터 디렉터리 생성 — default 빌드로
$PGBASE/pgsql-default/bin/initdb -D $PGBASE/pgdata --auth-local=trust
mkdir -p $PGBASE/pgdata/log
echo "io_method = sync" >> $PGBASE/pgdata/postgresql.conf

# (필요하면) 새 데이터베이스 생성 — default 빌드 서버로
$PGBASE/pgsql-default/bin/pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log start
$PGBASE/pgsql-default/bin/createdb mydb
$PGBASE/pgsql-default/bin/pg_ctl -D $PGBASE/pgdata stop

# 서버 기동과 접속 — snudbx 빌드로
pg_ctl -D $PGBASE/pgdata -l $PGBASE/pgdata/log/postgresql.log start
psql -d postgres

# 종료
pg_ctl -D $PGBASE/pgdata stop
```

---

## 5. 고친 뒤 다시 빌드하기

소스만 고쳤다면 `make`와 `make install`만 다시 하면 된다. configure부터 다시 할 필요는 없다.

```bash
make -C $PGBASE/build-snudbx -j$(nproc) install
make -C $PGBASE/build-default -j$(nproc) install
```

서버가 떠 있었다면 `pg_ctl restart`로 다시 띄워야 새롭게 빌드된 서버가 실행된다.

---

## 6. 확장 모듈(`contrib/snudbx`) 빌드하기

Phase 1의 Step 1-2부터는 `contrib/snudbx/`에 확장 모듈을 직접 만들게 된다. 이 폴더는 `contrib/Makefile`에 등록되어 있지 않으므로 5절의 `make`로는 빌드되지 않고, 아래처럼 따로 빌드한다.

```bash
make -C $PGBASE/postgres-18/contrib/snudbx USE_PGXS=1 PG_CONFIG=$PGBASE/pgsql/bin/pg_config install
```

이 방식은 **설치된** snudbx 서버의 헤더를 기준으로 컴파일한다. 따라서 헤더(`src/include/**`)를 고쳤다면 5절의 `make install`을 먼저 해서 PostgreSQL 빌드를 새로이 바꾸고 나서 확장 모듈을 빌드해야 한다.

확장 모듈은 snudbx 빌드에만 설치한다. default 빌드에는 이 모듈이 읽을 대상이 없다.

---

## 7. 채점기 직접 돌리기

제출 전에 로컬 채점기를 직접 돌려 볼 수 있다. 로컬 채점기에는 각 step의 **가장 기초적인 항목 한두 개만** 들어 있다. 실제 채점은 Gradescope에서 훨씬 많은 항목으로 하므로, 로컬 채점기를 통과했다고 만점이 보장되지는 않는다.

로컬 채점기는 `pa2/local-grader/`에 있다. 새 step이 나오면 코스 레포를 `git pull`해서 갱신한다.

```bash
pip3 install --user gradescope-utils     # 처음 한 번
cd $PGBASE/../local-grader
./run_local.sh $PGBASE/postgres-18
```

- 두 빌드를 `/tmp/pa2-work` 아래에 새로 만들고(`$PGBASE`의 빌드·설치본과는 별개), DB를 만들고, 테스트를 실행한다.
- 처음 실행은 빌드 때문에 몇 분 걸린다. 다음부터는 바뀐 파일만 다시 컴파일한다.

---

## 8. 자주 하는 실수의 유형들

- **`buffer/`를 고친다.** 수정 대상은 `snudbx_buffer/`이다. `buffer/`를 고치면 snudbx 빌드에는 아무 변화가 없고, 제출 시 반려된다.
- **`make`만 하고 `make install`을 하지 않는다.** 서버는 `$PGBASE/pgsql`에 설치된 바이너리로 뜬다. 설치하지 않으면 예전 코드가 실행된다.
- **서버를 재시작하지 않는다.** 다시 설치했더라도 이미 떠 있는 서버는 예전 바이너리다. `pg_ctl restart`를 해야 한다.
- **default 빌드를 확인하지 않는다.** 공용 파일을 고칠 때 `#ifdef SNUDBX`를 빠뜨려도 snudbx 빌드는 멀쩡하다. default 빌드를 해 봐야만 드러난다.
- **`--enable-cassert` 없이 빌드한다.** 
- **`initdb`를 snudbx 빌드로 한다.**