#include <stdio.h>

struct Student {
  char name[32];
  int id;
  double score;
};

static void update_score(struct Student *student, double score) {
  if (student != NULL) {
    student->score = score;
  }
}

static void print_student(const struct Student *student) {
  if (student != NULL) {
    printf("Name: %s, Id: %d, Score: %.1f\n", student->name, student->id, student->score);
  }
}

int main(void) {
  struct Student student1 = {"Kim", 20260001, 91.5};
  print_student(&student1);

	struct Student *p = &student1;
	update_score(p, 95.0);
	
  printf("\nAfter update_score()...\n");
	printf("student1.score: %.1f\n", student1.score);
	printf("*p->score: %.1f\n", p->score);
	printf("(*p).score: %.1f\n", (*p).score);
  print_student(&student1);

  // Every member (including the array) is copied.
  struct Student student2 = student1;
  student2.score = 80.0;

  printf("\nAfter structure assignment...\n");
  printf("student1.score: %.1f\n", student1.score);
  printf("student2.score: %.1f\n", student2.score);

  struct Student students[] = {
      {"Kim", 20260001, 91.5},
      {"Park", 20260002, 87.9},
      {"Seo", 20260003, 95.5},
  };

  printf("\nStruct Array: \n");
  for (size_t i = 0; i < sizeof(students) / sizeof(students[0]); i++) {
    print_student(&students[i]);
  }

  // Designated Initializers
  struct Student designated_student = {
      .name = "Lee",
      .id = 20260004,
      .score = 93.0,
  };
  printf("\nDesignated initializer:\n");
  print_student(&designated_student);

  return 0;
}
