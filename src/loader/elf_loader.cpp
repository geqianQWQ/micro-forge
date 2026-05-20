#include "loader/elf_loader.hpp"
#include "autogen/arch_details.hpp"
#include "loader/binary_loader.hpp"
#include "loader/utils/def.h"
#include "loader/utils/elf_parser.hpp"

#include <cstring>
#include <string>

namespace micro_forge::loader {

namespace {

template <typename T> std::expected<T, std::string>
read_struct(std::span<const uint8_t> data, size_t offset) {
    if (offset + sizeof(T) > data.size()) {
        return std::unexpected("ELF data truncated at offset " +
                               std::to_string(offset));
    }
    T val;
    std::memcpy(&val, data.data() + offset, sizeof(T));
    return val;
}

std::expected<void, std::string>
write_segment(memory::Bus& bus, uint32_t addr,
              std::span<const uint8_t> seg_data, uint32_t memsz) {

    // Write file-backed portion
    for (size_t i = 0; i < seg_data.size(); i += 4) {
        uint32_t word = 0;
        size_t chunk = std::min(seg_data.size() - i, size_t(4));
        for (size_t b = 0; b < chunk; ++b) {
            word |= static_cast<uint32_t>(seg_data[i + b]) << (b * 8);
        }
        Width w = chunk == 4   ? Width::Word
                  : chunk == 2 ? Width::HalfWord
                               : Width::Byte;

        auto res = bus.write(addr + static_cast<uint32_t>(i), word, w);
        if (!res.has_value()) {
            return std::unexpected(
                "failed to write segment at 0x" + ([](uint32_t v) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%08X", v);
                    return std::string(buf);
                })(addr + static_cast<uint32_t>(i)));
        }
    }

    // Zero-fill BSS (memsz > filesz)
    for (size_t i = seg_data.size(); i < memsz; i += 4) {
        auto res = bus.write(addr + static_cast<uint32_t>(i), 0, Width::Word);
        if (!res.has_value()) {
            return std::unexpected("failed to zero-fill BSS at offset " +
                                   std::to_string(i));
        }
    }

    return {};
}

} // anonymous namespace

std::expected<ElfLoadResult, std::string>
load_elf(memory::Bus& bus, std::span<const uint8_t> elf_data) {
    using namespace utils;

    // Validate ELF header
    auto mut_span = std::span<uint8_t>(const_cast<uint8_t*>(elf_data.data()),
                                       elf_data.size());
    if (!is_valid_elf(mut_span)) {
        return std::unexpected("invalid ELF magic");
    }
    if (auto r = is_elf32(mut_span); !r) {
        return std::unexpected("not a 32-bit ELF");
    }
    if (auto r = is_le(mut_span); !r) {
        return std::unexpected("not little-endian ELF");
    }
    if (auto r = is_arm(mut_span); !r) {
        return std::unexpected("not an ARM ELF");
    }

    // Read ELF header
    auto ehdr = read_struct<Elf32_Ehdr>(elf_data, 0);
    if (!ehdr) {
        return std::unexpected(ehdr.error());
    }

    static_assert(sizeof(Elf32_Ehdr) == 52);

    // Iterate program headers
    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        size_t ph_offset =
            ehdr->e_phoff + static_cast<size_t>(i) * ehdr->e_phentsize;
        auto phdr = read_struct<Elf32_Phdr>(elf_data, ph_offset);
        if (!phdr) {
            return std::unexpected(phdr.error());
        }

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        // Extract segment data
        if (phdr->p_offset + phdr->p_filesz > elf_data.size()) {
            return std::unexpected("PT_LOAD segment data truncated");
        }

        auto seg_data = elf_data.subspan(phdr->p_offset, phdr->p_filesz);

        auto res = write_segment(bus, phdr->p_vaddr, seg_data, phdr->p_memsz);
        if (!res) {
            return std::unexpected(res.error());
        }
    }

    return ElfLoadResult{.entry_point = ehdr->e_entry};
}

} // namespace micro_forge::loader
