#include <catch2/catch.hpp>
#include <spsc/ringbuffer.hpp>

TEST_CASE("Push and Pop single elements", "[push_pop]") {
    spsc::Ringbuffer<int, 8> rb;

    SECTION("Push then Pop returns same value") {
        REQUIRE(rb.Push(42));
        int val = 0;
        REQUIRE(rb.Pop(val));
        REQUIRE(val == 42);
    }

    SECTION("Pop from empty buffer fails") {
        int val = 0;
        REQUIRE_FALSE(rb.Pop(val));
    }

    SECTION("Push to full buffer fails") {
        for (int i = 0; i < 8; ++i) {
            REQUIRE(rb.Push(i));
        }
        REQUIRE_FALSE(rb.Push(999));
    }

    SECTION("FIFO order preserved") {
        for (int i = 0; i < 8; ++i) {
            REQUIRE(rb.Push(i));
        }
        for (int i = 0; i < 8; ++i) {
            int val = -1;
            REQUIRE(rb.Pop(val));
            REQUIRE(val == i);
        }
    }

    SECTION("Push by move") {
        struct MoveTracker {
            int value;
            bool moved;
        };
        spsc::Ringbuffer<MoveTracker, 4> mrb;
        MoveTracker m{42, false};
        REQUIRE(mrb.Push(std::move(m)));
        MoveTracker out{};
        REQUIRE(mrb.Pop(out));
        REQUIRE(out.value == 42);
    }

    SECTION("Interleaved push and pop") {
        for (int round = 0; round < 100; ++round) {
            REQUIRE(rb.Push(round));
            int val = -1;
            REQUIRE(rb.Pop(val));
            REQUIRE(val == round);
        }
    }

    SECTION("Fill, drain, refill cycle") {
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (int i = 0; i < 8; ++i) {
                REQUIRE(rb.Push(cycle * 100 + i));
            }
            REQUIRE(rb.IsFull());
            for (int i = 0; i < 8; ++i) {
                int val = -1;
                REQUIRE(rb.Pop(val));
                REQUIRE(val == cycle * 100 + i);
            }
            REQUIRE(rb.IsEmpty());
        }
    }
}
