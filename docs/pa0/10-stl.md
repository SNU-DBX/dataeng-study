# 10 · STL (Standard Template Library)

## STL (Standard Template Library)
여러 자료형에서 재사용할 수 있도록 템플릿으로 구현된 컨테이너(Container), 반복자(Iterator), 알고리즘(Algorithm)을 제공하는 C++ 표준 템플릿 라이브러리입니다.

1. **컨테이너 (Container)**: 데이터 저장소 역할 (e.g. `vector`, `list`, `map`)
2. **반복자 (Iterator)**: 컨테이너 요소에 접근할 수 있도록 하는 객체
3. **알고리즘 (Algorithm)**: 정렬, 검색 등의 표준화된 연산


## 컨테이너 (Container)
**컨테이너 (Container)** 는 객체와 데이터를 목적에 맞는 방식으로 저장하고 관리하는 자료구조입니다.
필요한 연산과 그 비용을 기준으로 적절한 컨테이너를 선택해서 사용할 수 있습니다. (e.g. 순서 보장, 검색 방식, 중복 허용 여부 등)

아래와 같이 구분할 수 있습니다.

- **시퀀스 컨테이너 (Sequence Container)**: 메모리 상에서 원소의 논리적인 순서가 유지됩니다.
	- `vector`, `deque`, `list`, `forward_list`, `array`
- **연관 컨테이너 (Associative Container)**: key 또는 key/value를 하나의 쌍으로 저장합니다.
	- `set`, `multiset`, `map`, `multimap`
- **비정렬 연관 컨테이너 (Unordered Associative Container)**: 비정렬 상태로 유지되는 연관 컨테이너입니다.
    - `unordered_set`, `unordered_multiset`, `unordered_map`, `unordered_multimap`
- **컨테이너 어댑터 (Container Adapter)**: 기존 컨테이너를 변경하여 특정 인터페이스(기능)만을 제공하도록 만들어진 컨테이너입니다.
	- `stack`, `queue`, `priority_queue`

```text
[sequence containers]

vector / deque / array
┌────┬────┬────┬────┬────┐
│ e0 │ e1 │ e2 │ e3 │ e4 │
└────┴────┴────┴────┴────┘

list / forward_list
┌────┐      ┌────┐     ┌────┐
│ e0 │ ◀──▶ │ e1 │ ◀──▶│ e2 │
└────┘      └────┘     └────┘

[associative containers]

set / map
          ┌────┐
          │ e1 │
          └────┘
         /      \
     ┌────┐    ┌────┐
     │ e0 │    │ e2 │
     └────┘    └────┘

[unordered associative containers]

bucket array              elements
┌──────┐                  ┌─────┐
│ h(1) │─────────────────▶│ key │
├──────┤                  └─────┘
│ h(2) │────────────┐     ┌─────┬───────┐
├──────┤            └────▶│ key │ value │
│ ...  │                  └─────┴───────┘
└──────┘
```


주요하게 사용될 수 있는 컨테이너들을 소개하겠습니다.
### `std::vector`
연속된 메모리에 같은 자료형의 원소를 저장하는 동적 배열입니다. 
인덱스를 통한 접근은 $O(1)$, 원소를 추가하는 연산은 평균적으로 $O(1)$입니다. 반면 중간에 원소를 삽입하거나 삭제하면 뒤쪽 원소를 이동해야 하므로 $O(n)$의 비용이 발생합니다.

저장 공간이 부족해지면 더 큰 메모리를 확보한 뒤 기존 원소를 이동하는 재할당(reallocation)이 일어날 수 있고 이 때 기존 원소를 가리키던 포인터, 참조자, 반복자는 무효화될 수 있습니다. 

```cpp
#include <iostream>
#include <vector>

int main() {
    std::vector<int> numbers = {10, 20, 30};
    numbers.push_back(40);
    numbers.emplace_back(50);

    numbers.erase(numbers.begin() + 1);  // 20

    for (const int &number : numbers) {
        std::cout << number << " ";     // 10 30 40 50
    }

    numbers.reserve(6);
}
```
- `push_back(value)`: 기존 객체를 벡터의 끝에 복사하거나 이동합니다.
- `emplace_back(args...)`: 전달받은 생성자 인자로 벡터 내부에 객체를 직접 생성합니다.
- `operator[]`: 범위 검사 없이 원소에 접근합니다.
- `at()`: 범위를 검사하며 원소에 접근합니다.
- `erase(iterator)`: 반복자가 가리키는 원소를 삭제합니다.
- `reserve()`: 필요한 원소의 수를 알고 있다면, 이미 용량을 확보할 수도 있습니다. (실제 원소 수를 늘리지는 않습니다)

```text
// numbers = {10, 20, 30}

주소       0x1000   0x1004   0x1008
        ┌────────┬────────┬────────┐
        │   10   │   20   │   30   │     size = 3, capacity = 3
        └────────┴────────┴────────┘


// numbers.push_back(40)
                        temporary object
                        ┌────────┐
                        │   40   │
                        └────────┘
                           │ copy to
                           ▼

주소       0x5000   0x5004   0x5008   0x500C   0x5010   0x5014
        ┌────────┬────────┬────────┬────────┬┄┄┄┄┄┄┄┄┬┄┄┄┄┄┄┄┄┐
        │   10   │   20   │   30   │   40   │        │        │   size = 4, capacity = 6
        └────────┴────────┴────────┴────────┴┄┄┄┄┄┄┄┄┴┄┄┄┄┄┄┄┄┘


numbers.emplace_back(50)

주소       0x5000   0x5004   0x5008   0x500C   0x5010   0x5014
        ┌────────┬────────┬────────┬────────┬────────┬┄┄┄┄┄┄┄┄┐
        │   10   │   20   │   30   │   40   │   50   │        │   size = 5, capacity = 6
        └────────┴────────┴────────┴────────┴────────┴┄┄┄┄┄┄┄┄┘
                                                ▲
                                                │ direct object creation


numbers.erase(numbers.begin() + 1)

주소       0x5000   0x5004   0x5008   0x500C   0x5010   0x5014
        ┌────────┬────────┬────────┬────────┬┄┄┄┄┄┄┄┄┬┄┄┄┄┄┄┄┄┐
        │   10   │   30   │   40   │   50   │        │        │   size = 4, capacity = 6
        └────────┴────────┴────────┴────────┴┄┄┄┄┄┄┄┄┴┄┄┄┄┄┄┄┄┘
```

### `std::set`
중복되지 않는 원소를 정렬된 상태로 관리하는 연관 컨테이너입니다. 
일반적으로 균형 이진 탐색 트리로 구현되며 삽입, 삭제, 검색 모두 $O(\log n)$입니다. 
원소는 정렬 기준에서 key 역할을 하기 때문에 반복자를 통해 직접 수정할 수 없고 값을 변경하려면 기존 원소를 삭제한 뒤 새 값을 삽입해야 합니다.

- `insert(value)`: 원소를 삽입합니다. 이미 같은 값이 있으면 새 원소를 추가하지 않습니다.
- `find(value)`: 원소를 찾고, 없으면 `end()`를 반환합니다.
- `count(value)`: 해당 값이 있으면 `1`, 없으면 `0`을 반환합니다.
- `erase(value)`: 해당 값을 삭제합니다.

```cpp
#include <iostream>
#include <set>

int main() {
    std::set<int> numbers = {30, 10, 20, 20};
    numbers.insert(40);

    if (numbers.find(20) != numbers.end()) {
        std::cout << "20 exists\n";
    }

    for (const int &number : numbers) {
        std::cout << number << " ";       // 10 20 30 40
    }
}
```

```text
set<int> numbers = {30, 10, 20, 20}

          ┌────┐                // already exists, not inserted
          │ 20 │                        ┌────┐
          └────┘                        │ 20 │
         /      \                       └────┘
     ┌────┐    ┌────┐
     │ 10 │    │ 30 │
     └────┘    └────┘

numbers.insert(40)

          ┌────┐
          │ 20 │
          └────┘
         /      \
     ┌────┐    ┌────┐
     │ 10 │    │ 30 │
     └────┘    └────┘
       /
   ┌────┐
   │ 40 │
   └────┘
```

### `std::unordered_map`
key와 value를 쌍으로 가지는 저장하는 해시 기반 연관 컨테이너입니다. key를 이용한 삽입, 삭제, 검색은 평균적으로 $O(1)$이지만 해시 충돌이 많으면 최악의 경우 $O(n)$이 될 수 있습니다. 
(원소의 순서는 보장되지 않으므로 정렬된 순서가 필요하다면 `std::map`을 고려해야 합니다)

- `insert({key, value})`: key-value 쌍을 삽입합니다. (이미 같은 key가 있으면 삽입하지 않습니다)
- `operator[](key)`: key에 대응하는 value에 접근합니다 (key가 없으면 기본값으로 새 원소를 생성합니다)
- `find(key)`: key를 검색하고, 없으면 `end()`를 반환합니다.
- `count(key)`: key가 있으면 `1`, 없으면 `0`을 반환합니다.
- `erase(key)`: key에 해당하는 원소를 삭제합니다.

```cpp
#include <iostream>
#include <string>
#include <unordered_map>

int main() {
    std::unordered_map<std::string, int> scores;
    scores.insert({"Alice", 90});
    scores["Bob"] = 85;

    auto it = scores.find("Alice");
    if (it != scores.end()) {
        std::cout << it->first << ": " << it->second << "\n";
    }

    for (const auto &[name, score] : scores) {
        std::cout << name << ": " << score << "\n";
    }
}
```

```text
unordered_map<string, int> scores

                   ┌────────────────────┐     ┌───────────┐
                   │ hash(key)          │────▶│ bucket[0] │
                   │ % bucket_count     │     ├───────────┤
                   └────────────────────┘     │ bucket[1] │
                                              ├───────────┤
                                              │ bucket[2] │
                                              └───────────┘

scores.insert({"Alice", 90})

┌────────────┐     ┌────────────────────┐     ┌───────────┐     ┌────────────┐
│ Alice : 90 │────▶│ hash(key)          │────▶│ bucket[0] │────▶│ Alice : 90 │
└────────────┘     │ % bucket_count     │     ├───────────┤     └────────────┘
                   └────────────────────┘     │ bucket[1] │
                                              ├───────────┤
                                              │ bucket[2] │
                                              └───────────┘

scores.insert({"Bob", 85})

┌──────────┐     ┌────────────────────┐     ┌───────────┐     ┌────────────┐
│ Bob : 85 │────▶│ hash(key)          │────▶│ bucket[0] │────▶│ Alice : 90 │
└──────────┘     │ % bucket_count     │     ├───────────┤     └────────────┘
                 └────────────────────┘     │ bucket[1] │
                                            ├───────────┤     ┌────────────┐
                                            │ bucket[2] │────▶│ Bob : 85   │
                                            └───────────┘     └────────────┘


scores.find("Alice")

┌─────────┐     ┌────────────────────┐     ┌───────────┐     ┌────────────┐
│ "Alice" │────▶│ hash(key)          │────▶│ bucket[0] │────▶│ Alice : 90 │
└─────────┘     │ % bucket_count     │     ├───────────┤     └────────────┘
                └────────────────────┘     │ bucket[1] │
                                           ├───────────┤     ┌────────────┐
                                           │ bucket[2] │────▶│ Bob : 85   │
                                           └───────────┘     └────────────┘
```

## 반복자 (Iterator)

**반복자 (Iterator)** 는 컨테이너 내부의 특정 원소를 가리키면서 다음 원소로 이동할 수 있는 객체입니다.
컨테이너마다 내부 구조는 다르지만 반복자라는 공통 인터페이스를 통해 동일한 형태의 알고리즘을 적용할 수 있습니다.
포인터와 유사하게 역참조 연산으로 현재 원소에 접근하고 증가 연산으로 위치를 이동할 수 있습니다.

```cpp
#include <iostream>
#include <vector>

int main()
{
    vector<int> v = {10, 20, 30, 40};

    for (vector<int>::iterator it = v.begin(); it != v.end(); ++it) {
        std::cout << *it << " "; 
    }

    return 0;
}
```

## `auto` keyword
`auto`는 변수의 초기화 표현식을 기준으로 컴파일러가 자료형을 자동으로 추론하도록 하는 키워드입니다.
자료형을 간단하게 표현할 수 있고, `for` 구문에서도 유용하게 사용할 수 있습니다.

```cpp
#include <string>

int main() {
    // using auto to declare basic variables
    auto a = 1;
    auto b = 3.2;
    auto c = std::string("Hello");

    Abcdefg obj(2);
    // using auto to declare class object
    auto obj1 = Abcdefg(2);
}
```

하지만 실제 자료형을 명확히 인지하지 못한 상태에서 사용하게 되면, 의도하지 않은 복사나 형식 오류가 발생할 수 있으므로 주의해야 합니다.
