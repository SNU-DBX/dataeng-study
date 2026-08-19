#include <iostream>
#include <memory>
#include <utility>


class Point {
public:
  Point(int x, int y) : x_(x), y_(y) {}

  int GetX() {
    return x_;
  }

  int GetY() {
    return y_;
  }

 private:
  int x_ = 0;
  int y_ = 0;
};

int main() {
  auto source = std::make_unique<Point>(10, 20);

  auto destination = std::move(source);

  std::cout << "After move...\n";
  std::cout << "source: " << (source ? "not empty" : "empty") << std::endl;
  std::cout << "destination: Point(" << destination->GetX() << ", " << destination->GetY() << ")" << std::endl;

  return 0;
}
