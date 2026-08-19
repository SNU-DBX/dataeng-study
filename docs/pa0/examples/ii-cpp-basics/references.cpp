#include <iostream>

void Add(int &num) {
    num = num + 1;
}

int main() {
    int a = 10;
    int c = 12;

    // B is another name for a
    int &b = a;
    
    std::cout << "Initial values...\n";
    std::cout << "a: " << a << " (&a: " << &a << ")" << std::endl;    
    std::cout << "b: " << b << " (&b: " << &b << ")" << std::endl;
    std::cout << "c: " << c << " (&c: " << &c << ")" << std::endl;

    // This copies c's value into a; does not make b refer to c.
    b = c;

    std::cout << "\nAfter b = c...\n";
    std::cout << "a: " << a << " (&a: " << &a << ")" << std::endl;    
    std::cout << "b: " << b << " (&b: " << &b << ")" << std::endl;
    std::cout << "c: " << c << " (&c: " << &c << ")" << std::endl;

    Add(a);

    std::cout << "\nAfter Add(a)...\n";
    std::cout << "a: " << a << " (&a: " << &a << ")" << std::endl;    
    std::cout << "b: " << b << " (&b: " << &b << ")" << std::endl;
    std::cout << "c: " << c << " (&c: " << &c << ")" << std::endl;

    return 0;
}
