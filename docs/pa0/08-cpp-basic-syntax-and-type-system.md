# 8 · C++ 기본 문법과 타입 시스템

## 객체 지향 언어

C++은 **프로그램의 모음을 조합해서 실행하는 객체지향 (Object-Oriented) 언어**라는 점에서 프로그램이 실행되는 순서를 중심으로 설계된 절차지향 언어인 C와는 다른 언어입니다.
(C의 기본적인 문법은 일부 동일하게 가져가고 있습니다)

객체지향 프로그래밍에서는 **클래스(Class)** 라는 형식을 정의하고,
프로그램이 실행되는 동안 클래스에 대한 **인스턴스(Instance)** 를 생성해서 사용합니다.
여기서 인스턴스는 실제 메모리에 존재하며 상태와 기능을 가집니다.

## 자원 동적 할당: `new` / `delete`
C에서 동적 메모리를 다룰 때 사용하는 `malloc`과 `free` 대신 C++에서는 `new`와 `delete`를 사용합니다.
`new` 연산자는 필요한 메모리를 확보하면서 객체의 생성자를 호출하고, `delete` 는 객체의 소멸자를 호출한 뒤에 메모리를 해제합니다.
(이 때, 배열 형태로 생성한 객체는 반드시 배열로 삭제해야 합니다)

```cpp
// dynamically allocate an integer and store its address
int *data = new int;
// assign value to allocated memory
*data = 5;

int *n_data = new int(10);

// dynamically allocate an integer array
int size = 2;
int *array = new int[size];

for (int i = 0; i < size; i++) {
	array[i] = (i + 1) * 10;
}

delete data;
delete n_data;
delete[] array;
```


```text
                  stack
        ┌─────────────────────────┐
        │ main stack frame        │
        ├─────────────────────────┤
        │ data   = 0x3000         │
        ├─────────────────────────┤
        │ n_data = 0x3010         │
        ├─────────────────────────┤
        │ array  = 0x4000         │
        └─────────────────────────┘
                    │
                    │  stack에는 heap 주소가 저장됨
                    ▼

                  heap
                    ▲
                    │  heap 주소는 stack의 포인터 변수에서 참조
        ┌─────────────────────────┐
0x4004  │ array[1] = 20           │
        ├─────────────────────────┤
0x4000  │ array[0] = 10           │
        ├─────────────────────────┤
0x3010  │ *n_data = 10            │
        ├─────────────────────────┤
0x3000  │ *data = 5               │
        └─────────────────────────┘
```

!!! note "현대 C++의 동적 메모리 관리"
    현대 C++에서는 직접 `new`와 `delete`를 사용하기보다는
    자원의 획득과 해제를 객체의 생명주기와 결합해서 사용하는 **RAII (Resource Acquisition is Initialization)** 설계 원칙과 스마트 포인터를 사용합니다.
    이 내용은 [[4. 자원 관리와 동시성]]에서 더 자세히 다루겠습니다.


<details>
<summary>예제: <code>new_delete.cpp</code></summary>

```cpp
#include <iostream>

int main(void) {
    // Dynamically allocate an integer and store its address
    int *data = new int;
    // Assign value to allocated memory
    *data = 5;

    int *n_data = new int(10);

    std::cout << *data << std::endl;
    std::cout << *n_data << std::endl;

    int size = 5;
    int *array = new int[size];

    for (int i = 0; i < size; i++) {
        array[i] = (i + 1) * 10;
    }

    std::cout << "\nArray allocated with new[]..." << std::endl;
    for (int i = 0; i < size; i++) {
        std::cout << array[i] << " ";
    }
    std::cout << std::endl;

    delete data;
    delete n_data;
    delete[] array;

    // ERROR: dereferencing a dangling pointer will cause undefined behavior
    // std::cout << *data;
    // ERROR: deallocating the memory again will also lead to undefined behavior
    // delete data;

    return 0;
}
```

</details>

## 참조자

기존 변수에 대하여 별칭(alias)을 사용할 수 있는데 이것을 **참조자 (reference; `&`)** 라고 합니다.
선언할 때 반드시 대상을 지정해야 하며, 선언 이후에는 다른 변수를 가리키도록 변경할 수 없습니다.
참조자를 통해 값을 변경하면 참조 대상이 아니라 원본의 값을 변경합니다.

```cpp
int value = 10;
int &reference = value;
```


원본의 값을 보장해야 한다면, `const` 참조자를 사용하여 변경을 제한하는 방식으로 코드를 구현할 수 있습니다.
아래는 함수 매개변수에 참조자를 사용해서 객체 복사 없이 원본을 전달하는 예시입니다.

```cpp
void TestFunc(const int &param) {
    // param = 30;  // 컴파일 오류: const 참조자를 통해 원본을 수정할 수 없음
}

int main(void) {
	int value = 10;
    TestFunc(value);

	return 0;
}
```


<details>
<summary>예제: <code>references.cpp</code></summary>

```cpp
#include <iostream>

void Add(int &num) {
    num = num + 1;
}

int main() {
    int a = 10;
    int c = 12;

    // B is another name for a
    int &b = a;

    std::cout << "Initial values...\n";
    std::cout << "a: " << a << " (&a: " << &a << ")" << std::endl;
    std::cout << "b: " << b << " (&b: " << &b << ")" << std::endl;
    std::cout << "c: " << c << " (&c: " << &c << ")" << std::endl;

    // This copies c's value into a; does not make b refer to c.
    b = c;

    std::cout << "\nAfter b = c...\n";
    std::cout << "a: " << a << " (&a: " << &a << ")" << std::endl;
    std::cout << "b: " << b << " (&b: " << &b << ")" << std::endl;
    std::cout << "c: " << c << " (&c: " << &c << ")" << std::endl;

    Add(a);

    std::cout << "\nAfter Add(a)...\n";
    std::cout << "a: " << a << " (&a: " << &a << ")" << std::endl;
    std::cout << "b: " << b << " (&b: " << &b << ")" << std::endl;
    std::cout << "c: " << c << " (&c: " << &c << ")" << std::endl;

    return 0;
}
```

</details>

## 함수

**함수 (Function)** 를 호출하기 위해서는 호출 지점에서 함수의 선언을 알 수 있어야 합니다.
- 이름
- 반환 형식(return type)
- 매개변수 목록(parameter)

1. 함수를 선언할 때 **매개변수의 기본값(default value)** 은 반드시 **오른쪽의 것부터 작성** 해야 합니다.
```cpp
void TestFunc(int p1 = 1, int p2)    // (X)
void TestFunc(int p1, int p2 = 2)   // (O)
```

2. **매개변수가 여러 개**일 때, 중간에 위치한 것의 기본값을 생략할 수 없습니다.
```cpp
void TestFunc(int p1 = 1, int p2, int p3 = 2) // (X)
void TestFunc(int p1, int p2 = 1, int p3 = 2) // (O)
```

3. 호출자가 함수 매개변수에 인자값을 전달하면 왼쪽부터 순서대로 짝을 맞추어서 지정됩니다.
```cpp
int TestFunc(int p1, int p2 = 2) {
	return p1 * p2;
}

int main() {
	TestFunc(10);        // p1 = 10
	TestFunc(10, 5);     // p1 = 10, p2 = 5
}
```

함수 하나가 여러 의미를 동시에 가지는 것을 **함수 다중 정의 (Overloading)** 라고 합니다.
같은 이름의 함수를 서로 다른 매개변수를 사용해서 선언하는 경우가 바로 다중 정의입니다.

```cpp
void TestFunc(int a) {}
void TestFunc(double a) {}
```

컴파일러는 전달된 인자의 형식을 기준으로 호출할 함수를 선택합니다.
둘 이상의 함수가 동일한 수준에서 적합한 경우, 호출이 모호하다고 판단하고 컴파일 오류를 발생시킵니다.

```cpp
void TestFunc(int a) {}
void TestFunc(int a, int b = 10) {}

int main() {
	// Compile Error: Call to 'TestFunc' is ambiguous
	TestFunc(10);
}
```

## 네임스페이스

**네임스페이스 (Namespace)** 는 함수, 변수, 클래스를 하나의 논리적인 범주로 묶는 문법입니다.  
서로 다른 네임스페이스 안에서는 이름과 매개변수 형태가 같은 함수도 각각 정의할 수 있습니다.  
또한 네임스페이스는 중첩해서 정의하고, 범위 지정 연산자 `::`로 원하는 이름을 선택해서 사용할 수 있습니다.

```cpp
namespace A {
int data = 100;

	namespace B {
		int data = 200;

		namespace C {
			int data = 300;
		}
	}
}

int main() {
	cout << "A::data " << A::data << endl;              // 100
	cout << "A::B::data " << A::B::data << endl;        // 200
	cout << "A::B::C::data " << A::B::C::data << endl;  // 300

	return 0;
}
```

`using namespace`를 사용하면 네임스페이스 이름을 생략하고 내부 요소를 사용할 수 있습니다. 하지만 여러 개의 네임스페이스를 사용할 때는 충돌 가능성이 커질 수 있으므로, 필요한 경우만 사용하는 것이 안전합니다.

```cpp
#include <iostream>
using namespace std;

int main() {
	cout << "using namespace std" << endl;
}
```


<details>
<summary>예제: <code>namespaces.cpp</code></summary>

```cpp
#include <iostream>

void foo(int a) {
    std::cout << "Print from foo: " << a << std::endl;
}
namespace A {
    int data = 100;

    void foo(int a) {
        std::cout << "Print from A::foo: " << a << std::endl;
    }

    namespace B {
        int data = 200;

        void bar(int a) {
            std::cout << "Print from A::B::bar: " << a << std::endl;
        }
    }
}

namespace C {
    void foo(int a) {
        std::cout << "Print from C::foo: " << a << std::endl;
    }
}


int main() {
    foo(10);        // Implicit global function usage
    ::foo(10);      // Explicit global function usage

    A::foo(10);
    C::foo(10);

    A::B::bar(20);

    std::cout << std::endl;
    std::cout << "A::data: " << A::data << std::endl;
    std::cout << "A::B::data: " << A::B::data << std::endl;

    return 0;
}
```

</details>
