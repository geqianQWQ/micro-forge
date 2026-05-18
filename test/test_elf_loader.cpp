#include <gtest/gtest.h>

#include "loader/elf_loader.hpp"
#include "memory/bus.hpp"
#include "memory/flat_memory.hpp"

#include <cstring>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::loader;
using namespace micro_forge::memory;

namespace {

void write_u16(std::vector<uint8_t>& buf, size_t off, uint16_t val) {
    std::memcpy(buf.data() + off, &val, 2);
}
void write_u32(std::vector<uint8_t>& buf, size_t off, uint32_t val) {
    std::memcpy(buf.data() + off, &val, 4);
}

std::vector<uint8_t> build_minimal_elf(uint32_t vaddr, uint32_t entry,
                                        std::span<const uint8_t> payload) {
    size_t ph_off = 52;
    size_t data_off = ph_off + 32;
    size_t total = data_off + payload.size();

    std::vector<uint8_t> buf(total, 0);

    // e_ident
    buf[0] = 0x7F; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 1;  // ELFCLASS32
    buf[5] = 1;  // ELFDATA2LSB

    write_u16(buf, 16, 2);   // e_type = ET_EXEC
    write_u16(buf, 18, 40);  // e_machine = EM_ARM
    write_u32(buf, 20, 1);   // e_version
    write_u32(buf, 24, entry);
    write_u32(buf, 28, static_cast<uint32_t>(ph_off));
    write_u32(buf, 32, 0);   // e_shoff
    write_u32(buf, 36, 0);   // e_flags
    write_u16(buf, 40, 52);  // e_ehsize
    write_u16(buf, 42, 32);  // e_phentsize
    write_u16(buf, 44, 1);   // e_phnum

    // Program header: PT_LOAD
    write_u32(buf, ph_off + 0, 1);  // p_type
    write_u32(buf, ph_off + 4, static_cast<uint32_t>(data_off));
    write_u32(buf, ph_off + 8, vaddr);
    write_u32(buf, ph_off + 12, vaddr);
    write_u32(buf, ph_off + 16, static_cast<uint32_t>(payload.size()));
    write_u32(buf, ph_off + 20, static_cast<uint32_t>(payload.size()));
    write_u32(buf, ph_off + 24, 5);
    write_u32(buf, ph_off + 28, 4);

    std::memcpy(buf.data() + data_off, payload.data(), payload.size());
    return buf;
}

} // anonymous namespace

TEST(ElfLoaderTest, MinimalValidElf) {
    Bus bus;
    FlatMemory flash(4 * 1024);
    ASSERT_TRUE(bus.map(region(0x08000000, 4 * 1024, flash.GetWeak())).has_value());

    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78};
    auto elf = build_minimal_elf(0x08000000, 0x08000001, data);

    auto result = load_elf(bus, elf);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->entry_point, 0x08000001u);

    auto r0 = bus.read(0x08000000, Width::Word);
    ASSERT_TRUE(r0.has_value());
    EXPECT_EQ(*r0, 0xEFBEADDEu);
}

TEST(ElfLoaderTest, InvalidMagic) {
    Bus bus;
    std::vector<uint8_t> bad(52, 0);
    auto result = load_elf(bus, bad);
    EXPECT_FALSE(result.has_value());
}

TEST(ElfLoaderTest, Not32Bit) {
    Bus bus;
    std::vector<uint8_t> elf = build_minimal_elf(0, 0, {});
    elf[4] = 2;
    auto result = load_elf(bus, elf);
    EXPECT_FALSE(result.has_value());
}

TEST(ElfLoaderTest, NotArm) {
    Bus bus;
    std::vector<uint8_t> elf = build_minimal_elf(0, 0, {});
    uint16_t not_arm = 99;
    std::memcpy(elf.data() + 18, &not_arm, 2);
    auto result = load_elf(bus, elf);
    EXPECT_FALSE(result.has_value());
}

TEST(ElfLoaderTest, BssZeroFill) {
    Bus bus;
    FlatMemory mem(4 * 1024);
    ASSERT_TRUE(bus.map(region(0x08000000, 4 * 1024, mem.GetWeak())).has_value());

    uint8_t data[] = {0xAA, 0xBB, 0xCC, 0xDD};
    auto elf = build_minimal_elf(0x08000000, 0x08000001, data);

    // Patch memsz to be larger
    size_t ph_off = 52;
    uint32_t memsz = 16;
    std::memcpy(elf.data() + ph_off + 20, &memsz, 4);

    auto result = load_elf(bus, elf);
    ASSERT_TRUE(result.has_value());

    auto r0 = bus.read(0x08000000, Width::Word);
    ASSERT_TRUE(r0.has_value());
    EXPECT_EQ(*r0, 0xDDCCBBAAu);

    auto r1 = bus.read(0x08000004, Width::Word);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(*r1, 0u);
}
