#pragma once

#include <cstdint>
#include <expected>

namespace micro_forge {

using half_word_t = uint16_t;
using byte_t = uint8_t;

enum class BusError {
    Unmapped,  // No peripheral/memory at this address
    Unaligned, // Unsupported unaligned access
    Fault,     // Generic bus fault
    ReadOnly,  // Write to read-only region
};

enum class Width : uint32_t { Byte = 1, HalfWord = 2, Word = 4 };

template <typename T> using Expected = std::expected<T, BusError>;

} // namespace micro_forge
