---
icon: lucide/layers
---

# PA2 — PostgreSQL 버퍼 매니저

PA2에서 고치게 될 코드를 따라가며, 버퍼 풀이 어디서 만들어지고, 페이지가 어떻게 들어오고, 어떤 페이지를 내보낼지 어떻게 정하는지 살펴본다. 모든 코드 발췌는 PostgreSQL 18.4 원본에서 가져왔다.

## 이 가이드에서 다루는 것

1. **버퍼 풀은 어디서 오는가?** 누가, 언제, 무엇으로부터 버퍼 풀을 할당하는가? 그리고 왜 힙이 아니라
   공유 메모리에 두는가? ([§02](02-shared-memory.md)–[§05](05-pin-lock-lwlock.md))
2. **페이지를 요청하면 무슨 일이 일어나는가?** `ReadBuffer`부터 `BufferAlloc`까지, 해시 테이블 조회,
   그리고 교체 대상을 찾는 클럭 스윕 .
   ([§06](06-buffer-from-user-perspective.md)–[§09](09-replacement-strategy.md))
3. **페이지는 어떻게 디스크로 돌아가는가?** (준비 중)

---

## 목차

**버퍼 풀이란**

- [01 · 버퍼 풀은 왜 존재하는가](01-why-a-buffer-pool.md)

**I · 버퍼 풀의 구성요소**

- [02 · 공유 메모리](02-shared-memory.md)
- [03 · 버퍼 풀 초기화](03-buffermanagershmeminit.md)
- [04 · 버퍼 상태 및 스핀락](04-buffer-state.md)
- [05 · 버퍼 핀과 내용 락](05-pin-lock-lwlock.md)

**II · 버퍼 풀 할당, 교체, 읽기**

- [06 · "사용자" 입장에서 본 버퍼 풀](06-buffer-from-user-perspective.md)
- [07 · 버퍼 풀의 읽기 요청 처리 과정](07-buffer-read-path.md)
- [08 · 버퍼 프레임 할당](08-buffer-alloc.md)
- [09 · 교체 정책](09-replacement-strategy.md)

**부록**

- [읽기 스트림(`read_stream.c`)](20-read-stream.md)
- [서로 다른 버퍼 ID 지칭법: `buf_id`와 `Buffer`](21-buffer-id.md)
