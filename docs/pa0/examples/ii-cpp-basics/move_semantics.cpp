#include <iostream>
#include <utility>

// Explicit copy and move behavior example.
class UniqueNumber {
public:
    UniqueNumber(int value) : value_(new int(value)) {
        std::cout << "ctor: " << *value_ << std::endl;
    }

    // Copy operations are deleted to prevent two objects from owning one address.
    UniqueNumber(const UniqueNumber &) = delete;
    UniqueNumber &operator=(const UniqueNumber &) = delete;

    // Move constructor transfers ownership from other to this new object.
    UniqueNumber(UniqueNumber &&other) : value_(other.value_) {
        other.value_ = nullptr;
        std::cout << "move ctor" << std::endl;
    }

    // Move assignment releases current memory and then takes ownership from other.
    UniqueNumber &operator=(UniqueNumber &&other) {
        std::cout << "move assign" << std::endl;

        if (this != &other) {
            delete value_;
            value_ = other.value_;
            other.value_ = nullptr;
        }

        return *this;
    }

    ~UniqueNumber() {
        delete value_;
    }

    int GetValue() const {
        if (value_ == nullptr) {
            return 0;
        }

        return *value_;
    }

    void Print(const char *name) const {
        std::cout << name << ": ptr=" << value_;

        if (value_ != nullptr) {
            std::cout << ", value=" << *value_;
        }

        std::cout << std::endl;
    }

private:
    int *value_ = nullptr;
};

int main(void) {
    std::cout << "\nDeleted copy and move semantics...\n";
    UniqueNumber u1(10);
    u1.Print("u1 before: ");

    // Move construction from an rvalue.
    // Allows ownership to move instead of copying the object.
    UniqueNumber u2(std::move(u1));
    u1.Print("u1 after");
    u2.Print("u2 after");

    UniqueNumber u3(20);
    u3.Print("u3 before");

    // Move assignment from an rvalue.
    u3 = std::move(u2);
    u2.Print("u2 after");
    u3.Print("u3 after");
    std::cout << "u3: " << u3.GetValue() << std::endl;

    return 0;
}
