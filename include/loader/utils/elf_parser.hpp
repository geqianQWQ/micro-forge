#pragma once
#include <cstdint>
#include <expected>
#include <span>

namespace micro_forge::loader::utils {

enum class ElfLoadError { ElfValidationFailed, NotELF32, NotTargetArch, NotLE };
bool is_valid_elf(const std::span<uint8_t>& span_data);

std::expected<void, ElfLoadError> is_elf32(const std::span<uint8_t>& span_data);
std::expected<void, ElfLoadError> is_arm(const std::span<uint8_t>& span_data);
std::expected<void, ElfLoadError> is_le(const std::span<uint8_t>& span_data);

} // namespace micro_forge::loader::utils