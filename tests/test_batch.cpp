#include <catch2/catch.hpp>
#include <spsc/ringbuffer.hpp>
#include <vector>

TEST_CASE("PushBatch and PopBatch", "[batch]") {
    spsc::Ringbuffer<int, 16> rb;

    SECTION("Batch push then batch pop") {
        int src[8] = {10, 20, 30, 40, 50, 60, 70, 80};
        REQUIRE(rb.PushBatch(src, 8) == 8);
        REQUIRE(rb.Size() == 8);

        int dst[8] = {};
        REQUIRE(rb.PopBatch(dst, 8) == 8);
        for (int i = 0; i < 8; ++i) {
            REQUIRE(dst[i] == src[i]);
        }
        REQUIRE(rb.IsEmpty());
    }

    SECTION("Batch push exceeding capacity") {
        int src[20];
        for (int i = 0; i < 20; ++i) src[i] = i;
        std::size_t pushed = rb.PushBatch(src, 20);
        REQUIRE(pushed == 16);
        REQUIRE(rb.IsFull());
    }

    SECTION("Batch pop from partially filled buffer") {
        int src[4] = {1, 2, 3, 4};
        rb.PushBatch(src, 4);

        int dst[8] = {};
        std::size_t popped = rb.PopBatch(dst, 8);
        REQUIRE(popped == 4);
        for (int i = 0; i < 4; ++i) {
            REQUIRE(dst[i] == i + 1);
        }
    }

    SECTION("Batch wraps around ring boundary") {
        // Fill 12, pop 12, then push 10 — forces wrap-around
        int fill[12];
        for (int i = 0; i < 12; ++i) fill[i] = i;
        rb.PushBatch(fill, 12);

        int drain[12];
        rb.PopBatch(drain, 12);
        REQUIRE(rb.IsEmpty());

        // Now head is at index 12, push 10 elements wraps around index 16 -> 0
        int src[10];
        for (int i = 0; i < 10; ++i) src[i] = 100 + i;
        REQUIRE(rb.PushBatch(src, 10) == 10);

        int dst[10] = {};
        REQUIRE(rb.PopBatch(dst, 10) == 10);
        for (int i = 0; i < 10; ++i) {
            REQUIRE(dst[i] == 100 + i);
        }
    }

    SECTION("Batch push with callback") {
        int call_count = 0;
        auto cb = [&call_count]() { ++call_count; };

        int src[16];
        for (int i = 0; i < 16; ++i) src[i] = i;
        rb.PushBatch(src, 16, cb);
        REQUIRE(call_count >= 1);
        REQUIRE(rb.IsFull());
    }

    SECTION("Batch pop with callback") {
        int src[8];
        for (int i = 0; i < 8; ++i) src[i] = i;
        rb.PushBatch(src, 8);

        int call_count = 0;
        auto cb = [&call_count]() { ++call_count; };
        int dst[8] = {};
        rb.PopBatch(dst, 8, cb);
        REQUIRE(call_count >= 1);
    }
}
