# 9 · 클래스(Class), 상속과 다형성 (Polymorphism)

## 클래스 (Class) 와 구조체 (Struct)

앞서 [[1. C++ 기본 문법과 타입 시스템]]에서 객체지향 프로그래밍의 **클래스(Class)** 를 간단하게 언급했습니다.
C++ 에서 데이터뿐 아니라 데이터를 다루는 함수도 함께 포함할 수 있는 사용자 정의 형식에는 2가지가 존재합니다.
1. **클래스 (`class`)**
2. **구조체 (`struct`)**

클래스와 구조체 모두 멤버별로 접근 범위를 `public`, `protected`, `private`로 구분해서 사용할 수 있습니다.
클래스의 접근 수준은 `private`이고 구조체의 접근 수준은 `public`이라는 차이점을 제외하면, 모두 데이터와 멤버 함수를 가질 수 있다는 점에서 두 문법은 기본적으로 동일합니다. 

```cpp
// struct members are public by default.
struct Point {
	int x = 0;
	int y = 0;
	
	void Print() const {
		std::cout << "Point(" << x << ", " << y << ")" << std::endl;
	}
};

// class members are private by default, so public methods are used for access.

class Number {	
public:
	Number() : value_(0) { }
	~Number() { }

private:
	int value_ = 0;
};
```


!!! note "클래스와 구조체, 언제 사용해야 할까?"
    - **클래스 (`class`)**: 외부에서 내부 상태나 동작을 직접 변경하지 못하도록 제어해야 하는 경우 (e.g. 계좌 잔액 관리)
    - **구조체 (`struct`)**: 외부에서 멤버를 직접 읽거나 수정해도 문제가 없는 경우 (e.g. 색상/좌표값)

이후 내용에서는 내부 상태와 동작을 함께 관리하는 객체를 중심으로 설명하기 위해 클래스를 주로 다루겠습니다.

- 멤버 함수는 클래스가 제공하는 기능을 표현합니다. 
- 멤버 함수 안의 `this` 포인터는 현재 호출 대상 객체를 가리킵니다.
```cpp
class Number {
public:
  void SetValue(int value) {
    this->value_ = value;
  }

private:
  int value_ = 0;
};
```

- `const` 멤버 함수에서는 `this`가 가리키는 객체의 non-static 변수나 non-mutable 멤버를 직접 변경할 수 없습니다.

```cpp
class Number {
public:
  int GetValue() const {
    m_value = 20;     // mutable member
    return value_;    // non-static, non-mutable member
  }

private:
  int value_ = 10;
  mutable int m_value_ = 0;
};
```

- `static` 멤버는 특정 인스턴스가 아니라 클래스 자체에 속합니다.
```cpp
class Counter {
public:
  static int count;
};

int main() {
  int Counter::count = 0;

  Counter a;
  Counter b;

  a.count++;
  b.count++;
  Counter::count++;

  std::cout << Counter::count;  // 3

  return 0;
}
```

- 참조자 멤버는 객체가 생성된 뒤 다른 대상에 연결할 수 없으므로 생성과 동시에 초기화해야 합니다.
```cpp
class Wrapper {
public:
  Wrapper(int &value) : value_(value) {}

private:
  int &value_;
};

int main() {
  int number = 10;
  Wrapper wrapper(number);
  
  return 0;
}
```

## 생성자 (Constructor)와 소멸자 (Destructor)

사용자는 클래스에 생성자와 소멸자를 직접 선언할 수 있습니다.
직접 선언하지 않은 경우에는 컴파일러가 기본 생성자와 소멸자를 를 조건에 따라 자동으로 생성합니다.

- **생성자 (Constructor)** 는 객체가 생성될 때 클래스의 멤버를 초기화하기 위해 자동으로 호출되는 멤버 함수입니다.
	- 별도의 반환 형식이 없으며, 객체가 만들어질 때 적절한 시점에 자동으로 호출됩니다.
	- 오버로딩으로 여러 개의 생성자를 선언할 수 있고, 한 생성자가 다른 생성자에게 초기화를 위임하는 방식으로도 구현할 수 있습니다. 
	- 호출 시점에는 컴파일러가 전달된 인자를 보고 호출 대상을 결정하게 됩니다.
- **소멸자 (Destructor)** 는 객체의 생명주기가 끝날 때 자원을 정리하기 위해 자동으로 호출되는 멤버 함수입니다.
- 전역 객체의 생성자는 main 함수 실행 전에 호출되고, 소멸자는 main 함수가 끝난 뒤에 호출됩니다.

```cpp
class Number {	
public:
	// constructor
	Number() : Number(0) { }
	// destructor
	~Number() { }

private:
	int value_ = 0;
};
```

## 복사(Copy) / 이동 (Move)

클래스 타입의 객체를 함수에 값으로 전달하면, 새 객체를 만들기 위해 **복사 생성자 (Copy Constructor)** 가 호출되면서 복사가 발생할 수 있습니다.

복사 생성자는 클래스 타입의 객체가 복사될 때 사용되는 생성자입니다.
(참고로 기본 타입에는 이 개념이 없습니다. e.g. `int` / `float` / `double`)
```cpp
class C {
public:
    C() = default;
    // copy constructor
    C(const C& other) : ptr_(other.ptr_) {}

private:
	int* ptr_ = nullptr;
};

void g(C obj) {
}

int main() {
  C object;
  g(object);

  return 0;
}
```

이때 복사 방식은 멤버의 종류와 복사 생성자의 구현에 따라 달라집니다.

- **얕은 복사(Shallow Copy)** 는 포인터가 가리키는 객체가 아니라 주소만 복사하여, 두 객체가 같은 자원을 공유하게 됩니다.
```cpp
Number a(10);
Number b(20);

// shallow copy
b = a;
```

- **깊은 복사(Deep Copy)** 는 포인터가 가리키는 자원까지 새로 생성하여 각 객체가 독립된 자원을 소유하도록 합니다.
```cpp
class Number {
public:
    Number(int value) : value_(new int(value)) {}

    // deep copy constructor
    Number(const Number& other) : value_(new int(*other.value_)) {}

    // deep copy assignment operator
    Number& operator=(const Number& other) {
        if (this != &other) {
            int* new_value = new int(*other.value_);
            delete value_;
            value_ = new_value;
        }
        return *this;
    }

    ~Number() {
        delete value_;
    }

private:
    int* value_;
};

Number a(10);
Number b(20);

// deep copy construction
Number c = a;

// deep copy assignment
Number d(30);
Number d = a;

// may cause a double-free 
// because the compiler-generated copy assignment operator 
// performs a shallow copy of the raw pointer.
b = a;
```


!!! note "깊은 복사(Deep Copy) 와 복사 대입 연산자 (Copy Assignment Operator)"
    **소멸자 (destructor)** 와 **깊은 복사 생성자(deep copy constructor)** 를 정의한 클래스에서는 **복사 대입 연산자(copy assignment operator)** 를 직접 구현하거나 명시적으로 삭제해주어야 안전합니다.
    
    별도로 처리하지 않는 경우, 컴파일러가 생성하는 기본 복사 대입 연산자가 각 멤버를 그대로 복사하면서 포인터가 가리키는 자원이 아닌 포인터의 주소만 복사하게 됩니다. 그 결과 두 개 이상의 객체가 같은 자원을 소유하게 되고, 소멸하는 과정에서 이중으로 해제(double-free)되는 문제로 이어질 수 있습니다.

```text
[before copy]

Source                         heap
┌──────────────────┐           ┌──────────────┐
│ ptr = 0x8000     │──────────▶│ 0x8000: 10   │
└──────────────────┘           └──────────────┘

Destination
┌──────────────────┐
│ empty            │
└──────────────────┘


[after deep copy]

Source                         heap
┌──────────────────┐           ┌──────────────┐
│ ptr = 0x8000     │──────────▶│ 0x8000: 10   │
└──────────────────┘           └──────────────┘
                                      │
                                      │  deep-copy object
                                      │  (allocate new heap memory)
                                      │
Destination                           ▼
┌──────────────────┐           ┌──────────────┐
│ ptr = 0x9000     │──────────▶│ 0x9000: 10   │
└──────────────────┘           └──────────────┘
```


- 함수에 객체를 값으로 전달하면 호출할 때마다 객체의 복사본이 생성되어 불필요한 비용이 발생할 수 있습니다. 
  이를 방지하기 위해 일반적으로 `const` 참조를 사용합니다. `const` 참조는 새로운 객체를 생성하지 않고 기존 객체를 읽기 전용으로 참조하므로, 복사 생성자 호출과 이에 따른 메모리 할당 및 해제 비용을 줄일 수 있습니다.

```cpp
class C {
public:
    C() = default;
};

void g(const C& obj) {
    // disable copy by using const, read-only
}

int main() {
    C object;
    g(object);
}
```

- 복사를 허용하면 안되는 클래스의 경우, 복사 생성자를 명시적으로 삭제할 수도 있습니다.
	- e.g. file handle, mutex, unique ownership resources
```cpp
class C {
public:
    C() = default;

	// explicitly delete copy constructor & assignment operator
    C(const C& other) = delete;
    C& operator=(const C&) = delete;
};
```

객체의 자원을 다른 객체로 소유권을 이전하여 비용을 줄이는 개념을 **이동 (Move)** 라고 합니다.
r- value reference(`&&`)는 임시 객체 또는 `std::move`로 변환된 객체를 참조하며, 해당 객체의 자원을 이동할 수 있음을 나타냅니다.

- **이동 생성자 (Move Constructor)** 는 새 객체를 만들면서 기존 객체의 자원을 넘겨받고
- **이동 대입 연산자 (Move Assignment Operator)** 는 이미 존재하는 객체에 자원을 이전합니다.

```cpp
#include <utility>

class Number {
public:
  Number(int value) : value_(new int(value)) {}

  // move constructor
  Number(Number &&other) : value_(other.value_) {
    other.value_ = nullptr;
  }

  // move assignment operator
  Number &operator=(Number &&other) {
    if (this != &other) {
      delete value_;
      value_ = other.value_;
      other.value_ = nullptr;
    }
    return *this;
  }

  ~Number() {
    delete value_;
  }

private:
  int *value_;
};

Number first(10);
Number second(std::move(first));  // move construction
Number third(20);
third = std::move(second);        // move assignment
```

```text
[before move]

Source                         heap
┌──────────────────┐           ┌──────────────┐
│ ptr = 0x8000     │──────────▶│ 0x8000: 10   │
└──────────────────┘           └──────────────┘

Destination
┌──────────────────┐
│ empty            │
└──────────────────┘


[after move]

Source                         heap
┌──────────────────┐           ┌──────────────┐
│ nullptr          │    ─ ─ ─ ▶│ 0x8000: 10   │
└──────────────────┘           └──────────────┘
                                      ▲
                                      │  move object
Destination                           │  (no heap memory allocation)
┌──────────────────┐                  │
│ ptr = 0x8000     │──────────────────┘
└──────────────────┘
```

## 상속 (Inheritance)

**상속 (Inheritance)** 는 객체 단위의 코드를 '재사용'하는 개념으로 기존 클래스를 바탕으로 새로운 파생 클래스를 정의해서 사용할 수 있습니다.

```cpp
class BaseClass {
  void BaseFunction() {
  } 
};

class DerivedClass : public BaseClass {
  void DerivedFunction() {
  } 
};
```

```text
                 ┌──────────────────────────────┐
                 │ BaseClass                    │
                 ├──────────────────────────────┤
                 │ Base member variables        │
                 ├──────────────────────────────┤
                 │ NormalMethod()               │
                 └──────────────────────────────┘
                         ▲                ▲
                         │                │
        ┌────────────────┘                └────────────────┐
        │                                                  │
┌──────────────────────────────┐          ┌──────────────────────────────┐
│ DerivedClassA                │          │ DerivedClassB                │
├──────────────────────────────┤          ├──────────────────────────────┤
│ A member variables           │          │ B member variables           │
├──────────────────────────────┤          ├──────────────────────────────┤
│ NormalMethod()               │          │ NormalMethod()               │
└──────────────────────────────┘          └──────────────────────────────┘
```

- 파생 클래스는 기본 클래스의 `public` 및 `protected` 멤버에 접근할 수 있지만 `private` 멤버에는 직접 접근할 수 없습니다. 
- 파생 클래스 객체를 통해 기본 클래스의 `public` 메서드를 호출할 수도 있습니다.
- 파생 클래스의 인스턴스가 생성될 때, 기본 클래스의 생성자도 함께 호출됩니다. 
	- 여러 단계로 상속되는 클래스에서 생성자는 가장 하위의 파생 클래스부터 가장 상위의 기본 클래스 방향으로 호출되고, 반대 방향으로 실행됩니다. 
	- 소멸자는 호출과 실행 모두 가장 하위의 파생 클래스부터 가장 상위의 기본 클래스 방향으로 수행됩니다. 

## 오버라이딩 (Overriding)

파생 클래스에서 기본 클래스의 함수를 재정의하는 것을 **오버라이딩 (Overriding)** 이라고 합니다.

- 오버라이딩을 사용하기 위해서는 기본 클래스와 파생 클래스 함수의 **이름, 매개변수, `const` 여부**가 일치해야 합니다. 
```cpp
class Animal {
public:
  virtual void Speak() const {
    std::cout << "Animal";
  }
};

class Dog : public Animal {
public:
  void Speak() const override {
    std::cout << "Dog";
  }
};
```

- 기본 클래스에는 `virtual`을 명시해야 합니다. 
  기본 클래스의 참조로 파생 클래스 객체를 다룰 때는, `virtual`을 사용해야 오버라이딩이 적용될 수 있습니다.
```cpp
int main() {
    Dog dog;
    Animal& ref = dog;
}
```

- 파생 클래스에서는 선택적으로 `override`를 명시할 수 있습니다.
  `override`가 없으면 시그니처가 다른 함수가 별개의 함수로 선언되어도 실수를 발견하기 어렵기 때문에 명시하는 것을 권장합니다.


```text
┌──────────────────────────────┐
│ BaseClass                    │
├──────────────────────────────┤
│ NormalMethod()               │  base 구현
├──────────────────────────────┤
│ VirtualMethod() <virtual>    │  파생 클래스에서 재정의 가능
└──────────────────────────────┘
              ▲
              │ public inheritance
              │
┌──────────────────────────────┐
│ DerivedClass                 │
├──────────────────────────────┤
│ NormalMethod()               │  상속받은 일반 메서드
├──────────────────────────────┤
│ VirtualMethod() <override>   │  파생 클래스 구현
└──────────────────────────────┘
```
