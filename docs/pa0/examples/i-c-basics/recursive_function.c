#include <stdio.h>

unsigned int factorial(unsigned int n) {
  // A condition that stops the recursion. (best case)
  if (n == 0) {
    return 1;
  }

  // Reduces the problem and calls the function itself. (recursive case)
  return n * factorial(n - 1);
}

int main(void) {
  for (unsigned int i = 0; i <= 10; i++) {
    printf("%u! = %u\n", i, factorial(i));
  }

  return 0;
}
