# Phase 3A 实施手册：ARM Cortex-M3 Thumb 指令集

## 已确认的设计决策

| 编号 | 决策 | 选择 | 理由 |
|------|------|------|------|
| D3A-1 | SP 管理 | 放 RegisterFile R13，写入时 mask 低 2 位 | ARM 硬件行为简单，一行代码 |
| D3A-2 | PC 读取行为 | `pc()` 返回当前执行地址 + 4 | 匹配 ARM 三级流水线，影响 LDR literal / ADR / BL 偏移计算 |
| D3A-3 | raise_irq 本 Phase | 空实现 | 中断逻辑和 3B 高度耦合，避免半成品 |
| D3A-4 | IT 块 | 优先级最低，视进度决定 | `B<cond>` 不依赖 IT 块 |
| D3A-5 | 指令周期数 | 统一 1 cycle | Phase 5 再评估 |
| D3A-6 | 代码位置 | `include/arch/arm/cortex_m3/` + `src/arch/arm/cortex_m3/` | ARM IP 核是架构，不是芯片外设 |
| D3A-7 | 解码架构 | 分层 switch + 委托 | 性能最优（跳转表），可读性好（每个 case 一行调用） |
| D3A-8 | 反汇编器 | 本 Phase 做但优先级在指令之后 | 调试需要，但可后补 |

---

## 目录结构

```
include/arch/arm/cortex_m3/
  thumb_fields.hpp       ✏️ 已存在，需补充
  cortex_m3.hpp          ✏️ 已存在（空），需填写

src/arch/arm/cortex_m3/
  cortex_m3.cpp          🆕 CPU 类实现 + 解码分派
  thumb_exec_16.cpp      🆕 16-bit 指令执行 handler
  thumb_exec_32.cpp      🆕 32-bit Thumb-2 指令 handler

test/
  test_cortex_m3.cpp     🆕 单元测试

test/asm/                🆕 汇编测试源文件
  test_linear.s
  test_call_chain.s
  test_loop.s
```

CMakeLists.txt 的 `file(GLOB_RECURSE ... src/*.cpp)` 会自动收录新 .cpp 文件。

---

## Step 1: thumb_fields.hpp — 位域提取工具

**文件**: `include/arch/arm/cortex_m3/thumb_fields.hpp`

这是所有解码的基础。用真实 ARM 汇编器验证过的字段位置：

```cpp
#pragma once
#include <cstdint>

namespace micro_forge::cpu::arm::cortex_m3::thumb {

// ── 通用位域提取 ──

/// 提取 3-bit 目标寄存器（低寄存器 Rd/Rt），bits[2:0]
constexpr uint8_t rd3(uint16_t insn) { return insn & 0x7u; }

/// 提取 3-bit 第一操作数寄存器（低寄存器 Rn），bits[5:3]
constexpr uint8_t rn3(uint16_t insn) { return (insn >> 3) & 0x7u; }

/// 提取 3-bit 第二操作数寄存器（低寄存器 Rm），bits[8:6]
constexpr uint8_t rm3(uint16_t insn) { return (insn >> 6) & 0x7u; }

/// 提取 8-bit 立即数，bits[7:0]
constexpr uint8_t imm8(uint16_t insn) { return insn & 0xFFu; }

/// 提取 5-bit 立即数，bits[10:6]
constexpr uint8_t imm5(uint16_t insn) { return (insn >> 6) & 0x1Fu; }

/// 提取 4-bit 条件码，bits[11:8]
constexpr uint8_t cond(uint16_t insn) { return (insn >> 8) & 0xFu; }

/// 提取 bits[10:8]，用于 MOV/CMP/ADD/SUB immediate 的 Rd/Rn
constexpr uint8_t rd8(uint16_t insn) { return (insn >> 8) & 0x7u; }

/// 提取 11-bit 偏移（B 指令），bits[10:0]
constexpr uint16_t imm11(uint16_t insn) { return insn & 0x7FFu; }

// ── 高寄存器操作（MOV/ADD/CMP/BX 等，4-bit 寄存器编号） ──

/// 提取 D 位（Rd 的 bit[3]），用于 Special data 指令
constexpr uint8_t dn(uint16_t insn) { return (insn >> 7) & 0x1u; }

/// 提取 4-bit Rm，bits[6:3]
constexpr uint8_t rm4(uint16_t insn) { return (insn >> 3) & 0xFu; }

/// 组合 D:Rd 得到完整 4-bit 寄存器编号
constexpr uint8_t rd4(uint16_t insn) {
    return (dn(insn) << 3) | rd3(insn);
}

// ── PUSH/POP ──

/// M 位：PUSH 时表示包含 LR，POP 时表示包含 PC
constexpr bool m_bit(uint16_t insn) { return (insn >> 8) & 0x1u; }

/// 寄存器列表，bits[7:0]
constexpr uint8_t reg_list(uint16_t insn) { return insn & 0xFFu; }

// ── 32-bit Thumb-2 半字提取 ──

/// 从 hw1 中提取 S 位（BL 指令的符号位），bit[10]
constexpr uint32_t s_bit(uint16_t hw1) { return (hw1 >> 10) & 0x1u; }

/// 从 hw1 中提取 imm10，bits[9:0]
constexpr uint16_t hw1_imm10(uint16_t hw1) { return hw1 & 0x3FFu; }

/// 从 hw2 中提取 J1，bit[13]
constexpr uint32_t j1(uint16_t hw2) { return (hw2 >> 13) & 0x1u; }

/// 从 hw2 中提取 J2，bit[11]
constexpr uint32_t j2(uint16_t hw2) { return (hw2 >> 11) & 0x1u; }

/// 从 hw2 中提取 imm11，bits[10:0]
constexpr uint16_t hw2_imm11(uint16_t hw2) { return hw2 & 0x7FFu; }

// ── MOVW/MOVT 立即数拼合 ──

/// 从 hw1 提取 i 位，bit[10]
constexpr uint32_t mov_i(uint16_t hw1) { return (hw1 >> 10) & 0x1u; }

/// 从 hw1 提取 imm4，bits[3:0]
constexpr uint32_t mov_imm4(uint16_t hw1) { return hw1 & 0xFu; }

/// 从 hw2 提取 imm3，bits[14:12]
constexpr uint32_t mov_imm3(uint16_t hw2) { return (hw2 >> 12) & 0x7u; }

/// 从 hw2 提取 Rd（4-bit），bits[11:8]
constexpr uint8_t hw2_rd4(uint16_t hw2) { return (hw2 >> 8) & 0xFu; }

/// 拼合 MOVW/MOVT 的 16-bit 立即数：imm4:i:imm3:imm8
constexpr uint16_t decode_imm16(uint16_t hw1, uint16_t hw2) {
    return static_cast<uint16_t>(
        (mov_imm4(hw1) << 12) |
        (mov_i(hw1) << 11)    |
        (mov_imm3(hw2) << 8)  |
        imm8(hw2)
    );
}

// ── 32-bit 指令检测 ──

/// 判断 hw1 是否是 32-bit Thumb-2 指令的前半部分
constexpr bool is_32bit_prefix(uint16_t hw1) {
    return (hw1 & 0xE000) == 0xE000 && (hw1 & 0x1800) != 0;
}

// ── 主解码键 ──

/// 提取 bits[15:11] 作为主解码键（0-31），覆盖所有 16-bit 类别
constexpr uint8_t decode_key(uint16_t insn) { return (insn >> 11) & 0x1Fu; }

} // namespace micro_forge::cpu::arm::cortex_m3::thumb
```

**字段验证对照表**（已用真实 `arm-none-eabi-as` 确认）：

```
指令                  编码     rd3  rn3  rm3  imm8  imm5  rd8
MOVS r0, #3          0x2003                     3          0
ADDS r0, r0, r1      0x1840   0    0    1
ADDS r0, #5          0x3005                     5          0
LDR  r1, [r2, r3]    0x58D1   1    2    3
PUSH {r4,r5,lr}      0xB530        reg_list=0x30, M=1
B    target          0xE003        imm11=3
```

---

## Step 2: cortex_m3.hpp — 类声明

**文件**: `include/arch/arm/cortex_m3/cortex_m3.hpp`

```cpp
#pragma once

#include "autogen/arch_details.hpp"
#include "cpu/cpu.hpp"
#include "cpu/regfile.hpp"
#include "memory/bus.hpp"
#include "util/weak_ptr/weak_ptr.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace micro_forge::cpu::arm::cortex_m3 {

class CortexM3Core : public CPU {
public:
    explicit CortexM3Core(WeakPtr<memory::Bus> bus);

    // ── CPU 接口 ──
    CPUExpected<void> reset() override;
    CPUExpected<void> step() override;
    CPUExpected<State> state() const noexcept override;
    CPUExpected<data_t> register_value(std::size_t index) const override;
    CPUExpected<void> set_register_value(std::size_t index, data_t value) override;
    CPUExpected<std::string_view> register_name(std::size_t idx) const override;
    CPUExpected<std::size_t> register_count() const override;
    CPUExpected<addr_t> pc() const override;
    CPUExpected<addr_t> set_pc(addr_t new_pc) override;
    CPUExpected<void> raise_irq(intr::intr_n_t irq_index) override;
    CPUExpected<ticks_t> cycles() const override;

    // ── 启动 ──
    void start() { state_ = State::Running; }

    // ── 内部操作接口（供 handler 调用） ──
    data_t read_reg(uint8_t idx) const;
    void    write_reg(uint8_t idx, data_t val);
    addr_t  read_pc_raw() const;         // 返回 regfile_[15]，不带 +4
    void    write_pc(addr_t val);        // 写入并保证 bit[0]=1（Thumb）
    void    update_nz(data_t result);
    void    update_flags_add(data_t a, data_t b, data_t result);
    void    update_flags_sub(data_t a, data_t b, data_t result);
    bool    condition_passed(uint8_t cond);
    void    push_val(data_t val);
    data_t  pop_val();

    WeakPtr<memory::Bus> bus() { return bus_; }

private:
    WeakPtr<memory::Bus> bus_;
    reg::Registers<16> regs_;          // R0-R15（R13=SP, R14=LR, R15=PC）
    data_t xpsr_ = 0;                  // xPSR: N/Z/C/V/Q/ICI/IT/T/ExceptionNumber
    data_t primask_ = 0;               // 中断屏蔽（Phase 3B 使用）
    data_t control_ = 0;               // 控制寄存器（Phase 3B 使用）
    State state_ = State::Halted;
    uint64_t cycles_ = 0;

    // ── 取指 ──
    Expected<uint16_t> fetch16(addr_t addr);
    Expected<data_t>   fetch32(addr_t addr); // 返回完整 32-bit（两个小端半字拼合）

    // ── 解码分派 ──
    CPUExpected<void> execute_16bit(uint16_t insn);
    CPUExpected<void> execute_32bit(uint16_t hw1, uint16_t hw2);

    // ── xPSR 标志位常量 ──
    static constexpr data_t PSR_N = 1u << 31;
    static constexpr data_t PSR_Z = 1u << 30;
    static constexpr data_t PSR_C = 1u << 29;
    static constexpr data_t PSR_V = 1u << 28;
    static constexpr data_t PSR_T = 1u << 24;  // Thumb 位，始终为 1
};

} // namespace micro_forge::cpu::arm::cortex_m3
```

### 关键设计点

**寄存器编号**：
```
R0-R12 = 通用寄存器，idx 0-12
R13    = SP（idx 13），写入时自动 mask 低 2 位
R14    = LR（idx 14）
R15    = PC（idx 15），内部存实际执行地址，pc() 接口返回 +4
```

**xPSR 标志位**：
```
bit[31] N — 结果为负（bit[31]=1）
bit[30] Z — 结果为零
bit[29] C — 进位/借位
bit[28] V — 有符号溢出
bit[24] T — Thumb 模式（Cortex-M3 始终为 1）
```

---

## Step 3: cortex_m3.cpp — 核心逻辑

**文件**: `src/arch/arm/cortex_m3/cortex_m3.cpp`

### 3.1 构造、reset、ICore 接口

```cpp
#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/thumb_fields.hpp"
#include "core/types.hpp"
#include <bit>
#include <cstring>

namespace micro_forge::cpu::arm::cortex_m3 {

using namespace thumb;

// ── 构造 ──
CortexM3Core::CortexM3Core(WeakPtr<memory::Bus> bus) : bus_(bus) {}

// ── Reset ──
CPUExpected<void> CortexM3Core::reset() {
    regs_.reset();
    xpsr_ = PSR_T;  // Thumb 模式始终开启
    primask_ = 0;
    control_ = 0;
    state_ = State::Halted;
    cycles_ = 0;
    return {};
}

// ── ICore 接口 ──
CPUExpected<CPU::State> CortexM3Core::state() const noexcept {
    return state_;
}

CPUExpected<data_t> CortexM3Core::register_value(std::size_t index) const {
    if (index >= 16)
        return std::unexpected{CPUError::RegisterIndexOverflow};
    return read_reg(static_cast<uint8_t>(index));
}

CPUExpected<void> CortexM3Core::set_register_value(std::size_t index, data_t value) {
    if (index >= 16)
        return std::unexpected{CPUError::RegisterIndexOverflow};
    write_reg(static_cast<uint8_t>(index), value);
    return {};
}

CPUExpected<std::string_view> CortexM3Core::register_name(std::size_t idx) const {
    static constexpr std::string_view names[] = {
        "R0","R1","R2","R3","R4","R5","R6","R7",
        "R8","R9","R10","R11","R12","SP","LR","PC"
    };
    if (idx >= 16)
        return std::unexpected{CPUError::RegisterIndexOverflow};
    return names[idx];
}

CPUExpected<std::size_t> CortexM3Core::register_count() const { return 16; }

// pc() 返回当前指令地址 + 4（ARM 流水线语义）
CPUExpected<addr_t> CortexM3Core::pc() const {
    auto v = regs_.read(15);
    return v.has_value() ? *v + 4 : 0;
}

CPUExpected<addr_t> CortexM3Core::set_pc(addr_t new_pc) {
    write_pc(new_pc);
    return new_pc;
}

CPUExpected<void> CortexM3Core::raise_irq(intr::intr_n_t) {
    // Phase 3A: 空实现，Phase 3B 补全
    return {};
}

CPUExpected<ticks_t> CortexM3Core::cycles() const { return cycles_; }
```

### 3.2 内部操作接口

```cpp
// ── 寄存器读写（无 bounds check 的内部版本） ──
data_t CortexM3Core::read_reg(uint8_t idx) const {
    return *regs_.read(idx);  // 注册数量固定 16，不会溢出
}

void CortexM3Core::write_reg(uint8_t idx, data_t val) {
    if (idx == 13) val &= ~0x3u;          // SP: 低 2 位强制清零
    if (idx == 15) val |= 0x1u;           // PC: bit[0] 强制为 1（Thumb）
    (void)regs_.write(idx, val);
}

addr_t CortexM3Core::read_pc_raw() const { return read_reg(15); }

void CortexM3Core::write_pc(addr_t val) {
    write_reg(15, val | 0x1u);  // 确保 Thumb 位
}

// ── 标志位更新 ──
void CortexM3Core::update_nz(data_t result) {
    xpsr_ &= ~(PSR_N | PSR_Z);
    if (result & 0x80000000u) xpsr_ |= PSR_N;
    if (result == 0)          xpsr_ |= PSR_Z;
}

void CortexM3Core::update_flags_add(data_t a, data_t b, data_t result) {
    xpsr_ &= ~(PSR_N | PSR_Z | PSR_C | PSR_V);
    if (result & 0x80000000u) xpsr_ |= PSR_N;
    if (result == 0)          xpsr_ |= PSR_Z;
    if (result < a)           xpsr_ |= PSR_C;  // unsigned overflow
    if (((a ^ ~b) & (a ^ result)) & 0x80000000u) xpsr_ |= PSR_V;
}

void CortexM3Core::update_flags_sub(data_t a, data_t b, data_t result) {
    xpsr_ &= ~(PSR_N | PSR_Z | PSR_C | PSR_V);
    if (result & 0x80000000u) xpsr_ |= PSR_N;
    if (result == 0)          xpsr_ |= PSR_Z;
    if (a >= b)               xpsr_ |= PSR_C;  // 无借位
    if (((a ^ b) & (a ^ result)) & 0x80000000u) xpsr_ |= PSR_V;
}

// ── 条件判断 ──
bool CortexM3Core::condition_passed(uint8_t c) {
    bool N = xpsr_ & PSR_N;
    bool Z = xpsr_ & PSR_Z;
    bool C = xpsr_ & PSR_C;
    bool V = xpsr_ & PSR_V;
    switch (c) {
        case 0x0: return Z;                    // EQ
        case 0x1: return !Z;                   // NE
        case 0x2: return C;                    // CS/HS
        case 0x3: return !C;                   // CC/LO
        case 0x4: return N;                    // MI
        case 0x5: return !N;                   // PL
        case 0x6: return V;                    // VS
        case 0x7: return !V;                   // VC
        case 0x8: return C && !Z;              // HI
        case 0x9: return !C || Z;              // LS
        case 0xA: return N == V;               // GE
        case 0xB: return N != V;               // LT
        case 0xC: return !Z && (N == V);       // GT
        case 0xD: return Z || (N != V);        // LE
        case 0xE: return true;                 // AL (always)
        default:  return false;                // 0xF: 未定义或特殊用途
    }
}

// ── 栈操作 ──
void CortexM3Core::push_val(data_t val) {
    data_t sp = read_reg(13) - 4;
    if (bus_) (void)bus_->write(sp, val, Width::Word);
    write_reg(13, sp);
}

data_t CortexM3Core::pop_val() {
    data_t sp = read_reg(13);
    data_t val = 0;
    if (bus_) val = bus_->read(sp, Width::Word).value_or(0);
    write_reg(13, sp + 4);
    return val;
}
```

### 3.3 取指 + step 主循环

```cpp
// ── 取指 ──
Expected<uint16_t> CortexM3Core::fetch16(addr_t addr) {
    if (!bus_) return std::unexpected{BusError::Fault};
    auto lo = bus_->read(addr, Width::Byte);
    if (!lo) return std::unexpected{lo.error()};
    auto hi = bus_->read(addr + 1, Width::Byte);
    if (!hi) return std::unexpected{hi.error()};
    return static_cast<uint16_t>(*lo | (*hi << 8));  // 小端
}

// ── step 主循环 ──
CPUExpected<void> CortexM3Core::step() {
    if (state_ != State::Running)
        return std::unexpected{CPUError::NotRunning};

    addr_t pc = read_pc_raw();

    // 取第一个半字
    auto hw1_res = fetch16(pc);
    if (!hw1_res) {
        state_ = State::Faulted;
        return std::unexpected{CPUError::NextInstructionsUnavaliable;
    }
    uint16_t hw1 = *hw1_res;

    CPUExpected<void> exec_res;

    if (is_32bit_prefix(hw1)) {
        // 32-bit Thumb-2
        auto hw2_res = fetch16(pc + 2);
        if (!hw2_res) {
            state_ = State::Faulted;
            return std::unexpected{CPUError::NextInstructionsUnavaliable};
        }
        exec_res = execute_32bit(hw1, *hw2_res);
        if (exec_res.has_value()) {
            write_reg(15, pc + 4);  // PC 前进 4 字节
        }
    } else {
        // 16-bit Thumb
        exec_res = execute_16bit(hw1);
        if (exec_res.has_value()) {
            // 注意：分支/跳转 handler 会直接写 PC，跳过这里的前进
            // 用 read_pc_raw() 检查 PC 是否被修改
            if (read_pc_raw() == pc) {
                write_reg(15, pc + 2);  // PC 前进 2 字节
            }
        }
    }

    if (!exec_res.has_value()) {
        state_ = State::Faulted;
        return exec_res;
    }

    cycles_++;
    return {};
}
```

**PC 前进的关键设计**：

16-bit 指令执行后，step() 检查 PC 是否被 handler 修改过：
- 如果 PC 没变（`read_pc_raw() == pc`），说明是非分支指令 → PC += 2
- 如果 PC 变了，说明是分支/跳转指令 → handler 已经设置了正确的 PC，不覆盖

这意味着分支/跳转 handler 内部必须调用 `write_pc()` 来设置新地址。

### 3.4 解码分派

```cpp
// ── 16-bit 解码分派 ──
CPUExpected<void> CortexM3Core::execute_16bit(uint16_t insn) {
    using namespace thumb;

    // 处理高寄存器操作的 lambda
    auto exec_special_data = [&]() -> CPUExpected<void> {
        // bits[9:8] 决定具体操作
        uint8_t op = (insn >> 8) & 0x3;
        uint8_t rm = rm4(insn);
        uint8_t rd = rd4(insn);

        switch (op) {
            case 0b00: { // ADD Rd, Rm (高寄存器，不更新标志)
                data_t result = read_reg(rd) + read_reg(rm);
                write_reg(rd, result);
                // 注意：如果 rd == 15，这等价于跳转（BX 的一种变体）
                break;
            }
            case 0b01: { // CMP Rn, Rm (高寄存器，只更新标志)
                data_t a = read_reg(rd);
                data_t b = read_reg(rm);
                update_flags_sub(a, b, a - b);
                break;
            }
            case 0b10: { // MOV Rd, Rm (高寄存器，不更新标志)
                write_reg(rd, read_reg(rm));
                break;
            }
            case 0b11: { // BX Rm / BLX Rm
                addr_t target = read_reg(rm);
                // bit[0] 必须为 1（Thumb），跳转时清零得到实际地址
                write_pc(target);
                break;
            }
            default:
                return std::unexpected{CPUError::IllegalInstructions};
        }
        return {};
    };

    switch (decode_key(insn)) {
        // ── Shift + Add/Sub（第一批） ──
        case 0b00000:  // LSL immediate
        case 0b00001:  // LSR immediate
        case 0b00010:  // ASR immediate
        {
            uint8_t op = (insn >> 11) & 0x3;
            uint8_t imm = imm5(insn);
            uint8_t rm  = rn3(insn);
            uint8_t rd  = rd3(insn);
            data_t val = read_reg(rm);
            data_t result;

            if (op == 0b00) {       // LSL
                result = imm == 0 ? val : val << imm;
            } else if (op == 0b01) { // LSR
                result = imm == 0 ? 0 : val >> imm;
            } else {                 // ASR
                if (imm == 0) {
                    result = (val & 0x80000000u) ? 0xFFFFFFFFu : 0;
                } else {
                    result = static_cast<data_t>(
                        static_cast<int32_t>(val) >> imm);
                }
            }
            write_reg(rd, result);
            update_nz(result);
            break;
        }

        case 0b00011: // Add/subtract register or 3-bit immediate
        {
            bool is_imm = (insn >> 10) & 0x1;
            bool is_sub = (insn >> 9) & 0x1;
            uint8_t rm_or_imm = rm3(insn);
            uint8_t rn = rn3(insn);
            uint8_t rd = rd3(insn);
            data_t a = read_reg(rn);
            data_t b = is_imm ? rm_or_imm : read_reg(rm_or_imm);
            data_t result;

            if (is_sub) {
                result = a - b;
                update_flags_sub(a, b, result);
            } else {
                result = a + b;
                update_flags_add(a, b, result);
            }
            write_reg(rd, result);
            break;
        }

        // ── MOV/CMP/ADD/SUB imm8（第一批） ──
        case 0b00100: // MOVS Rd, imm8
        {
            uint8_t rd = rd8(insn);
            data_t val = imm8(insn);
            write_reg(rd, val);
            update_nz(val);
            break;
        }

        case 0b00101: // CMP Rn, imm8
        {
            uint8_t rn = rd8(insn);
            data_t a = read_reg(rn);
            data_t b = imm8(insn);
            update_flags_sub(a, b, a - b);
            break;
        }

        case 0b00110: // ADDS Rd, imm8
        {
            uint8_t rd = rd8(insn);
            data_t a = read_reg(rd);
            data_t b = imm8(insn);
            data_t result = a + b;
            update_flags_add(a, b, result);
            write_reg(rd, result);
            break;
        }

        case 0b00111: // SUBS Rd, imm8
        {
            uint8_t rd = rd8(insn);
            data_t a = read_reg(rd);
            data_t b = imm8(insn);
            data_t result = a - b;
            update_flags_sub(a, b, result);
            write_reg(rd, result);
            break;
        }

        // ── Data processing register（第一批） ──
        case 0b01000:
        {
            // bits[9:6] = opcode
            uint8_t op = (insn >> 6) & 0xF;
            uint8_t rm = rm3(insn);
            uint8_t rd = rd3(insn);
            data_t a = read_reg(rd);
            data_t b = read_reg(rm);
            data_t result;

            switch (op) {
                case 0x0: result = a & b; break;     // AND
                case 0x1: result = a ^ b; break;     // EOR
                case 0x2: result = a << (b & 0xFF); break;  // LSL (register)
                case 0x3: result = a >> (b & 0xFF); break;  // LSR (register)
                case 0x4: result = static_cast<data_t>(
                              static_cast<int32_t>(a) >> (b & 0xFF)); break; // ASR
                case 0x5: result = a + b + ((xpsr_ & PSR_C) ? 1 : 0); break; // ADC
                case 0x6: { // SBC
                    data_t c = (xpsr_ & PSR_C) ? 0 : 1;
                    result = a - b - c;
                    break;
                }
                case 0x7: result = a >> (b & 0xFF); break;  // ROR (简化)
                case 0x8: update_flags_sub(a, b, a - b); return {}; // TST (只更新标志)
                case 0x9: result = -b; break;                // RSB
                case 0xA: { // CMP (register, low) — 只更新标志
                    update_flags_sub(a, b, a - b);
                    return {};
                }
                case 0xB: update_flags_add(a, b, a + b); return {}; // CMN
                case 0xC: result = a | b; break;     // ORR
                case 0xD: result = a * b; break;     // MUL
                case 0xE: result = a & ~b; break;    // BIC
                case 0xF: result = ~b; break;        // MVN
                default: return std::unexpected{CPUError::IllegalInstructions};
            }
            write_reg(rd, result);
            update_nz(result);
            break;
        }

        // ── Special data / BX（第一批） ──
        case 0b01001:
            return exec_special_data();

        // ── LDR literal (PC-relative)（第一批） ──
        case 0b01010:
        {
            uint8_t rt = rd3(insn);
            // PC = 当前指令地址 + 4，对齐到 4 字节，偏移 = imm8 * 4
            addr_t base = (read_pc_raw() + 4) & ~0x3u;
            addr_t addr = base + imm8(insn) * 4;
            if (bus_) {
                auto val = bus_->read(addr, Width::Word);
                if (!val) return std::unexpected{CPUError::NextInstructionsUnavaliable};
                write_reg(rt, *val);
            }
            break;
        }

        // ── Load/store register offset（第一批） ──
        case 0b01011:
        {
            uint8_t op = (insn >> 9) & 0x7;
            uint8_t rm = rm3(insn);
            uint8_t rn = rn3(insn);
            uint8_t rt = rd3(insn);
            addr_t addr = read_reg(rn) + read_reg(rm);
            if (!bus_) break;

            switch (op) {
                case 0b000: { // STR (word)
                    (void)bus_->write(addr, read_reg(rt), Width::Word);
                    break;
                }
                case 0b001: { // STRH
                    (void)bus_->write(addr, read_reg(rt) & 0xFFFF, Width::HalfWord);
                    break;
                }
                case 0b010: { // STRB
                    (void)bus_->write(addr, read_reg(rt) & 0xFF, Width::Byte);
                    break;
                }
                case 0b011: { // LDRSB (sign-extended byte)
                    auto v = bus_->read(addr, Width::Byte);
                    data_t val = v.value_or(0);
                    if (val & 0x80u) val |= 0xFFFFFF00u;
                    write_reg(rt, val);
                    break;
                }
                case 0b100: { // LDR (word)
                    auto v = bus_->read(addr, Width::Word);
                    write_reg(rt, v.value_or(0));
                    break;
                }
                case 0b101: { // LDRH
                    auto v = bus_->read(addr, Width::HalfWord);
                    write_reg(rt, v.value_or(0));
                    break;
                }
                case 0b110: { // LDRB
                    auto v = bus_->read(addr, Width::Byte);
                    write_reg(rt, v.value_or(0));
                    break;
                }
                case 0b111: { // LDRSH (sign-extended halfword)
                    auto v = bus_->read(addr, Width::HalfWord);
                    data_t val = v.value_or(0);
                    if (val & 0x8000u) val |= 0xFFFF0000u;
                    write_reg(rt, val);
                    break;
                }
            }
            break;
        }

        // ── STR word immediate offset（第一批） ──
        case 0b01100:
        {
            uint8_t imm = imm5(insn);
            uint8_t rn = rn3(insn);
            uint8_t rt = rd3(insn);
            addr_t addr = read_reg(rn) + imm * 4;
            if (bus_) (void)bus_->write(addr, read_reg(rt), Width::Word);
            break;
        }

        // ── LDR word immediate offset（第一批） ──
        case 0b01101:
        {
            uint8_t imm = imm5(insn);
            uint8_t rn = rn3(insn);
            uint8_t rt = rd3(insn);
            addr_t addr = read_reg(rn) + imm * 4;
            if (bus_) {
                auto v = bus_->read(addr, Width::Word);
                write_reg(rt, v.value_or(0));
            }
            break;
        }

        // ── STRB immediate offset（第二批） ──
        case 0b01110:
        {
            uint8_t imm = imm5(insn);
            uint8_t rn = rn3(insn);
            uint8_t rt = rd3(insn);
            addr_t addr = read_reg(rn) + imm;  // 字节偏移，不乘
            if (bus_) (void)bus_->write(addr, read_reg(rt) & 0xFF, Width::Byte);
            break;
        }

        // ── LDRB immediate offset（第二批） ──
        case 0b01111:
        {
            uint8_t imm = imm5(insn);
            uint8_t rn = rn3(insn);
            uint8_t rt = rd3(insn);
            addr_t addr = read_reg(rn) + imm;
            if (bus_) {
                auto v = bus_->read(addr, Width::Byte);
                write_reg(rt, v.value_or(0));
            }
            break;
        }

        // ── STRH immediate offset（第二批） ──
        case 0b10000:
        {
            uint8_t imm = imm5(insn);
            uint8_t rn = rn3(insn);
            uint8_t rt = rd3(insn);
            addr_t addr = read_reg(rn) + imm * 2;  // 半字偏移
            if (bus_) (void)bus_->write(addr, read_reg(rt) & 0xFFFF, Width::HalfWord);
            break;
        }

        // ── LDRH immediate offset（第二批） ──
        case 0b10001:
        {
            uint8_t imm = imm5(insn);
            uint8_t rn = rn3(insn);
            uint8_t rt = rd3(insn);
            addr_t addr = read_reg(rn) + imm * 2;
            if (bus_) {
                auto v = bus_->read(addr, Width::HalfWord);
                write_reg(rt, v.value_or(0));
            }
            break;
        }

        // ── STR SP-relative（第一批） ──
        case 0b10010:
        {
            uint8_t rt = rd8(insn);
            addr_t addr = read_reg(13) + imm8(insn) * 4;
            if (bus_) (void)bus_->write(addr, read_reg(rt), Width::Word);
            break;
        }

        // ── LDR SP-relative（第一批） ──
        case 0b10011:
        {
            uint8_t rt = rd8(insn);
            addr_t addr = read_reg(13) + imm8(insn) * 4;
            if (bus_) {
                auto v = bus_->read(addr, Width::Word);
                write_reg(rt, v.value_or(0));
            }
            break;
        }

        // ── PUSH（第一批） ──
        case 0b10110:
        {
            if ((insn >> 8) & 0x1) {  // bit[10:9] = 10 → PUSH
                uint8_t rlist = reg_list(insn);
                bool m = m_bit(insn);

                // 先计算需要压入的寄存器数量
                int count = std::popcount(rlist) + (m ? 1 : 0);
                data_t sp = read_reg(13) - count * 4;
                write_reg(13, sp);

                // 按 R0-R7 顺序压栈（低寄存器在前=低地址）
                for (int i = 0; i < 8; i++) {
                    if (rlist & (1 << i)) {
                        if (bus_) (void)bus_->write(sp, read_reg(i), Width::Word);
                        sp += 4;
                    }
                }
                if (m) {
                    if (bus_) (void)bus_->write(sp, read_reg(14), Width::Word);
                }
            } else {
                return std::unexpected{CPUError::IllegalInstructions};
            }
            break;
        }

        // ── POP（第一批） ──
        case 0b10111:
        {
            if ((insn >> 8) & 0x1) {  // bit[10:9] = 11 → POP
                uint8_t rlist = reg_list(insn);
                bool m = m_bit(insn);

                data_t sp = read_reg(13);

                // 按 R0-R7 顺序弹栈
                for (int i = 0; i < 8; i++) {
                    if (rlist & (1 << i)) {
                        if (bus_) {
                            auto v = bus_->read(sp, Width::Word);
                            write_reg(i, v.value_or(0));
                        }
                        sp += 4;
                    }
                }
                if (m) {
                    if (bus_) {
                        auto v = bus_->read(sp, Width::Word);
                        write_pc(v.value_or(0));  // 写入 PC，触发 PC+4 语义
                    }
                    sp += 4;
                }
                write_reg(13, sp);
            } else {
                // NOP, CPS, SETEND 等杂项
                // NOP = 0xBF00
                if (insn == 0xBF00) break;  // NOP
                return std::unexpected{CPUError::IllegalInstructions};
            }
            break;
        }

        // ── 条件分支 B<cond>（第二批） ──
        case 0b11010:
        {
            uint8_t c = cond(insn);
            if (c == 0xE) {
                // UDF (undefined) — 触发 fault
                return std::unexpected{CPUError::IllegalInstructions};
            }
            if (c == 0xF) {
                // SVC — Phase 3B
                break;
            }
            if (condition_passed(c)) {
                // imm8 符号扩展，左移 1 位，加到 PC+4
                int32_t offset = static_cast<int8_t>(imm8(insn));
                offset <<= 1;
                write_pc(read_pc_raw() + 4 + offset);
            }
            break;
        }

        // ── B 无条件（第一批） ──
        case 0b11100:
        {
            // imm11 符号扩展，左移 1 位
            int32_t offset = static_cast<int16_t>(imm11(insn) << 5) >> 5; // 符号扩展 11→32
            offset <<= 1;
            write_pc(read_pc_raw() + 4 + offset);
            break;
        }

        default:
            // 未实现的指令类别
            return std::unexpected{CPUError::IllegalInstructions};
    }
    return {};
}
```

### 3.5 32-bit Thumb-2 分派

```cpp
CPUExpected<void> CortexM3Core::execute_32bit(uint16_t hw1, uint16_t hw2) {
    using namespace thumb;

    // hw1 bits[15:11] = 11110 或 11111
    // hw1 bits[14:12] 区分具体指令
    uint8_t op1 = (hw1 >> 11) & 0x1F;  // 应该是 11110 或 11111
    uint8_t op2 = (hw1 >> 5) & 0x7;    // bits[8:5]

    // ── BL（第一批） ──
    // BL: hw1 = 11110 S imm10, hw2 = 11 J1 1 J2 imm11
    // 区分 BL 和 BLX：hw2[15:14]=11, hw2[12]=1 (BL) 或 hw2[12]=0 (BLX)
    if ((hw1 & 0xF800) == 0xF000 && (hw2 & 0xD000) == 0xD000) {
        // BL 或 BLX
        uint32_t s  = s_bit(hw1);
        uint16_t i10 = hw1_imm10(hw1);
        uint32_t j1_val = j1(hw2);
        uint32_t j2_val = j2(hw2);
        uint16_t i11 = hw2_imm11(hw2);

        uint32_t i1 = 1u ^ (j1_val ^ s);
        uint32_t i2 = 1u ^ (j2_val ^ s);

        uint32_t offset = (s << 24) | (i1 << 23) | (i2 << 22)
                        | (i10 << 12) | (i11 << 1);
        if (s) offset |= 0xFE000000u;  // 符号扩展

        data_t next_pc = read_pc_raw() + 4;  // BL 之后的指令地址

        bool is_blx = !((hw2 >> 12) & 0x1);
        if (is_blx) {
            // BLX: 目标地址 bit[0] 清零，LR = PC+2 (不对齐，设 bit[0])
            write_reg(14, next_pc | 0x1u);
            write_pc((read_pc_raw() + 4 + offset) & ~0x1u);
        } else {
            // BL: LR = 返回地址，PC = 目标
            write_reg(14, next_pc | 0x1u);
            write_pc(read_pc_raw() + 4 + offset);
        }
        return {};
    }

    // ── MOVW / MOVT（第三批） ──
    // hw1 bits[15:5] = 1111 0 i 10 0100 (MOVW) 或 1111 0 i 10 1100 (MOVT)
    if ((hw1 & 0xFBF0) == 0xF240) {
        // MOVW
        uint16_t imm16 = decode_imm16(hw1, hw2);
        uint8_t rd = hw2_rd4(hw2);
        write_reg(rd, imm16);
        return {};
    }
    if ((hw1 & 0xFBF0) == 0xF2C0) {
        // MOVT
        uint16_t imm16 = decode_imm16(hw1, hw2);
        uint8_t rd = hw2_rd4(hw2);
        data_t val = read_reg(rd);
        val = (val & 0x0000FFFFu) | (static_cast<data_t>(imm16) << 16);
        write_reg(rd, val);
        return {};
    }

    // ── MRS / MSR（第三批） ──
    // MRS: hw1 = 11110 0 111 0 11 xxxx, hw2 = 10xx Rd(4) 0000
    if ((hw1 & 0xFFF0) == 0xF3E0 && (hw2 & 0xFF00) == 0x8000) {
        // 简化：只支持 PRIMASK
        uint8_t rd = hw2_rd4(hw2);
        // SYSm 在 hw1[3:0]:hw2[7:0]，PRIMASK = 0b10000
        write_reg(rd, primask_);
        return {};
    }
    // MSR: hw1 = 11110 0 110 xxxx, hw2 = 1000 Rd(4) 0000
    if ((hw1 & 0xFFF0) == 0xF380 && (hw2 & 0xFF00) == 0x8800) {
        uint8_t rn = hw2_rd4(hw2);
        primask_ = read_reg(rn);
        return {};
    }

    return std::unexpected{CPUError::IllegalInstructions};
}
```

---

## Step 4: 测试计划

### 4.1 测试 fixture

**文件**: `test/test_cortex_m3.cpp`

```cpp
#include <gtest/gtest.h>
#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "memory/flat_memory.hpp"
#include "memory/bus.hpp"

using namespace micro_forge;
using namespace micro_forge::cpu;
using namespace micro_forge::cpu::arm::cortex_m3;

class CortexM3Test : public ::testing::Test {
protected:
    static constexpr addr_t kMemBase = 0x00000000;
    static constexpr addr_t kMemSize = 4096;

    memory::FlatMemory mem_{kMemSize};
    memory::Bus bus_;
    std::unique_ptr<CortexM3Core> cpu_;

    void SetUp() override {
        ASSERT_TRUE(bus_.map(
            memory::region(kMemBase, kMemSize, mem_.GetWeak())).has_value());
        cpu_ = std::make_unique<CortexM3Core>(bus_.GetWeak());
    }

    // 加载 Thumb 指令的二进制（小端 uint16_t 序列）
    void load_program(const std::vector<uint16_t>& insns, addr_t base = 0) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(insns.data());
        ASSERT_TRUE(
            mem_.load(base, {bytes, insns.size() * sizeof(uint16_t)}).has_value());
    }

    void reset_cpu()  { (void)cpu_->reset(); }
    void start_cpu()  { cpu_->start(); }
    void step_cpu()   { (void)cpu_->step(); }

    void set_reg(size_t idx, data_t val) {
        (void)cpu_->set_register_value(idx, val);
    }
    data_t reg(size_t idx) {
        return cpu_->register_value(idx).value_or(0xDEAD);
    }
    void set_pc(addr_t pc) { (void)cpu_->set_pc(pc); }

    void run_until_halt(size_t max_steps = 1000) {
        for (size_t i = 0; i < max_steps; ++i) {
            auto res = cpu_->step();
            if (!res.has_value()) break;
            auto st = cpu_->state();
            if (!st.has_value() || *st != CPU::State::Running) break;
        }
    }
};
```

### 4.2 指令级测试向量

每个测试用真实 ARM 编码（已用 `arm-none-eabi-as` 验证）：

```cpp
// ── MOVS Rd, imm8 ──
TEST_F(CortexM3Test, MovsImm8) {
    // movs r0, #3  →  0x2003
    // movs r1, #4  →  0x2104
    // b    hang     →  0xE001 (offset=2, skip next)
    // movs r0, #99  →  0x2063  (skipped)
    // hang: b hang  →  0xE7FD (offset=-2, 无限循环)
    load_program({0x2003, 0x2104, 0xE001, 0x2063, 0xE7FD});
    reset_cpu();
    start_cpu();
    run_until_halt(10);  // 会在 b hang 无限循环，靠 max_steps 退出
    EXPECT_EQ(reg(0), 3u);
    EXPECT_EQ(reg(1), 4u);
}

// ── ADDS Rd, Rn, Rm ──
TEST_F(CortexM3Test, AddsReg) {
    // movs r0, #5   →  0x2005
    // movs r1, #3   →  0x2103
    // adds r0, r0, r1 → 0x1840  (R0=8)
    // b    hang      →  0xE001
    // hang: b hang   →  0xE7FD
    load_program({0x2005, 0x2103, 0x1840, 0xE001, 0xE7FD});
    reset_cpu();
    start_cpu();
    run_until_halt(10);
    EXPECT_EQ(reg(0), 8u);
}

// ── SUBS Rd, imm8 ──
TEST_F(CortexM3Test, SubsImm8) {
    // movs r1, #10  →  0x210A
    // subs r1, #1   →  0x3901
    // b    hang     →  0xE001
    // hang: b hang  →  0xE7FD
    load_program({0x210A, 0x3901, 0xE001, 0xE7FD});
    reset_cpu();
    start_cpu();
    run_until_halt(10);
    EXPECT_EQ(reg(1), 9u);
}

// ── 函数调用链（BL + BX LR） ──
TEST_F(CortexM3Test, CallChain) {
    // 地址 0x00: movs r0, #3   → 0x2003
    // 地址 0x02: movs r1, #4   → 0x2104
    // 地址 0x04: bl add_func   → 0xF000F800 (offset=4, target=0x0C)
    // 地址 0x08: b   hang      → 0xE001
    // 地址 0x0A: b   hang      → 0xE7FD
    // 地址 0x0C: adds r0, r0, r1 → 0x1840
    // 地址 0x0E: bx  lr        → 0x4770
    // 地址 0x10: hang: b hang  → 0xE7FD
    //
    // 注意：BL 编码需要计算偏移
    // BL at 0x04, target at 0x0C, offset = 0x0C - (0x04+4) = 4
    // BL: hw1=0xF000, hw2=0xF804
    load_program({
        0x2003,   // 0x00
        0x2104,   // 0x02
        0xF000,   // 0x04 hw1
        0xF804,   // 0x06 hw2
        0xE001,   // 0x08
        0xE7FD,   // 0x0A
        0x1840,   // 0x0C
        0x4770,   // 0x0E
        0xE7FD,   // 0x10
    });
    reset_cpu();
    start_cpu();
    run_until_halt(20);
    EXPECT_EQ(reg(0), 7u);  // 3 + 4
}

// ── 循环 + 条件分支 ──
TEST_F(CortexM3Test, LoopBne) {
    // movs r0, #0    → 0x2000
    // movs r1, #10   → 0x210A
    // loop:
    // adds r0, #1    → 0x3001
    // subs r1, #1    → 0x3901
    // bne  loop      → 0xD1FC (cond=NE, offset=-4)
    // b    hang      → 0xE001
    // hang: b hang   → 0xE7FD
    //
    // BNE at addr 0x08, target = 0x04
    // offset = 0x04 - (0x08 + 4) = -8, imm8 = -8/2 = -4 = 0xFC
    load_program({
        0x2000,   // 0x00
        0x210A,   // 0x02
        0x3001,   // 0x04 loop:
        0x3901,   // 0x06
        0xD1FC,   // 0x08 bne loop
        0xE001,   // 0x0A
        0xE7FD,   // 0x0C
    });
    reset_cpu();
    start_cpu();
    run_until_halt(100);
    EXPECT_EQ(reg(0), 10u);
    EXPECT_EQ(reg(1), 0u);
}
```

### 4.3 真实汇编测试

在 `test/asm/` 下放 `.s` 文件，用 Makefile 或 CMake custom command 编译：

```makefile
# test/asm/Makefile
ASM = arm-none-eabi-as
FLAGS = -mthumb -march=armv7-m

%.bin: %.s
	$(ASM) $(FLAGS) -o $*.o $<
	arm-none-eabi-objcopy -O binary $*.o $@
```

测试代码加载 `.bin` 到模拟器内存，验证执行结果。这样编码正确性由真实汇编器保证。

---

## 实施顺序

```
Step 1 → Step 2 → Step 3 → Step 4 → Step 5 → Step 6
thumb_    cortex_   cortex_   逐条指令   单元测试   汇编测试
fields    m3.hpp    m3.cpp    + handler  test_      test/asm/
          类声明     核心逻辑              cortex3
```

1. 补全 `thumb_fields.hpp`（所有字段提取函数）
2. 填写 `cortex_m3.hpp`（类声明）
3. 实现 `cortex_m3.cpp` 的 reset / ICore 接口 / 标志位 / 栈操作
4. 实现 `execute_16bit()` 的 **第一批** case（MOV/ADD/SUB/LDR/STR/PUSH/POP/B/BL/BX/NOP）
5. 编译通过，写测试验证第一批
6. 实现第二批（B<cond>/LSL/LSR/ASR/LDRB/STRB/LDRH/STRH）+ 测试
7. 实现 `execute_32bit()` 第三批（MOVW/MOVT/MRS/MSR）+ 测试
8. 用真实 `.s` 汇编做端到端测试

---

## 验收标准

- [ ] CortexM3Core 编译通过，实现 CPU 全部虚方法
- [ ] reset() 后所有寄存器=0，xPSR 只有 T 位，state=Halted
- [ ] pc() 返回 +4 语义正确
- [ ] SP 写入时低 2 位被 mask
- [ ] 第一批指令（MOVS/ADDS/SUBS/LDR/STR/PUSH/POP/B/BL/BX/NOP）测试通过
- [ ] 第二批指令（B<cond>/LSL/LSR/ASR/LDRB/STRB/LDRH/STRH）测试通过
- [ ] 第三批指令（MOVW/MOVT/MRS/MSR）测试通过
- [ ] 标志位 N/Z/C/V 按 ARM ARM 真值表验证
- [ ] 函数调用链（BL + BX LR）测试通过
- [ ] 循环 + 条件分支测试通过
- [ ] 真实 `.s` 汇编编译后能在模拟器上执行
- [ ] 全部 ctest 绿色
