#pragma once

#include <cstdint>
#include <expected>

namespace micro_forge {

using half_word_t = uint16_t;
using byte_t = uint8_t;

enum class BusError {
    Unmapped,        // No peripheral/memory at this address
    Unaligned,       // Unsupported unaligned access
    ReadOnly,        // Write to read-only region
    InvalidDevice,   // Device WeakPtr expired or null
    RegionOverlap,   // map() region overlap
    OutOfRange,      // Offset exceeds device/memory bounds
    PeripheralFault, // Peripheral register offset unimplemented
};

enum class Width : uint32_t { Byte = 1, HalfWord = 2, Word = 4 };

template <typename T> using Expected = std::expected<T, BusError>;

} // namespace micro_forge
