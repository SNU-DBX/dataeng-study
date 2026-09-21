---
icon: lucide/layers
---

# PA1 — PostgreSQL Heap

PostgreSQL 18.4가 테이블을 디스크에 어떻게 저장하고 읽는지를 파일, 블록, 페이지, 튜플, 컬럼 순서로 내려가며 살펴봅니다.


## 목차

**I · 세팅**

- [01 · 환경 세팅](01-environment-setup.md) — PostgreSQL 18.4 빌드하기, 서버 띄우기, `lab` 테이블 만들기

**II · 파일과 블록**

- [02 · PGDATA 안의 테이블 파일](02-relation-files.md) — 테이블 이름에서 파일 경로 찾기, oid와 relfilenode 구분하기, 세그먼트로 블록 수 세기
- [03 · 블록 단위로 읽기](03-block-io.md) — 블록 번호에서 세그먼트와 위치 구하기, `pread`로 8 KB 읽기, `CHECKPOINT` 뒤에 파일 보기

**III · 페이지와 라인 포인터**

- [04 · 슬롯 페이지 레이아웃](04-page-layout.md) — 페이지 헤더 24바이트 읽기, `pd_lower`·`pd_upper`·`pd_special`로 구역 나누기, 정상 페이지 가려내기
- [05 · 라인 포인터](05-line-pointers.md) — 4바이트에서 `lp_off`·`lp_flags`·`lp_len` 꺼내기, 라인 포인터에서 튜플 찾기, 네 가지 상태 구분하기
- [06 · 페이지를 손으로 읽기](06-reading-a-page-by-hand.md) — `hexdump`와 `pageinspect`로 같은 페이지 맞춰 보기

**IV · 튜플과 컬럼**

- [07 · 힙 튜플 헤더](07-tuple-header.md) — 튜플 헤더에서 컬럼 수, null bitmap 유무, 데이터 시작 위치 읽기
- [08 · 컬럼 레이아웃은 카탈로그가 정한다](08-column-layout.md) — `pg_attribute`에서 컬럼의 순서, 길이, 정렬 읽기
- [09 · null bitmap과 정렬](09-null-bitmap-and-alignment.md) — null bitmap 읽기, 정렬 맞추기, 튜플을 컬럼으로 끊기
- [10 · 가변 길이 값(varlena)](10-varlena.md) — varlena 헤더에서 길이 꺼내기, 1바이트 헤더와 4바이트 헤더 구분하기

**V · 테이블 훑기**

- [11 · 순차 스캔](11-sequential-scan.md) — `heapgettup()` 따라가기, 호출마다 튜플 하나씩 이어 읽기
