# 1 · C 프로그램 구조와 컴파일

## C Language Overview

C는 **컴파일 방식의 정적 타입 언어**이며, 프로그램의 실행 순서와 함수 호출을 중심으로 구성되는 **절차지향  언어**입니다.
메모리와 시스템 자원에 낮은 수준으로 직접 접근할 수 있어 프로그램과 메모리, 운영체제가 동작하는 원리를 이해하는 데 도움이 됩니다.
이러한 특성으로 인해 C는 임베디드 프로그래밍, 시스템 프로그래밍, 고성능 컴퓨팅, GPU 프로그래밍 등의 분야에서 주로 사용됩니다.

### Memory Model

프로그램의 메모리 공간은 일반적으로 아래와 같이 4개 영역으로 나뉘어집니다.

- **스택(Stack) 영역**: 지역 변수와 함수 호출 정보가 저장되는 영역
- **힙(Heap) 영역**: 동적으로 할당된 메모리가 저장되는 영역
- **데이터(Data) 영역**: 전역(global), 정적(static) 변수가 저장되는 데이터 영역
- **코드(Code) 영역**: 실행되는 명령어가 저장되는 영역

이 중에 힙과 스택은 프로그램 실행 중 필요에 따라 크기가 변하며, 서로를 향하는 방향으로 확장됩니다.

```text
                  높은 주소
        ┌─────────────────────────┐
        │ stack                   │  지역 변수, 함수 호출 정보
        │                         │
        │            ▼            │  아래 방향으로 확장
        ├─────────────────────────┤
        │                         │
        │      unused memory      │  stack과 heap 사이의 여유 공간
        │                         │
        ├─────────────────────────┤
        │            ▲            │  위 방향으로 확장
        │ heap                    │  동적 할당 메모리
        ├─────────────────────────┤
        │ data                    │  전역 변수, 정적 변수
        ├─────────────────────────┤
        │ code                    │  실행되는 명령어
        └─────────────────────────┘
                  낮은 주소
```

## Minimal C Program Structure
C 프로그램을 구동하기 위해서는 먼저 소스 파일을 작성해야 합니다.
기본적인 소스 파일은 다음 요소들로 구성됩니다.

- 표준 라이브러리 또는 사용자가 정의한 헤더 파일
- **`main` function**: 프로그램의 시작점
- **statement**: 프로그램이 수행할 명령문
- **`return` value**: 프로그램의 실행 결과를 나타내는 값

```c
#include <stdio.h>

int main(void) {
	printf("Hello, World!\n");
	return 0;
}
```

### Header Inclusion
```c
#include <stdio.h>
```
- `#include` 지시문은 컴파일 전에 전처리기(preprocessor)에 의해 처리됩니다.
- `stdio` library에는 표준 입출력 함수(`printf`), 표준 입출력 스트림(`stdin/stdout`) 이 선언되어 있습니다.

### Main Function: Entry Point
```c
int main(void) {
	/* statements */
}
```
-  실행 가능한 C 프로그램은 `main` 함수를 진입점(entry point)로 사용합니다.
- 함수는 매개변수 목록(parameter list)과 return type을 가집니다.
	- return type이 `void`이면 값을 반환하지 않습니다.
	- 매개변수 목록이 `void`이면 매개변수를 받지 않습니다.

### Statements and Function Calls
```c
printf("Hello, World!\n");
```
- 명령문(statement)은 프로그램이 수행할 동작을 나타내며, 일반적으로 세미콜론(`;`)으로 끝납니다.
- `printf` 함수는 지정된 형식에 따라 표준 출력 스트림(stdout)에 데이터를 출력합니다.
	- `\n`은 줄바꿈을 나타내는 escape 문자입니다.

### Return Value
```c
return 0;
```
- `main` 함수는 운영체제에 정수형 종료 상태(exit status)를 반환합니다.
	- `0`은 프로그램이 정상적으로 종료되었음을 의미합니다.
	- `0`이 아닌 값(`1`, `-1`)은 에러나 문제가 발생해서 종료되었음을 의미합니다.

### Extended Source File Organization
그 외에도 소스 파일은 다양한 요소들을 가질 수 있습니다.
[[source_file.cpp]] 실습 예시 파일을 참고해주세요.

!!! note "선언(Declaration) vs 정의(Definition)"

    |                     | Declaration                    | Definition                                       |
    | ------------------- | ------------------------------ | ------------------------------------------------ |
    | 목적 | 컴파일러에게 식별자(identifier)의 이름과 타입을 알림 | 식별자(identifier)의 메모리를 할당하고 변수를 초기화하거나 함수의 구현을 제공 |
    | 메모리 사용 | 선언만으로는 객체의 저장 공간이 확보되지 않음      | 객체의 저장 공간을 확보하고 메모리를 할당함                         |
    | 실행방식 | 동일한 식별자를 여러 번 선언할 수 있음         | 일반적으로 하나의 프로그램 내에서 정확히 1번만 정의                   |

## Build, Run, and Compilation Mode
앞서 살펴본 헤더와 함수 등을 사용해서 C 소스파일을 작성하더라도, 컴퓨터가 이 파일을 바로 실행할 수는 없습니다.
사람이 읽을 수 있는 형태로 작성된 소스 코드를 컴퓨터가 실행할 수 있는 형태로 변환하는 과정이 필요합니다. 우리는 이 과정을 일반적으로 **빌드 (Build)** 라고 부릅니다. 

`gcc hello.c -o hello` 명령을 실행하면 내부적으로 다음 과정이 수행됩니다.

```text
┌────────────────┐       ┌──────────┐       ┌───────────┐       ┌────────┐       
│ Preprocessor   │──────▶│ Compiler │──────▶│ Assembler │──────▶│ Linker │──────▶ ./hello 
└────────────────┘       └──────────┘       └───────────┘       └────────┘      (executable)
        ▲                     │                   │                   ▲        
        │                     ▼                   ▼                   │          
     hello.c               hello.i             hello.s             hello.o      
  (source code)        (preprocessed)        (assembly)           (object)
        ▲                                                             ▲
        │                                                             │
   header files                                                   libraries
```

### Preprocessor
- `#include`, `#define`과 같은 전처리 지시문을 처리합니다.
- 전처리 지시문이 반영된 확장 소스 파일(`.i`)을 생성합니다.

### Compiler
- C 코드의 문법과 타입을 검사합니다. (일부 문법은 경고만 출력한 상태로 결과물을 생성할 수도 있으니, 유의해야 합니다.)
- 전처리가 끝난 C 코드를 어셈블리 코드(`.s`)로 변환합니다.

### Assembler
- 어셈블리 코드를 기계어로 변환합니다.
- 목적 파일(object file / `.o`)을 생성합니다. 
- 목적 파일은 일반적으로 그 자체로 실행할 수 없습니다.

### Linker
- 목적 파일(`.o`)과 필요한 라이브러리를 결합하고, 최종 실행 파일을 생성합니다.
- `printf`와 같은 함수가 실제로 정의된 위치를 찾아 연결합니다.

### Build and Run
컴파일 명령에는 다양한 옵션을 사용할 수 있고, 필요에 따라 단계를 분리해서 수행할 수도 있습니다.
아래는 대표적인 컴파일 명령어 예시입니다.
더 자세한 추가 옵션은 [gcc(1) — Linux manual page](https://man7.org/linux/man-pages/man1/gcc.1.html)를 참고하세요.

```bash
# a. Compile and link at once
gcc -Wall -Wextra -std=c11 hello.c -o hello

# b. Generate object file only
gcc -Wall -Wextra -std=c11 -c hello.c -o hello.o
# c. Link object file
gcc hello.o -o hello

# Run
./hello
```
- `-Wall -Wextra`: 주요 컴파일 경고 활성화 옵션
- `-std=c11`: C 표준 사용 지정 옵션
- `-o hello`: 실행 파일 이름 지정
