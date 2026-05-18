#pragma once
#include "autogen/arch_details.hpp"
#include "memory/bus.hpp"
#include <expected>
#include <span>

namespace micro_forge::loader {
struct BinaryLoadResult {
    // entry for CPU to start execute
    addr_t entry_point;
};

struct BinaryPack {
    memory::Bus& bus;              // bus to load in
    addr_t base_addr;              // basic address
    std::span<const uint8_t> data; // binary data
};

enum class LoadError { General, MemoryUnmapped };
std::expected<BinaryLoadResult, LoadError> load_binary(const BinaryPack& pack);

} // namespace micro_forge::loader
