#include <iostream>

void foo(int a) {
    std::cout << "Print from foo: " << a << std::endl;
}
namespace A {
    int data = 100;
    
    void foo(int a) {
        std::cout << "Print from A::foo: " << a << std::endl;
    }

    namespace B {
        int data = 200;
        
        void bar(int a) {
            std::cout << "Print from A::B::bar: " << a << std::endl;
        }
    }
}

namespace C {
    void foo(int a) {
        std::cout << "Print from C::foo: " << a << std::endl;
    }
}


int main() {
    foo(10);        // Implicit global function usage
    ::foo(10);      // Explicit global function usage
    
    A::foo(10);
    C::foo(10);

    A::B::bar(20);

    std::cout << std::endl;
    std::cout << "A::data: " << A::data << std::endl;
    std::cout << "A::B::data: " << A::B::data << std::endl;

    return 0;
}
 
 
