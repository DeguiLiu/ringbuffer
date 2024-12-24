#include <catch2/catch.hpp>
#include <spsc/ringbuffer.hpp>
#include <cstdint>

TEST_CASE("Boundary conditions", "[boundary]") {
    SECTION("Minimum buffer size (2)") {
        spsc::Ringbuffer<int, 2> rb;
        REQUIRE(rb.Push(1));
        REQUIRE(rb.Push(2));
        REQUIRE_FALSE(rb.Push(3));

        int val = 0;
        REQUIRE(rb.Pop(val));
        REQUIRE(val == 1);
        REQUIRE(rb.Pop(val));
        REQUIRE(val == 2);
        REQUIRE_FALSE(rb.Pop(val));
    }

    SECTION("Large buffer size (4096)") {
        spsc::Ringbuffer<int, 4096> rb;
        for (int i = 0; i < 4096; ++i) {
            REQUIRE(rb.Push(i));
        }
        REQUIRE(rb.IsFull());
        for (int i = 0; i < 4096; ++i) {
            int val = -1;
            REQUIRE(rb.Pop(val));
            REQUIRE(val == i);
        }
        REQUIRE(rb.IsEmpty());
    }

    SECTION("Custom index type uint16_t") {
        spsc::Ringbuffer<int, 64, false, uint16_t> rb;
        for (int i = 0; i < 64; ++i) rb.Push(i);
        REQUIRE(rb.IsFull());
        for (int i = 0; i < 64; ++i) {
            int val = -1;
            rb.Pop(val);
            REQUIRE(val == i);
        }
    }

    SECTION("Custom index type uint8_t with small buffer") {
        spsc::Ringbuffer<int, 4, false, uint8_t> rb;
        // Wrap index around uint8_t range (0-255)
        for (int round = 0; round < 100; ++round) {
            for (int i = 0; i < 4; ++i) {
                REQUIRE(rb.Push(round * 4 + i));
            }
            for (int i = 0; i < 4; ++i) {
                int val = -1;
                REQUIRE(rb.Pop(val));
                REQUIRE(val == round * 4 + i);
            }
        }
    }

    SECTION("FakeTSO mode") {
        spsc::Ringbuffer<int, 8, true> rb;
        for (int i = 0; i < 8; ++i) rb.Push(i);
        for (int i = 0; i < 8; ++i) {
            int val = -1;
            rb.Pop(val);
            REQUIRE(val == i);
        }
    }

    SECTION("Struct element type") {
        struct Packet {
            uint32_t id;
            uint16_t len;
            uint8_t data[6];
        };
        static_assert(std::is_trivially_copyable<Packet>::value, "");

        spsc::Ringbuffer<Packet, 4> rb;
        Packet p{};
        p.id = 0xDEADBEEF;
        p.len = 6;
        p.data[0] = 0xAA;
        REQUIRE(rb.Push(p));

        Packet out{};
        REQUIRE(rb.Pop(out));
        REQUIRE(out.id == 0xDEADBEEF);
        REQUIRE(out.len == 6);
        REQUIRE(out.data[0] == 0xAA);
    }

    SECTION("Index wraparound stress") {
        // Push/pop many times to force index overflow for uint8_t
        spsc::Ringbuffer<int, 4, false, uint8_t> rb;
        for (int i = 0; i < 1000; ++i) {
            REQUIRE(rb.Push(i));
            int val = -1;
            REQUIRE(rb.Pop(val));
            REQUIRE(val == i);
        }
    }
}
