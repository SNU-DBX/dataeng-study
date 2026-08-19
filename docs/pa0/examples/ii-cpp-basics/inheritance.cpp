#include <iostream>

class Animal {
public:
    Animal() {
        std::cout << "Animal()" << std::endl;
    }

    Animal(int age) : age_(age) {
        std::cout << "Animal(int)" << std::endl;
    }

    virtual ~Animal() {
        std::cout << "~Animal()" << std::endl;
    }

    void SetAge(int age) {
        age_ = age;
    }

    int GetAge() const {
        return age_;
    }

    virtual void Speak() const = 0;

protected:
    void PrintAge() const {
        std::cout << "Animal::PrintAge()::age: " << age_ << std::endl;
    }

private:
    int age_ = 0;
};

class Dog : public Animal {
public:
    Dog() {
        std::cout << "Dog()" << std::endl;
    }

    Dog(int age) : Animal(age) {
        std::cout << "Dog(int)" << std::endl;
    }

    ~Dog() override {
        std::cout << "~Dog()" << std::endl;
    }

    void PrintInfo() const {
        std::cout << "Dog::PrintInfo() " << std::endl;
        // Derived classes can access protected members from the base class.
        PrintAge();
        // ERROR: even the derived class cannot access private members from the base class.
        // std::cout << "age: " << age_ << std::endl;
    }

    void Speak() const override {
        std::cout << "Dog::Speak()" << std::endl;
    }
};



int main() {
    Dog dog(3);

    std::cout << "\nAccessing public member inherited from base class...\n";
    dog.SetAge(5);
    std::cout << "age: " << dog.GetAge() << std::endl;

    std::cout << "\nAccessing protected member used inside derived class...\n";
    dog.PrintInfo();

    // ERROR: Cannot instantiate Animal pure virtual method is unimplemented. (L31)
    // Animal animal;

    std::cout << "\nVirtual function call...\n";
    Animal &animal_ref = dog;
    animal_ref.Speak();

    Animal *ptr = new Dog(7);
    ptr->Speak();
    delete ptr;

    return 0;
}
