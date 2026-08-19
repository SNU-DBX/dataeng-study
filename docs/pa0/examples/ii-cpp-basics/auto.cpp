#include <iostream>
#include <vector>

class ExampleClass {
public:
    ExampleClass() {}
    ExampleClass(int param) : data(param) {}

    void Print() const {
        std::cout << data << std::endl;
    }

private:
    int data = 0;
};

int main() {
    // Using auto to declare basic variables
    auto a = 1;
    auto b = 3.2;

    auto c = std::string("Hello");

    // Using auto to declare class object
    ExampleClass ec;
    auto obj1 = ExampleClass(10);
    std::cout << "\nPrinting class member with auto...\n";
    obj1.Print();

    std::vector<int> vec = {10, 20, 30, 40};

    std::cout << "\nPrinting elements in vector with iterator...\n";
    for (std::vector<int>::iterator it = vec.begin(); it != vec.end(); ++it) {
        std:: cout << *it << " "; 
    }
    std::cout << std::endl;

    // Using auto for std::vector iteration
    std::cout << "\nPrinting elements in vector with auto...\n";
    for (const auto& elem : vec) {
      std::cout << elem << " ";
    }
    std::cout << std::endl;

    return 0;
}
