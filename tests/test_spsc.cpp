#include <catch2/catch.hpp>
#include <spsc/ringbuffer.hpp>
#include <atomic>
#include <thread>

TEST_CASE("SPSC concurrent correctness", "[spsc]") {
    constexpr std::size_t kBufSize = 1024;
    constexpr std::size_t kCount = 1000000;

    SECTION("Single-producer single-consumer FIFO order") {
        spsc::Ringbuffer<int, kBufSize> rb;
        std::atomic<bool> error{false};

        std::thread producer([&]() {
            for (std::size_t i = 0; i < kCount; ++i) {
                while (!rb.Push(static_cast<int>(i))) {
                    std::this_thread::yield();
                }
            }
        });

        std::thread consumer([&]() {
            for (std::size_t i = 0; i < kCount; ++i) {
                int val = -1;
                while (!rb.Pop(val)) {
                    std::this_thread::yield();
                }
                if (val != static_cast<int>(i)) {
                    error.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });

        producer.join();
        consumer.join();
        REQUIRE_FALSE(error.load());
        REQUIRE(rb.IsEmpty());
    }

    SECTION("Batch SPSC correctness") {
        spsc::Ringbuffer<int, kBufSize> rb;
        constexpr std::size_t kBatchCount = 100000;
        std::atomic<bool> error{false};

        std::thread producer([&]() {
            int buf[64];
            std::size_t sent = 0;
            while (sent < kBatchCount) {
                std::size_t batch = std::min<std::size_t>(64, kBatchCount - sent);
                for (std::size_t i = 0; i < batch; ++i) {
                    buf[i] = static_cast<int>(sent + i);
                }
                std::size_t pushed = rb.PushBatch(buf, batch);
                sent += pushed;
                if (pushed == 0) std::this_thread::yield();
            }
        });

        std::thread consumer([&]() {
            int buf[64];
            std::size_t received = 0;
            while (received < kBatchCount) {
                std::size_t batch = std::min<std::size_t>(64, kBatchCount - received);
                std::size_t popped = rb.PopBatch(buf, batch);
                for (std::size_t i = 0; i < popped; ++i) {
                    if (buf[i] != static_cast<int>(received + i)) {
                        error.store(true, std::memory_order_relaxed);
                        return;
                    }
                }
                received += popped;
                if (popped == 0) std::this_thread::yield();
            }
        });

        producer.join();
        consumer.join();
        REQUIRE_FALSE(error.load());
        REQUIRE(rb.IsEmpty());
    }
}
