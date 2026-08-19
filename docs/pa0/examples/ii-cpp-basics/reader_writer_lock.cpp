#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

int count = 0;
// Reader-writer lock: shared for readers, exclusive for writers.
std::shared_mutex count_mutex;
// Print lock for std::cout, so messages from different threads do not interleave.
std::mutex output_mutex;

void read_value(int reader_id) {
  int snapshot = 0;
  {
    std::shared_lock<std::shared_mutex> read_lock(count_mutex);
    snapshot = count;
  }

  std::lock_guard<std::mutex> output_lock(output_mutex);
  std::cout << "reader: " << reader_id << "(" << snapshot << ")" << '\n';
}

void write_value(int amount) {
  std::unique_lock<std::shared_mutex> write_lock(count_mutex);
  count += amount;
}

int main() {
  std::vector<std::thread> threads;

  std::cout << "Reader output order and observed values may vary...\n";
  threads.emplace_back(read_value, 1);
  threads.emplace_back(read_value, 2);
  threads.emplace_back(write_value, 3);
  threads.emplace_back(read_value, 3);
  threads.emplace_back(write_value, 7);

  for (std::thread &thread : threads) {
    thread.join();
  }

  std::cout << "\nCount: " << count << '\n';
  return 0;
}
