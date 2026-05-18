#pragma once
#include "thumb_fields.hpp"
#include <cstdint>

namespace micro_forge::cpu {
namespace arm::cortex_m3::thumb32 {
constexpr uint32_t s_bit(uint16_t hw1) {
    return (hw1 >> 10) & 0x1u;
}

constexpr uint16_t hw1_imm10(uint16_t hw1) {
    return hw1 & 0x3FFu;
}

constexpr uint16_t hw2_imm11(uint16_t hw2) {
    return hw2 & 0x7FFu;
}

// Get the bits[13]
constexpr uint32_t j1(uint16_t hw2) {
    return (hw2 >> 13) & 0x1u;
}

// Get the bits[11]
constexpr uint32_t j2(uint16_t hw2) {
    return (hw2 >> 11) & 0x1u;
}

constexpr uint32_t mov_i(uint16_t hw1) {
    return (hw1 >> 10) & 0x1u;
}

constexpr uint32_t mov_imm4(uint16_t hw1) {
    return hw1 & 0xFu;
}

constexpr uint32_t mov_imm3(uint16_t hw2) {
    return (hw2 >> 12) & 0x7u;
}

constexpr uint8_t hw2_rd4(uint16_t hw2) {
    return (hw2 >> 8) & 0xFu;
}

constexpr uint16_t decode_imm16(uint16_t hw1, uint16_t hw2) {
    return static_cast<uint16_t>((mov_imm4(hw1) << 12) | (mov_i(hw1) << 11) |
                                 (mov_imm3(hw2) << 8) | thumb::imm8(hw2));
}

constexpr uint8_t decode_key(uint16_t insn) {
    return (insn >> 11) & 0x1Fu;
}

} // namespace arm::cortex_m3::thumb32
} // namespace micro_forge::cpu