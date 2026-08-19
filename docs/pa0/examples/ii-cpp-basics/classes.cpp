#include <iostream>

// Struct members are public by default.
struct Point {
    int x = 0;
    int y = 0;

    void Print() const {
        std::cout << "Point(" << x << ", " << y << ")" << std::endl;
    }
};

// Class members are private by default, so public methods are used for access.
class Number {
public:
    // Delegating constructor calls another constructor in the same class.
    Number() : Number(0) {
        std::cout << "Number()" << std::endl;
    }

    // Member initializer list initializes value_ before the constructor body runs.
    Number(int value) : value_(value) {
        std::cout << "Number(int)" << std::endl;
    }

    // Copy constructor creates a new object from an existing object.
    Number(const Number &other) : value_(other.value_) {
        std::cout << "Number(const Number &)" << std::endl;
    }

    // Destructor is called automatically when the object lifetime ends.
    ~Number() {
        std::cout << "~Number(): " << value_ << std::endl;
    }

    void SetValue(int value) {
        this->value_ = value;
    }

    int GetValue() const {
        return value_;
    }

private:
    int value_ = 0;
};

int main() {
    std::cout << "\nClass and struct...\n";

    Point point;
    point.x = 10;
    point.y = 20;
    point.Print();

    Number number;
    // Private member is changed through a public member function.
    number.SetValue(30);
    std::cout << "number: " << number.GetValue() << std::endl;

    Number n1(1);    
    Number n2(n1);       // Calls copy constructor

    std::cout << "n1: " << n1.GetValue() << std::endl;
    std::cout << "n2: " << n2.GetValue() << std::endl;

    return 0;
}
