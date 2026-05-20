#include <gtest/gtest.h>

#include "core/types.hpp"
#include "memory/flat_memory.hpp"

#include <cstdint>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::memory;

// -- Read/Write consistency --

TEST(FlatMemoryTest, WriteWordReadWord) {
    FlatMemory mem(1024);
    ASSERT_TRUE(mem.write(0, 0xDEADBEEF, Width::Word).has_value());
    auto result = mem.read(0, Width::Word);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 0xDEADBEEFu);
}

TEST(FlatMemoryTest, WriteHalfWordReadHalfWord) {
    FlatMemory mem(1024);
    ASSERT_TRUE(mem.write(10, 0x1234, Width::HalfWord).has_value());
    auto result = mem.read(10, Width::HalfWord);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 0x1234u);
}

TEST(FlatMemoryTest, WriteByteReadByte) {
    FlatMemory mem(1024);
    ASSERT_TRUE(mem.write(5, 0xAB, Width::Byte).has_value());
    auto result = mem.read(5, Width::Byte);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 0xABu);
}

// -- Little-endian verification --

TEST(FlatMemoryTest, LittleEndianByte) {
    FlatMemory mem(1024);
    ASSERT_TRUE(mem.write(0, 0x12345678, Width::Word).has_value());
    auto b0 = mem.read(0, Width::Byte);
    ASSERT_TRUE(b0.has_value());
    EXPECT_EQ(b0.value(), 0x78u);

    auto b1 = mem.read(1, Width::Byte);
    ASSERT_TRUE(b1.has_value());
    EXPECT_EQ(b1.value(), 0x56u);
}

TEST(FlatMemoryTest, LittleEndianHalfWord) {
    FlatMemory mem(1024);
    ASSERT_TRUE(mem.write(0, 0x12345678, Width::Word).has_value());
    auto h0 = mem.read(0, Width::HalfWord);
    ASSERT_TRUE(h0.has_value());
    EXPECT_EQ(h0.value(), 0x5678u);

    auto h1 = mem.read(2, Width::HalfWord);
    ASSERT_TRUE(h1.has_value());
    EXPECT_EQ(h1.value(), 0x1234u);
}

// -- Byte independence --

TEST(FlatMemoryTest, ByteIndependence) {
    FlatMemory mem(1024);
    ASSERT_TRUE(mem.write(4, 0xFF, Width::Byte).has_value());
    ASSERT_TRUE(mem.write(6, 0xFF, Width::Byte).has_value());

    auto b5 = mem.read(5, Width::Byte);
    ASSERT_TRUE(b5.has_value());
    EXPECT_EQ(b5.value(), 0x00u);

    auto b4 = mem.read(4, Width::Byte);
    ASSERT_TRUE(b4.has_value());
    EXPECT_NE(b4.value(), b5.value());
}

// -- Out-of-bounds --

TEST(FlatMemoryTest, OutOfBoundsRead) {
    FlatMemory mem(16);
    auto result = mem.read(14, Width::Word);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BusError::OutOfRange);
}

TEST(FlatMemoryTest, OutOfBoundsWrite) {
    FlatMemory mem(16);
    auto result = mem.write(15, 0xFF, Width::HalfWord);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BusError::OutOfRange);
}

TEST(FlatMemoryTest, ExactBoundRead) {
    FlatMemory mem(16);
    auto result = mem.read(15, Width::Byte);
    EXPECT_TRUE(result.has_value());
}

// -- Load + read back --

TEST(FlatMemoryTest, LoadAndReadback) {
    FlatMemory mem(256);
    std::vector<uint8_t> firmware = {0x78, 0x56, 0x34, 0x12};

    auto load_result = mem.load(0, firmware);
    ASSERT_TRUE(load_result.has_value());

    auto val = mem.read(0, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 0x12345678u);
}

TEST(FlatMemoryTest, LoadOutOfBounds) {
    FlatMemory mem(4);
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    auto result = mem.load(2, data);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BusError::OutOfRange);
}

// -- Name --

TEST(FlatMemoryTest, Name) {
    FlatMemory mem(16);
    EXPECT_EQ(mem.name(), "FlatMemory");
}

// -- Size --

TEST(FlatMemoryTest, Size) {
    FlatMemory mem(1024);
    EXPECT_EQ(mem.size(), 1024u);
}
