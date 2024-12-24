// test.cpp

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "ringbuffer.hpp"

// 常量定义
constexpr std::size_t BUFFER_SIZE = 1024;
constexpr std::size_t NUM_OPERATIONS = 1000000;

// 测试功能：插入和移除元素
void FunctionalTest() {
  std::cout << "Starting Functional Test..." << std::endl;

  utility::Ringbuffer<int, BUFFER_SIZE> ring_buffer;

  // 插入元素
  for (std::size_t i = 0; i < BUFFER_SIZE; ++i) {
    bool inserted = ring_buffer.Insert(static_cast<int>(i));
    assert(inserted);
  }

  // 尝试插入到已满的缓冲区
  bool insert_fail = ring_buffer.Insert(9999);
  assert(!insert_fail);
  std::cout << "Buffer correctly identified as full." << std::endl;

  // 移除元素并验证
  for (std::size_t i = 0; i < BUFFER_SIZE; ++i) {
    int value = -1;
    bool removed = ring_buffer.Remove(value);
    assert(removed);
    assert(value == static_cast<int>(i));
  }

  // 尝试移除从空缓冲区
  int dummy = 0;
  bool remove_fail = ring_buffer.Remove(dummy);
  assert(!remove_fail);
  std::cout << "Buffer correctly identified as empty." << std::endl;

  // 测试回调插入
  auto callback = []() -> int { return 123; };
  bool callback_inserted = ring_buffer.InsertFromCallbackWhenAvailable(callback);
  assert(callback_inserted);
  int peek_value = 0;
  bool peek_removed = ring_buffer.Remove(peek_value);
  assert(peek_removed);
  assert(peek_value == 123);

  std::cout << "Functional Test Passed." << std::endl;
}

// 性能测试：单生产者和单消费者
void PerformanceTest() {
  std::cout << "Starting Performance Test..." << std::endl;

  utility::Ringbuffer<int, BUFFER_SIZE> ring_buffer;

  // 生产者线程：插入 NUM_OPERATIONS 个元素
  auto producer = [&ring_buffer]() {
    for (std::size_t i = 0; i < NUM_OPERATIONS; ++i) {
      while (!ring_buffer.Insert(static_cast<int>(i))) {
        // 如果缓冲区满，等待
        std::this_thread::yield();
      }
    }
  };

  // 消费者线程：移除 NUM_OPERATIONS 个元素
  auto consumer = [&ring_buffer]() {
    int value = 0;
    for (std::size_t i = 0; i < NUM_OPERATIONS; ++i) {
      while (!ring_buffer.Remove(value)) {
        // 如果缓冲区空，等待
        std::this_thread::yield();
      }
      // 可选：验证值的正确性
      // assert(value == static_cast<int>(i));
    }
  };

  auto start_time = std::chrono::high_resolution_clock::now();

  // 启动生产者和消费者线程
  std::thread producer_thread(producer);
  std::thread consumer_thread(consumer);

  // 等待线程完成
  producer_thread.join();
  consumer_thread.join();

  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end_time - start_time;

  std::cout << "Inserted and Removed " << NUM_OPERATIONS << " elements in " << duration.count() << " seconds."
            << std::endl;
  std::cout << "Throughput: " << (NUM_OPERATIONS / duration.count()) << " operations per second." << std::endl;
}

// 压测：高频率插入和移除
void HighFrequencyTest() {
  std::cout << "Starting High Frequency Test..." << std::endl;

  utility::Ringbuffer<int, BUFFER_SIZE> ring_buffer;
  constexpr std::size_t high_freq_operations = 10000000;

  // 生产者线程
  auto producer = [&ring_buffer, high_freq_operations]() {
    for (std::size_t i = 0; i < high_freq_operations; ++i) {
      while (!ring_buffer.Insert(static_cast<int>(i))) {
        std::this_thread::yield();
      }
    }
  };

  // 消费者线程
  auto consumer = [&ring_buffer, high_freq_operations]() {
    int value = 0;
    for (std::size_t i = 0; i < high_freq_operations; ++i) {
      while (!ring_buffer.Remove(value)) {
        std::this_thread::yield();
      }
    }
  };

  auto start_time = std::chrono::high_resolution_clock::now();

  // 启动生产者和消费者线程
  std::thread producer_thread(producer);
  std::thread consumer_thread(consumer);

  // 等待线程完成
  producer_thread.join();
  consumer_thread.join();

  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end_time - start_time;

  std::cout << "High Frequency Test: Inserted and Removed " << high_freq_operations << " elements in "
            << duration.count() << " seconds." << std::endl;
  std::cout << "Throughput: " << (high_freq_operations / duration.count()) << " operations per second." << std::endl;
}

int main() {
  // 功能压测
  FunctionalTest();

  // 性能压测
  PerformanceTest();

  // 高频率压测
  HighFrequencyTest();

  return 0;
}
