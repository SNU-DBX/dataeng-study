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
