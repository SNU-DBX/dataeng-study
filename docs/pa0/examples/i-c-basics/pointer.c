#include <stdio.h>

static void update_copy(int value) {
  value = 10;
  printf("inside update_copy: %d\n", value);
}

static void update_through_pointer(int *pointer) {
  if (pointer != NULL) {
    *pointer = 99;
  }
}

int main(void) {
  int v1 = 42;
  int *pointer = &v1;
  printf("v1: %d\n\n", v1);

  // Dereferencing the pointer accesses the object(v1) it points to.
  *pointer = 20;
  printf("v1: %d\n", v1);
  printf("*pointer: %d\n\n", *pointer);

  int v2 = 30;
  // Assigning a new address changes which object the pointer refers to.
  pointer = &v2;
  printf("v1: %d\n", v1);
  printf("*pointer: %d\n\n", *pointer);

  // Check for NULL before dereferencing a pointer.
  int *null_ptr = NULL;
  if (null_ptr != NULL) {
    printf("null_ptr: %d\n\n", *null_ptr);
  } else {
    printf("null_ptr does not point to an object.\n\n");
  }

  // ERROR: Dereferencing a null pointer causes undefined behavior.
  // printf("%d\n", *null_ptr);

  int values[] = {10, 20, 30};

  // In most expressions, an array name is converted to a pointer to its first element.
  int *it = values;
  printf("*it: %d\n", *it);
  // Pointer arithmetic moves in units of the pointed-to type.
  printf("*(it + 1): %d, it[1]: %d\n", *(it + 1), it[1]);

  // A pointer to an array can be used to access rows of a matrix.
  int matrix[2][3] = {
      {1, 2, 3},
      {4, 5, 6},
  };
  int (*row)[3] = matrix;

  printf("row[1][2]: %d\n", row[1][2]);

  // The function receives a copy, so the caller's value remains unchanged.
  int number = 1;
  update_copy(number);
  printf("after update_copy: %d\n", number);

  // The pointer is also copied, but it still refers to the caller's object.
  update_through_pointer(&number);
  printf("after update_through_pointer: %d\n", number);

  return 0;
}
