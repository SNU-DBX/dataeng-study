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
