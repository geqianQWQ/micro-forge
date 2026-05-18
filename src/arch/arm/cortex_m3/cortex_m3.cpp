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
        case 0x0: return Z;                  // EQ
        case 0x1: return !Z;                 // NE
        case 0x2: return C;                  // CS/HS
        case 0x3: return !C;                 // CC/LO
        case 0x4: return N;                  // MI
        case 0x5: return !N;                 // PL
        case 0x6: return V;                  // VS
        case 0x7: return !V;                 // VC
        case 0x8: return C && !Z;            // HI
        case 0x9: return !C || Z;            // LS
        case 0xA: return N == V;             // GE
        case 0xB: return N != V;             // LT
        case 0xC: return !Z && (N == V);     // GT
        case 0xD: return Z || (N != V);      // LE
        case 0xE: return true;               // AL
        default:  return false;              // 0xF
    }
}

// ── Stack operations ──

CPU::CPUExpected<void> CortexM3CPU::push_stack(data_t val) {
    auto sp_res = regs_.read(13);
    data_t sp = *sp_res - 4;
    if (bus_) {
        auto w = bus_->write(sp, val, Width::Word);
        if (!w) return std::unexpected{CPUError::NextInstructionsUnavaliable};
    }
    (void)regs_.write(13, sp);
    return {};
}

CPU::CPUExpected<data_t> CortexM3CPU::pop_stack() {
    data_t sp = *regs_.read(13);
    data_t val = 0;
    if (bus_) {
        auto r = bus_->read(sp, Width::Word);
        if (!r) return std::unexpected{CPUError::NextInstructionsUnavaliable};
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
    if (!lo) return std::unexpected{lo.error()};
    auto hi = bus_->read(addr + 1, Width::Byte);
    if (!hi) return std::unexpected{hi.error()};
    return static_cast<uint16_t>(*lo | (*hi << 8));
}

// ── Step ──

CPU::CPUExpected<void> CortexM3CPU::step() {
    if (current_status_ != State::Running) {
        return std::unexpected{CPUError::NotRunning};
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

    if (is_32bit_prefix_instruction(hw1)) {
        auto hw2_res = fetch16(pc + 2);
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
        current_status_ = State::Faulted;
        return exec_res;
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
        if (!res) return std::unexpected{res.error()};
        return {};
    };
    // Bus read helper: returns error on failure
    auto br = [&](addr_t addr, Width w) -> CPUExpected<data_t> {
        if (!bus_) return std::unexpected{CPUError::NextInstructionsUnavaliable};
        auto v = bus_->read(addr, w);
        if (!v) return std::unexpected{CPUError::NextInstructionsUnavaliable};
        return *v;
    };
    // Bus write helper: returns error on failure
    auto bw = [&](addr_t addr, data_t val, Width w) -> CPUExpected<void> {
        if (!bus_) return std::unexpected{CPUError::NextInstructionsUnavaliable};
        auto v = bus_->write(addr, val, w);
        if (!v) return std::unexpected{CPUError::NextInstructionsUnavaliable};
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

        if (op == 0b00) {        // LSL
            result = imm == 0 ? val : val << imm;
        } else if (op == 0b01) { // LSR
            result = imm == 0 ? 0 : val >> imm;
        } else {                 // ASR
            result = (imm == 0)
                ? ((val & 0x80000000u) ? 0xFFFFFFFFu : 0)
                : static_cast<data_t>(static_cast<int32_t>(val) >> imm);
        }
        auto res = wr(rd, result);
        if (!res) return res;
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

        if (is_sub) update_flags(FlagPostOperation::Sub, a, b, result);
        else        update_flags(FlagPostOperation::Add, a, b, result);
        return wr(rd, result);
    }

    // ── MOVS Rd, imm8 ──
    case 0b00100: {
        uint8_t rd = rd8(insn);
        data_t val = imm8(insn);
        auto res = wr(rd, val);
        if (!res) return res;
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
            case 0b00: return wr(rd, rr(rd) + rr(rm));         // ADD high
            case 0b01: { // CMP high
                data_t a = rr(rd), b = rr(rm);
                update_flags(FlagPostOperation::Sub, a, b, a - b);
                return {};
            }
            case 0b10: return wr(rd, rr(rm));                   // MOV high
            case 0b11: return write_reg(15, rr(rm));            // BX
            }
        }
        // Data processing register
        uint8_t op = (insn >> 6) & 0xF;
        uint8_t rm = rm3(insn);
        uint8_t rd = rd3(insn);
        data_t a = rr(rd), b = rr(rm);
        data_t result;

        switch (op) {
        case 0x0: result = a & b; break;
        case 0x1: result = a ^ b; break;
        case 0x2: result = a << (b & 0xFF); break;
        case 0x3: result = a >> (b & 0xFF); break;
        case 0x4: result = static_cast<data_t>(static_cast<int32_t>(a) >> (b & 0xFF)); break;
        case 0x5: result = a + b + ((xpsr_ & PSR_C) ? 1 : 0); break;
        case 0x6: result = a - b - ((xpsr_ & PSR_C) ? 0 : 1); break;
        case 0x7: {
            uint8_t n = (b & 0xFF) & 0x1F;
            result = n ? ((a >> n) | (a << (32 - n))) : a;
            break;
        }
        case 0x8: update_nz(a & b); return {};     // TST
        case 0x9: result = -b; break;               // RSB
        case 0xA: update_flags(FlagPostOperation::Sub, a, b, a - b); return {};
        case 0xB: update_flags(FlagPostOperation::Add, a, b, a + b); return {};
        case 0xC: result = a | b; break;
        case 0xD: result = a * b; break;
        case 0xE: result = a & ~b; break;
        case 0xF: result = ~b; break;
        default:  return std::unexpected{CPUError::IllegalInstructions};
        }
        auto res = wr(rd, result);
        if (!res) return res;
        update_nz(result);
        break;
    }

    // ── LDR literal (PC-relative) ──
    case 0b01010: {
        uint8_t rt = rd3(insn);
        addr_t addr = ((rr(15) + 4) & ~0x3u) + imm8(insn) * 4;
        auto val = br(addr, Width::Word);
        if (!val) return std::unexpected{val.error()};
        return wr(rt, *val);
    }

    // ── Load/store register offset ──
    case 0b01011: {
        uint8_t op = (insn >> 9) & 0x7;
        uint8_t rm = rm3(insn), rn = rn3(insn), rt = rd3(insn);
        addr_t addr = rr(rn) + rr(rm);

        switch (op) {
        case 0b000: return bw(addr, rr(rt), Width::Word);
        case 0b001: return bw(addr, rr(rt) & 0xFFFF, Width::HalfWord);
        case 0b010: return bw(addr, rr(rt) & 0xFF, Width::Byte);
        case 0b011: { // LDRSB
            auto v = br(addr, Width::Byte);
            if (!v) return std::unexpected{v.error()};
            data_t val = *v;
            if (val & 0x80u) val |= 0xFFFFFF00u;
            return wr(rt, val);
        }
        case 0b100: { auto v = br(addr, Width::Word); if (!v) return std::unexpected{v.error()}; return wr(rt, *v); }
        case 0b101: { auto v = br(addr, Width::HalfWord); if (!v) return std::unexpected{v.error()}; return wr(rt, *v); }
        case 0b110: { auto v = br(addr, Width::Byte); if (!v) return std::unexpected{v.error()}; return wr(rt, *v); }
        case 0b111: { // LDRSH
            auto v = br(addr, Width::HalfWord);
            if (!v) return std::unexpected{v.error()};
            data_t val = *v;
            if (val & 0x8000u) val |= 0xFFFF0000u;
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
        if (!v) return std::unexpected{v.error()};
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
        if (!v) return std::unexpected{v.error()};
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
        if (!v) return std::unexpected{v.error()};
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
        if (!v) return std::unexpected{v.error()};
        return wr(rd8(insn), *v);
    }

    // ── PUSH ──
    case 0b10110: {
        if (!((insn >> 8) & 0x1))
            return std::unexpected{CPUError::IllegalInstructions};
        uint8_t rlist = reg_list(insn);
        bool m = m_bit(insn);
        int count = std::popcount(rlist) + (m ? 1 : 0);

        data_t sp = rr(13) - count * 4;
        [[maybe_unused]] auto _ = write_reg(13, sp);

        for (int i = 0; i < 8; i++) {
            if (rlist & (1 << i)) {
                auto res = bw(sp, rr(i), Width::Word);
                if (!res) return res;
                sp += 4;
            }
        }
        if (m) return bw(sp, rr(14), Width::Word);
        break;
    }

    // ── POP ──
    case 0b10111: {
        if (!((insn >> 8) & 0x1)) {
            if (insn == 0xBF00) break; // NOP
            return std::unexpected{CPUError::IllegalInstructions};
        }
        uint8_t rlist = reg_list(insn);
        bool m = m_bit(insn);
        data_t sp = rr(13);

        for (int i = 0; i < 8; i++) {
            if (rlist & (1 << i)) {
                auto v = br(sp, Width::Word);
                if (!v) return std::unexpected{v.error()};
                auto res = write_reg(i, *v);
                if (!res) return res;
                sp += 4;
            }
        }
        if (m) {
            auto v = br(sp, Width::Word);
            if (!v) return std::unexpected{v.error()};
            auto res = write_reg(15, *v);
            if (!res) return res;
            sp += 4;
        }
        [[maybe_unused]] auto _ = write_reg(13, sp);
        break;
    }

    // ── Conditional branch B<cond> ──
    case 0b11010: {
        uint8_t c = cond(insn);
        if (c == 0xE) return std::unexpected{CPUError::IllegalInstructions};
        if (c == 0xF) break; // SVC — Phase 3B
        if (condition_need_execute(c)) {
            int32_t offset = static_cast<int8_t>(imm8(insn));
            offset <<= 1;
            auto pc_res = read_pc_raw();
            if (!pc_res) return std::unexpected{pc_res.error()};
            return write_reg(15, *pc_res + 4 + offset);
        }
        break;
    }

    // ── B unconditional ──
    case 0b11100: {
        int32_t offset = static_cast<int16_t>(imm11(insn) << 5) >> 5;
        offset <<= 1;
        auto pc_res = read_pc_raw();
        if (!pc_res) return std::unexpected{pc_res.error()};
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
        if (!res) return std::unexpected{res.error()};
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

        uint32_t offset = (s << 24) | (i1 << 23) | (i2 << 22)
                        | (i10 << 12) | (i11 << 1);
        if (s) offset |= 0xFE000000u;

        auto pc_res = read_pc_raw();
        if (!pc_res) return std::unexpected{pc_res.error()};
        data_t next_pc = *pc_res + 4;

        auto lr_res = wr(14, next_pc);
        if (!lr_res) return lr_res;

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
        data_t val = (rr(rd) & 0x0000FFFFu) | (static_cast<data_t>(imm16) << 16);
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

    return std::unexpected{CPUError::IllegalInstructions};
}

} // namespace micro_forge::cpu::arm::cortex_m3
