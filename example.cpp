// example.cpp

#include <chrono>
#include <iostream>
#include <thread>

#include "ringbuffer.hpp"

int main() {
  // 使用整型作为缓冲区元素类型，缓冲区大小为 16
  utility::Ringbuffer<int, 16> ring_buffer;

  // 插入一些元素
  for (int i = 0; i < 10; ++i) {
    if (ring_buffer.Insert(i)) {
      std::cout << "Inserted: " << i << std::endl;
    } else {
      std::cout << "Failed to insert: " << i << " (Buffer Full)" << std::endl;
    }
  }

  // 检查缓冲区是否为空或已满
  std::cout << "Buffer is " << (ring_buffer.IsEmpty() ? "Empty" : "Not Empty") << std::endl;
  std::cout << "Buffer is " << (ring_buffer.IsFull() ? "Full" : "Not Full") << std::endl;

  // 读取并移除元素
  int value = 0;
  while (ring_buffer.Remove(value)) {
    std::cout << "Removed: " << value << std::endl;
  }

  // 使用 peek 方法查看下一个要读取的元素
  if (ring_buffer.Insert(100)) {
    std::cout << "Inserted: " << 100 << std::endl;
  }

  int* peek_value = ring_buffer.Peek();
  if (peek_value != nullptr) {
    std::cout << "Peeked Value: " << *peek_value << std::endl;
  } else {
    std::cout << "Buffer is empty, nothing to peek." << std::endl;
  }

  // 使用 at 方法访问特定位置的元素
  for (std::size_t i = 0; i < ring_buffer.ReadAvailable(); ++i) {
    int* at_value = ring_buffer.At(i);
    if (at_value != nullptr) {
      std::cout << "Element at index " << i << ": " << *at_value << std::endl;
    }
  }

  return 0;
}
