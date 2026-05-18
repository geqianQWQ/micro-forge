#pragma once
#include "autogen/arch_details.hpp"
#include "memory/bus.hpp"
#include <span>

namespace micro_forge::loader {
struct ElfLoadResult {
    // entry for CPU to start execute
    addr_t entry_point;
};

struct ElfPack {
    memory::Bus& bus;              // bus to load in
    addr_t base_addr;              // basic address
    std::span<const uint8_t> data; // binary data
};

std::expected<ElfLoadResult, std::string>
load_elf(memory::Bus& bus, std::span<const uint8_t> elf_data);

} // namespace micro_forge::loader
