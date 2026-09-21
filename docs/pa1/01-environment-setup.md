# 01 · 환경 세팅

!!! abstract "목표"
    PostgreSQL 18.4를 소스에서 빌드해 띄우고, 뒤의 장에서 계속 쓸 `lab` 테이블을 만듭니다. 실습에서
    한 번 해 본 순서를 그대로 적어 둔 것이니, 이미 되어 있으면 5절의 `lab` 테이블만 만들면 됩니다.

!!! info "실습 영상"
    LMS에 올라와 있는 "[Lab2] 9/18 과제 수행 기초.mp4"가 이 장의 순서를 그대로 따라갑니다. 막히는
    곳이 있으면 그 영상을 참고하면 됩니다.

---

## 1 · 디렉터리와 `PGBASE`

PostgreSQL에 관한 것은 전부 `$PGBASE` 아래에 둡니다.

```sh title="shell"
cd $HOME
mkdir PostgreSQL
cd PostgreSQL

echo 'export PGBASE="$HOME/PostgreSQL"' >> ~/.bashrc
source ~/.bashrc
```

| 경로 | 내용 |
|---|---|
| `$PGBASE/postgres/` | GitHub에서 받은 소스 코드 |
| `$PGBASE/pgsql/` | 빌드해서 설치한 실행 파일과 라이브러리 |
| `$PGBASE/pgdata/` | 데이터베이스 파일. 02장부터 들여다보는 곳 |
| `$PGBASE/pgdata/log/` | 서버 로그 |

이 가이드의 명령은 전부 이 배치를 전제로 합니다.

---

## 2 · 빌드에 필요한 패키지

```sh title="shell"
sudo apt update
sudo apt install -y git build-essential pkg-config libreadline-dev zlib1g-dev \
  flex bison perl libicu-dev bsdextrautils
```

---

## 3 · 소스 받기, 빌드, 설치

```sh title="shell"
git clone --depth 1 --branch REL_18_4 https://github.com/postgres/postgres.git
cd postgres

./configure --prefix="$PGBASE/pgsql"
make -s -j"$(nproc)"
make install
```

`REL_18_4`가 이 가이드가 인용하는 소스 트리입니다.

`pageinspect`도 같이 설치해 둡니다. 뒤의 장에서 쓰는 도구이고 설명은 06장에 있습니다.

```sh title="shell"
make -C contrib/pageinspect install
```

설치가 됐는지 확인해 봅시다. `$PGBASE/pgsql/bin` 아래에 `postgres`, `initdb`, `pg_ctl`, `psql` 같은 실행
파일이 생겼으면 됩니다.

```sh title="shell"
ls $PGBASE/pgsql/bin
```

버전도 확인해 봅시다.

```sh title="shell"
$PGBASE/pgsql/bin/postgres --version
```
```
postgres (PostgreSQL) 18.4
```

실행 파일과 라이브러리를 찾을 수 있게 경로를 잡아 둡니다.

```sh title="shell"
echo 'export PATH="$PGBASE/pgsql/bin:$PATH"' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH="$PGBASE/pgsql/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"' >> ~/.bashrc
source ~/.bashrc
```

---

## 4 · 데이터 디렉터리 만들고 서버 띄우기

```sh title="shell"
initdb -D "$PGBASE/pgdata" --auth-local=trust

mkdir -p "$PGBASE/pgdata/log"
pg_ctl -D "$PGBASE/pgdata" -l "$PGBASE/pgdata/log/postgresql.log" start
```

`initdb`가 `$PGBASE/pgdata`에 빈 클러스터를 만듭니다. 02장에서 볼 `base/` 디렉터리가 이때 생깁니다.
`--auth-local=trust`는 같은 컴퓨터에서 붙을 때 비밀번호를 묻지 않게 하는 옵션입니다.

서버가 떠 있는지는 `pg_ctl status`로 볼 수 있습니다.

```sh title="shell"
pg_ctl -D "$PGBASE/pgdata" status
```

끄고 켤 때는 `stop`, `start`를 쓰면 됩니다. 06장에서 말하듯 서버를 끄면 종료 전에 checkpoint를 하므로, 파일을
들여다보기 전에 `CHECKPOINT`를 치는 대신 껐다 켜도 됩니다.

---

## 5 · `lab` 테이블

`lab` 테이블을 둘 데이터베이스 `labdb`를 만들고 `psql`로 접속합시다.

```sh title="shell"
createdb labdb
psql -d labdb
```

`lab`은 02장부터 11장까지 계속 따라가는 테이블입니다. 컬럼 타입이 고정 길이와 가변 길이로 섞여 있고 NULL도
들어 있어서 튜플의 바이트를 볼 때 쓸모가 있습니다.

```sql title="psql"
create table lab (id int, flag boolean, big bigint, name text, born date);
insert into lab values
  (1, true,  1234567890123, 'kim',  '2024-02-29'),
  (2, false, -1,            null,   '1999-12-31'),
  (3, true,  0,             '한글', null);
checkpoint;
```

`checkpoint`는 방금 넣은 행을 디스크의 파일까지 내려보내는 명령입니다(03장).

```sql title="psql"
select * from lab order by id;
```
```
 id | flag |      big      | name |    born
----+------+---------------+------+------------
  1 | t    | 1234567890123 | kim  | 2024-02-29
  2 | f    |            -1 |      | 1999-12-31
  3 | t    |             0 | 한글 |
(3 rows)
```

이 세 행이 보이면 준비가 끝난 것입니다. 02장으로 넘어갑시다.
