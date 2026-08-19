# 3 · 함수와 유효 범위

## Function

**함수(function)** 는 특정 동작을 수행하는 단위입니다.
함수를 사용하면 프로그램을 작은 단위로 분리하고, 같은 동작을 여러 곳에서 재사용할 수 있습니다.

C 언어에서는:

- **함수의 원형(prototype)** 을 사용하여 함수를 미리 선언할 수 있습니다.
- **함수의 header에는 매개변수와 반환값의 자료형을 명시적으로 작성** 해야 합니다.
- 함수를 여러 번 선언할 수 있지만, **일반적으로 하나의 프로그램에서는 한 번만 정의** 합니다.

### Basic Structure
```c
//  ①          ②            ③
return_type function_name(parameter_list) {
	// ④ function body
	// ⑤
	return expression;
}
```

① **return type**: 함수가 실행된 후 반환되는 값의 자료형 (e.g. int, float, void, etc.)  
② **function_name**: 함수의 이름  
③ **parameter_list**: 함수 호출 시 전달받는 값  
④ **function body**: 함수가 실제로 호출하는 코드  
⑤ **return**: 함수를 호출한 곳으로 결과값을 반환  


!!! note "`void`"
    `void` 함수는 별도의 값을 반환하지 않고, `return;` 만 사용해서 함수를 종료합니다.
    특정 작업만 수행하고 종료하면 되는 함수에 사용합니다.

### Function Declaration & Definition
```c
#include <stdio.h>

// 1. Prototype (Declaration)
// tells the compiler about the function's name, return type and parameters without a body.
int add(int x, int y);

// 2. Definition
// the actual implementation of the function/
int add(int x, int y) {
	return x + y;
}

int main(void) {
	// 3. Function Call
	// pass arguments to parameter and execute function body
	int result = add(3, 5);
	printf("%d\n", result);
	
	return 0;
}
```


!!! note "함수의 선언(Declaration)이  없는 경우"
    함수의 선언은 컴파일러에게 함수를 알려주는 역할을 합니다.
    	만약 함수의 선언이 없는 경우, 함수의 정의(Definition)가 반드시 호출보다 먼저 나와야 컴파일러가 함수를 인식하고 사용할 수 있습니다.
    ```c
    #include <stdio.h>
    // Definition
    int add(int x, int y) {
    	return x + y;
    }
    
    int main(void) {
    	int result = add(3, 5);   // Function call
    	printf("%d\n", result);
    	
    	return 0;
    }
    ```

### Function Parameters and Arguments
- **매개변수(parameter)**: 함수 정의에 선언되고, 함수가 호출될 때 전달받는 변수입니다.
- **인자(argument)**: 함수를 호출할때 실제로 전달하는 값입니다.
	- 인자의 개수와 타입은 함수 원형과 맞아야 합니다.
	- 각 인자는 대응하는 매개변수 타입으로 변환됩니다.
	
```c
int square(int value) { // parameter
	return value * value;
}

int result = square(5); // argument
```

모든 인자는 값으로 전달되고, 함수는 그 복사본을 받습니다. 따라서 매개변수를 변경해도 호출자의 변수값은 바뀌지 않습니다.
```c
void increment(int value) {
	value++;
	return;
}

int number = 10;
increment(number);
printf("%d\n", number); // 10 (value itself is not modified)
```


## Function Call Mechanism: Stack Frame
함수 호출은 서로 중첩되며, 가장 마지막에 호출된 함수가 가장 먼저 종료됩니다. 이러한 **LIFO(Last In, First Out)** 순서를 관리하기 위해 일반적인 C 실행 환경에서는 호출 스택(call stack)을 사용합니다.

- 함수를 호출하면 해당 호출에 필요한 스택 프레임(stack frame)이 호출 스택에 추가(push)됩니다.
- 함수가 반환되면 해당 스택 프레임이 제거(pop)되고 호출한 함수의 실행을 이어갑니다.
- 스택은 일반적으로 낮은 메모리 주소 방향으로 증가하지만, 실제 방향과 배치는 시스템 및 컴파일러에 따라 달라질 수 있습니다.

### Anatomy of a Stack Frame
스택 프레임(stack frame)은 한 번의 함수 호출에 필요한 정보를 저장하는 메모리 영역이며, activation record라고도 합니다. 일반적으로 매개변수, 반환 주소, 이전 프레임 정보, 지역 변수 등이 포함되지만 실제 구성은 CPU, 호출 규약, 컴파일러 최적화에 따라 달라집니다.

```c
#include <stdio.h>

int add(int x, int y) {
	int result = x + y;
	return result;
}

int main(void) {
	int sum = add(3, 5);
	printf("%d\n", sum);
	return 0;
}
```

`main`이 `add(3, 5)`를 호출하면 호출 스택은 개념적으로 다음과 같이 구성됩니다.

```text
                  낮은 주소
        ┌─────────────────────────┐
SP ───▶ │ result = 8              │  add의 지역 변수
        ├─────────────────────────┤
FP ───▶ │ saved frame information │  호출자의 프레임 복원 정보
        ├─────────────────────────┤
        │ return address          │  main으로 돌아갈 위치
        ├─────────────────────────┤
        │ x = 3, y = 5            │  add의 매개변수
        ├─────────────────────────┤
        │ main stack frame        │  호출자인 main의 프레임
        └─────────────────────────┘
                  높은 주소
```

호출 및 반환 과정은 다음과 같습니다.

1. `main`의 스택 프레임이 생성됩니다.
2. `add(3, 5)`를 호출하면 `add`의 스택 프레임이 `main` 프레임 위에 추가됩니다.
3. `add`는 매개변수 `x`, `y`와 지역 변수 `result`를 사용하여 `8`을 반환합니다.
4. `add`의 스택 프레임이 제거되고, 저장된 반환 주소를 이용해 `main`의 호출 다음 지점부터 실행을 계속합니다.
5. 반환값 `8`이 `main`의 지역 변수 `sum`에 저장됩니다.

!!! note "실제 Stack Frame"
    위 그림은 스택 프레임의 동작을 설명하기 위한 개념적 모델입니다. 
	실제 프로그램에서는 매개변수와 반환값이 레지스터를 통해 전달되거나, 최적화로 지역 변수 및 스택 프레임 자체가 생략될 수 있습니다.

### Recursive Function
재귀 함수(recursive function)는 함수 내부에서 자기 자신을 다시 호출하는 함수입니다. 

```c
unsigned long long factorial(unsigned int n) {
	if (n == 0) {                   // base case
		return 1;
	}

	return n * factorial(n - 1);   // recursive call
}
```

재귀 호출을 끝내는 종료 조건(base case)을 작성하지 않는 경우, 함수 호출(recursive case) 이 무한으로 반복될 수 있습니다.

!!! note "스택 오버플로우 (Stack Overflow)"
    재귀 호출마다 새로운 함수 호출에 대한 스택 프레임이 쌓이게 됩니다. 
    스택 크기는 제한적이기 때문에, 함수 호출이 연속적으로 발생하다가 스택 공간이 꽉 찬 경우 더 이상 함수를 호출할 수 없게 될 수 있습니다.  
    우리는 이것을 **스택 오버플로우 (Stack Overflow)** 라고 합니다.

## Scope 
스코프(scope)란 변수가 접근 가능한 코드의 범위, 즉 변수가 존재할 수 있는 범위를 의미합니다.

| Type             | Description                                                                                          | Example               |
| ---------------- | ---------------------------------------------------------------------------------------------------- | --------------------- |
| **Block Scope**  | 블록 내부에서 선언된 변수는 선언 지점부터 해당 블록이 끝나는 범위(`{}`) 내에서만 사용할 수 있습니다.<br>함수가 끝나면 메모리에서 사라지며, 더 이상 접근할 수 없습니다. | local variable        |
| **File Scope**   | 하나의 파일 내에서 접근할 수 있습니다                                                                                | static global varible |
| **Global Scope** | 프로그램 내에서 모두 접근할 수 있습니다.                                                                              | global variable       |

!!! note "정적 변수 (static variable)"
    - 특정 block 안에서만 접근하도록 범위를 제한하면서도 프로그램이 실행되는 동안 값을 유지하기 위해 사용하는 변수입니다.
    - 프로그램이 시작할 때 메모리를 할당해서 값을 유지할 수 있으므로, 함수 호출이 끝나도 값이 사라지지 않고 여러 호출에 걸쳐서 사용이 가능합니다.
    ```c
    static int static_var = 100;
    ```


```c
#include <stdio.h>

// 1. Block Scope + Local Variable
void local_counter(void) {
	int local_count = 0;
	local_count++;
	printf("local_counter(): called %d times\n", local_count);
}

// Block Scope + Static Variable
void static_counter(void) {
	static int static_count = 0;
	static_count++;
	printf("counter(): called %d times\n", static_count);
}

// File Scope + Global Variable
int global_count = 0;

// File Scope + Static Global Variable
static int file_count = 0;
void file_counter(void) {
	file_count++;
	printf("file_counter(): called %d times\n", file_count);
}

int main(void) {
	local_counter();
	local_counter();
	// Cannot access a local variable outside its block.
	// printf("local_count: %d\n", local_count);

	static_counter();
	static_counter();
	
	file_counter();
	file_counter();

	return 0;
}

/** Output:
	local_counter(): called 1 times
	local_counter(): called 1 times
	counter(): called 1 times
	counter(): called 2 times
	file_counter(): called 1 times
	file_counter(): called 2 times
  */
```


!!! note "외부 변수 (external variable)"
    - 다른 파일에 정의되어 있는 변수를 참조하기 위해 사용하는 변수입니다.
    - C 컴파일러는 각 파일을 독립적으로 컴파일하기 때문에, 한 파일에서 정의한 전역 변수 (global variable)을 다른 파일에서 사용하기 위해서는 `extern` 선언이 필요합니다.
    - 일반적으로 header에 external 을 선언하고, 하나의 source file에서 정의합니다.
    ```c
    // counter.h
    extern int global_count;
    
    // counter.c
    int global_count = 0;
    ```
