#include <stdio.h>

int main(void) {
  // Array Declaration and Initialization:
  int values[] = {10, 20, 30};

  // Array-to-Pointer Conversion:
  // In most expressions, the array name is converted to a pointer to its
  // first element. This conversion does not occur with sizeof or &.
  int *pointer = values;
  printf("first element through pointer: %d\n", *pointer);
  printf("array size: %zu bytes\n", sizeof(values));
  printf("pointer size: %zu bytes\n", sizeof(pointer));

  // Pointer Arithmetic:
  // Adding one advances by one element, not by one byte.
  printf("*(pointer + 1): %d\n", *(pointer + 1));
  printf("pointer[1]: %d\n", pointer[1]);

  // Array Indexing and Bounds:
  // A valid index ranges from 0 to the number of elements minus one.
  printf("values before update:");
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    printf(" %d", values[i]);
  }
  printf("\n");

  values[1] = 25;
  printf("values[1] after update: %d\n", values[1]);

  // Accessing an index outside the valid range causes undefined behavior.
  // printf("%d\n", values[3]);  // Do not do this.

  // Array Length:
  size_t count = sizeof(values) / sizeof(values[0]);
  printf("number of elements in values: %zu\n", count);

  // Multidimensional Arrays:
  // Elements are stored contiguously in row-major order.
  int matrix[2][3] = {
      {1, 2, 3},
      {4, 5, 6},
  };

  printf("matrix[1][2]: %d\n", matrix[1][2]);
  printf("matrix:\n");
  for (size_t row = 0; row < sizeof(matrix) / sizeof(matrix[0]); row++) {
    for (size_t column = 0;
         column < sizeof(matrix[row]) / sizeof(matrix[row][0]); column++) {
      printf("%d ", matrix[row][column]);
    }
    printf("\n");
  }

  return 0;
}
