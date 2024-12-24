// spsc_example.cpp
// Demonstrates single-producer single-consumer usage with threads.

#include <chrono>
#include <cstdio>
#include <thread>

#include <spsc/ringbuffer.hpp>

int main() {
    constexpr std::size_t kBufSize = 1024;
    constexpr std::size_t kCount = 1000000;

    spsc::Ringbuffer<int, kBufSize> rb;

    auto t0 = std::chrono::high_resolution_clock::now();

    // Producer thread
    std::thread producer([&]() {
        for (std::size_t i = 0; i < kCount; ++i) {
            while (!rb.Push(static_cast<int>(i))) {
                std::this_thread::yield();
            }
        }
    });

    // Consumer thread
    std::thread consumer([&]() {
        int val = 0;
        for (std::size_t i = 0; i < kCount; ++i) {
            while (!rb.Pop(val)) {
                std::this_thread::yield();
            }
            // Verify FIFO order
            if (val != static_cast<int>(i)) {
                std::printf("ERROR: expected %d, got %d\n", static_cast<int>(i), val);
                return;
            }
        }
    });

    producer.join();
    consumer.join();

    auto t1 = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();

    std::printf("Transferred %zu elements in %.4f s\n", kCount, sec);
    std::printf("Throughput: %.2f M ops/s\n", kCount / sec / 1e6);

    return 0;
}
