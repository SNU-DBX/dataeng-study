# 7 · 빌드, 디버깅


## 컴파일러

[빌드 및 실행](01-c-program-structure-and-compilation.md#빌드-및-실행)에서 프로그램의 전처리, 컴파일, 어셈블, 링크 과정과 주요 컴파일러 옵션을 살펴보았습니다.  

소스 파일이 하나라면 컴파일 명령을 직접 실행해도 충분합니다.
```sh
$ gcc -Wall -Wextra -Wpedantic -std=c11 -g hello.c -o hello
```

하지만 프로그램이 여러 소스 파일로 구성되는 경우에는, 각 파일에 대한 컴파일과 링크 과정을 반복해야 합니다. 파일이 변경될 때마다 이러한 명령을 실행하는 작업은 번거롭고 실수가 발생할 확률이 높습니다. 우리는 이와 같은 빌드 과정을 자동화하기 위해 `make` 를 사용할 수 있습니다.

## `make`/ Makefile
`make` 는 `Makefile`에 작성된 규칙에 따라 명령을 실행하는 빌드 자동화 도구입니다.  
반복되는 컴파일 및 링크 명령을 자동화하고, 변경된 파일을 기준으로 필요한 대상을 다시 빌드할 수 있습니다.

여기서 `make`는 `Makefile`에 정의된 파일 간의 관계를 확인하여 어떤 명령을 실행할지 결정하는 도구로, 실제 컴파일과 링크는 동일하게 컴파일러에 의해 수행됩니다.

`Makefile`의 빌드 규칙은 다음 형태로 작성합니다.
```makefile
target: dependencies
	recipe
```

- **target**: 규칙을 통해 생성할 파일 또는 실행할 작업의 이름입니다.
- **dependencies**: target을 만드는 데 필요한 파일입니다.
- **recipe**: target을 생성하기 위해 실행할 명령입니다.
  - **recipe** 앞의 들여쓰기는 공백이 아니라 **탭(tab)** 이어야 합니다.

다음은 `main.c/h`와 `hello.c/h`를 각각 목적 파일로 컴파일한 뒤, 하나의 `main` 실행 파일로 링크하는 Makefile 예시입니다.
```makefile
CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic
TARGET := main
OBJECTS := main.o hello.o

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET)

main.o: main.c main.h
	$(CC) $(CFLAGS) -c main.c -o main.o

hello.o: hello.c hello.h
	$(CC) $(CFLAGS) -c hello.c -o hello.o
```

- `$(TARGET)`: 최종 실행 파일
- `$(OBJECTS)`: 컴파일 과정에서 생성된 오브젝트 파일(`.o`)

`Makefile`이 있는 디렉터리에서 `make`를 실행하면 첫 번째 target인 `main`을 빌드합니다.
```sh
$ make
```

`make`는 **target**과 **dependency**의 수정 시간을 비교해서 필요한 작업만 수행합니다.

- **target** 파일이 존재하지 않으면 해당 규칙의 **recipe**를 실행합니다.
- **dependency**가 **target**보다 최근에 변경되었다면 **target**을 다시 생성합니다.
- 변경되지 않은 파일에 대한 **recipe**는 실행하지 않습니다.

예를 들어 `hello.c`만 수정한 뒤 `make`를 다시 실행하면 다음 순서로 필요한 작업만 수행됩니다.
1. 변경된 `hello.c`를 `hello.o`로 다시 컴파일합니다.
2. 새 `hello.o`와 기존 `main.o`를 링크하여 `main`을 다시 생성합니다.
3. 변경되지 않은 `main.c`는 다시 컴파일하지 않습니다.

`Makefile`에서 다음과 같이 `clean`이라는 target을 사용하면, 다음 파일을 삭제하는 작업을 손쉽게 수행할 수 있습니다.
```
.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJECTS)
```

- `.PHONY`: 실제 파일 이름이 아닌, 실행할 작업 이름임을 `make` 에 알려주는 구문 (파일의 존재 여부와 관계없이 항상 명령을 실행합니다)

```sh
$ make clean
```

## GDB 를 사용한 디버깅

**GDB(GNU Debugger)** 는 실행 중인 프로그램의 동작과 내부 상태를 확인할 수 있는 디버거입니다.  
코드만 읽거나 출력문을 추가하는 방법으로는 문제가 발생한 정확한 위치와 런타임의 프로그램 상태를 파악하기 어려울 수 있습니다.  

GDB를 사용하면 특정 위치에서 멈추거나 한 줄씩 실행하면서 변수와 호출 스택을 확인하고 프로그램이 예상과 다르게 동작하는 원인을 단계적으로 추적할 수 있습니다.

GDB에서 제공하는 소스 라인, 지역 변수/타입 등의 정보를 보기 위해서는 먼저 `-g` 옵션으로 프로그램을 컴파일해야 합니다.

```sh
$ cc -std=c11 -Wall -Wextra -g main.c -o main

$ gdb ./main
```

`Makefile` 을 사용하는 경우라면 `CFLAGS`에 `-g`를 반드시 지정해주어야 합니다.

```makefile
CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g
TARGET := main
OBJECTS := main.o hello.o

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET)
...
```

GDB에서 자주 사용할 수 있는 명령은 다음과 같습니다.

| Command          | Description                  |
| ---------------- | ---------------------------- |
| `break main`     | `main` 함수에 중단점을 설정합니다.       |
| `run`            | 프로그램을 실행합니다.                 |
| `next`           | 현재 함수 안에서 다음 소스 코드 줄을 실행합니다. |
| `step`           | 함수 호출 내부로 들어가 한 줄을 실행합니다.    |
| `print variable` | 변수의 현재 값을 출력합니다.             |
| `backtrace`      | 현재 호출 스택을 출력합니다.             |
| `continue`       | 다음 중단점까지 실행을 계속합니다.          |
| `quit`           | GDB를 종료합니다.                  |

```text
(gdb) break main
(gdb) run
(gdb) next
(gdb) print count
(gdb) backtrace
(gdb) continue
(gdb) quit
```

macOS의 LLDB에서도 유사한 명령을 사용할 수 있습니다.

| GDB | LLDB | Description |
| --- | --- | --- |
| `break main` | `breakpoint set -n main` | 중단점을 설정합니다. |
| `run` | `run` | 프로그램을 실행합니다. |
| `next` | `next` | 다음 줄을 실행합니다. |
| `print value` | `print value` | 변수 값을 출력합니다. |
| `backtrace` | `bt` | 호출 스택을 출력합니다. |

## `AddressSanitizer` (ASan)

`AddressSanitizer`는 프로그램 실행 중 발생하는 메모리 접근 오류를 탐지하기 위한 도구입니다.  
문제가 발생한 지점에서 프로그램을 중단하고, 어떤 종류의 메모리 오류가 어느 호출 경로에서 발생했는지 출력해 디버깅을 돕습니다.

주로 동적 할당/해제와 관련해서 발생하는 다음과 같은 문제들을 찾을 때 도움이 될 수 있습니다.

- 배열 범위를 벗어난 접근 (buffer overflow)
- 해제한 메모리에 다시 접근 (use-after-free)
- 같은 메모리를 두 번 해제 (double-free)
- 잘못된 포인터를 `free`에 전달

컴파일할 때 `-fsanitize=address` 옵션을 추가하여 사용할 수 있습니다.  
더 자세한 추가 옵션은 [gcc - Program Instrumentation Options 문서](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)를 참고하세요.
```sh
$ cc -std=c11 -Wall -Wextra -g \
  -fsanitize=address \
  main.c -o main 

$ ./main
```

!!! note "Memory Leak Detection"
    메모리 누수(memory leak)은 LeakSanitizer(LSan)로 감지할 수 있습니다.
    일부 Linux 환경에서는 `AddressSanitizer`를 활성화하면 `LeakSanitizer`도 함께 활성화되지만, 모든 운영체제와 컴파일러가 이를 지원하는 것은 아니니 각자 환경에 맞게 확인이 필요합니다.
    명시적으로 `gcc`에서는 `-fsanitize=leak` 옵션을 추가하거나, 실행 시점에 `ASAN_OPTIONS`  환경변수를 설정하여 활성화할 수 있습니다.
    ```sh
    ASAN_OPTIONS=detect_leaks=1 ./main
    ```
