# 4 · 배열과 포인터

## Arrays
배열(array)은 같은 자료형의 객체를 연속된 메모리 공간에 저장할 수 있는 자료구조입니다. 
0부터 시작하는 인덱스(index)를 통해 각 요소에 접근할 수 있습니다.

### Array Declaration and Initialization
배열 선언에는 원소 타입과 길이를 지정합니다. 

```c
// ①        ②          ③
data_type array_name[array_size];
```
① **data type**: 배열에 저장할 데이터의 자료형  
② **array name**: 배열 이름  
③ **array size**: 배열의 크기 (필요한 크기만큼 메모리에서 연속적인 공간을 할당받습니다)  

```c
// default initialization
int scores[4] = {72, 85, 91, 64};
// initialize partial elements only 
int scores[4] = {72, 85};                // {72, 85, 0, 0}

// initialization omitting array size (컴파일러가 요소의 개수를 파악해서 자동으로 크기를 계산)
int values[] = {10, 20, 30};

// initialize zero at once
int zeros[5] = {0};                     // {0, 0, 0, 0, 0}

// 초기화하지 않으면, 쓰레기값이 들어있다.
int arr[];
```

### Array Indexing and Bounds
인덱스(index)로 지정된 위치의 요소에 접근하거나 값을 대입할 수 있습니다.
인덱스는 `0`부터 `size-1`까지의 범위를 가집니다. 
```c
int values[5] = {10, 20, 30, 40, 50};
	
for (int i = 0; i < 5; i++) {
	printf("values[%d] = %d\n", i, values[i]);
}
	
values[1] = 25;
printf("values[1] = %d\n", values[1]);

/** Output:
	values[0] = 10
	values[1] = 20
	values[2] = 30
	values[3] = 40
	values[4] = 50
	values[1] = 25
  */
```

이 때 범위를 초과해서 접근하는 경우, 예상치 못한 값이 출력될 수 있습니다.
```c
printf("values[5] = %d\n", values[5]);  // e.g. values[5] = -858993460
```

### Array Length: `sizeof`
이러한 문제를 피하기 위해 `sizeof` 연산자를 사용, 배열의 바이트 크기를 원소 하나의 크기로 나누어 범위를 지정할 수 있습니다.

```c
#include <stddef.h>

int values[] = {10, 20, 30, 40};
size_t count = sizeof(values) / sizeof(values[0]);
```

### Multidimensional Arrays
다차원 배열은 배열을 원소로 가지는 배열입니다.
메모리에는 행 우선(row-major) 순서로 1차원 배열처럼 연속적으로 저장됩니다.

```c
int matrix[2][3] = {
	{1, 2, 3},
	{4, 5, 6}
};
printf("%d\n", matrix[1][2]); // 6
	
for (int i = 0; i < 2; i++) {        // iterate row
	for (int j = 0; j < 3; j++) {    // iterate column
		printf("%d ", matrix[i][j]);
	}
	printf("\n");
}
```

## Pointer Fundamentals
포인터(pointer)는 객체나 함수의 주소를 저장하며, 포인터 타입은 가리키는 대상의 타입을 나타냅니다.

### Addresses and Pointer Types:  `&` and `*`
`pointer`의 타입은 `int *`이며 `int` 객체의 주소를 저장합니다. 
주소 연산자 `&`로 객체의 주소를 얻고, 역참조 연산자 `*`로 포인터가 가리키는 객체에 접근할 수 있습니다.
포인터를 다른 주소로 변경하는 것(`int *p = &v2;`)과 포인터가 가리키는 값을 변경하는 것(`*p=20;`)은 서로 다른 연산이니 주의해야 합니다.


```c
int v = 42;
int *p = &v;       // &v: address of v

*p = 20;           // *p: follows the stored address and access v
printf("%d\n", v); // 20
```
- `&v`: `v`의 주소
- `p`: `v`의 주소를 저장한 `int *` 객체
- `*p`: `p`에 저장된 주소를 따라서 `v`에 접근

```text
주소        객체

0x1000    ┌───────────────────┐
          │ object v          │
          │ value: 42         │◀─────┐
          └───────────────────┘      │
                                     │  *p는 이 주소를 따라가서 v에 접근
0x2000    ┌───────────────────┐      │
          │ pointer object p  │──────┘
          │ value: 0x1000     │
          └───────────────────┘
```


### Null Pointers
Null pointer는 유효한 객체나 함수를 가리키지 않는 포인터입니다. 
포인터를 즉시 유효한 주소로 초기화할 수 없다면 `NULL`로 초기화할 수 있습니다.
```c
int *pointer = NULL;

if (pointer != NULL) {
	printf("%d\n", *pointer);
}
```

Null pointer를 역참조하면 undefined behavior가 발생합니다.
따라서 포인터를 역참조하기 전에 `NULL` 여부를 확인해야 합니다. 다만 `pointer != NULL`은 포인터가 실제로 유효한 객체를 가리킨다는 것까지 보장하지 않습니다.

## Arrays and Pointers

### Array-to-Pointer Conversion
하나의 배열은 연속적인 메모리 공간에 존재하고, 대부분의 표현식에서 배열 이름은 첫 번째 원소를 가리키는 포인터로 변환됩니다.
단, `sizeof`나 `&`에는 적용되지 않습니다.
```c
int v[] = {10, 20, 30};
int *p = v;            // &v[0]

printf("%d\n", *p);    // 10

sizeof(v);             // total bytes of the array
sizeof(p);             // size of pointer
```

또한 배열과 포인터가 같은 타입이 아니라는 점을 유의해야 합니다.
배열은 원소 저장 공간 전체를 포함하지만, 포인터는 주소를 저장하는 별도의 객체입니다. 

### Pointer Arithmetic
포인터에 정수를 더하거나 빼면 가리키는 타입의 크기만큼 원소 단위로 이동할 수 있습니다.

```c
int v[] = {10, 20, 30};
int *p = v;

printf("%d\n", *(p + 1)); // 20
printf("%d\n", p[1]);     // 20
```

```text
// int 크기가 4바이트라고 가정
// p == &v[0]

            p                  p + 1              p + 2
            │                    │                  │
            ▼                    ▼                  ▼
주소     0x1000               0x1004             0x1008
        ┌──────────┐         ┌──────────┐       ┌──────────┐
v[0]    │    10    │  v[1]   │    20    │ v[2]  │    30    │
        └──────────┘         └──────────┘       └──────────┘
							*(p + 1) == v[1] == 20
```

### Pointers to Arrays
배열 포인터는 배열 전체를 가리키며, 다차원 배열의 행을 순회할 때 사용할 수 있습니다.

```c
int matrix[2][3] = {
	{1, 2, 3}, 
	{4, 5, 6},
};

// pointer to array
int (*row)[3] = matrix;
// array of pointers
int *row[3] = {&matrix[0][0], &matrix[0][1], &matrix[0][2]};

printf("%d\n", row[1][2]); // array 
```

### Call by Value
C 언어의 함수 인자는 항상 **값으로 전달(call by value)** 됩니다.
함수를 호출하면 인자의 값은 함수 매개변수에 복사되고, 함수 안에서 매개변수 자체를 변경해도 호출한 쪽의 변수는 변경되지 않습니다.

```c
#include <stdio.h>

void update_value(int value) {
	value = 10;  // modifies only the copied value
}

int main(void) {
	int n = 1;
	update_value(n);

	printf("%d\n", n); // 1
	return 0;
}
```

포인터를 인자로 전달하는 경우에도 동일하게 값으로 전달된다는 점은 동일합니다.
다만 이 때는 객체 자체가 아닌 객체의 주소(address)가 복사되지만, 원본과 같은 객체를 가리키므로 역참조를 통해 호출자의 객체를 수정할 수 있습니다.

```c
#include <stdio.h>

void update_value(int *p) {
	*p = 99;  // modifies the object at the copied address
}

int main(void) {
	int n = 10;
	update_value(&n);

	printf("%d\n", n); // 99
	return 0;
}
```

- `&n`: `n`의 주소를 함수에 전달
- `p`: 전달받은 주소의 복사본을 저장하는 지역 포인터 변수
- `*p = 99`: 복사된 주소를 따라가 호출자의 `n`값을 변경
