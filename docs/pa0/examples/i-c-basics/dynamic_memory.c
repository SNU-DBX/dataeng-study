#include <stdio.h>
#include <stdlib.h>

int main(void) {
  // Malloc receives the required size in bytes and returns a pointer to the
  // allocated memory.
  int *a = (int*)malloc(sizeof(int));

  // Dereference the returned pointer after checking for NULL.
  if (a == NULL) {
    fprintf(stderr, "memory allocation failed\n");
    return 1;
  }

  *a = 10;
  printf("*a: %d\n", *a);

  // Release dynamic memory after usage.
  // Pass the same starting address returned by malloc.
  free(a);
  a = NULL;

  size_t count = 5;
  int *values = (int*)malloc(count * sizeof(int));

  if (values == NULL) {
    fprintf(stderr, "memory allocation failed\n");
    return 1;
  }

  // Memory returned by malloc has indeterminate initial values.
  // Initialize each element before usage.
  for (size_t i = 0; i < count; i++) {
    values[i] = (int)(i + 1) * 10;
  }

  for (size_t i = 0; i < count; i++) {
    printf(" %d", values[i]);
  }
  printf("\n");

  free(values);
  values = NULL;

  // ERROR: Reading or writing through a pointer after free is undefined behavior.
  // printf("%d\n", values[0]);

  return 0;
}
