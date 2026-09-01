// Comments (multi-line)
/**
 * @file source_file_structures.c
 * @author your name (you@domain.com)
 * @brief Tutorial code for basic source file structures.
 */

// Header Inclusions
// Includes the standard library header. (printf, stdout, etc.)
#include <stdio.h>
// Includes the local header file.
#include "local_header.h"

// Symbolic constants
// Define symbolic constants.
#define MAX_SIZE 100

// Type declarations
// Declare global variables.
int global_variable = 0;
static int static_variables[] = {1, 2, 3, 4, 5};

// Function declarations
// Declare function prototypes.
int function_prototype(int a, int b);
void function_prototype_2(void);

// Define functions
// C functions have return type and arguments.
// Code block is enclosed in curly braces '{' and '}'.
int function_prototype(int a, int b) {
	return a + b;
}
void function_prototype_2(void) {
	printf("Hello, World!\n");
}

// Main Functions
// Define the main function.
// The main function is the entry point of the program.
int main() {
    // Reads a global variable defined in custom header file.
    printf("%d\n", LOCAL_HEADER_MAX_SIZE);

	// Reads an integer from the user and stores it in the global variable.
	// The argument should be pointers to the right type of variable.
	scanf("%d", &global_variable);
	
	// Prints the value of the global variable to the console.
	// Corresponding arguments should have the right types. Compiler might check, but not guaranteed.
	printf("%d\n", global_variable);
	
	function_prototype(1, 2);
	function_prototype_2();

	return 0;
}
