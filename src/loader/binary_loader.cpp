#include "loader/binary_loader.hpp"
#include "autogen/arch_details.hpp"

namespace micro_forge::loader {

namespace {

bool add_overflows(addr_t base, addr_t size) {
    return size > (UINT32_MAX - base);
}

Width widest_width_for(size_t remaining) {
    if (remaining >= 4) {
        return Width::Word;
    }
    if (remaining >= 2) {
        return Width::HalfWord;
    }
    return Width::Byte;
}

uint32_t pack_little_endian(std::span<const uint8_t> data) {
    uint32_t word = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        word |= static_cast<uint32_t>(data[i]) << (i * 8);
    }
    return word;
}

} // namespace

std::expected<BinaryLoadResult, LoadError> load_binary(const BinaryPack& pack) {
    BinaryLoadResult result{.entry_point = pack.base_addr};
    auto& mem_bus = pack.bus;

    const size_t total = pack.data.size();
    if (total > UINT32_MAX ||
        add_overflows(pack.base_addr, static_cast<addr_t>(total))) {
        return std::unexpected(LoadError::General);
    }

    size_t offset = 0;

    while (offset < total) {
        const size_t remaining = total - offset;
        if (offset > UINT32_MAX ||
            add_overflows(pack.base_addr, static_cast<addr_t>(offset))) {
            return std::unexpected(LoadError::General);
        }

        const addr_t addr = pack.base_addr + static_cast<addr_t>(offset);
        const Width w = widest_width_for(remaining);
        const size_t chunk = static_cast<size_t>(w);
        const uint32_t word =
            pack_little_endian(pack.data.subspan(offset, chunk));

        auto write_result = mem_bus.write(addr, word, w);
        if (!write_result.has_value()) {
            return std::unexpected(LoadError::MemoryUnmapped);
        }

        offset += chunk;
    }

    return result;
}
} // namespace micro_forge::loader
