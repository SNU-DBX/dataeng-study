# 2 · 자료형과 제어 흐름

## Primitive Data Types
**자료형(Data Type)** 은 해당 객체가 가질 수 있는 값의 범위와 수행할 수 있는 연산을 결정합니다.
C 언어의 대표적인 자료형은 다음과 같습니다.

| Type   | Size (bytes) | Range                                     |
| ------ | ------------ | ----------------------------------------- |
| int    | 4            | -2,147,483,648 ~ 2,147,483,647            |
| float  | 4            | -3.4 × 10³⁸ ~ 3.4 × 10³⁸                  |
| double | 8            | -1.7 × 10³⁰⁸ ~ 1.7 × 10³⁰⁸                |
| char   | 1            | -128 ~ 127 (signed) or 0 ~ 255 (unsigned) |
| bool   | 1            | 0 or 1                                    |

## Operators
연산자(operator)는 하나 이상의 값이나 변수에 대하여 특정 연산을 수행하도록 명시하는 기호입니다.
C 언어에서는 다음과 같은 연산자들을 지원합니다.

### Arithmetic Operators
사칙연산과 정수에 대한 나머지 연산을 수행합니다.

| Operator Type | Meaning                   | Examples                                                                                                                                                  |
| ------------- | ------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `+`           | Addition                  | • `x = 3 + 2; /* constants */`<br>• `x = y + z; /* variables */`<br>• `x = y + 2; /* both */`                                                             |
| `-`           | Subtraction               | • `x = 8 - 3; /* constants */`<br>• `x = y - z; /* variables */`<br>• `x = y - 2; /* both */`                                                             |
| `*`           | Multiplication            | • `x = 4 * 3; /* constants */`<br>• `area = width * height; /* variables */`<br>• `x = y * 2; /* both */`                                                 |
| `/`           | Division                  | • `double x = 5 / 2; /* x = 2.0: integer division */`<br>• `double x = 5.0 / 2; /* x = 2.5 */`<br>• `int x = 5.0 / 2; /* x = 2: assignment conversion */` |
| `%`           | Remainder (integers only) | • `x = 5 % 2; /* x = 1 */`<br>• `x = 11 % 4; /* x = 3 */`<br>• `x = 7 % 10; /* x = 7 */`                                                                  |

### Relational Operators
두 값을 비교하여 참 또는 거짓의 결과를 생성합니다. (1: true, 0: false)

| Operator Type | Meaning | Examples |
| ------------- | ------- | -------- |
| `==` | Equal to | • `3 == 3 /* evaluates to 1 */`<br>• `'A' == 'a' /* evaluates to 0 */` |
| `!=` | Not equal to | • `3 != 3 /* evaluates to 0 */`<br>• `2.5 != 3 /* evaluates to 1 */` |
| `<` | Less than | • `3 < 3 /* evaluates to 0 */`<br>• `'A' < 'B' /* evaluates to 1 */` |
| `<=` | Less than or equal to | • `3 <= 3 /* evaluates to 1 */`<br>• `3.5 <= 3 /* evaluates to 0 */` |
| `>` | Greater than | • `4 > 2 /* evaluates to 1 */`<br>• `2.5 > 3 /* evaluates to 0 */` |
| `>=` | Greater than or equal to | • `4 >= 4 /* evaluates to 1 */`<br>• `2.5 >= 3 /* evaluates to 0 */` |

### Logical Operators
조건식의 AND, OR, NOT을 계산합니다.

| Operator Type | Meaning | Examples |
| ------------- | ------- | -------- |
| `&&` | Logical AND | • `((12 / 4) == 3) && (2 * 4 == 8) /* 1 */`<br>• `('A' == 'a') && (3 == 3) /* 0 */` |
| `\|\|` | Logical OR | • `(2 == 3) \|\| ('A' == 'A') /* 1 */`<br>• `(2.5 >= 3) \|\| 0 /* 0 */` |
| `!` | Logical NOT | • `!(3 == 3) /* 0 */`<br>• `!(2.5 >= 3) /* 1 */` |

### Bitwise Operators
정수의 개별 bit을 조작하는 연산을 수행합니다.

| Operator Type | Meaning | Examples |
| ------------- | ------- | -------- |
| `&` | Bitwise AND | • `0x57 & 0x03 /* evaluates to 0x03 */`<br>• `0x57 & 0x00 /* evaluates to 0 */` |
| `\|` | Bitwise OR | • `0x500 \| 0x23 /* evaluates to 0x523 */`<br>• `0x050 \| 0 /* evaluates to 0x050 */` |
| `^` | Bitwise XOR | • `0x550 ^ 0x553 /* evaluates to 0x03 */`<br>• `0x33 ^ 0x33 /* evaluates to 0 */` |
| `~` | Bitwise NOT | • `inverted = ~flags;`<br>• `masked = (~flags) & 0xFF;` |
| `<<` | Left shift | • `0x01 << 4 /* evaluates to 0x10 */`<br>• `1 << 3 /* evaluates to 8 */` |
| `>>` | Right shift | • `0x10 >> 4 /* evaluates to 0x01 */`<br>• `8 >> 1 /* evaluates to 4 */` |

### Assignment Operators
변수에 값을 저장하거나 연산의 값을 저장합니다.

| Operator Type | Meaning                        | Examples                                    |
| ------------- | ------------------------------ | ------------------------------------------- |
| `=`           | Assignment                     | • `count = 10;`<br>• `result = x + y;`      |
| `+=`          | Add and assign                 | • `x += 2;`<br>• equivalent to `x = x + 2;` |
| `-=`          | Subtract and assign            | • `x -= 3;`<br>• equivalent to `x = x - 3;` |
| `*=`          | Multiply and assign            | • `x *= 5;`<br>• equivalent to `x = x * 5;` |
| `/=`          | Divide and assign              | • `x /= 2;`<br>• equivalent to `x = x / 2;` |
| `%=`          | Calculate remainder and assign | • `x %= 3;`<br>• equivalent to `x = x % 3;` |

### Increment/Decrement Operators
정수값을 증가/감소시키는 연산을 수행합니다.

| Operator Type | Meaning | Examples |
| ------------- | ------- | -------- |
| `++x` | Increment before evaluation | • `x = 4; y = ++x;`<br>• result: `x = 5`, `y = 5` |
| `x++` | Increment after evaluation | • `x = 4; y = x++;`<br>• result: `x = 5`, `y = 4` |
| `--x` | Decrement before evaluation | • `x = 4; y = --x;`<br>• result: `x = 3`, `y = 3` |
| `x--` | Decrement after evaluation | • `x = 4; y = x--;`<br>• result: `x = 3`, `y = 4` |

## Type Conversion
- C 언어는 자료형에 대하여 서로 다른 타입을 하나의 식에 사용하거나 특정 타입의 변수에 다른 값을 저장할 수 있습니다. (Implicit Type Conversion)
- 또한 강제적인 형 변환도 지원합니다. (Explicit Type Casting)
- 단, 변환 과정에서 값의 범위나 정밀도가 손실될 수 있으므로 반드시 결과 타입을 확인해야 합니다.

### Assignment Conversion
- 오른쪽 값은 왼쪽 변수의 타입으로 변환된 후 저장됩니다.
```c
double value = 3.9;
int x = value;  // 3; fractional part is discarded

int n = 300;
char c = n;    // result is not portable if char cannot represent 300
```

### Integer Promotion
- `char`, `signed char`, `unsigned char`, `short` 등의 작은 정수 타입은 산술 연산 전에 일반적으로 `int` 또는 `unsigned int`로 승격됩니다.
- 따라서 작은 정수 타입끼리 계산하더라도 실제 연산은 보통 `int` 타입으로 수행됩니다.
```c
char a = 10;
char b = 20;
int sum = a + b;  // 30; small integer types are promoted before arithmetic (char, signed char, unsigned char, short, etc.)
```

### Usual Arithmetic Conversions
- 서로 다른 산술 타입을 연산하면 두 피연산자(operand)는 공통 타입으로 변환됩니다.
- 표현 범위가 넓거나 정밀도(precision)가 높은 타입이 공통 타입으로 선택됩니다.
```c
float a = 1.5f;
double b = 2.5;
double result1 = a + b;  // 4.0 = 1.5 + 2.5; float → double

int x = 3;
double y = 2.5;
double result2 = x + y;  // 5.5 = 3.0 + 2.5; int → double
```

### Signed and Unsigned Conversion
- 같은 rank의 signed 타입과 unsigned 타입을 함께 연산하면 signed 값이 unsigned 타입으로 변환됩니다.
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

### Explicit Cast
- 원하는 변환 타입을 명시하여 casting할 수 있습니다.
- 정수 나눗셈 전에 피연산자 하나를 `double`로 변환하면 부동소수점 나눗셈을 수행할 수 있습니다.
```c
int total = 5;
int count = 2;
double average = (double)total / count;
printf("%.1f\n", average);  // 2.5; cast one operand to double before division to perform floating-point division.

int x;
x = (int) 3.7;              // 3; truncation, not rounding
```

## Conditional
조건에 따라 실행할 구문을 선택합니다.
C에서는 0을 거짓(false), 0이 아닌 값을 참(true)으로 판단합니다.

### `if` / `else if` / `else` Statement
```c
if (condition1) {
	// executed when condition1 is true
} else if (condition2) {
	// executed when condition1 is false AND condition2 is true
} else {
	// executed when all conditions are false
}
```

### `switch` Statement
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

## Loops
조건이 충족되는 동안 동일한 코드 블록을 반복해서 실행합니다.

### `while` / `do-while` Loops
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

### `for` Loop
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

### Loop Control: `break` / `continue`
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
