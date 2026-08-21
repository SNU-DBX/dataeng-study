# 11 · 자원 관리와 동시성

## RAII 

[RAII ((Resource Acquisition Is Initialization))](https://en.cppreference.com/cpp/language/raii) 는 메모리, 파일, 소켓, 뮤텍스와 같이 반드시 해제해야 하는 자원의 수명을 객체의 생명주기에 결합하여 사용하는 C++의 자원 관리 방식입니다.

객체가 초기화될 때 생성자에서 자원을 획득하고, 객체가 소멸할 때 소멸자에서 자원을 해제합니다. 따라서 정상적인 함수 종료뿐 아니라 중간 `return`이나 예외가 발생한 경우에도 객체가 선언된 범위를 벗어나면 자동으로 소멸자가 호출되어 자원이 정리됩니다.
여러 RAII 객체가 있다면 생성된 순서의 반대로 소멸하므로, 자원 해제 순서도 객체의 생명주기를 통해 결정됩니다.

## 표준 라이브러리
### `std::unique_ptr`
`std::unique_ptr`은 동적으로 할당된 객체에 유일한 소유권을 부여하는 스마트 포인터입니다. `std::make_unique`로 객체를 생성할 수 있습니다.

```cpp
auto source = std::make_unique<int>(10);
auto destination = std::move(source);
```

- 또한 소유권을 보장하기 위해 복사는 금지되지만, non-const `unique_ptr`은 `std::move`를 사용하여 소유권 자체를 다른 `unique_ptr`로 이전할 수 있습니다.
- 객체의 생명주기는 `unique_ptr` 에 의해 관리되고, 포인터가 선언된 범위를 벗어나면 관리 중인 객체도 자동으로 소멸됩니다.

```cpp
const auto source = std::make_unique<int>(10);

// compile error
auto destination = std::move(source);
```

<details>
<summary>예제: <code>unique_ptr.cpp</code></summary>

```cpp
#include <iostream>
#include <memory>
#include <utility>

class Point {
public:
  Point(int x, int y) : x_(x), y_(y) {}

  int GetX() {
    return x_;
  }

  int GetY() {
    return y_;
  }

 private:
  int x_ = 0;
  int y_ = 0;
};

int main() {
  auto source = std::make_unique<Point>(10, 20);

  auto destination = std::move(source);

  std::cout << "After move...\n";
  std::cout << "source: " << (source ? "not empty" : "empty") << std::endl;
  std::cout << "destination: Point(" << destination->GetX() << ", " << destination->GetY() << ")" << std::endl;

  return 0;
}
```

</details>

### `std::shared_ptr`
`std::shared_ptr`는 동적으로 할당된 하나의 객체를 여러 포인터가 공동으로 소유할 수 있도록 하는 스마트 포인터입니다.
```cpp
auto first = std::make_shared<Point>(2, 3);
```

- `unique_ptr`과 달리 복사가 가능합니다. `shared_ptr`를 복사하면 같은 객체를 가리키는 소유자가 하나 늘어나면서 참조 횟수가 증가하고, 마지막 소유자까지 사라져서 참조 횟수가 0이 되는 시점에 객체가 소멸됩니다.
- 값으로 함수에 전달하면 함수 내부에 소유자 복사본이 만들어져 호출 중 참조 횟수가 증가하지만, 참조자로 전달하면 소유자를 추가하지 않고 같은 스마트 포인터를 사용합니다.
- 반면 `std::move`은 소유자 하나를 옮길 뿐 전체 참조 횟수는 증가시키지 않습니다.

```cpp
#include <iostream>
#include <memory>

void Test(std::shared_ptr<Point>& point) {
    std::cout << point.use_count() << std::endl;
}


auto first = std::make_shared<Point>(2, 3);       // first: 0x7f8710706188
std::cout << first.use_count() << std::endl;      // 1 (first)

std::shared_ptr<Point> second = first;            // second: 0x7f8710706188
std::cout << first.use_count() << std::endl;      // 2 (first, second)

// pass by reference
// shared_ptr is not copied and the number of owners remains unchanged
Test(first);
std::cout << first.use_count() << std::endl;      // 2 (first, second)

// first and third shares the same pointer.
std::shared_ptr<Point> third = std::move(second); // third: 0x7f8710706188
std::cout << first.use_count() << std::endl;      // 2 (first, third)

```


<details>
<summary>예제: <code>shared_ptr.cpp</code></summary>

```cpp
#include <iostream>
#include <memory>
#include <utility>

class Point {
 public:
  Point(int x, int y) : x_(x), y_(y) {}

 private:
  int x_ = 0;
  int y_ = 0;
};

void Test(std::shared_ptr<Point>& point) {
  std::cout << point.use_count() << std::endl;
}

int main() {
  auto first = std::make_shared<Point>(2, 3);
  std::cout << first.use_count() << std::endl;

  // Copied shared_ptr
  std::shared_ptr<Point> second = first;
  std::cout << first.use_count() << std::endl;

  // Pass by reference
  // shared_ptr is not copied and the number of owners remains unchanged.
  Test(first);
  std::cout << first.use_count() << std::endl;

  // The first and third shared_ptr objects share ownership of the same object.
  std::shared_ptr<Point> third = std::move(second);
  std::cout << first.use_count() << std::endl;

  return 0;
}
```

</details>

## 스레드 (Thread)와 뮤텍스 (Mutex)
여러 스레드가 같은 메모리 위치에 동시에 접근하면서, 그 중 하나 이상의 스레드가 값을 변경하려고 할 때 접근 사이에 적절한 **동기화(Synchronization)**가 없으면 **데이터 경쟁 (Data Race)** 이 발생할 수 있습니다.
이 때, `std::mutex`를 사용하면 공유 자원에 대한 접근을 동기화할 수 있습니다.
단, `mutex`가 자동으로 접근을 제한하는 것은 아니므로, 공유 자원에 접근하는 모든 스레드가 동일한 `mutex`를 잠근 상태에서 실행되도록 해야 합니다.

- `lock()`을 호출한 스레드가 `mutex`의 소유권을 획득합니다. 잠금을 획득한 상태에서 공유 자원을 사용합니다.
- 작업이 끝나면 을 호출하여 `mutex`를 해제합니다.
- 작업 대상 `std::thread` 객체에 `join()` 을 호출하여, 해당 스레드가 끝날 때까지 기다리도록 합니다.

```cpp
#include <mutex>
#include <thread>

int count = 0;
std::mutex m;

void add_count() {
    m.lock();
    count += 1;
    m.unlock();
}

int main() {
    std::thread t1(add_count);
    std::thread t2(add_count);

    t1.join();
    t2.join();
}
```


<details>
<summary>예제: <code>mutex.cpp</code></summary>

```cpp
#include <iostream>
#include <mutex>
#include <thread>

int count = 0;
std::mutex m;

void AddCount() {
  m.lock();
  count += 1;
  m.unlock();
}

int main() {
  std::thread t1(AddCount);
  std::thread t2(AddCount);

  t1.join();
  t2.join();

  std::cout << "count: " << count << '\n';
  return 0;
}
```

</details>

### `std::lock_guard` /  `std::scoped_lock`

직접 `lock()`과 `unlock()`을 호출하면 함수가 중간에 반환되거나 예외가 발생했을 때 `unlock()`이 실행되지 않을 수 있습니다. 이 경우 `mutex`가 잠긴 상태로 남아 다른 스레드가 해당 뮤텍스를 계속 기다리게 되면서 프로그램이 더 이상 진행되지 않는 **교착 상태(Deadlock)** 로 이어질 수 있습니다.

 [RAII](./11-resource-management-and-concurrency.md) 잠금 객체는 생성될 때 뮤텍스를 잠그고, 현재 범위를 벗어나 소멸할 때 자동으로 잠금을 해제합니다.

- `std::lock_guard`는 하나의 mutex만을 관리하는 단순한 RAII 잠금 객체입니다. 생성 시점에 자동으로 mutex를 잠그고, 현재 범위를 벗어나 소멸할 때 자동으로 잠금을 해제합니다.
```cpp
std::lock_guard<std::mutex> lock(m);
```

- `std::scoped_lock`도 생성 시점에 자동으로 `mutex`를 잠그고 현재 범위를 벗어나 소멸할 때 자동으로 잠금을 해제하는 RAII 잠금 객체라는 점은 동일합니다. 단 여러 `mutex`를 한 번에 전달할 수 있습니다.이 경우 서로 다른 잠금 순서 때문에 발생할 수 있는 교착 상태를 피하도록 자동으로 잠금을 획득합니다.

```cpp
std::mutex m1;
std::mutex m2;
int a = 0;
int b = 0;

void transfer() {
    std::scoped_lock lock(m1, m2);
    a += 1;
    b -= 1;
}
```

### 읽기·쓰기 잠금 

읽기 작업은 공유 자원의 상태를 변경하지 않으므로 여러 스레드가 동시에 수행해도 문제가 되지 않습니다. 하지만 쓰기 작업은 공유 자원의 상태를 직접적으로 변경하므로 다른 읽기 및 쓰기 작업과 동시에 수행할 수 없습니다.

C++에서는 `std::shared_mutex`에 읽기용 `std::shared_lock`과 쓰기용 `std::unique_lock`을 조합하여 읽기 작업은 동시에 허용하되 쓰기 작업은 배타적으로 수행하는 동기화 개념인 **읽기·쓰기 잠금 (Reader-Writer Lock)**을 구현할 수 있습니다.

- `std::shared_mutex`: 공유 잠금과 배타적 잠금을 모두 지원하는 기본 동기화 객체
- `std::shared_lock`: RAII 읽기 잠금 / `lock_shared()`와 `unlock_shared()`를 관리
- `std::unique_lock`:  RAII 배타적 잠금 / `std::shared_mutex`의 `lock()`과 `unlock()`을 관리

`std::shared_lock`은 동시에 공유 잠금을 획득할 수 있으므로 여러 reader가 동일한 자원을 동시에 읽을 수 있습니다.
반면 `std::unique_lock`이 배타적 잠금을 획득하면, 다른 reader와 writer는 모두 해당 자원에 접근할 수 없습니다.
(참고로 reader가 하나라도 공유 잠금을 가진 동안에는 writer도 배타적 잠금을 획득할 수 없습니다.)

```cpp
#include <mutex>
#include <shared_mutex>

int count = 0;
std::shared_mutex m;

void read_value() {
    std::shared_lock lock(m);
    int value = count;
}

void write_value() {
    std::unique_lock lock(m);
    count += 1;
}
```


<details>
<summary>예제: <code>reader_writer_lock.cpp</code></summary>

```cpp
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

int count = 0;
// Reader-writer lock: shared for readers, exclusive for writers.
std::shared_mutex count_mutex;
// Print lock for std::cout, so messages from different threads do not interleave.
std::mutex output_mutex;

void read_value(int reader_id) {
  int snapshot = 0;
  {
    std::shared_lock<std::shared_mutex> read_lock(count_mutex);
    snapshot = count;
  }

  std::lock_guard<std::mutex> output_lock(output_mutex);
  std::cout << "reader: " << reader_id << "(" << snapshot << ")" << '\n';
}

void write_value(int amount) {
  std::unique_lock<std::shared_mutex> write_lock(count_mutex);
  count += amount;
}

int main() {
  std::vector<std::thread> threads;

  std::cout << "Reader output order and observed values may vary...\n";
  threads.emplace_back(read_value, 1);
  threads.emplace_back(read_value, 2);
  threads.emplace_back(write_value, 3);
  threads.emplace_back(read_value, 3);
  threads.emplace_back(write_value, 7);

  for (std::thread &thread : threads) {
    thread.join();
  }

  std::cout << "\nCount: " << count << '\n';
  return 0;
}
```

</details>

### 조건 변수

스레드가 특정 조건을 만족할 때까지 CPU를 점유하지 않고 대기하도록 하는 작업이 필요한 경우가 있습니다.
이 때는 **조건 변수 (Condition Variable)** `std::condition_variable`을 사용할 수 있습니다.

- `wait()` 은 조건식이 `false`인 동안 `mutex` 를 해제하고, 스레드를 대기시킵니다.
  알림을 받고 스레드가 깨어나면 `mutex`를 획득한 뒤에 조건을 검사하고, 조건식이 `true` 일때만 `mutex`를 소유한 상태로 반환합니다.
- `wait()`가 대기 과정에서 잠금을 중간에 해제하고 다시 획득해야 하므로 `std::scoped_lock`이 아니라 `std::unique_lock`을 사용해야 합니다.
- 다른 스레드는 같은 `mutex`를 획득한 상태에서 공유 상태를 변경합니다. 상태 변경이 끝나면 `mutex`를 해제하고, `notify_one()` 또는 `notify_all()`을 호출하여 대기 중인 스레드가 조건식을 다시 검사하도록 알립니다. (여기서 알림 자체가 조건의 만족을 보장하지는 않는다는 점을 기억해야 합니다)

```cpp
#include <condition_variable>
#include <mutex>

int count = 0;
std::mutex m;
std::condition_variable cv;

void waiter() {
    std::unique_lock lock(m);
    cv.wait(lock, [] { return count == 2; });
}

void increment() {
    std::scoped_lock lock(m);
    count += 1;
    if (count == 2) {
        cv.notify_one();
    }
}
```


<details>
<summary>예제: <code>condition_variable.cpp</code></summary>

```cpp
#include <iostream>
#include <mutex>
#include <thread>
#include <condition_variable>

int count = 0;
std::mutex count_mutex;
std::condition_variable cv;

void waiter() {
  std::unique_lock<std::mutex> lock(count_mutex);

  // Wait until the predicate is true
  cv.wait(lock, [] { return count == 2; });
  std::cout << "waiter: count reached " << count << '\n';
}

void increment() {
  bool should_notify = false;

  {
    std::lock_guard<std::mutex> lock(count_mutex);
    count += 1;
    should_notify = (count == 2);
  }

  if (should_notify) {
    cv.notify_one();
  }
}
int main() {
  std::thread t1(waiter);
  std::thread t2(increment);
  std::thread t3(increment);

  t2.join();
  t3.join();
  t1.join();

  std::cout << "main: final count = " << count << '\n';
  return 0;
}
```

</details>
