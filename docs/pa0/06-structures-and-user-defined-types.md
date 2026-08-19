# 6 · 구조체와 사용자 정의 자료형

## Structure
구조체(structure)는 서로 다른 자료형의 데이터를 하나의 객체로 묶어서 관리할 수 있는 사용자 정의 자료형입니다.

### Structure Declaration
`struct` 선언은 구조체의 형태를 정의합니다. 구조체 안에 선언된 각 변수를 **멤버(member)**라고 합니다.

```c
// ①       ②
struct Student {
	// ③
	char name[32];
	int id;
	double score;
};
```

① **`struct` keyword**: 구조체 자료형을 선언합니다.  
② **structure tag**: 구조체 자료형을 구분하는 이름입니다.  
③ **members**: 하나의 구조체에 포함할 데이터입니다.  

### Initialization and Member Access

구조체 선언은 자료형을 정의할 뿐, 그 자체로 데이터를 저장하는 객체를 생성하지는 않습니다.

```c
struct Student student;          // declare one object
struct Student students[30];     // declare an array of objects
```

구조체 객체를 선언과 동시에 초기화할 수도 있습니다. 
기본적으로 초기화 목록은 구조체에 선언된 멤버 순서와 일치해야 합니다. 

```c
#include <stdio.h>

struct Student {
	char name[32];
	int id;
	double score;
};

int main(void) {
	struct Student student = {"Kim", 20260001, 91.5};
	student.id = "20260001";    // access with dot operator
}
```

### Pointer to Structure
- `.` (Dot): 구조체 변수의 멤버에 접근할 수 있습니다.
- `->` (Arrow): 구조체 포인터가 가리키는 객체의 멤버에 접근할 수 있습니다.

```c
void update_score(struct Student *student, double score) {
  if (student != NULL) {
    student->score = score;
  }
}

int main(void) {
	struct Student student = {"Kim", 20260001, 91.5};
	struct Student *p = &student;
	
	update_score(p, 95.0);
	
	printf("%.1f\n", student.score);  // dot: 91.5
	printf("%.1f\n", p->score);       // arrow: 91.5
	printf("%.1f\n", (*p).score);     // dereference + dot: 91.5
}
```

!!! note "얕은 복사 (Shallow Copy)"
    - 구조체 멤버가 포인터인 경우에는 포인터가 가리키는 데이터가 아니라 주소값만 복사되는데, 이것을 **얕은 복사 (Shallow Copy)** 라고 합니다.
    - 두 구조체는 같은 동적 메모리를 가리킬 수 있으며, 소유권과 해제 시점은 별도로 관리해야 합니다.

### Structure Assignment
같은 구조체 타입의 객체끼리는 대입할 수 있습니다. 이 경우 모든 멤버의 값이 복사됩니다.

```c
struct Student student1 = {"Kim", 20260001, 91.5};
struct Student student2 = student1;

student2.score = 80.0;

printf("%.1f\n", student1.score); // 91.5
printf("%.1f\n", student1.score); // 80.0
```

- 배열은 기본적으로 대입할 수 없지만, 배열을 멤버로 포함한 구조체는 구조체 전체를 대입하여 복사할 수 있습니다.


### Array of Structures
구조체로 이루어진 배열을 구성할 수 있습니다.

```c
struct Student students[3] = {
	{"Kim", 20260001, 91.5},
	{"Park", 20260002, 87.9},
	{"Seo", 20260003, 95.5},
};

for (int i = 0; i < 3; i++)
	printf("Name: %s, Id: %d, Score: %.1f\n", students[i].name,  students[i].id, students[i].score);
```

## Memory Layout of Structures

### Designated Initializers
지정 초기화(designated initializer)를 사용하면 구조체 멤버의 이름을 직접 지정하여 초기화할 수 있습니다.

```c
struct Student student = {
	.id = 20260001,
	.score = 91.5,
	.name = "Kim"
};
```

### Alignment and Padding
- 컴파일러는 CPU가 각 자료형을 효율적으로 접근할 수 있도록 구조체 멤버를 일정한 주소 단위에 맞춰 배치합니다. 이를 **정렬(alignment)**이라고 합니다.
- 정렬 조건을 맞추기 위해 멤버 사이 또는 구조체 끝에 추가되는 빈 공간을 **패딩(padding)** 이라고 합니다. 
  따라서 구조체의 크기는 각 멤버 크기의 단순 합보다 클 수 있고,
  자료형의 크기와 정렬 조건, 패딩의 위치는 컴파일러와 시스템에 따라 달라질 수 있습니다.

```c
struct S {
	char c; // 1 byte
	        // padding may be inserted here
	int n;  // commonly 4 bytes
};

printf("%zu\n", sizeof(struct S)); // commonly 8, not 5
```

### Member Ordering
구조체 멤버의 순서를 조정하면 필요한 패딩을 줄일 수 있습니다. 
정렬 요구사항이 큰 타입부터 작은 타입 순으로 배치하면 메모리 공간을 효율적으로 사용할 수 있습니다.
하지만 항상 최소 크기를 보장하는 것은 아니니 유의해야 합니다.

```c
struct MorePadding {
	char first;
	double value;
	char second;
};

struct LessPadding {
	double value;
	char first;
	char second;
};

printf("%zu\n", sizeof(struct MorePadding));
printf("%zu\n", sizeof(struct LessPadding));
```

단, 멤버 순서에 따른 실제 크기 차이는 구현 환경에 따라 달라지므로
구조체의 의미와 가독성을 해치지 않는 범위에서 멤버 순서를 결정해야 합니다.
