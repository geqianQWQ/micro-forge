#include "chips/machine.hpp"
#include "loader/binary_loader.hpp"
#include "loader/elf_loader.hpp"

namespace micro_forge::chips {

std::expected<void, std::string> Machine::load_bin(uint32_t base,
                                                    std::span<const uint8_t> data) {
    if (!bus) return std::unexpected("bus not initialized");
    auto result = loader::load_binary(loader::BinaryPack{*bus, base, data});
    if (!result) {
        return std::unexpected("binary load failed");
    }
    return {};
}

std::expected<void, std::string> Machine::load_elf(std::span<const uint8_t> data) {
    if (!bus) return std::unexpected("bus not initialized");
    auto result = loader::load_elf(*bus, data);
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

void Machine::run(size_t max_steps) {
    if (coord) coord->run(max_steps);
}

} // namespace micro_forge::chips
