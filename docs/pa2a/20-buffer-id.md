# 20 · 서로 다른 버퍼 ID 지칭법: `buf_id`와 `Buffer`

!!! abstract 목표
    하나의 버퍼 프레임이 계층마다 다른 자료형과 다른 번호 체계로 불린다는 점을 이해한다. 이 코드를
    읽는 사람이라면 누구나 한 번은 헷갈리는 1 차이(off-by-one)가 여기서 나온다.

버퍼 프레임은 버퍼 관리 상의 숫자로서 버퍼 기술자가 들고 있는 `buf_id` 그리고 버퍼 외부에서 사용할 때 불리는 `buffer`로 서로 다르게 지칭된다.

```text
   frame index      buf_id      0        1        2        3   …  NBuffers-1
   (internal)                   │        │        │        │
                                ▼        ▼        ▼        ▼
   BufferDescriptors[]      ┌────────┬────────┬────────┬────────┐
                            │ desc 0 │ desc 1 │ desc 2 │ desc 3 │  metadata
                            └────────┴────────┴────────┴────────┘
   BufferBlocks             ┌────────┬────────┬────────┬────────┐
   (char *, flat)           │ 8 KB   │ 8 KB   │ 8 KB   │ 8 KB   │  the page bytes
                            └────────┴────────┴────────┴────────┘
                            ▲
                            └─ BufHdrGetBlock(hdr) = BufferBlocks + buf_id * BLCKSZ

   public handle    Buffer       1        2        3        4
                                 └── Buffer = buf_id + 1,  so 0 == InvalidBuffer

   local (temp) buffers use NEGATIVE Buffer values: -1, -2, -3, …
```

`Buffer`는 `bufmgr.c` 바깥의 모든 호출자가 들고 다니는 핸들이다. 여기서 0은 `InvalidBuffer`로 사용되기 때문에, 유효한 버퍼들은 모두 `buf_id`에서 1을 더한 숫자를 사용한다. 한편, 공유 메모리 상의 버퍼 풀이 아니라 각 프로세스들의 로컬 버퍼에서 사용되는 프레임들은 마이너스 값으로 표현된다.

```c title="bufmgr.c · buf_internals.h:"
/* bufmgr.c */
#define BufHdrGetBlock(bufHdr) \
        ((Block) (BufferBlocks + ((Size) (bufHdr)->buf_id) * BLCKSZ))
#define BufferGetLSN(bufHdr)    (PageGetLSN(BufHdrGetBlock(bufHdr)))

/* buf_internals.h */
static inline BufferDesc *
GetBufferDescriptor(uint32 id)
{
    return &(BufferDescriptors[id]).bufferdesc;
}

/* buf_internals.h */
static inline Buffer
BufferDescriptorGetBuffer(const BufferDesc *bdesc)
{
    return (Buffer) (bdesc->buf_id + 1);
}
```
