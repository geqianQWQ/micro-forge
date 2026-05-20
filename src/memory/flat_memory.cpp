#include "memory/flat_memory.hpp"

#include <cstring>

namespace micro_forge::memory {

FlatMemory::FlatMemory(addr_t size) : data_(size, 0), size_(size) {}

Expected<data_t> FlatMemory::read(addr_t offset, Width w) {
    auto width_bytes = static_cast<addr_t>(w);
    if (offset + width_bytes > size_) {
        return std::unexpected(BusError::OutOfRange);
    }

    data_t result = 0;
    for (addr_t i = 0; i < width_bytes; ++i) {
        result |= static_cast<data_t>(data_[offset + i]) << (i * 8);
    }
    return result;
}

Expected<void> FlatMemory::write(addr_t offset, data_t data, Width w) {
    auto width_bytes = static_cast<addr_t>(w);
    if (offset + width_bytes > size_) {
        return std::unexpected(BusError::OutOfRange);
    }

    for (addr_t i = 0; i < width_bytes; ++i) {
        data_[offset + i] = static_cast<uint8_t>((data >> (i * 8)) & 0xFF);
    }
    return {};
}

Expected<void> FlatMemory::load(addr_t offset, std::span<const uint8_t> data) {
    if (offset + data.size() > size_) {
        return std::unexpected(BusError::OutOfRange);
    }
    std::memcpy(data_.data() + offset, data.data(), data.size());
    return {};
}

} // namespace micro_forge::memory
