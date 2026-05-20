#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/def.h"
#include "arch/arm/cortex_m3/thumb32_fields.hpp"
#include "core/types.hpp"
#include "util/logger.hpp"
#include <bit>
#include <expected>

namespace micro_forge::cpu::arm::cortex_m3 {

using namespace thumb;

namespace {

const char* cpu_error_name(CPU::CPUError error) {
    switch (error) {
        case CPU::CPUError::IllegalInstruction:
            return "IllegalInstruction";
        case CPU::CPUError::DataAccessFault:
            return "DataAccessFault";
        case CPU::CPUError::InstructionFetchFault:
            return "InstructionFetchFault";
        case CPU::CPUError::InvalidPc:
            return "InvalidPc";
        case CPU::CPUError::ExceptionEntryFault:
            return "ExceptionEntryFault";
        case CPU::CPUError::ExceptionReturnFault:
            return "ExceptionReturnFault";
        case CPU::CPUError::NotRunning:
            return "NotRunning";
        case CPU::CPUError::RegisterIndexOverflow:
            return "RegisterIndexOverflow";
        case CPU::CPUError::FailedPollIntr:
            return "FailedPollIntr";
    }
    return "Unknown";
}

} // namespace

// ── Reset & ICore interface ──

CPU::CPUExpected<void> CortexM3CPU::reset() {
    regs_.reset();
    xpsr_ = PSR_T; // Thumb mode always on
    primask_ = 0;
    basepri_ = 0;
    faultmask_ = 0;
    control_ = 0;
    msp_ = 0;
    psp_ = 0;
    it_conditions_.clear();
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
        return std::unexpected{CPUError::InvalidPc};
    }
    return *result;
}

CPU::CPUExpected<void> CortexM3CPU::write_reg(uint8_t index, data_t value) {
    if (index == 13) {
        value &= ~0x3u; // SP: low 2 bits forced to 0
        if (!in_handler_mode_ && (control_ & 0x2u)) {
            psp_ = value;
        } else {
            msp_ = value;
        }
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
            return std::unexpected{CPUError::DataAccessFault};
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
            return std::unexpected{CPUError::DataAccessFault};
        }
        val = *r;
    }
    (void)regs_.write(13, sp + 4);
    return val;
}

// ── Fetch ──

Expected<uint16_t> CortexM3CPU::fetch16(addr_t addr) {
    if (!bus_) {
        return std::unexpected{BusError::InvalidDevice};
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
        record_fault(CPUError::InstructionFetchFault, pc, 0, 0, false);
        LOG_ERROR("fault",
                  "PC=0x%08X opcode=0x%04X kind=%s detail=fetch",
                  pc, 0, cpu_error_name(CPUError::InstructionFetchFault));
        return std::unexpected{CPUError::InstructionFetchFault};
    }
    uint16_t hw1 = *hw1_res;

    CPUExpected<void> exec_res;
    Expected<uint16_t> hw2_res;
    bool execute_instruction = true;
    if (!it_conditions_.empty()) {
        uint8_t it_cond = it_conditions_.front();
        it_conditions_.erase(it_conditions_.begin());
        execute_instruction = condition_need_execute(it_cond);
    }

    if (is_32bit_prefix_instruction(hw1)) {
        hw2_res = fetch16(pc + 2);
        if (!hw2_res) {
            current_status_ = State::Faulted;
            record_fault(CPUError::InstructionFetchFault, pc, hw1, 0, true);
            LOG_ERROR("fault",
                      "PC=0x%08X opcode=0x%04X%04X kind=%s detail=fetch-hw2",
                      pc, hw1, 0,
                      cpu_error_name(CPUError::InstructionFetchFault));
            return std::unexpected{CPUError::InstructionFetchFault};
        }
        exec_res = execute_instruction ? execute_32bit(hw1, *hw2_res)
                                       : CPUExpected<void>{};
        if (exec_res.has_value()) {
            auto new_pc = read_pc_raw();
            if (new_pc && *new_pc == pc) {
                (void)write_reg(15, pc + 4);
            }
        }
    } else {
        exec_res = execute_instruction ? execute_16bit(hw1) : CPUExpected<void>{};
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

        if (is32) {
            uint32_t insn32 =
                (static_cast<uint32_t>(hw1) << 16) | hw2_val;
            LOG_ERROR("fault",
                      "PC=0x%08X opcode=0x%08X hw1=0x%04X hw2=0x%04X kind=%s",
                      pc, insn32, hw1, hw2_val,
                      cpu_error_name(exec_res.error()));
        } else {
            LOG_ERROR("fault", "PC=0x%08X opcode=0x%04X kind=%s", pc, hw1,
                      cpu_error_name(exec_res.error()));
        }

        if (probe_mode_) {
            missing_opcodes_.emplace_back(pc, hw1, hw2_val);
            (void)write_reg(15, pc + (is32 ? 4 : 2));
        } else {
            record_fault(exec_res.error(), pc, hw1, hw2_val, is32);
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
            return std::unexpected{CPUError::DataAccessFault};
        }
        auto v = bus_->read(addr, w);
        if (!v) {
            return std::unexpected{CPUError::DataAccessFault};
        }
        return *v;
    };
    // Bus write helper: returns error on failure
    auto bw = [&](addr_t addr, data_t val, Width w) -> CPUExpected<void> {
        if (!bus_) {
            return std::unexpected{CPUError::DataAccessFault};
        }
        auto v = bus_->write(addr, val, w);
        if (!v) {
            return std::unexpected{CPUError::DataAccessFault};
        }
        return {};
    };

    // ── CPSIE i / CPSID i ──
    if ((insn & 0xFFF0u) == 0xB660u) {
        primask_ &= ~1u;
        return {};
    }
    if ((insn & 0xFFF0u) == 0xB670u) {
        primask_ |= 1u;
        return {};
    }

    // ── Compare and branch on zero/non-zero (CBZ / CBNZ) ──
    // Encoding: 1011 op 0 i 1 imm5 Rn
    if ((insn & 0xF500u) == 0xB100u) {
        uint8_t rn = insn & 0x7u;
        bool non_zero = insn & 0x0800u;
        uint32_t offset = (((insn >> 9) & 0x1u) << 6) |
                          (((insn >> 3) & 0x1Fu) << 1);
        bool branch = non_zero ? (rr(rn) != 0) : (rr(rn) == 0);
        if (branch) {
            auto pc_res = read_pc_raw();
            if (!pc_res) {
                return std::unexpected{pc_res.error()};
            }
            return write_reg(15, *pc_res + 4 + offset);
        }
        return {};
    }

    // ── Sign/zero extend byte/halfword ──
    if ((insn & 0xFF00u) == 0xB200u) {
        uint8_t rm = (insn >> 3) & 0x7u;
        uint8_t rd = insn & 0x7u;
        uint8_t op = (insn >> 6) & 0x3u;
        data_t value = rr(rm);
        switch (op) {
            case 0b00:
                return wr(rd, static_cast<data_t>(
                                static_cast<int32_t>(
                                    static_cast<int16_t>(value & 0xFFFFu))));
            case 0b01:
                return wr(rd, static_cast<data_t>(
                                static_cast<int32_t>(
                                    static_cast<int8_t>(value & 0xFFu))));
            case 0b10:
                return wr(rd, value & 0xFFFFu);
            case 0b11:
                return wr(rd, value & 0xFFu);
        }
    }

    // ── Byte-reversal instructions ──
    if ((insn & 0xFF00u) == 0xBA00u) {
        uint8_t rm = (insn >> 3) & 0x7u;
        uint8_t rd = insn & 0x7u;
        uint8_t op = (insn >> 6) & 0x3u;
        data_t value = rr(rm);
        switch (op) {
            case 0b00:
                return wr(rd, ((value & 0x000000FFu) << 24) |
                                  ((value & 0x0000FF00u) << 8) |
                                  ((value & 0x00FF0000u) >> 8) |
                                  ((value & 0xFF000000u) >> 24));
            case 0b01:
                return wr(rd, ((value & 0x00FF00FFu) << 8) |
                                  ((value & 0xFF00FF00u) >> 8));
            case 0b11: {
                uint32_t rev_half = ((value & 0x00FFu) << 8) |
                                    ((value & 0xFF00u) >> 8);
                return wr(rd, static_cast<data_t>(
                                  static_cast<int32_t>(
                                      static_cast<int16_t>(rev_half))));
            }
            default:
                return std::unexpected{CPUError::IllegalInstruction};
        }
    }

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
                    return std::unexpected{CPUError::IllegalInstruction};
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
                if ((insn & 0xFF00u) == 0xBF00u && (insn & 0xFu) != 0) {
                    uint8_t first_cond = (insn >> 4) & 0xFu;
                    uint8_t mask = insn & 0xFu;
                    if (first_cond == 0xFu) {
                        return std::unexpected{CPUError::IllegalInstruction};
                    }
                    int count = 4 - std::countr_zero(static_cast<unsigned>(mask));
                    it_conditions_.clear();
                    it_conditions_.reserve(static_cast<std::size_t>(count));
                    for (int slot = 0; slot < count; ++slot) {
                        if (slot == 0) {
                            it_conditions_.push_back(first_cond);
                            continue;
                        }
                        uint8_t bit = 1u << (3 - slot);
                        bool then_path = (mask & bit) != 0;
                        it_conditions_.push_back(then_path ? first_cond
                                                           : (first_cond ^ 1u));
                    }
                }
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
                return std::unexpected{CPUError::IllegalInstruction};
            }
            if (c == 0xF) {
                auto pc_res = read_pc_raw();
                if (!pc_res) {
                    return std::unexpected{pc_res.error()};
                }
                auto pc_write = write_reg(15, *pc_res + 2);
                if (!pc_write) {
                    return pc_write;
                }
                return interrupt_entry_system(11);
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
            return std::unexpected{CPUError::IllegalInstruction};
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
    auto br = [&](addr_t addr, Width w) -> CPUExpected<data_t> {
        if (!bus_) {
            return std::unexpected{CPUError::DataAccessFault};
        }
        auto v = bus_->read(addr, w);
        if (!v) {
            return std::unexpected{CPUError::DataAccessFault};
        }
        return *v;
    };
    auto bw = [&](addr_t addr, data_t val, Width w) -> CPUExpected<void> {
        if (!bus_) {
            return std::unexpected{CPUError::DataAccessFault};
        }
        auto v = bus_->write(addr, val, w);
        if (!v) {
            return std::unexpected{CPUError::DataAccessFault};
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

    // ── B.W T3 (conditional branch) ──
    if ((hw1 & 0xF800) == 0xF000 && (hw2 & 0xD000) == 0x8000 &&
        (((hw1 >> 6) & 0xFu) < 0xEu)) {
        uint8_t c = (hw1 >> 6) & 0xFu;
        if (!condition_need_execute(c)) {
            return {};
        }

        uint32_t s = (hw1 >> 10) & 0x1u;
        uint32_t j1_val = thumb32::j1(hw2);
        uint32_t j2_val = thumb32::j2(hw2);
        uint32_t imm6 = hw1 & 0x3Fu;
        uint32_t imm11 = thumb32::hw2_imm11(hw2);
        uint32_t offset =
            (s << 20) | (j2_val << 19) | (j1_val << 18) | (imm6 << 12) |
            (imm11 << 1);
        if (s) {
            offset |= 0xFFE00000u;
        }

        auto pc_res = read_pc_raw();
        if (!pc_res) {
            return std::unexpected{pc_res.error()};
        }
        return write_reg(15, *pc_res + 4 + offset);
    }

    // ── B.W T4 (unconditional branch) ──
    if ((hw1 & 0xF800) == 0xF000 && (hw2 & 0xD000) == 0x9000) {
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

    // ── DMB / DSB / ISB ──
    if (hw1 == 0xF3BF && (hw2 & 0xFF0Fu) == 0x8F0Fu) {
        uint8_t option = hw2 & 0xFu;
        uint8_t op = (hw2 >> 4) & 0xFu;
        if (option != 0xFu || (op != 0x4u && op != 0x5u && op != 0x6u)) {
            return std::unexpected{CPUError::IllegalInstruction};
        }
        return {};
    }

    auto read_special = [&]() -> data_t {
        uint8_t sysm = hw2 & 0xFFu;
        switch (sysm) {
            case 0x00: return xpsr_ & (PSR_N | PSR_Z | PSR_C | PSR_V);
            case 0x08: return msp_;
            case 0x09: return psp_;
            case 0x10: return primask_;
            case 0x11: return basepri_;
            case 0x13: return faultmask_;
            case 0x14: return control_;
            default: return 0;
        }
    };

    auto write_special = [&](data_t value) -> CPUExpected<void> {
        uint8_t sysm = hw2 & 0xFFu;
        switch (sysm) {
            case 0x00:
                xpsr_ = (xpsr_ & ~(PSR_N | PSR_Z | PSR_C | PSR_V)) |
                        (value & (PSR_N | PSR_Z | PSR_C | PSR_V)) | PSR_T;
                return {};
            case 0x08:
                msp_ = value & ~0x3u;
                if (in_handler_mode_ || !(control_ & 0x2u)) {
                    return write_reg(13, msp_);
                }
                return {};
            case 0x09:
                psp_ = value & ~0x3u;
                if (!in_handler_mode_ && (control_ & 0x2u)) {
                    return write_reg(13, psp_);
                }
                return {};
            case 0x10:
                primask_ = value & 1u;
                return {};
            case 0x11:
                basepri_ = value & 0xFFu;
                return {};
            case 0x13:
                faultmask_ = value & 1u;
                return {};
            case 0x14: {
                control_ = value & 0x3u;
                data_t active_sp = (!in_handler_mode_ && (control_ & 0x2u))
                                       ? psp_
                                       : msp_;
                return write_reg(13, active_sp);
            }
            default:
                return std::unexpected{CPUError::IllegalInstruction};
        }
    };

    // ── MRS ──
    if ((hw1 & 0xFFF0) == 0xF3E0 && (hw2 & 0xF000) == 0x8000) {
        return wr(thumb32::hw2_rd4(hw2), read_special());
    }

    // ── MSR ──
    if ((hw1 & 0xFFF0) == 0xF380 && (hw2 & 0xFF00) == 0x8800) {
        return write_special(rr(hw1 & 0xFu));
    }

    // ── BFI / BFC ──
    if ((hw1 & 0xFB70u) == 0xF360u) {
        uint8_t rn = hw1 & 0xFu;
        uint8_t rd = (hw2 >> 8) & 0xFu;
        uint8_t lsb = (((hw2 >> 12) & 0x7u) << 2) | ((hw2 >> 6) & 0x3u);
        uint8_t msb = hw2 & 0x1Fu;
        if (msb < lsb) {
            return std::unexpected{CPUError::IllegalInstruction};
        }
        uint32_t width = static_cast<uint32_t>(msb - lsb + 1);
        uint32_t field_mask = width == 32 ? 0xFFFFFFFFu : ((1u << width) - 1u);
        uint32_t mask = field_mask << lsb;
        uint32_t src = (rn == 15) ? 0u : (rr(rn) << lsb);
        return wr(rd, (rr(rd) & ~mask) | (src & mask));
    }

    // ── SBFX / UBFX ──
    if ((hw1 & 0xFB70u) == 0xF340u || (hw1 & 0xFB70u) == 0xF3C0u) {
        bool is_unsigned = (hw1 & 0x0080u) != 0;
        uint8_t rn = hw1 & 0xFu;
        uint8_t rd = (hw2 >> 8) & 0xFu;
        uint8_t lsb = (((hw2 >> 12) & 0x7u) << 2) | ((hw2 >> 6) & 0x3u);
        uint8_t width = (hw2 & 0x1Fu) + 1u;
        if (lsb + width > 32) {
            return std::unexpected{CPUError::IllegalInstruction};
        }
        uint32_t raw = rr(rn) >> lsb;
        uint32_t mask = width == 32 ? 0xFFFFFFFFu : ((1u << width) - 1u);
        uint32_t result = raw & mask;
        if (!is_unsigned && width < 32 && (result & (1u << (width - 1u)))) {
            result |= ~mask;
        }
        return wr(rd, result);
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
                result = rn_val + imm32 + ((xpsr_ & PSR_C) ? 1u : 0u);
                break; // ADC
            case 11: {
                uint32_t borrow = (xpsr_ & PSR_C) ? 0u : 1u;
                result = rn_val - imm32 - borrow;
                break; // SBC
            }
            case 13:
                result = rn_val - imm32;
                break; // SUB
            case 14:
                result = imm32 - rn_val;
                break; // RSB
            default:
                return std::unexpected{CPUError::IllegalInstruction};
        }

        if (s_bit) {
            if (op2 == 8 || op2 == 10 || op2 == 13 || op2 == 14 || op2 == 11) {
                uint32_t flag_rhs =
                    op2 == 10 ? imm32 + ((xpsr_ & PSR_C) ? 1u : 0u)
                    : op2 == 11 ? imm32 + ((xpsr_ & PSR_C) ? 0u : 1u)
                                : imm32;
                update_flags(op2 <= 10 ? FlagPostOperation::Add
                                       : FlagPostOperation::Sub,
                             rn_val, flag_rhs, result);
            } else {
                update_nz(result);
            }
        }
        // CMP/CMN/TST/TEQ: S=1, Rd=15 → flags only, no register write
        if (s_bit && rd == 15) {
            return {};
        }
        return wr(rd, result);
    }

    // ── Load/Store immediate (LDR[B/H].W / STR[B/H].W / LDR.W / STR.W) ──
    if ((hw1 & 0xFF00) == 0xF800) {
        uint8_t rn = hw1 & 0xF;
        bool load = (hw1 >> 4) & 1;
        uint8_t size = (hw1 >> 5) & 0x3;
        uint8_t rt = (hw2 >> 12) & 0xF;
        uint8_t sub_op = (hw2 >> 8) & 0xF;
        uint32_t imm8 = hw2 & 0xFF;
        uint32_t rn_val = rr(rn);
        Width width;
        switch (size) {
            case 0: width = Width::Byte; break;
            case 1: width = Width::HalfWord; break;
            case 2: width = Width::Word; break;
            default: return std::unexpected{CPUError::IllegalInstruction};
        }

        if (sub_op == 0x0) { // offset: addr = Rn + imm8, no writeback
            addr_t addr = rn_val + imm8;
            if (load) {
                auto r = bus_->read(addr, width);
                if (!r) {
                    return std::unexpected{
                        CPUError::DataAccessFault};
                }
                return wr(rt, *r);
            } else {
                if (!bus_->write(addr, rr(rt), width)) {
                    return std::unexpected{
                        CPUError::DataAccessFault};
                }
                return {};
            }
        }

        if (sub_op == 0xB) { // post-index: op, then Rn += imm8
            addr_t addr = rn_val;
            if (load) {
                auto r = bus_->read(addr, width);
                if (!r) {
                    return std::unexpected{
                        CPUError::DataAccessFault};
                }
                auto w = wr(rt, *r);
                if (!w) {
                    return w;
                }
            } else {
                if (!bus_->write(addr, rr(rt), width)) {
                    return std::unexpected{
                        CPUError::DataAccessFault};
                }
            }
            return wr(rn, rn_val + imm8);
        }

        if (sub_op == 0xF) { // pre-index: addr = Rn + imm8, op, Rn = addr
            addr_t addr = rn_val + imm8;
            if (load) {
                auto r = bus_->read(addr, width);
                if (!r) {
                    return std::unexpected{
                        CPUError::DataAccessFault};
                }
                auto w = wr(rt, *r);
                if (!w) {
                    return w;
                }
            } else {
                if (!bus_->write(addr, rr(rt), width)) {
                    return std::unexpected{
                        CPUError::DataAccessFault};
                }
            }
            return wr(rn, addr);
        }

        return std::unexpected{CPUError::IllegalInstruction};
    }

    // ── UDIV / SDIV ──
    if ((hw1 & 0xFFD0) == 0xFB90 && (hw2 & 0xF0F0) == 0xF0F0) {
        uint8_t rn = hw1 & 0xF;
        uint8_t rm = hw2 & 0xF;
        uint8_t rd = (hw2 >> 8) & 0xF;
        bool is_signed = (hw1 & 0x0020u) == 0;
        if (is_signed) {
            int32_t a = static_cast<int32_t>(rr(rn));
            int32_t b = static_cast<int32_t>(rr(rm));
            if (b == 0) return wr(rd, 0);
            return wr(rd, static_cast<uint32_t>(a / b));
        }
        uint32_t a = rr(rn);
        uint32_t b = rr(rm);
        if (b == 0) return wr(rd, 0);
        return wr(rd, a / b);
    }

    // ── MLA / MLS ──
    if ((hw1 & 0xFFF0u) == 0xFB00u &&
        ((hw2 & 0x00F0u) == 0x0000u || (hw2 & 0x00F0u) == 0x0010u)) {
        uint8_t rn = hw1 & 0xFu;
        uint8_t rm = hw2 & 0xFu;
        uint8_t rd = (hw2 >> 8) & 0xFu;
        uint8_t ra = (hw2 >> 12) & 0xFu;
        uint32_t product = rr(rn) * rr(rm);
        uint32_t result = (hw2 & 0x0010u) ? (rr(ra) - product)
                                          : (product + rr(ra));
        return wr(rd, result);
    }

    // ── SMULL / UMULL ──
    if (((hw1 & 0xFFF0u) == 0xFB80u || (hw1 & 0xFFF0u) == 0xFBA0u) &&
        (hw2 & 0x00F0u) == 0x0000u) {
        uint8_t rn = hw1 & 0xFu;
        uint8_t rm = hw2 & 0xFu;
        uint8_t rdlo = (hw2 >> 12) & 0xFu;
        uint8_t rdhi = (hw2 >> 8) & 0xFu;
        uint64_t result;
        if ((hw1 & 0xFFF0u) == 0xFB80u) {
            result = static_cast<uint64_t>(
                static_cast<int64_t>(static_cast<int32_t>(rr(rn))) *
                static_cast<int64_t>(static_cast<int32_t>(rr(rm))));
        } else {
            result = static_cast<uint64_t>(rr(rn)) *
                     static_cast<uint64_t>(rr(rm));
        }
        auto lo = wr(rdlo, static_cast<uint32_t>(result));
        if (!lo) return lo;
        return wr(rdhi, static_cast<uint32_t>(result >> 32));
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
            default: return std::unexpected{CPUError::IllegalInstruction};
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
            default: return std::unexpected{CPUError::IllegalInstruction};
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

    // ── Shift register (LSL/LSR/ASR/ROR register) ──
    if ((hw1 & 0xFF00) == 0xFA00 && (hw2 & 0xF0F0) == 0xF000) {
        uint8_t rn = hw1 & 0xF;
        bool s_bit = (hw1 >> 4) & 1;
        uint8_t shift_type = (hw1 >> 5) & 0x3;
        uint8_t rd = (hw2 >> 8) & 0xF;
        uint8_t rm = hw2 & 0xF;

        uint32_t value = rr(rn);
        uint32_t shift = rr(rm) & 0xFFu;
        uint32_t result = value;

        switch (shift_type) {
            case 0: // LSL
                result = shift == 0 ? value : (shift < 32 ? value << shift : 0);
                break;
            case 1: // LSR
                result = shift == 0 ? value : (shift < 32 ? value >> shift : 0);
                break;
            case 2: // ASR
                if (shift == 0) {
                    result = value;
                } else if (shift >= 32) {
                    result = (value & 0x80000000u) ? 0xFFFFFFFFu : 0;
                } else {
                    result = static_cast<uint32_t>(
                        static_cast<int32_t>(value) >> shift);
                }
                break;
            case 3: { // ROR
                uint32_t rot = shift & 31u;
                result = rot == 0 ? value : ((value >> rot) | (value << (32 - rot)));
                break;
            }
        }

        auto w = wr(rd, result);
        if (!w) {
            return w;
        }
        if (s_bit) {
            update_nz(result);
        }
        return {};
    }

    // ── TBB / TBH (Table Branch) ──
    if ((hw1 & 0xFFF0) == 0xE8D0 && (hw2 & 0xF0F0) == 0xF000) {
        uint8_t rn = hw1 & 0xF;
        uint8_t rm = hw2 & 0xF;
        bool H = (hw2 >> 4) & 1;

        uint32_t pc_val = rr(15) + 4;
        uint32_t base = (rn == 15) ? pc_val : rr(rn);
        uint32_t index = (rm == 15) ? 0u : rr(rm);

        uint32_t halfwords;
        if (H) {
            auto v = br(base + index * 2, Width::HalfWord);
            if (!v) return std::unexpected{v.error()};
            halfwords = *v;
        } else {
            auto v = br(base + index, Width::Byte);
            if (!v) return std::unexpected{v.error()};
            halfwords = *v;
        }

        addr_t target = pc_val + halfwords * 2;
        return write_pc(target);
    }

    // ── STRD / LDRD (Store/Load Dual, immediate offset) ──
    if ((hw1 & 0xFE40) == 0xE840) {
        bool P = (hw1 >> 8) & 1;
        bool U = (hw1 >> 7) & 1;
        bool W = (hw1 >> 5) & 1;
        bool L = (hw1 >> 4) & 1;
        uint8_t rn = hw1 & 0xF;
        uint8_t rt = (hw2 >> 12) & 0xF;
        uint8_t rt2 = (hw2 >> 8) & 0xF;
        uint32_t offset = static_cast<uint32_t>((hw2 & 0xFF)) * 4;

        uint32_t rn_val = rr(rn);
        addr_t offset_addr = U ? (rn_val + offset) : (rn_val - offset);
        addr_t addr = P ? offset_addr : rn_val;

        if (L) {
            auto v1 = br(addr, Width::Word);
            if (!v1) return std::unexpected{v1.error()};
            auto v2 = br(addr + 4, Width::Word);
            if (!v2) return std::unexpected{v2.error()};
            auto w1 = wr(rt, *v1);
            if (!w1) return w1;
            auto w2 = wr(rt2, *v2);
            if (!w2) return w2;
        } else {
            auto w1 = bw(addr, rr(rt), Width::Word);
            if (!w1) return w1;
            auto w2 = bw(addr + 4, rr(rt2), Width::Word);
            if (!w2) return w2;
        }

        if (W) {
            return wr(rn, offset_addr);
        }
        return {};
    }

    // ── STM / LDM (Store/Load Multiple) ──
    if ((hw1 & 0xFE40) == 0xE800) {
        bool U = (hw1 >> 7) & 1;
        bool W = (hw1 >> 5) & 1;
        bool L = (hw1 >> 4) & 1;
        uint8_t rn = hw1 & 0xF;
        uint16_t rlist = hw2;

        int count = std::popcount(rlist);
        if (count == 0) {
            return std::unexpected{CPUError::IllegalInstruction};
        }

        uint32_t rn_val = rr(rn);
        bool decrement = !U;
        addr_t start_addr =
            decrement ? rn_val - static_cast<uint32_t>(count * 4) : rn_val;
        addr_t addr = start_addr;

        if (L) {
            for (int i = 0; i < 16; i++) {
                if (rlist & (1 << i)) {
                    auto v = br(addr, Width::Word);
                    if (!v) return std::unexpected{v.error()};
                    if (i == 15) {
                        auto w = write_pc(*v);
                        if (!w) return w;
                    } else {
                        auto w = wr(i, *v);
                        if (!w) return w;
                    }
                    addr += 4;
                }
            }
        } else {
            for (int i = 0; i < 16; i++) {
                if (rlist & (1 << i)) {
                    data_t val = (i == 15) ? (rr(15) + 4) : rr(i);
                    auto w = bw(addr, val, Width::Word);
                    if (!w) return w;
                    addr += 4;
                }
            }
        }

        if (W) {
            uint32_t new_rn = decrement
                                  ? rn_val - static_cast<uint32_t>(count * 4)
                                  : rn_val + static_cast<uint32_t>(count * 4);
            return wr(rn, new_rn);
        }
        return {};
    }

    return std::unexpected{CPUError::IllegalInstruction};
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
    if (in_handler_mode_) {
        return {};
    }
    if (primask_ & 1) {
        return {};
    }
    if (faultmask_ & 1) {
        return {};
    }

    // SysTick is a system exception (vector index 15), bypasses NVIC
    if (pending_sys_tick_) {
        pending_sys_tick_ = false;
        return interrupt_entry_system(15);
    }

    if (!nvic_) {
        return {};
    }
    if (!nvic_->has_pending_irq()) {
        return {};
    }

    uint8_t irq_n = nvic_->highest_pending_irq();
    if (!nvic_->is_enabled(irq_n)) {
        return {};
    }
    if (basepri_ != 0 && nvic_->irq_priority(irq_n) >= basepri_) {
        return {};
    }

    return interrupt_entry(irq_n);
}

CPU::CPUExpected<void> CortexM3CPU::interrupt_entry(uint8_t irq_n) {
    // Push {xPSR, PC, LR, R12, R3, R2, R1, R0} (ARM spec order)
    auto push_one = [&](data_t val) -> CPUExpected<void> {
        auto r = push_stack(val);
        if (!r) {
            return std::unexpected{CPUError::ExceptionEntryFault};
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

    // Read handler from vector table: NVIC IRQs start at vector index 16
    addr_t handler_offset = vector_table_base_ + 4u * (static_cast<addr_t>(irq_n) + 16);
    if (!bus_) {
        return std::unexpected{CPUError::ExceptionEntryFault};
    }
    auto handler = bus_->read(handler_offset, Width::Word);
    if (!handler) {
        return std::unexpected{CPUError::ExceptionEntryFault};
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
            return std::unexpected{CPUError::ExceptionReturnFault};
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

CPU::CPUExpected<void> CortexM3CPU::interrupt_entry_system(uint8_t exception_num) {
    auto push_one = [&](data_t val) -> CPUExpected<void> {
        auto r = push_stack(val);
        if (!r) {
            return std::unexpected{CPUError::ExceptionEntryFault};
        }
        return {};
    };

    auto res = push_one(xpsr_);
    if (!res) return res;
    res = push_one(regs_.read(15).value_or(0));
    if (!res) return res;
    res = push_one(regs_.read(14).value_or(0));
    if (!res) return res;
    res = push_one(regs_.read(12).value_or(0));
    if (!res) return res;
    res = push_one(regs_.read(3).value_or(0));
    if (!res) return res;
    res = push_one(regs_.read(2).value_or(0));
    if (!res) return res;
    res = push_one(regs_.read(1).value_or(0));
    if (!res) return res;
    res = push_one(regs_.read(0).value_or(0));
    if (!res) return res;

    data_t exc_return = in_handler_mode_ ? 0xFFFFFFF1u : 0xFFFFFFF9u;
    res = write_reg(14, exc_return);
    if (!res) return res;

    // System exceptions use vector index == exception_num directly
    addr_t handler_offset = vector_table_base_ + 4u * exception_num;
    if (!bus_)
        return std::unexpected{CPUError::ExceptionEntryFault};
    auto handler = bus_->read(handler_offset, Width::Word);
    if (!handler) {
        return std::unexpected{CPUError::ExceptionEntryFault};
    }

    res = write_reg(15, *handler & ~1u);
    if (!res) return res;

    in_handler_mode_ = true;
    current_priority_ = 0; // System exceptions have fixed priority
    return {};
}

CPU::CPUExpected<void> CortexM3CPU::trigger_hardfault() {
    if (!nvic_ || in_handler_mode_) {
        return std::unexpected{CPUError::IllegalInstruction};
    }
    return interrupt_entry(3); // HardFault = exception number 3
}

} // namespace micro_forge::cpu::arm::cortex_m3
