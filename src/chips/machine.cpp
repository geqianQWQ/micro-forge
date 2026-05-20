#include "chips/machine.hpp"
#include "loader/binary_loader.hpp"
#include "loader/elf_loader.hpp"

namespace micro_forge::chips {

namespace {

std::string load_error_message(loader::LoadError error) {
    switch (error) {
        case loader::LoadError::MemoryUnmapped:
            return "binary load failed: memory unmapped";
        case loader::LoadError::General:
            return "binary load failed: invalid binary load request";
    }
    return "binary load failed";
}

} // namespace

std::expected<void, std::string>
Machine::load_bin(uint32_t base, std::span<const uint8_t> data) {
    if (!bus) {
        return std::unexpected("bus not initialized");
    }
    auto result = loader::load_binary(loader::BinaryPack{*bus, base, data});
    if (!result) {
        return std::unexpected(load_error_message(result.error()));
    }
    return {};
}

std::expected<void, std::string>
Machine::load_elf(std::span<const uint8_t> data) {
    if (!bus) {
        return std::unexpected("bus not initialized");
    }
    auto result = loader::load_elf(*bus, data);
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

sim::RunResult Machine::run(size_t max_steps) {
    if (!coord) {
        return sim::RunResult::Halted;
    }
    return coord->run(max_steps);
}

} // namespace micro_forge::chips
