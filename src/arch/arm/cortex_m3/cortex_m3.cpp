#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/def.h"
#include "arch/arm/cortex_m3/thumb32_fields.hpp"
#include "core/types.hpp"
#include <bit>
#include <expected>

namespace micro_forge::cpu::arm::cortex_m3 {

using namespace thumb;

// ── Reset & ICore interface ──

CPU::CPUExpected<void> CortexM3CPU::reset() {
    regs_.reset();
    xpsr_ = PSR_T; // Thumb mode always on
    primask_ = 0;
    control_ = 0;
    current_status_ = State::Halted;
    cycles_ = 0;
    return {};
}

CPU::CPUExpected<data_t> CortexM3CPU::register_value(std::size_t index) const {
    if (index >= 16) {
        return std::unexpected{CPUError::RegisterIndexOverflow};
    }
    const auto result = regs_.read(index);
    if (!result) {
        return std::unexpected{CPUError::RegisterIndexOverflow};
    }
    return *result;
}

CPU::CPUExpected<void> CortexM3CPU::set_register_value(std::size_t index,
                                                       data_t value) {
    if (index >= 16) {
        return std::unexpected{CPUError::RegisterIndexOverflow};
    }
    return write_reg(static_cast<uint8_t>(index), value);
}

CPU::CPUExpected<std::string_view>
CortexM3CPU::register_name(std::size_t idx) const {
    if (idx >= REGCNT) {
        return std::unexpected{CPUError::RegisterIndexOverflow};
    }
    return names[idx];
}

CPU::CPUExpected<addr_t> CortexM3CPU::pc() const {
    auto v = regs_.read(15);
    return v ? *v : 0;
}

CPU::CPUExpected<addr_t> CortexM3CPU::set_pc(addr_t new_pc) {
    const auto res = write_reg(15, new_pc);
    if (!res) {
        return std::unexpected{res.error()};
    }
    return new_pc;
}

CPU::CPUExpected<void> CortexM3CPU::raise_irq(intr::intr_n_t) {
    return {};
}

CPU::CPUExpected<CPU::ticks_t> CortexM3CPU::cycles() const {
    return cycles_;
}

// ── Internal register access ──

CPU::CPUExpected<addr_t> CortexM3CPU::read_pc_raw() const {
    auto result = regs_.read(15);
    if (!result) {
        return std::unexpected{CPUError::PCUnavaibale};
    }
    return *result;
}

CPU::CPUExpected<void> CortexM3CPU::write_reg(uint8_t index, data_t value) {
    if (index == 13) {
        value &= ~0x3u; // SP: low 2 bits forced to 0
    }
    auto result = regs_.write(index, value);
    if (!result) {
        return std::unexpected{CPUError::RegisterIndexOverflow};
    }
    return {};
}

// ── Flag helpers ──

void CortexM3CPU::update_nz(data_t result) {
    xpsr_ &= ~(PSR_N | PSR_Z);
    if (result & 0x80000000u) {
        xpsr_ |= PSR_N;
    }
    if (result == 0) {
        xpsr_ |= PSR_Z;
    }
}

void CortexM3CPU::update_flags(FlagPostOperation p, data_t a, data_t b,
                               data_t result) {
    xpsr_ &= ~(PSR_N | PSR_Z | PSR_C | PSR_V);
    if (result & 0x80000000u) {
        xpsr_ |= PSR_N;
    }
    if (result == 0) {
        xpsr_ |= PSR_Z;
    }

    if (p == FlagPostOperation::Add) {
        if (result < a) {
            xpsr_ |= PSR_C;
        }
        if (((a ^ ~b) & (a ^ result)) & 0x80000000u) {
            xpsr_ |= PSR_V;
        }
    } else {
        if (a >= b) {
            xpsr_ |= PSR_C;
        }
        if (((a ^ b) & (a ^ result)) & 0x80000000u) {
            xpsr_ |= PSR_V;
        }
    }
}

bool CortexM3CPU::condition_need_execute(uint8_t c) {
    bool N = xpsr_ & PSR_N;
    bool Z = xpsr_ & PSR_Z;
    bool C = xpsr_ & PSR_C;
    bool V = xpsr_ & PSR_V;
    switch (c) {
        case 0x0:
            return Z; // EQ
        case 0x1:
            return !Z; // NE
        case 0x2:
            return C; // CS/HS
        case 0x3:
            return !C; // CC/LO
        case 0x4:
            return N; // MI
        case 0x5:
            return !N; // PL
        case 0x6:
            return V; // VS
        case 0x7:
            return !V; // VC
        case 0x8:
            return C && !Z; // HI
        case 0x9:
            return !C || Z; // LS
        case 0xA:
            return N == V; // GE
        case 0xB:
            return N != V; // LT
        case 0xC:
            return !Z && (N == V); // GT
        case 0xD:
            return Z || (N != V); // LE
        case 0xE:
            return true; // AL
        default:
            return false; // 0xF
    }
}

// ── Stack operations ──

CPU::CPUExpected<void> CortexM3CPU::push_stack(data_t val) {
    auto sp_res = regs_.read(13);
    data_t sp = *sp_res - 4;
    if (bus_) {
        auto w = bus_->write(sp, val, Width::Word);
        if (!w) {
            return std::unexpected{CPUError::NextInstructionsUnavaliable};
        }
    }
    (void)regs_.write(13, sp);
    return {};
}

CPU::CPUExpected<data_t> CortexM3CPU::pop_stack() {
    data_t sp = *regs_.read(13);
    data_t val = 0;
    if (bus_) {
        auto r = bus_->read(sp, Width::Word);
        if (!r) {
            return std::unexpected{CPUError::NextInstructionsUnavaliable};
        }
        val = *r;
    }
    (void)regs_.write(13, sp + 4);
    return val;
}

// ── Fetch ──

Expected<uint16_t> CortexM3CPU::fetch16(addr_t addr) {
    if (!bus_) {
        return std::unexpected{BusError::Fault};
    }
    auto lo = bus_->read(addr, Width::Byte);
    if (!lo) {
        return std::unexpected{lo.error()};
    }
    auto hi = bus_->read(addr + 1, Width::Byte);
    if (!hi) {
        return std::unexpected{hi.error()};
    }
    return static_cast<uint16_t>(*lo | (*hi << 8));
}

// ── Step ──

CPU::CPUExpected<void> CortexM3CPU::step() {
    if (current_status_ != State::Running) {
        return std::unexpected{CPUError::NotRunning};
    }

    // Check for pending interrupts before instruction fetch.
    // If an interrupt is taken, this step is consumed by the entry sequence
    // (stacking + vector fetch). Handler instructions start executing next
    // step.
    bool was_handler = in_handler_mode_;
    auto irq_res = check_and_handle_interrupt();
    if (!irq_res) {
        current_status_ = State::Faulted;
        return std::unexpected{irq_res.error()};
    }
    if (in_handler_mode_ && !was_handler) {
        // Interrupt taken — entry consumed this step
        cycles_++;
        return {};
    }

    auto pc_res = read_pc_raw();
    if (!pc_res) {
        current_status_ = State::Faulted;
        return std::unexpected{pc_res.error()};
    }
    addr_t pc = *pc_res;

    auto hw1_res = fetch16(pc);
    if (!hw1_res) {
        current_status_ = State::Faulted;
        return std::unexpected{CPUError::NextInstructionsUnavaliable};
    }
    uint16_t hw1 = *hw1_res;

    CPUExpected<void> exec_res;
    Expected<uint16_t> hw2_res;

    if (is_32bit_prefix_instruction(hw1)) {
        hw2_res = fetch16(pc + 2);
        if (!hw2_res) {
            current_status_ = State::Faulted;
            return std::unexpected{CPUError::NextInstructionsUnavaliable};
        }
        exec_res = execute_32bit(hw1, *hw2_res);
        if (exec_res.has_value()) {
            auto new_pc = read_pc_raw();
            if (new_pc && *new_pc == pc) {
                (void)write_reg(15, pc + 4);
            }
        }
    } else {
        exec_res = execute_16bit(hw1);
        if (exec_res.has_value()) {
            auto new_pc = read_pc_raw();
            if (new_pc && *new_pc == pc) {
                (void)write_reg(15, pc + 2);
            }
        }
    }

    if (!exec_res.has_value()) {
        bool is32 = is_32bit_prefix_instruction(hw1);
        uint16_t hw2_val = (is32 && hw2_res.has_value()) ? *hw2_res : 0;

        fprintf(stderr, "[FAULT] PC=0x%08X hw1=0x%04X", pc, hw1);
        if (is32) {
            fprintf(stderr, " hw2=0x%04X", hw2_val);
        }
        fprintf(stderr, "\n");

        if (probe_mode_) {
            missing_opcodes_.emplace_back(pc, hw1, hw2_val);
            (void)write_reg(15, pc + (is32 ? 4 : 2));
        } else {
            current_status_ = State::Faulted;
            return exec_res;
        }
    }

    cycles_++;
    return {};
}

// ── 16-bit Thumb decode ──

CPU::CPUExpected<void> CortexM3CPU::execute_16bit(uint16_t insn) {

    auto rr = [&](uint8_t idx) -> data_t {
        return regs_.read(idx).value_or(0);
    };
    auto wr = [&](uint8_t idx, data_t val) -> CPUExpected<void> {
        auto res = write_reg(idx, val);
        if (!res) {
            return std::unexpected{res.error()};
        }
        return {};
    };
    // Bus read helper: returns error on failure
    auto br = [&](addr_t addr, Width w) -> CPUExpected<data_t> {
        if (!bus_) {
            return std::unexpected{CPUError::NextInstructionsUnavaliable};
        }
        auto v = bus_->read(addr, w);
        if (!v) {
            return std::unexpected{CPUError::NextInstructionsUnavaliable};
        }
        return *v;
    };
    // Bus write helper: returns error on failure
    auto bw = [&](addr_t addr, data_t val, Width w) -> CPUExpected<void> {
        if (!bus_) {
            return std::unexpected{CPUError::NextInstructionsUnavaliable};
        }
        auto v = bus_->write(addr, val, w);
        if (!v) {
            return std::unexpected{CPUError::NextInstructionsUnavaliable};
        }
        return {};
    };

    switch (decode_key(insn)) {

        // ── Shift immediate (LSL/LSR/ASR) ──
        case 0b00000:
        case 0b00001:
        case 0b00010: {
            uint8_t op = (insn >> 11) & 0x3;
            uint8_t imm = imm5(insn);
            uint8_t rm = rn3(insn);
            uint8_t rd = rd3(insn);
            data_t val = rr(rm);
            data_t result;

            if (op == 0b00) { // LSL
                result = imm == 0 ? val : val << imm;
            } else if (op == 0b01) { // LSR
                result = imm == 0 ? 0 : val >> imm;
            } else { // ASR
                result =
                    (imm == 0)
                        ? ((val & 0x80000000u) ? 0xFFFFFFFFu : 0)
                        : static_cast<data_t>(static_cast<int32_t>(val) >> imm);
            }
            auto res = wr(rd, result);
            if (!res) {
                return res;
            }
            update_nz(result);
            break;
        }

        // ── Add/subtract register or 3-bit immediate ──
        case 0b00011: {
            bool is_imm = (insn >> 10) & 0x1;
            bool is_sub = (insn >> 9) & 0x1;
            uint8_t rm_or_imm = rm3(insn);
            uint8_t rn = rn3(insn);
            uint8_t rd = rd3(insn);
            data_t a = rr(rn);
            data_t b = is_imm ? rm_or_imm : rr(rm_or_imm);
            data_t result = is_sub ? a - b : a + b;

            if (is_sub) {
                update_flags(FlagPostOperation::Sub, a, b, result);
            } else {
                update_flags(FlagPostOperation::Add, a, b, result);
            }
            return wr(rd, result);
        }

        // ── MOVS Rd, imm8 ──
        case 0b00100: {
            uint8_t rd = rd8(insn);
            data_t val = imm8(insn);
            auto res = wr(rd, val);
            if (!res) {
                return res;
            }
            update_nz(val);
            break;
        }

        // ── CMP Rn, imm8 ──
        case 0b00101: {
            data_t a = rr(rd8(insn));
            data_t b = imm8(insn);
            update_flags(FlagPostOperation::Sub, a, b, a - b);
            break;
        }

        // ── ADDS Rd, imm8 ──
        case 0b00110: {
            uint8_t rd = rd8(insn);
            data_t a = rr(rd), b = imm8(insn);
            data_t result = a + b;
            update_flags(FlagPostOperation::Add, a, b, result);
            return wr(rd, result);
        }

        // ── SUBS Rd, imm8 ──
        case 0b00111: {
            uint8_t rd = rd8(insn);
            data_t a = rr(rd), b = imm8(insn);
            data_t result = a - b;
            update_flags(FlagPostOperation::Sub, a, b, result);
            return wr(rd, result);
        }

        // ── Data processing register OR Special data / BX ──
        case 0b01000: {
            if ((insn >> 10) & 1) {
                // Special data instructions / BX
                uint8_t op = (insn >> 8) & 0x3;
                uint8_t rm = rm4(insn);
                uint8_t rd = rd4(insn);

                switch (op) {
                    case 0b00:
                        return wr(rd, rr(rd) + rr(rm)); // ADD high
                    case 0b01: {                        // CMP high
                        data_t a = rr(rd), b = rr(rm);
                        update_flags(FlagPostOperation::Sub, a, b, a - b);
                        return {};
                    }
                    case 0b10:
                        return wr(rd, rr(rm)); // MOV high
                    case 0b11:
                        return write_pc(rr(rm)); // BX
                }
            }
            // Data processing register
            uint8_t op = (insn >> 6) & 0xF;
            uint8_t rm = rm3(insn);
            uint8_t rd = rd3(insn);
            data_t a = rr(rd), b = rr(rm);
            data_t result;

            switch (op) {
                case 0x0:
                    result = a & b;
                    break;
                case 0x1:
                    result = a ^ b;
                    break;
                case 0x2:
                    result = a << (b & 0xFF);
                    break;
                case 0x3:
                    result = a >> (b & 0xFF);
                    break;
                case 0x4:
                    result = static_cast<data_t>(static_cast<int32_t>(a) >>
                                                 (b & 0xFF));
                    break;
                case 0x5:
                    result = a + b + ((xpsr_ & PSR_C) ? 1 : 0);
                    break;
                case 0x6:
                    result = a - b - ((xpsr_ & PSR_C) ? 0 : 1);
                    break;
                case 0x7: {
                    uint8_t n = (b & 0xFF) & 0x1F;
                    result = n ? ((a >> n) | (a << (32 - n))) : a;
                    break;
                }
                case 0x8:
                    update_nz(a & b);
                    return {}; // TST
                case 0x9:
                    result = -b;
                    break; // RSB
                case 0xA:
                    update_flags(FlagPostOperation::Sub, a, b, a - b);
                    return {};
                case 0xB:
                    update_flags(FlagPostOperation::Add, a, b, a + b);
                    return {};
                case 0xC:
                    result = a | b;
                    break;
                case 0xD:
                    result = a * b;
                    break;
                case 0xE:
                    result = a & ~b;
                    break;
                case 0xF:
                    result = ~b;
                    break;
                default:
                    return std::unexpected{CPUError::IllegalInstructions};
            }
            auto res = wr(rd, result);
            if (!res) {
                return res;
            }
            update_nz(result);
            break;
        }

        // ── LDR literal (PC-relative) ──
        // Encoding: 0100 1 Rt imm8
        case 0b01001: {
            uint8_t rt = rd8(insn);
            addr_t addr = ((rr(15) + 4) & ~0x3u) + imm8(insn) * 4;
            auto val = br(addr, Width::Word);
            if (!val) {
                return std::unexpected{val.error()};
            }
            return wr(rt, *val);
        }

        // ── Store register offset (STR/STRH/STRB/LDRSB) ──
        // Encoding: 0101 0op Rm Rn Rt, op = bits[10:9]
        case 0b01010: {
            uint8_t op = (insn >> 9) & 0x3;
            uint8_t rm = rm3(insn), rn = rn3(insn), rt = rd3(insn);
            addr_t addr = rr(rn) + rr(rm);
            switch (op) {
                case 0b00: // STR
                    return bw(addr, rr(rt), Width::Word);
                case 0b01: // STRH
                    return bw(addr, rr(rt) & 0xFFFF, Width::HalfWord);
                case 0b10: // STRB
                    return bw(addr, rr(rt) & 0xFF, Width::Byte);
                case 0b11: { // LDRSB
                    auto v = br(addr, Width::Byte);
                    if (!v) {
                        return std::unexpected{v.error()};
                    }
                    data_t val = *v;
                    if (val & 0x80u) {
                        val |= 0xFFFFFF00u;
                    }
                    return wr(rt, val);
                }
            }
            break;
        }

        // ── Load register offset (LDR/LDRH/LDRB/LDRSH) ──
        // Encoding: 0101 1op Rm Rn Rt, op = bits[10:9]
        case 0b01011: {
            uint8_t op = (insn >> 9) & 0x3;
            uint8_t rm = rm3(insn), rn = rn3(insn), rt = rd3(insn);
            addr_t addr = rr(rn) + rr(rm);
            switch (op) {
                case 0b00: { // LDR
                    auto v = br(addr, Width::Word);
                    if (!v) {
                        return std::unexpected{v.error()};
                    }
                    return wr(rt, *v);
                }
                case 0b01: { // LDRH
                    auto v = br(addr, Width::HalfWord);
                    if (!v) {
                        return std::unexpected{v.error()};
                    }
                    return wr(rt, *v);
                }
                case 0b10: { // LDRB
                    auto v = br(addr, Width::Byte);
                    if (!v) {
                        return std::unexpected{v.error()};
                    }
                    return wr(rt, *v);
                }
                case 0b11: { // LDRSH
                    auto v = br(addr, Width::HalfWord);
                    if (!v) {
                        return std::unexpected{v.error()};
                    }
                    data_t val = *v;
                    if (val & 0x8000u) {
                        val |= 0xFFFF0000u;
                    }
                    return wr(rt, val);
                }
            }
            break;
        }

        // ── STR word immediate offset ──
        case 0b01100: {
            addr_t addr = rr(rn3(insn)) + imm5(insn) * 4;
            return bw(addr, rr(rd3(insn)), Width::Word);
        }

        // ── LDR word immediate offset ──
        case 0b01101: {
            addr_t addr = rr(rn3(insn)) + imm5(insn) * 4;
            auto v = br(addr, Width::Word);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rd3(insn), *v);
        }

        // ── STRB immediate offset ──
        case 0b01110: {
            addr_t addr = rr(rn3(insn)) + imm5(insn);
            return bw(addr, rr(rd3(insn)) & 0xFF, Width::Byte);
        }

        // ── LDRB immediate offset ──
        case 0b01111: {
            addr_t addr = rr(rn3(insn)) + imm5(insn);
            auto v = br(addr, Width::Byte);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rd3(insn), *v);
        }

        // ── STRH immediate offset ──
        case 0b10000: {
            addr_t addr = rr(rn3(insn)) + imm5(insn) * 2;
            return bw(addr, rr(rd3(insn)) & 0xFFFF, Width::HalfWord);
        }

        // ── LDRH immediate offset ──
        case 0b10001: {
            addr_t addr = rr(rn3(insn)) + imm5(insn) * 2;
            auto v = br(addr, Width::HalfWord);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rd3(insn), *v);
        }

        // ── STR SP-relative ──
        case 0b10010: {
            addr_t addr = rr(13) + imm8(insn) * 4;
            return bw(addr, rr(rd8(insn)), Width::Word);
        }

        // ── LDR SP-relative ──
        case 0b10011: {
            addr_t addr = rr(13) + imm8(insn) * 4;
            auto v = br(addr, Width::Word);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rd8(insn), *v);
        }

        // ── ADD Rd, SP/PC, #imm8 ──
        // Encoding: 1010 1 ddd iiii iiii
        // bit[11]=1 → ADD Rd, SP, #imm*4
        // bit[11]=0 → ADD Rd, PC, #imm*4
        case 0b10101: {
            uint8_t rd = rd8(insn);
            uint32_t base =
                (insn & (1 << 11)) ? rr(13) : (read_pc_raw().value_or(0) & ~3u);
            uint32_t offset = imm8(insn) * 4;
            return wr(rd, base + offset);
        }

        // ── PUSH / ADD SP / SUB SP ──
        // bits[10:9]=00 → ADD/SUB SP, SP, #imm7<<2
        // bits[10:9]=10 → PUSH
        case 0b10110: {
            uint8_t sub_op = (insn >> 9) & 0x3;
            if (sub_op == 0b00) {
                // ADD/SUB SP, SP, #imm7<<2
                uint8_t imm7 = insn & 0x7F;
                uint32_t offset = imm7 * 4;
                if (insn & (1 << 7)) {
                    // SUB SP
                    return write_reg(13, rr(13) - offset);
                } else {
                    // ADD SP
                    return write_reg(13, rr(13) + offset);
                }
            }
            // PUSH
            uint8_t rlist = reg_list(insn);
            bool m = m_bit(insn);
            int count = std::popcount(rlist) + (m ? 1 : 0);

            data_t sp = rr(13) - count * 4;
            [[maybe_unused]] auto _ = write_reg(13, sp);

            for (int i = 0; i < 8; i++) {
                if (rlist & (1 << i)) {
                    auto res = bw(sp, rr(i), Width::Word);
                    if (!res) {
                        return res;
                    }
                    sp += 4;
                }
            }
            if (m) {
                return bw(sp, rr(14), Width::Word);
            }
            break;
        }

        // ── POP / Hints ──
        // bits[10:9]=10 → POP
        // bits[10:9]=11 → Hints (NOP, YIELD, etc.)
        case 0b10111: {
            uint8_t sub_op = (insn >> 9) & 0x3;
            if (sub_op == 0b11) {
                // Hints: NOP (0xBF00), YIELD, WFE, WFI, SEV
                break; // treat all hints as NOP
            }
            // POP
            uint8_t rlist = reg_list(insn);
            bool m = m_bit(insn);
            data_t sp = rr(13);

            for (int i = 0; i < 8; i++) {
                if (rlist & (1 << i)) {
                    auto v = br(sp, Width::Word);
                    if (!v) {
                        return std::unexpected{v.error()};
                    }
                    auto res = write_reg(i, *v);
                    if (!res) {
                        return res;
                    }
                    sp += 4;
                }
            }
            if (m) {
                auto v = br(sp, Width::Word);
                if (!v) {
                    return std::unexpected{v.error()};
                }
                auto res = write_pc(*v);
                if (!res) {
                    return res;
                }
                sp += 4;
            }
            [[maybe_unused]] auto _ = write_reg(13, sp);
            break;
        }

        // ── Conditional branch B<cond> ──
        case 0b11010:
        case 0b11011: {
            uint8_t c = cond(insn);
            if (c == 0xE) {
                return std::unexpected{CPUError::IllegalInstructions};
            }
            if (c == 0xF) {
                break; // SVC — Phase 3B
            }
            if (condition_need_execute(c)) {
                int32_t offset = static_cast<int8_t>(imm8(insn));
                offset <<= 1;
                auto pc_res = read_pc_raw();
                if (!pc_res) {
                    return std::unexpected{pc_res.error()};
                }
                return write_reg(15, *pc_res + 4 + offset);
            }
            break;
        }

        // ── B unconditional ──
        case 0b11100: {
            int32_t offset = static_cast<int16_t>(imm11(insn) << 5) >> 5;
            offset <<= 1;
            auto pc_res = read_pc_raw();
            if (!pc_res) {
                return std::unexpected{pc_res.error()};
            }
            return write_reg(15, *pc_res + 4 + offset);
        }

        default:
            return std::unexpected{CPUError::IllegalInstructions};
    }
    return {};
}

// ── 32-bit Thumb-2 decode ──

CPU::CPUExpected<void> CortexM3CPU::execute_32bit(uint16_t hw1, uint16_t hw2) {

    auto rr = [&](uint8_t idx) -> data_t {
        return regs_.read(idx).value_or(0);
    };
    auto wr = [&](uint8_t idx, data_t val) -> CPUExpected<void> {
        auto res = write_reg(idx, val);
        if (!res) {
            return std::unexpected{res.error()};
        }
        return {};
    };

    // ── BL / BLX ──
    if ((hw1 & 0xF800) == 0xF000 && (hw2 & 0xD000) == 0xD000) {
        uint32_t s = thumb32::s_bit(hw1);
        uint16_t i10 = thumb32::hw1_imm10(hw1);
        uint32_t j1_val = thumb32::j1(hw2);
        uint32_t j2_val = thumb32::j2(hw2);
        uint16_t i11 = thumb32::hw2_imm11(hw2);

        uint32_t i1 = 1u ^ (j1_val ^ s);
        uint32_t i2 = 1u ^ (j2_val ^ s);

        uint32_t offset =
            (s << 24) | (i1 << 23) | (i2 << 22) | (i10 << 12) | (i11 << 1);
        if (s) {
            offset |= 0xFE000000u;
        }

        auto pc_res = read_pc_raw();
        if (!pc_res) {
            return std::unexpected{pc_res.error()};
        }
        data_t next_pc = *pc_res + 4;

        auto lr_res = wr(14, next_pc);
        if (!lr_res) {
            return lr_res;
        }

        bool is_blx = !((hw2 >> 12) & 0x1);
        if (is_blx) {
            return write_reg(15, (*pc_res + 4 + offset) & ~0x1u);
        }
        return write_reg(15, *pc_res + 4 + offset);
    }

    // ── MOVW ──
    if ((hw1 & 0xFBF0) == 0xF240) {
        return wr(thumb32::hw2_rd4(hw2), thumb32::decode_imm16(hw1, hw2));
    }

    // ── MOVT ──
    if ((hw1 & 0xFBF0) == 0xF2C0) {
        uint16_t imm16 = thumb32::decode_imm16(hw1, hw2);
        uint8_t rd = thumb32::hw2_rd4(hw2);
        data_t val =
            (rr(rd) & 0x0000FFFFu) | (static_cast<data_t>(imm16) << 16);
        return wr(rd, val);
    }

    // ── MRS (read PRIMASK) ──
    if ((hw1 & 0xFFF0) == 0xF3E0 && (hw2 & 0xFF00) == 0x8000) {
        return wr(thumb32::hw2_rd4(hw2), primask_);
    }

    // ── MSR (write PRIMASK) ──
    if ((hw1 & 0xFFF0) == 0xF380 && (hw2 & 0xFF00) == 0x8800) {
        primask_ = rr(thumb32::hw2_rd4(hw2));
        return {};
    }

    // ── Data processing (modified immediate) ──
    if ((hw1 & 0xF800) == 0xF000 && (hw2 & 0x8000) == 0) {
        uint8_t op2 = (hw1 >> 5) & 0xF;
        bool s_bit = (hw1 >> 4) & 1;
        uint8_t rn = thumb32::dp_rn(hw1);
        uint8_t rd = thumb32::dp_rd(hw2);
        uint32_t imm32 =
            thumb32::expand_imm12((hw1 >> 10) & 1, (hw2 >> 12) & 7, hw2 & 0xFF);
        uint32_t rn_val = rr(rn);
        uint32_t result;

        switch (op2) {
            case 0:
                result = rn_val & imm32;
                break; // AND
            case 1:
                result = rn_val & ~imm32;
                break; // BIC
            case 2:
                result = (rn == 15) ? imm32 : (rn_val | imm32);
                break; // ORR/MOV
            case 4:
                result = rn_val ^ imm32;
                break; // EOR
            case 8:
                result = rn_val + imm32;
                break; // ADD
            case 10:
                result = rn_val + imm32 + 0;
                break; // ADC (carry=0 for now)
            case 11:
                result = rn_val - imm32 - 1 + 0;
                break; // SBC (carry=0 → sub imm+1)
            case 13:
                result = rn_val - imm32;
                break; // SUB
            case 14:
                result = imm32 - rn_val;
                break; // RSB
            default:
                return std::unexpected{CPUError::IllegalInstructions};
        }

        if (s_bit) {
            if (op2 == 8 || op2 == 10 || op2 == 13 || op2 == 14 || op2 == 11) {
                update_flags(op2 <= 10 ? FlagPostOperation::Add
                                       : FlagPostOperation::Sub,
                             rn_val, imm32, result);
            } else {
                update_nz(result);
            }
        }
        return wr(rd, result);
    }

    // ── Load/Store byte immediate (LDRB.W / STRB.W) ──
    if ((hw1 & 0xFF00) == 0xF800) {
        uint8_t rn = hw1 & 0xF;
        bool load = (hw1 >> 4) & 1;
        uint8_t rt = (hw2 >> 12) & 0xF;
        uint8_t sub_op = (hw2 >> 8) & 0xF;
        uint32_t imm8 = hw2 & 0xFF;
        uint32_t rn_val = rr(rn);

        if (sub_op == 0xB) { // post-index: op, then Rn += imm8
            addr_t addr = rn_val;
            if (load) {
                auto r = bus_->read(addr, Width::Byte);
                if (!r) {
                    return std::unexpected{
                        CPUError::NextInstructionsUnavaliable};
                }
                auto w = wr(rt, *r);
                if (!w) {
                    return w;
                }
            } else {
                if (!bus_->write(addr, rr(rt) & 0xFF, Width::Byte)) {
                    return std::unexpected{
                        CPUError::NextInstructionsUnavaliable};
                }
            }
            return wr(rn, rn_val + imm8);
        }

        if (sub_op == 0xF) { // pre-index: addr = Rn + imm8, op, Rn = addr
            addr_t addr = rn_val + imm8;
            if (load) {
                auto r = bus_->read(addr, Width::Byte);
                if (!r) {
                    return std::unexpected{
                        CPUError::NextInstructionsUnavaliable};
                }
                auto w = wr(rt, *r);
                if (!w) {
                    return w;
                }
            } else {
                if (!bus_->write(addr, rr(rt) & 0xFF, Width::Byte)) {
                    return std::unexpected{
                        CPUError::NextInstructionsUnavaliable};
                }
            }
            return wr(rn, addr);
        }

        return std::unexpected{CPUError::IllegalInstructions};
    }

    // ── UDIV / SDIV ──
    if ((hw1 & 0xFFF0) == 0xFBB0 && (hw2 & 0xF0F0) == 0xF0F0) {
        uint8_t rn = hw1 & 0xF;
        uint8_t rm = (hw2 >> 8) & 0xF;
        uint8_t rd = hw2 & 0xF;
        uint32_t a = rr(rn);
        uint32_t b = rr(rm);
        if (b == 0) return wr(rd, 0);
        return wr(rd, a / b);
    }

    // ── Data processing (shifted register): AND, ORR, EOR, ADD, SUB, etc. ──
    if ((hw1 & 0xFE00) == 0xEA00 && (hw2 & 0x8000) == 0) {
        uint8_t op = (hw1 >> 5) & 0xF;
        bool s_bit = (hw1 >> 4) & 1;
        uint8_t rn = hw1 & 0xF;
        uint8_t rd = (hw2 >> 8) & 0xF;
        uint8_t rm = hw2 & 0xF;
        uint8_t imm3 = (hw2 >> 12) & 0x7;
        uint8_t imm2 = (hw2 >> 6) & 0x3;
        uint8_t shift_type = (hw2 >> 4) & 0x3;
        uint8_t shift_n = (imm3 << 2) | imm2;

        uint32_t rm_val = rr(rm);

        uint32_t shifted;
        switch (shift_type) {
            case 0: shifted = shift_n == 0 ? rm_val : rm_val << shift_n; break;
            case 1: shifted = rm_val >> (shift_n == 0 ? 0 : shift_n); break;
            case 2: {
                if (shift_n == 0) { shifted = rm_val; }
                else {
                    uint32_t sign = rm_val & 0x80000000u;
                    shifted = rm_val >> shift_n;
                    if (sign) shifted |= (0xFFFFFFFFu << (32 - shift_n));
                }
                break;
            }
            default: return std::unexpected{CPUError::IllegalInstructions};
        }

        uint32_t rn_val = rr(rn);
        uint32_t result;
        switch (op) {
            case 0: result = rn_val & shifted; break;
            case 1: result = rn_val & ~shifted; break;
            case 2: result = (rn == 15) ? shifted : (rn_val | shifted); break;
            case 3: result = ~shifted; break;
            case 4: result = rn_val ^ shifted; break;
            case 8: result = rn_val + shifted; break;
            case 13: result = rn_val - shifted; break;
            case 14: result = shifted - rn_val; break;
            default: return std::unexpected{CPUError::IllegalInstructions};
        }

        if (s_bit) {
            if (op == 8 || op == 13 || op == 14)
                update_flags(op <= 8 ? FlagPostOperation::Add
                                     : FlagPostOperation::Sub,
                             rn_val, shifted, result);
            else
                update_nz(result);
        }
        return wr(rd, result);
    }

    return std::unexpected{CPUError::IllegalInstructions};
}

// ── Interrupt handling ──

CPU::CPUExpected<void> CortexM3CPU::write_pc(data_t value) {
    if (in_handler_mode_ && (value & 0xFFFFFFF0u) == 0xFFFFFFF0u) {
        if (value == 0xFFFFFFF1u || value == 0xFFFFFFF9u ||
            value == 0xFFFFFFFDu) {
            return interrupt_return(value);
        }
    }
    return write_reg(15, value & ~1u);
}

CPU::CPUExpected<void> CortexM3CPU::check_and_handle_interrupt() {
    if (!nvic_) {
        return {};
    }
    if (in_handler_mode_) {
        return {};
    }
    if (primask_ & 1) {
        return {};
    }

    if (!nvic_->has_pending_irq()) {
        return {};
    }

    uint8_t irq_n = nvic_->highest_pending_irq();
    if (!nvic_->is_enabled(irq_n)) {
        return {};
    }

    return interrupt_entry(irq_n);
}

CPU::CPUExpected<void> CortexM3CPU::interrupt_entry(uint8_t irq_n) {
    // Push {xPSR, PC, LR, R12, R3, R2, R1, R0} (ARM spec order)
    auto push_one = [&](data_t val) -> CPUExpected<void> {
        auto r = push_stack(val);
        if (!r) {
            return std::unexpected{r.error()};
        }
        return {};
    };

    auto res = push_one(xpsr_);
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(15).value_or(0)); // PC
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(14).value_or(0)); // LR
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(12).value_or(0)); // R12
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(3).value_or(0));
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(2).value_or(0));
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(1).value_or(0));
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(0).value_or(0));
    if (!res) {
        return res;
    }

    // Set LR = EXC_RETURN
    data_t exc_return = in_handler_mode_ ? 0xFFFFFFF1u : 0xFFFFFFF9u;
    res = write_reg(14, exc_return);
    if (!res) {
        return res;
    }

    // Read handler from vector table
    addr_t handler_offset =
        vector_table_base_ + 4u * (static_cast<addr_t>(irq_n) + 16);
    if (!bus_) {
        return std::unexpected{CPUError::NextInstructionsUnavaliable};
    }
    auto handler = bus_->read(handler_offset, Width::Word);
    if (!handler) {
        return std::unexpected{CPUError::NextInstructionsUnavaliable};
    }

    // Vector entries carry the Thumb state in bit[0]; the architectural PC
    // stores the halfword-aligned execution address.
    res = write_reg(15, *handler & ~1u);
    if (!res) {
        return res;
    }

    // Enter Handler mode
    in_handler_mode_ = true;
    current_priority_ = nvic_->irq_priority(irq_n);

    // Clear pending in NVIC
    nvic_->clear_pending(irq_n);

    return {};
}

CPU::CPUExpected<void> CortexM3CPU::interrupt_return(data_t exc_return) {
    // Pop {R0, R1, R2, R3, R12, LR, PC, xPSR}
    auto pop_one = [&]() -> CPUExpected<data_t> {
        auto r = pop_stack();
        if (!r) {
            return std::unexpected{r.error()};
        }
        return *r;
    };

    auto pop_and_write = [&](uint8_t reg_idx) -> CPUExpected<void> {
        auto v = pop_one();
        if (!v) {
            return std::unexpected{v.error()};
        }
        return write_reg(reg_idx, *v);
    };

    auto res = pop_and_write(0);
    if (!res) {
        return res;
    }
    res = pop_and_write(1);
    if (!res) {
        return res;
    }
    res = pop_and_write(2);
    if (!res) {
        return res;
    }
    res = pop_and_write(3);
    if (!res) {
        return res;
    }
    res = pop_and_write(12);
    if (!res) {
        return res;
    }
    res = pop_and_write(14); // LR
    if (!res) {
        return res;
    }

    auto pc_val = pop_one();
    if (!pc_val) {
        return std::unexpected{pc_val.error()};
    }

    auto xpsr_val = pop_one();
    if (!xpsr_val) {
        return std::unexpected{xpsr_val.error()};
    }
    xpsr_ = *xpsr_val;

    // Set PC (clear bit[0] for address)
    res = write_reg(15, *pc_val & ~1u);
    if (!res) {
        return res;
    }

    // Exit Handler mode
    if (exc_return == 0xFFFFFFF1u) {
        // Return to Handler mode (nested) — keep in_handler_mode_ true
    } else {
        in_handler_mode_ = false;
        current_priority_ = 0xFF;
    }

    return {};
}

CPU::CPUExpected<void> CortexM3CPU::trigger_hardfault() {
    if (!nvic_ || in_handler_mode_) {
        return std::unexpected{CPUError::IllegalInstructions};
    }
    return interrupt_entry(3); // HardFault = exception number 3
}

} // namespace micro_forge::cpu::arm::cortex_m3
