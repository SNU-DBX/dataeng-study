# 2 · 자료형과 제어 흐름

## 기본 자료형
**자료형(Data Type)** 은 해당 객체가 가질 수 있는 값의 범위와 수행할 수 있는 연산을 결정합니다. C의 기본 자료형(Primitive Data Types)은 언어가 기본으로 제공하는 값의 종류입니다.
C 언어의 대표적인 자료형은 다음과 같습니다.

| Type   | Size (bytes) | Range                                     |
| ------ | ------------ | ----------------------------------------- |
| int    | 4            | -2,147,483,648 ~ 2,147,483,647            |
| float  | 4            | -3.4 × 10³⁸ ~ 3.4 × 10³⁸                  |
| double | 8            | -1.7 × 10³⁰⁸ ~ 1.7 × 10³⁰⁸                |
| char   | 1            | -128 ~ 127 (signed) or 0 ~ 255 (unsigned) |
| bool   | 1            | 0 or 1                                    |

## 연산자
연산자(Operators)는 하나 이상의 값이나 변수에 대하여 특정 연산을 수행하도록 명시하는 기호입니다.
C 언어에서는 다음과 같은 연산자들을 지원합니다.

### 정수 연산자
산술 연산자(Arithmetic Operators)는 사칙연산과 정수에 대한 나머지 연산을 수행합니다.

| Operator Type | Meaning                   | Examples                                                                                                                                                  |
| ------------- | ------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `+`           | Addition                  | • `x = 3 + 2; /* constants */`<br>• `x = y + z; /* variables */`<br>• `x = y + 2; /* both */`                                                             |
| `-`           | Subtraction               | • `x = 8 - 3; /* constants */`<br>• `x = y - z; /* variables */`<br>• `x = y - 2; /* both */`                                                             |
| `*`           | Multiplication            | • `x = 4 * 3; /* constants */`<br>• `area = width * height; /* variables */`<br>• `x = y * 2; /* both */`                                                 |
| `/`           | Division                  | • `double x = 5 / 2; /* x = 2.0: integer division */`<br>• `double x = 5.0 / 2; /* x = 2.5 */`<br>• `int x = 5.0 / 2; /* x = 2: assignment conversion */` |
| `%`           | Remainder (integers only) | • `x = 5 % 2; /* x = 1 */`<br>• `x = 11 % 4; /* x = 3 */`<br>• `x = 7 % 10; /* x = 7 */`                                                                  |

### 관계 연산자
관계 연산자(Relational Operators)는 두 값을 비교하여 참 또는 거짓의 결과를 생성합니다. (1: true, 0: false)

| Operator Type | Meaning | Examples |
| ------------- | ------- | -------- |
| `==` | Equal to | • `3 == 3 /* evaluates to 1 */`<br>• `'A' == 'a' /* evaluates to 0 */` |
| `!=` | Not equal to | • `3 != 3 /* evaluates to 0 */`<br>• `2.5 != 3 /* evaluates to 1 */` |
| `<` | Less than | • `3 < 3 /* evaluates to 0 */`<br>• `'A' < 'B' /* evaluates to 1 */` |
| `<=` | Less than or equal to | • `3 <= 3 /* evaluates to 1 */`<br>• `3.5 <= 3 /* evaluates to 0 */` |
| `>` | Greater than | • `4 > 2 /* evaluates to 1 */`<br>• `2.5 > 3 /* evaluates to 0 */` |
| `>=` | Greater than or equal to | • `4 >= 4 /* evaluates to 1 */`<br>• `2.5 >= 3 /* evaluates to 0 */` |

### 논리 연산자
논리 연산자(Logical Operators)는 조건식의 AND, OR, NOT을 계산합니다.

| Operator Type | Meaning | Examples |
| ------------- | ------- | -------- |
| `&&` | Logical AND | • `((12 / 4) == 3) && (2 * 4 == 8) /* 1 */`<br>• `('A' == 'a') && (3 == 3) /* 0 */` |
| `\|\|` | Logical OR | • `(2 == 3) \|\| ('A' == 'A') /* 1 */`<br>• `(2.5 >= 3) \|\| 0 /* 0 */` |
| `!` | Logical NOT | • `!(3 == 3) /* 0 */`<br>• `!(2.5 >= 3) /* 1 */` |

### 비트 연산자
비트 연산자(Bitwise Operators)는 정수의 개별 bit을 조작하는 연산을 수행합니다.

| Operator Type | Meaning | Examples |
| ------------- | ------- | -------- |
| `&` | Bitwise AND | • `0x57 & 0x03 /* evaluates to 0x03 */`<br>• `0x57 & 0x00 /* evaluates to 0 */` |
| `\|` | Bitwise OR | • `0x500 \| 0x23 /* evaluates to 0x523 */`<br>• `0x050 \| 0 /* evaluates to 0x050 */` |
| `^` | Bitwise XOR | • `0x550 ^ 0x553 /* evaluates to 0x03 */`<br>• `0x33 ^ 0x33 /* evaluates to 0 */` |
| `~` | Bitwise NOT | • `inverted = ~flags;`<br>• `masked = (~flags) & 0xFF;` |
| `<<` | Left shift | • `0x01 << 4 /* evaluates to 0x10 */`<br>• `1 << 3 /* evaluates to 8 */` |
| `>>` | Right shift | • `0x10 >> 4 /* evaluates to 0x01 */`<br>• `8 >> 1 /* evaluates to 4 */` |

### 대입 연산자
대입 연산자(Assignment Operators)는 변수에 값을 저장하거나 연산의 값을 저장합니다.

| Operator Type | Meaning                        | Examples                                    |
| ------------- | ------------------------------ | ------------------------------------------- |
| `=`           | Assignment                     | • `count = 10;`<br>• `result = x + y;`      |
| `+=`          | Add and assign                 | • `x += 2;`<br>• equivalent to `x = x + 2;` |
| `-=`          | Subtract and assign            | • `x -= 3;`<br>• equivalent to `x = x - 3;` |
| `*=`          | Multiply and assign            | • `x *= 5;`<br>• equivalent to `x = x * 5;` |
| `/=`          | Divide and assign              | • `x /= 2;`<br>• equivalent to `x = x / 2;` |
| `%=`          | Calculate remainder and assign | • `x %= 3;`<br>• equivalent to `x = x % 3;` |

### 증가/감소 연산자
증가/감소 연산자(Increment/Decrement Operators)는 정수값을 증가/감소시키는 연산을 수행합니다.

| Operator Type | Meaning | Examples |
| ------------- | ------- | -------- |
| `++x` | Increment before evaluation | • `x = 4; y = ++x;`<br>• result: `x = 5`, `y = 5` |
| `x++` | Increment after evaluation | • `x = 4; y = x++;`<br>• result: `x = 5`, `y = 4` |
| `--x` | Decrement before evaluation | • `x = 4; y = --x;`<br>• result: `x = 3`, `y = 3` |
| `x--` | Decrement after evaluation | • `x = 4; y = x--;`<br>• result: `x = 3`, `y = 4` |

## 형 변환
- 형 변환(Type Conversion)은 서로 다른 타입의 값을 필요한 타입으로 바꾸는 과정입니다.
- C 언어는 자료형에 대하여 서로 다른 타입을 하나의 식에 사용하거나 특정 타입의 변수에 다른 값을 저장할 수 있습니다. (Implicit Type Conversion)
- 또한 강제적인 형 변환도 지원합니다. (Explicit Type Casting)
- 단, 변환 과정에서 값의 범위나 정밀도가 손실될 수 있으므로 반드시 결과 타입을 확인해야 합니다.

### 대입 변환
- 대입 변환(Assignment Conversion)에서는 오른쪽 값이 왼쪽 변수의 타입으로 변환된 후 저장됩니다.
```c
double value = 3.9;
int x = value;  // 3; fractional part is discarded

int n = 300;
char c = n;    // result is not portable if char cannot represent 300
```

### 정수 승격
- 정수 승격(Integer Promotion)은 `char`, `signed char`, `unsigned char`, `short` 등의 작은 정수 타입이 산술 연산 전에 일반적으로 `int` 또는 `unsigned int`로 승격되는 규칙입니다.
- 따라서 작은 정수 타입끼리 계산하더라도 실제 연산은 보통 `int` 타입으로 수행됩니다.
```c
char a = 10;
char b = 20;
int sum = a + b;  // 30; small integer types are promoted before arithmetic (char, signed char, unsigned char, short, etc.)
```

### 일반 산술 변환
- 일반 산술 변환(Usual Arithmetic Conversions)은 서로 다른 산술 타입을 연산할 때 두 피연산자(operand)를 공통 타입으로 변환하는 규칙입니다.
- 표현 범위가 넓거나 정밀도(precision)가 높은 타입이 공통 타입으로 선택됩니다.
```c
float a = 1.5f;
double b = 2.5;
double result1 = a + b;  // 4.0 = 1.5 + 2.5; float → double

int x = 3;
double y = 2.5;
double result2 = x + y;  // 5.5 = 3.0 + 2.5; int → double
```

### 부호 있는 정수와 부호 없는 정수 변환
- 부호 있는 정수와 부호 없는 정수 변환(Signed and Unsigned Conversion)에서는 같은 rank의 signed 타입과 unsigned 타입을 함께 연산하면 signed 값이 unsigned 타입으로 변환됩니다.
- 부호가 다른 정수끼리 비교하거나 연산하는 코드는 올바르지 못한 비교를 수행할 수 있습니다.
```c
int x = -1;
unsigned int y = 1;

// A negative signed value may be converted
// to a large unsigned value. (for here, UINT_MAX)
if (x < y) {
	printf("x < y\n");
} else {
	printf("x >= y\n"); // this
}

// Check for a negative signed value
// before safely comparing both values as unsigned.
if (x < 0 || (unsigned int)x < y) {
	printf("x < y\n");  // this
} else {
	printf("x >= y\n");
}
```

### 명시적 형 변환
- 명시적 형 변환(Explicit Cast)은 원하는 변환 타입을 코드에 직접 써서 casting하는 방식입니다.
- 정수 나눗셈 전에 피연산자 하나를 `double`로 변환하면 부동소수점 나눗셈을 수행할 수 있습니다.
```c
int total = 5;
int count = 2;
double average = (double)total / count;
printf("%.1f\n", average);  // 2.5; cast one operand to double before division to perform floating-point division.

int x;
x = (int) 3.7;              // 3; truncation, not rounding
```


<details>
<summary>예제: <code>type_conversion.c</code></summary>

```c
#include <stdio.h>

int main(void) {
  // Assignment Conversion:
  // Fractional part will be discarded.
  double value = 3.9;
  int v = value;

  printf("double → int: %.1f → %d\n", value, v);

  // Integer Promotion:
  char a = 10;
  char b = 20;
  // Small integer types are promoted before arithmetic (char, signed char, unsigned char, short, etc.)
  int sum = a + b;
  printf("char + char → int: %d\n", sum);

  // Usual Arithmetic Conversions:
  // Operands of different arithmetic types are converted to a common type
  // usually one with a wider range or higher precision.
  float c = 1.5f;
  double d = 2.5;
  double result1 = c + d;
  printf("float + double: %.1f + %.1f = %.1f\n", c, d, result1);

  int e = 3;
  double f = 2.5;
  double result2 = e + f;
  printf("int + double: %d + %.1f = %.1f\n", e, f, result2);

  // Signed and Unsigned Conversion:
  int x = -1;
  unsigned int y = 1;

  // A negative signed value is converted to a large unsigned value. (for here, UINT_MAX)
  if (x < y) {
    printf("x < y\n");
  } else {
    printf("x >= y\n"); // This
  }

  // Check whether x is negative before converting it to unsigned.
  if (x < 0 || (unsigned int)x < y) {
    printf("x < y\n");  // This
  } else {
    printf("x >= y\n");
  }

  // Explicit Type Casting:
  int total = 5;
  int count = 2;
  // Cast one operand to double to perform floating-point division.
  double average = (double)total / count;
  printf("floating-point division: %d / %d = %.1f\n", total, count, average);

  int truncated = (int)3.7;
  printf("truncation, not rounding: %d\n", truncated);

  return 0;
}
```

</details>

## 조건문
조건문(Conditional)은 조건에 따라 실행할 구문을 선택합니다.
C에서는 0을 거짓(false), 0이 아닌 값을 참(true)으로 판단합니다.

### `if` / `else if` / `else`
```c
if (condition1) {
	// executed when condition1 is true
} else if (condition2) {
	// executed when condition1 is false AND condition2 is true
} else {
	// executed when all conditions are false
}
```

### `switch`
```c
switch (expression) {  // expression must be an integer type
	case value1:       // case label must be integer constant expressions
		// without break, execution falls through the next case
	case value2:
		// ...
		break;
	default:
		// when no case matches
}
```


<details>
<summary>예제: <code>condition.c</code></summary>

```c
#include <stdio.h>

static void if_else_condition(int score) {
  printf("score: %d, grade: ", score);

  if (score >= 90) {
    printf("A\n");
  } else if (score >= 80) {
    printf("B\n");
  } else if (score >= 70) {
    printf("C\n");
  } else {
    printf("F\n");
  }
}

// With break, execution stops after the matching case and exits the switch.
static void switch_case(int number) {
  switch (number) {
    case 1:
      printf("1\n");
      break;
    case 2:
      printf("2\n");
      break;
    case 3:
      printf("3\n");
      break;
    case 4:
      printf("4\n");
      break;
    default:
      printf("Not 1, 2, 3, or 4\n");
  }
}

// Without break, execution falls through to subsequent cases until break, return, or the end of the switch.
static void switch_case_fallthrough(int number) {
  switch (number) {
    case 1:
      printf("1\n");
    case 2:
      printf("2\n");
    case 3:
      printf("3\n");
    case 4:
      printf("4\n");
      break;
    default:
      printf("Not 1, 2, 3, or 4\n");
  }
}

int main(void) {
  if_else_condition(95);
  if_else_condition(82);
  if_else_condition(75);
  if_else_condition(60);
  if_else_condition(60);

  // With break, execution stops after the matching case and exits the switch.
  switch_case(2);
  // Without break, execution falls through to subsequent cases until break, return, or the end of the switch.
  switch_case_fallthrough(2);

  return 0;
}
```

</details>

## 반복문
반복문(Loops)은 조건이 충족되는 동안 동일한 코드 블록을 반복해서 실행합니다.

### `while` / `do-while`
```c
// Condition checked before each iteration.
// Code block may be executed 0 times.
while (condition) {
	// code block
}
```

```c
// Condition checked after each iteration.
// Code block executed at least once.
do {
	// code block
} while (condition);
```

### `for`
```c
/**
 * init-statement: executed once before the loop starts.
 * condition: checked before each iteration; loop exits when the condition is false.
 * inc-expression: executed after each iteration.
 */
for (init-statement; condition; inc-expression) {
	// code block
}
```

### `break` / `continue`
```c
// Exits the innermost loop immediately.
// No further iterations are executed.
for (int i = 0; i < 5; i++) {
	if (i == 3) break;
	printf("%d\n", i);
}

/* Output: 0 1 2 */
```

```c
// Skips the rest of the current iteration.
// Proceeds to the next iteration
for (int i = 0; i < 5; i++) {
	if (i == 3) continue;
	printf("%d ", i);
}

/* Output: 0 1 2 4 */
```


<details>
<summary>예제: <code>loop.c</code></summary>

```c
#include <stdio.h>

int main(void) {
  int i = 0;

  printf("while loop: ");
  while (i < 5) {
    printf(" %d", i);
    i++;
  }
  printf("\n");

  i = 5;

  printf("do-while loop: ");
  do {
    printf(" %d", i);
    i++;
  } while (i < 5);
  printf(" (the body runs at least once)\n");

  printf("\nfor loop... ");
  for (int i = 0; i < 5; i++) {
    printf(" %d", i);
  }
  printf("\n");

  printf("loop with break (i == 3): ");
  for (int i = 0; i < 5; i++) {
    if (i == 3) {
      break;
    }
    printf(" %d", i);
  }
  printf("\n");

  printf("loop with continue (i == 3): ");
  for (int i = 0; i < 5; i++) {
    if (i == 3) {
      continue;
    }
    printf(" %d", i);
  }
  printf("\n");

  return 0;
}
```

</details>
