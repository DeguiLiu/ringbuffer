#include <catch2/catch.hpp>
#include <spsc/ringbuffer.hpp>

TEST_CASE("Query API", "[query]") {
    spsc::Ringbuffer<int, 8> rb;

    SECTION("Empty buffer state") {
        REQUIRE(rb.IsEmpty());
        REQUIRE_FALSE(rb.IsFull());
        REQUIRE(rb.Size() == 0);
        REQUIRE(rb.Available() == 8);
        REQUIRE(rb.Capacity() == 8);
    }

    SECTION("Full buffer state") {
        for (int i = 0; i < 8; ++i) rb.Push(i);
        REQUIRE(rb.IsFull());
        REQUIRE_FALSE(rb.IsEmpty());
        REQUIRE(rb.Size() == 8);
        REQUIRE(rb.Available() == 0);
    }

    SECTION("Peek returns front element without removing") {
        rb.Push(10);
        rb.Push(20);
        REQUIRE(rb.Peek() != nullptr);
        REQUIRE(*rb.Peek() == 10);
        REQUIRE(rb.Size() == 2);  // not consumed
    }

    SECTION("Peek on empty returns nullptr") {
        REQUIRE(rb.Peek() == nullptr);
    }

    SECTION("At returns indexed element") {
        for (int i = 0; i < 5; ++i) rb.Push(i * 10);
        REQUIRE(rb.At(0) != nullptr);
        REQUIRE(*rb.At(0) == 0);
        REQUIRE(*rb.At(2) == 20);
        REQUIRE(*rb.At(4) == 40);
        REQUIRE(rb.At(5) == nullptr);  // out of range
    }

    SECTION("operator[] unchecked access") {
        for (int i = 0; i < 4; ++i) rb.Push(i + 100);
        REQUIRE(rb[0] == 100);
        REQUIRE(rb[3] == 103);
    }

    SECTION("Discard elements") {
        for (int i = 0; i < 6; ++i) rb.Push(i);
        REQUIRE(rb.Discard(3) == 3);
        REQUIRE(rb.Size() == 3);

        int val = -1;
        REQUIRE(rb.Pop(val));
        REQUIRE(val == 3);  // first 3 were discarded
    }

    SECTION("Discard more than available") {
        rb.Push(1);
        rb.Push(2);
        REQUIRE(rb.Discard(10) == 2);
        REQUIRE(rb.IsEmpty());
    }

    SECTION("Discard from empty buffer") {
        REQUIRE(rb.Discard() == 0);
    }

    SECTION("ProducerClear resets from producer side") {
        for (int i = 0; i < 4; ++i) rb.Push(i);
        rb.ProducerClear();
        REQUIRE(rb.IsEmpty());
    }

    SECTION("ConsumerClear resets from consumer side") {
        for (int i = 0; i < 4; ++i) rb.Push(i);
        rb.ConsumerClear();
        REQUIRE(rb.IsEmpty());
    }
}
