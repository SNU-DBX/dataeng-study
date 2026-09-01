#include <iostream>
#include <mutex>
#include <thread>
#include <condition_variable>

int count = 0;
std::mutex count_mutex;
std::condition_variable cv;

void waiter() {
  std::unique_lock<std::mutex> lock(count_mutex);

  // Wait until the predicate is true
  cv.wait(lock, [] { return count == 2; });
  std::cout << "waiter: count reached " << count << '\n';
}

void increment() {
  bool should_notify = false;

  {
    std::lock_guard<std::mutex> lock(count_mutex);
    count += 1;
    should_notify = (count == 2);
  }

  if (should_notify) {
    cv.notify_one();
  }
}
int main() {
  std::thread t1(waiter);
  std::thread t2(increment);
  std::thread t3(increment);

  t2.join();
  t3.join();
  t1.join();

  std::cout << "main: final count = " << count << '\n';
  return 0;
}
