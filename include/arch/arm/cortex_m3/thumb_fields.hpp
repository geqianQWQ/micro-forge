#pragma once

#include <cstdint>
namespace micro_forge::cpu {
namespace arm::cortex_m3::thumb {

constexpr uint8_t dn(uint16_t insn) {
    return (insn >> 7) & 0x1;
}

/**
 * @brief Extract the Destination Register, bitmask the low 3 bits
 *
 * @param instruction
 * @return constexpr uint8_t
 */
constexpr uint8_t rd3(uint16_t instruction) {
    return instruction & 0b111;
}

constexpr uint8_t rd4(uint16_t insn) {
    return (dn(insn) << 3) | rd3(insn);
}

/**
 * @brief MOV/CMP/ADD/SUB immediate's Rd/Rn
 *
 * @param insn
 * @return constexpr uint8_t
 */
constexpr uint8_t rd8(uint16_t insn) {
    return (insn >> 8) & 0b111;
}

/**
 * @brief Extract the first op_num, bitmask the middle 3 bits
 *
 * @param instruction
 * @return constexpr uint8_t
 */
constexpr uint8_t rn3(uint16_t instruction) {
    return (instruction >> 3) & 0b111;
}

/**
 * @brief Extract the second op_num, bitmask the middle 3 bits
 *
 * @param instruction
 * @return constexpr uint8_t
 */
constexpr uint8_t rm3(uint16_t instruction) {
    return (instruction >> 6) & 0b111;
}

constexpr uint8_t rm4(uint16_t insn) {
    return (insn >> 3) & 0b1111;
}

constexpr uint16_t imm11(uint16_t insn) {
    return insn & 0x7FF;
}

constexpr uint8_t imm8(uint16_t instruction) {
    return instruction & 0xFFu;
}
constexpr uint8_t imm5(uint16_t insn) {
    return (insn >> 6) & 0b11111;
}
constexpr uint8_t cond(uint16_t insn) {
    return (insn >> 8) & 0b1111;
}

constexpr bool m_bit(uint16_t insn) {
    return (insn >> 8) & 0x1u;
}

/**
 * @brief Get the Register Lists
 *
 * @param insn
 * @return constexpr uint8_t
 */
constexpr uint8_t reg_list(uint16_t insn) {
    return insn & 0xFFu;
}

constexpr bool is_32bit_prefix_instruction(uint16_t hw1) {
    return (hw1 & 0xE000) == 0xE000 && (hw1 & 0x1800) != 0;
}

/// Extract bits[15:11] as main decode key (0-31)
constexpr uint8_t decode_key(uint16_t insn) {
    return (insn >> 11) & 0x1Fu;
}

} // namespace arm::cortex_m3::thumb
} // namespace micro_forge::cpu
