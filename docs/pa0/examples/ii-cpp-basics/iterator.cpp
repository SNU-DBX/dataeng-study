#include <iostream>
#include <vector>

int main() {
    std::vector<int> numbers = {10, 20, 30, 40};

    // Iterator points to an element inside a container.
    std::vector<int>::iterator iter = numbers.begin();

    // Dereference (*) accesses the current element.
    std::cout << "*iter: " << *iter << std::endl;

    // Increment (++) moves the iterator to the next element.
    ++iter;
    std::cout << "*iter after ++iter: " << *iter << std::endl;

    std::cout << "\nPrinting vector with iterator...\n";
    for (std::vector<int>::iterator it = numbers.begin(); it != numbers.end(); ++it) {
        std::cout << *it << " ";
    }
    std::cout << std::endl;

    int array[4] = {1, 2, 3, 4};
    int *ptr = array;

    // A pointer can also act like an iterator for an array.
    std::cout << "\nPointer as iterator...\n";
    std::cout << "*ptr: " << *ptr << std::endl;
    ++ptr;
    std::cout << "*ptr after ++ptr: " << *ptr << std::endl;

    return 0;
}
