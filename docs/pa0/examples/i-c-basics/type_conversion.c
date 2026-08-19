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
