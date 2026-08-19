#include <iostream>
#include <memory>
#include <utility>

class Point {
 public:
  Point(int x, int y) : x_(x), y_(y) {}

 private:
  int x_ = 0;
  int y_ = 0;
};

void Test(std::shared_ptr<Point>& point) {
  std::cout << point.use_count() << std::endl;
}

int main() {
  auto first = std::make_shared<Point>(2, 3);
  std::cout << first.use_count() << std::endl;
  
  // Copied shared_ptr
  std::shared_ptr<Point> second = first;
  std::cout << first.use_count() << std::endl;
  
  // Pass by reference
  // shared_ptr is not copied and the number of owners remains unchanged.
  Test(first);
  std::cout << first.use_count() << std::endl;
  
  // The first and third shared_ptr objects share ownership of the same object.
  std::shared_ptr<Point> third = std::move(second);
  std::cout << first.use_count() << std::endl;

  return 0;
}
