#include <catch2/catch.hpp>
#include <spsc/ringbuffer.hpp>
#include <functional>

TEST_CASE("PushFromCallback", "[callback]") {
    spsc::Ringbuffer<int, 4> rb;

    SECTION("Lambda callback") {
        int counter = 0;
        auto gen = [&counter]() -> int { return ++counter; };

        REQUIRE(rb.PushFromCallback(gen));
        REQUIRE(rb.PushFromCallback(gen));
        REQUIRE(rb.Size() == 2);

        int val = 0;
        REQUIRE(rb.Pop(val));
        REQUIRE(val == 1);
        REQUIRE(rb.Pop(val));
        REQUIRE(val == 2);
    }

    SECTION("std::function callback") {
        std::function<int()> gen = []() -> int { return 77; };
        REQUIRE(rb.PushFromCallback(gen));

        int val = 0;
        REQUIRE(rb.Pop(val));
        REQUIRE(val == 77);
    }

    SECTION("Function pointer callback") {
        struct Helper {
            static int generate() { return 55; }
        };
        REQUIRE(rb.PushFromCallback(Helper::generate));

        int val = 0;
        REQUIRE(rb.Pop(val));
        REQUIRE(val == 55);
    }

    SECTION("Callback not called when buffer full") {
        for (int i = 0; i < 4; ++i) rb.Push(i);
        bool called = false;
        auto gen = [&called]() -> int { called = true; return 999; };
        REQUIRE_FALSE(rb.PushFromCallback(gen));
        REQUIRE_FALSE(called);
    }
}
