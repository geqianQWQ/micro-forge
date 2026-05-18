#include "loader/binary_loader.hpp"
#include "autogen/arch_details.hpp"

namespace micro_forge::loader {
std::expected<BinaryLoadResult, LoadError> load_binary(const BinaryPack& pack) {
    BinaryLoadResult result{.entry_point = pack.base_addr};
    auto& mem_bus = pack.bus;

    const size_t total = pack.data.size();
    size_t offset = 0;

    while (offset < total) {
        const size_t remaining = total - offset;
        const addr_t addr = pack.base_addr + static_cast<addr_t>(offset);

        uint32_t word = 0;
        const size_t chunk = std::min(remaining, size_t(4));

        for (size_t i = 0; i < chunk; ++i) {
            word |= static_cast<uint32_t>(pack.data[offset + i]) << (i * 8);
        }

        const Width w = chunk == 4   ? Width::Word
                        : chunk == 2 ? Width::HalfWord
                                     : Width::Byte;

        auto result = mem_bus.write(addr, word, w);
        if (!result.has_value()) {
            return std::unexpected(LoadError::MemoryUnmapped);
        }

        offset += chunk;
    }

    return result;
}
} // namespace micro_forge::loader
