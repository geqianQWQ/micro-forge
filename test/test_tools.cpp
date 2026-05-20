#include <gtest/gtest.h>

#include "memory/bus.hpp"
#include "memory/flat_memory.hpp"
#include "tools/memory_dump.hpp"
#include "tools/mmio_trace.hpp"

#include <string>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::tools;

namespace {

class ToolsTest : public ::testing::Test {
  protected:
    memory::Bus bus;
    memory::FlatMemory mem{1024};

    void SetUp() override {
        ASSERT_TRUE(bus.map(memory::region(0x0000, mem.size(), mem.GetWeak()))
                        .has_value());
    }
};

} // anonymous namespace

// ── memory_dump ──

TEST_F(ToolsTest, MemoryDumpBasic) {
    // Write known data
    ASSERT_TRUE(bus.write(0x0000, 0x41424344, Width::Word).has_value());
    ASSERT_TRUE(bus.write(0x0004, 0x45464748, Width::Word).has_value());

    std::vector<std::string> lines;
    memory_dump(bus, 0x0000, 8,
                [&](std::string_view line) { lines.emplace_back(line); });

    ASSERT_EQ(lines.size(), 1u);
    // Should contain hex bytes and ASCII "DCHGFEBA" (little-endian) or similar
    EXPECT_NE(lines[0].find("00000000:"), std::string::npos);
    // ASCII portion should contain ABCDEFGH (depends on endianness display)
    EXPECT_NE(lines[0].find("|"), std::string::npos);
}

TEST_F(ToolsTest, MemoryDumpMultipleLines) {
    // Write 32 bytes (2 lines of 16)
    for (addr_t i = 0; i < 32; i++) {
        ASSERT_TRUE(bus.write(i, i, Width::Byte).has_value());
    }

    std::vector<std::string> lines;
    memory_dump(bus, 0x0000, 32,
                [&](std::string_view line) { lines.emplace_back(line); });

    ASSERT_EQ(lines.size(), 2u);
    EXPECT_NE(lines[0].find("00000000:"), std::string::npos);
    EXPECT_NE(lines[1].find("00000010:"), std::string::npos);
}

TEST_F(ToolsTest, MemoryDumpUnmappedReadsFF) {
    // Dump from an unmapped region
    std::vector<std::string> lines;
    memory_dump(bus, 0xF000, 16,
                [&](std::string_view line) { lines.emplace_back(line); });

    ASSERT_EQ(lines.size(), 1u);
    // Unmapped reads should show 0xFF
    EXPECT_NE(lines[0].find("FF"), std::string::npos);
}

TEST_F(ToolsTest, MemoryDumpPartialLine) {
    ASSERT_TRUE(bus.write(0x0000, 0x42, Width::Byte).has_value());

    std::vector<std::string> lines;
    memory_dump(bus, 0x0000, 3,
                [&](std::string_view line) { lines.emplace_back(line); });

    ASSERT_EQ(lines.size(), 1u);
    // Short line should still have address and hex
    EXPECT_NE(lines[0].find("00000000:"), std::string::npos);
}

// ── mmio_trace ──

TEST_F(ToolsTest, MmioTraceCaptureRead) {
    ASSERT_TRUE(bus.write(0x0010, 0xDEADBEEF, Width::Word).has_value());

    std::vector<MmioAccess> accesses;
    enable_mmio_trace(bus, [&](const MmioAccess& a) { accesses.push_back(a); });

    auto val = bus.read(0x0010, Width::Word);
    ASSERT_TRUE(val.has_value());

    ASSERT_EQ(accesses.size(), 1u);
    EXPECT_FALSE(accesses[0].is_write);
    EXPECT_EQ(accesses[0].addr, 0x0010u);
    EXPECT_EQ(accesses[0].value, 0xDEADBEEFu);
    EXPECT_EQ(accesses[0].width, Width::Word);
    EXPECT_TRUE(accesses[0].ok);
    EXPECT_EQ(accesses[0].device, "FlatMemory");
}

TEST_F(ToolsTest, MmioTraceCaptureWrite) {
    std::vector<MmioAccess> accesses;
    enable_mmio_trace(bus, [&](const MmioAccess& a) { accesses.push_back(a); });

    ASSERT_TRUE(bus.write(0x0020, 0x12345678, Width::Word).has_value());

    ASSERT_EQ(accesses.size(), 1u);
    EXPECT_TRUE(accesses[0].is_write);
    EXPECT_EQ(accesses[0].addr, 0x0020u);
    EXPECT_EQ(accesses[0].value, 0x12345678u);
    EXPECT_EQ(accesses[0].width, Width::Word);
    EXPECT_TRUE(accesses[0].ok);
}

TEST_F(ToolsTest, MmioTraceCaptureFailure) {
    std::vector<MmioAccess> accesses;
    enable_mmio_trace(bus, [&](const MmioAccess& a) { accesses.push_back(a); });

    auto result = bus.read(0xF000, Width::Word);
    ASSERT_FALSE(result.has_value());

    ASSERT_EQ(accesses.size(), 1u);
    EXPECT_FALSE(accesses[0].is_write);
    EXPECT_FALSE(accesses[0].ok);
    EXPECT_EQ(accesses[0].error, BusError::Unmapped);
    EXPECT_EQ(accesses[0].device, "unmapped");
}

TEST_F(ToolsTest, MmioTraceMultipleAccesses) {
    std::vector<MmioAccess> accesses;
    enable_mmio_trace(bus, [&](const MmioAccess& a) { accesses.push_back(a); });

    ASSERT_TRUE(bus.write(0x0000, 0xAA, Width::Byte).has_value());
    ASSERT_TRUE(bus.write(0x0001, 0xBB, Width::HalfWord).has_value());
    (void)bus.read(0x0000, Width::Byte);

    ASSERT_EQ(accesses.size(), 3u);

    EXPECT_TRUE(accesses[0].is_write);
    EXPECT_EQ(accesses[0].width, Width::Byte);

    EXPECT_TRUE(accesses[1].is_write);
    EXPECT_EQ(accesses[1].width, Width::HalfWord);

    EXPECT_FALSE(accesses[2].is_write);
    EXPECT_EQ(accesses[2].width, Width::Byte);
}

TEST_F(ToolsTest, MmioTraceDisable) {
    std::vector<MmioAccess> accesses;
    enable_mmio_trace(bus, [&](const MmioAccess& a) { accesses.push_back(a); });

    ASSERT_TRUE(bus.write(0x0000, 0x01, Width::Byte).has_value());
    ASSERT_EQ(accesses.size(), 1u);

    disable_mmio_trace(bus);

    ASSERT_TRUE(bus.write(0x0000, 0x02, Width::Byte).has_value());
    // No new access captured after disable
    EXPECT_EQ(accesses.size(), 1u);
}

TEST_F(ToolsTest, MmioFormatAccess) {
    char buf[128];

    MmioAccess wr{true, 0x40010810,         0x20,   Width::Word,
                  true, BusError::Unmapped, "GPIOA"};
    auto sv = format_mmio_access(wr, buf, sizeof(buf));
    EXPECT_NE(sv.find("[WR]"), std::string::npos);
    EXPECT_NE(sv.find("0x40010810"), std::string::npos);
    EXPECT_NE(sv.find("(W)"), std::string::npos);
    EXPECT_NE(sv.find("OK"), std::string::npos);

    MmioAccess rd{false, 0x4001080C,         0xFF,   Width::Byte,
                  true,  BusError::Unmapped, "GPIOA"};
    sv = format_mmio_access(rd, buf, sizeof(buf));
    EXPECT_NE(sv.find("[RD]"), std::string::npos);
    EXPECT_NE(sv.find("(B)"), std::string::npos);

    MmioAccess hw{true, 0x40010804,         0x1234, Width::HalfWord,
                  true, BusError::Unmapped, "GPIOA"};
    sv = format_mmio_access(hw, buf, sizeof(buf));
    EXPECT_NE(sv.find("(H)"), std::string::npos);

    MmioAccess fail{true,  0x40010808,         0x1,    Width::Word,
                    false, BusError::ReadOnly, "GPIOA"};
    sv = format_mmio_access(fail, buf, sizeof(buf));
    EXPECT_NE(sv.find("ERR"), std::string::npos);
    EXPECT_NE(sv.find("GPIOA"), std::string::npos);
}
