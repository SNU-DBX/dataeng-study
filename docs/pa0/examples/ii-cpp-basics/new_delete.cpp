#include <iostream>

int main(void) {
    // Dynamically allocate an integer and store its address
    int *data = new int;
    // Assign value to allocated memory
    *data = 5;

    int *n_data = new int(10);

    std::cout << *data << std::endl;
    std::cout << *n_data << std::endl;

    int size = 5;
    int *array = new int[size];

    for (int i = 0; i < size; i++) {
        array[i] = (i + 1) * 10;
    }

    std::cout << "\nArray allocated with new[]..." << std::endl;
    for (int i = 0; i < size; i++) {
        std::cout << array[i] << " ";
    }
    std::cout << std::endl;

    delete data;
    delete n_data;
    delete[] array;

    // ERROR: dereferencing a dangling pointer will cause undefined behavior
    // std::cout << *data;
    // ERROR: deallocating the memory again will also lead to undefined behavior
    // delete data;

    return 0;
}
