#include <iostream>
#include <mutex>
#include <thread>

int count = 0;
std::mutex m;

void AddCount() {
  m.lock();
  count += 1;
  m.unlock();
}

int main() {
  std::thread t1(AddCount);
  std::thread t2(AddCount);

  t1.join();
  t2.join();

  std::cout << "count: " << count << '\n';
  return 0;
}
