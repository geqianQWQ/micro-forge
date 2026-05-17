#include <gtest/gtest.h>

#include "core/types.hpp"
#include "memory/bus.hpp"
#include "memory/flat_memory.hpp"
#include "memory/region.hpp"

using namespace micro_forge;
using namespace micro_forge::memory;

// -- Single region routing --

TEST(BusTest, SingleRegionReadWrite) {
    Bus bus;
    FlatMemory mem(256);
    ASSERT_TRUE(bus.map(region(0x1000, 256, mem.GetWeak())).has_value());

    auto wr = bus.write(0x1010, 0xCAFEBABE, Width::Word);
    ASSERT_TRUE(wr.has_value());

    auto rd = bus.read(0x1010, Width::Word);
    ASSERT_TRUE(rd.has_value());
    EXPECT_EQ(rd.value(), 0xCAFEBABEu);
}

// -- Dual region routing --

TEST(BusTest, DualRegionRouting) {
    Bus bus;
    FlatMemory mem_a(64);
    FlatMemory mem_b(64);

    ASSERT_TRUE(bus.map(region(0x0000, 64, mem_a.GetWeak())).has_value());
    ASSERT_TRUE(bus.map(region(0x1000, 64, mem_b.GetWeak())).has_value());

    ASSERT_TRUE(bus.write(0x0000, 0xAAAA, Width::HalfWord).has_value());
    ASSERT_TRUE(bus.write(0x1000, 0xBBBB, Width::HalfWord).has_value());

    auto a = bus.read(0x0000, Width::HalfWord);
    auto b = bus.read(0x1000, Width::HalfWord);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(a.value(), 0xAAAAu);
    EXPECT_EQ(b.value(), 0xBBBBu);

    // Verify isolation: mem_a should not see mem_b's data
    auto direct = mem_a.read(0, Width::HalfWord);
    ASSERT_TRUE(direct.has_value());
    EXPECT_EQ(direct.value(), 0xAAAAu);
}

// -- Overlap rejection --

TEST(BusTest, OverlapRejected) {
    Bus bus;
    FlatMemory mem_a(256);
    FlatMemory mem_b(256);

    ASSERT_TRUE(bus.map(region(0x0000, 256, mem_a.GetWeak())).has_value());

    auto result = bus.map(region(0x0080, 256, mem_b.GetWeak()));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BusError::Fault);
}

TEST(BusTest, AdjacentRegionsAllowed) {
    Bus bus;
    FlatMemory mem_a(128);
    FlatMemory mem_b(128);

    ASSERT_TRUE(bus.map(region(0x0000, 128, mem_a.GetWeak())).has_value());
    ASSERT_TRUE(bus.map(region(0x0080, 128, mem_b.GetWeak())).has_value());
}

// -- Unmapped address --

TEST(BusTest, UnmappedRead) {
    Bus bus;
    auto result = bus.read(0xDEAD, Width::Word);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BusError::Unmapped);
}

TEST(BusTest, UnmappedWrite) {
    Bus bus;
    auto result = bus.write(0xDEAD, 0, Width::Word);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BusError::Unmapped);
}

// -- Boundary tests --

TEST(BusTest, BoundaryFirstByte) {
    Bus bus;
    FlatMemory mem(16);
    ASSERT_TRUE(bus.map(region(0x100, 16, mem.GetWeak())).has_value());

    auto rd = bus.read(0x100, Width::Byte);
    ASSERT_TRUE(rd.has_value());
}

TEST(BusTest, BoundaryLastByte) {
    Bus bus;
    FlatMemory mem(16);
    ASSERT_TRUE(bus.map(region(0x100, 16, mem.GetWeak())).has_value());

    auto rd = bus.read(0x10F, Width::Byte);
    ASSERT_TRUE(rd.has_value());
}

TEST(BusTest, BoundaryOnePastEnd) {
    Bus bus;
    FlatMemory mem(16);
    ASSERT_TRUE(bus.map(region(0x100, 16, mem.GetWeak())).has_value());

    auto rd = bus.read(0x110, Width::Byte);
    ASSERT_FALSE(rd.has_value());
    EXPECT_EQ(rd.error(), BusError::Unmapped);
}

// -- Invalid WeakPtr rejection --

TEST(BusTest, InvalidWeakPtrRejected) {
    Bus bus;
    WeakPtr<periph::Device> null_ptr;
    auto result = bus.map(Region{0x0000, 0x0100, null_ptr});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BusError::Fault);
}
