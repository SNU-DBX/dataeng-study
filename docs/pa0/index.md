---
icon: lucide/layers
---

# PA0 — Bootcamp

C와 C++로 시스템 코드를 읽고 쓰는 법, 그리고 프로그래머의 관점에서 본 컴퓨터 시스템을 다룹니다.


## 목차

**I · C Basics**

- [0 · 개발 환경과 기본 도구](00-development-environment-and-tools.md) — 도구 확인, `vi`, Git
- [1 · C 프로그램 구조와 컴파일](01-c-program-structure-and-compilation.md) — 메모리 모델, `main`, 전처리부터 링크까지
- [2 · 자료형과 제어 흐름](02-data-types-and-control-flow.md) — 기본 자료형, 연산자, 타입 변환, 조건문과 반복문
- [3 · 함수와 유효 범위](03-functions-and-scope.md) — 스택 프레임으로 본 함수 호출, scope와 생명주기
- [4 · 배열과 포인터](04-arrays-and-pointers.md) — 포인터 산술, 배열-포인터 변환
- [5 · 동적 메모리 관리](05-dynamic-memory-management.md) — `malloc`과 `free`
- [6 · 구조체와 사용자 정의 자료형](06-structures-and-user-defined-types.md) — 구조체의 메모리 배치와 패딩
- [7 · 빌드, 디버깅](07-build-and-debugging.md) — Makefile, GDB, AddressSanitizer

**II · C++ Basics**

- [8 · C++ 기본 문법과 타입 시스템](08-cpp-basic-syntax-and-type-system.md) — `new`/`delete`, 참조자, 오버로딩, 네임스페이스
- [9 · 클래스, 상속과 다형성](09-classes-inheritance-and-polymorphism.md) — 생성자와 소멸자, 복사와 이동, 가상 함수
- [10 · STL](10-stl.md) — 컨테이너, 반복자, `auto`
- [11 · 자원 관리와 동시성](11-resource-management-and-concurrency.md) — RAII, 스마트 포인터, `std::thread`와 `std::mutex`

**III · System Programming**

- [12 · 정보 표현: 정수, 실수](12-data-representation.md) — 인코딩, 2의 보수, 정수 연산의 함정, 부동소수점
- [13 · 메모리 계층](13-memory.md) — SRAM과 DRAM, CPU–메모리 격차, 지역성
- [14 · 캐시](14-cache.md) — 캐시의 구조, 쓰기 정책, 캐시 친화적인 코드
- [15 · 프로세스와 스레드](15-process-thread.md) — 컨텍스트 스위칭, `fork`, `pthread_create`, 시스템 콜
- [16 · 주소 공간과 가상 메모리](16-address-space.md) — 가상 주소와 물리 주소, 페이지 테이블, 주소 변환, 메모리 매핑
- [17 · 동적 할당](17-dynamic-alloc.md) — 단편화, implicit/explicit/segregated free list, 실제 할당자
- [18 · 동시성](18-concurrency.md) — 데이터 레이스, 진행 그래프, 뮤텍스와 원자적 연산