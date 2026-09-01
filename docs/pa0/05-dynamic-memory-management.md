# 5 · 동적 메모리 관리

## 동적 메모리 관리
**지역 변수(local variable)** 는 일반적으로 스택 영역에 저장되고, 블록을 벗어나면 수명이 끝납니다.
하지만 프로그램 실행 중에 크기가 결정되거나 함수가 종료된 이후에도 데이터를 유지해야 하는 경우라면,
**메모리를 동적으로 할당** 해주어야 합니다. 이렇게 동적으로 할당된 메모리는 함수가 종료되어도 유지되므로, **더 이상 사용하지 않을 때 명시적으로 해제**해야 합니다.

## 메모리 할당: `malloc`
메모리 할당(Allocating Memory)에는 표준 라이브러리 함수 `malloc`을 사용할 수 있습니다.

```c
void* malloc(size_t size);
```

- `size`: 메모리를 할당할 byte 크기를 지정합니다.
- `malloc` 함수는 할당된 메모리의 시작 주소를 반환합니다.
- C에서는 `void *`를 다른 객체 포인터 타입으로 자동 변환합니다.

```c
#include <stdlib.h>

int* p = (int*) malloc(sizeof(*p));
```

```text
stack 영역                            heap 영역
┌────────────────────┐              ┌───────┬───────┬───────┬───────┐
│ pointer p          │─────────────▶│ int   │ int   │ int   │ int   │
│ heap start addr    │              └───────┴───────┴───────┴───────┘
└────────────────────┘               malloc으로 할당된 연속 메모리
```


### 할당 실패 검사
할당된 메모리를 사용하기 전에 `malloc`이 유효한 주소를 반환했는지 확인하는 과정이 필요합니다. 메모리를 할당할 수 없는 경우, `NULL`을 반환합니다.
반환된 포인터는 역참조하기 전에 검사가 필요합니다.

```c
int *values = malloc(count * sizeof(*values));

if (values == NULL) {
	fprintf(stderr, "memory allocation failed\n");
	return 1;
}
```

### 초기화
`malloc`으로 할당한 메모리의 초기값은 정해져 있지 않으므로 사용 전에 각 원소를 초기화해야 합니다.

```c
for (size_t i = 0; i < count; i++) {
	values[i] = 0;
}
```

### 배열 할당
배열을 할당할 때는 원소 개수와 원소 하나의 크기를 곱해서 사용할 수 있습니다.

```c
size_t count = 5;
int *values = malloc(count * sizeof(*values));
```

## 메모리 해제: `free`
더 이상 사용하지 않는 동적 메모리는 `free`로 해제합니다.

```c
free(void *);
```

```c
#include <stdlib.h>

free(values);
values = NULL;
```

- `free`에는 `malloc`이 반환한 시작 주소를 전달해야 합니다.
- 해제된 메모리는 다시 읽거나 쓸 수 없습니다.


<details>
<summary>예제: <code>dynamic_memory.c</code></summary>

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  // Malloc receives the required size in bytes and returns a pointer to the
  // allocated memory.
  int *a = (int*)malloc(sizeof(int));

  // Dereference the returned pointer after checking for NULL.
  if (a == NULL) {
    fprintf(stderr, "memory allocation failed\n");
    return 1;
  }

  *a = 10;
  printf("*a: %d\n", *a);

  // Release dynamic memory after usage.
  // Pass the same starting address returned by malloc.
  free(a);
  a = NULL;

  size_t count = 5;
  int *values = (int*)malloc(count * sizeof(int));

  if (values == NULL) {
    fprintf(stderr, "memory allocation failed\n");
    return 1;
  }

  // Memory returned by malloc has indeterminate initial values.
  // Initialize each element before usage.
  for (size_t i = 0; i < count; i++) {
    values[i] = (int)(i + 1) * 10;
  }

  for (size_t i = 0; i < count; i++) {
    printf(" %d", values[i]);
  }
  printf("\n");

  free(values);
  values = NULL;

  // ERROR: Reading or writing through a pointer after free is undefined behavior.
  // printf("%d\n", values[0]);

  return 0;
}
```

</details>
