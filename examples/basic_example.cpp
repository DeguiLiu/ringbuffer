// basic_example.cpp
// Demonstrates basic Push/Pop/Peek/At/Discard operations.

#include <cstdio>
#include <spsc/ringbuffer.hpp>

int main() {
    spsc::Ringbuffer<int, 16> rb;

    // Push elements
    for (int i = 0; i < 10; ++i) {
        if (rb.Push(i)) {
            std::printf("Pushed: %d\n", i);
        }
    }

    std::printf("Size: %zu, Available: %zu\n", static_cast<std::size_t>(rb.Size()),
                static_cast<std::size_t>(rb.Available()));
    std::printf("IsEmpty: %s, IsFull: %s\n", rb.IsEmpty() ? "yes" : "no",
                rb.IsFull() ? "yes" : "no");

    // Peek at front
    if (int* front = rb.Peek()) {
        std::printf("Peek: %d\n", *front);
    }

    // Access by index
    if (int* elem = rb.At(3)) {
        std::printf("At(3): %d\n", *elem);
    }

    // Discard first 2 elements
    std::size_t discarded = rb.Discard(2);
    std::printf("Discarded: %zu\n", discarded);

    // Pop remaining
    int val = 0;
    while (rb.Pop(val)) {
        std::printf("Popped: %d\n", val);
    }

    // Batch operations
    int src[] = {100, 200, 300, 400, 500};
    std::size_t pushed = rb.PushBatch(src, 5);
    std::printf("Batch pushed: %zu\n", pushed);

    int dst[5] = {};
    std::size_t popped = rb.PopBatch(dst, 5);
    std::printf("Batch popped: %zu ->", popped);
    for (std::size_t i = 0; i < popped; ++i) {
        std::printf(" %d", dst[i]);
    }
    std::printf("\n");

    // PushFromCallback
    int counter = 0;
    rb.PushFromCallback([&counter]() { return ++counter * 10; });
    rb.PushFromCallback([&counter]() { return ++counter * 10; });
    while (rb.Pop(val)) {
        std::printf("From callback: %d\n", val);
    }

    return 0;
}
