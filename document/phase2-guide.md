# Phase 2 实施手册：CPU 核心框架 + ToyCore 验证

## 已确认的设计决策

| 编号 | 决策 | 选择 | 理由 |
|------|------|------|------|
| D2-1 | ICore 接口范围 | 最小化，不加 `run()` / `special_reg()` | `run` 用外部循环替代；特殊寄存器由各 CPU 类自行暴露 |
| D2-2 | ICore 参数类型 | `addr_t` / `data_t`（来自 arch_details.hpp） | 32-bit 参数化，将来可扩展 |
| D2-3 | `reg_name()` 返回类型 | `std::string_view` | 零堆分配 |
| D2-4 | RegisterFile 定位 | 纯数据容器，PC 不放入 | PC 由各 CPU 类自行管理，ARM 的 R15 特殊行为在 CortexM3Core 处理 |
| D2-5 | Bus 连接方式 | 构造时注入 `Bus&` | 简单直接，不在 ICore 接口中体现 |
| D2-6 | ToyCore ISA | 完整版（含 CALL/RET/INT），约 12 条指令 | 作为参考实现，充分验证 ICore 接口 |
| D2-7 | ToyCore raise_irq | 简单实现（pending + 向量表跳转） | 验证 raise_irq() 不是空壳 |
| D2-8 | INT 与 raise_irq 关系 | INT n 内部调用 raise_irq(n)，共用路径 | 两个入口走同一套中断机制 |
| D2-9 | 中断返回 | RET 统一处理函数返回和中断返回，无魔术值 | ToyCore 保持简单，不需要 ARM 的 EXC_RETURN 机制 |
| D2-10 | 指令编码辅助 | constexpr 函数（`isa.hpp`），无独立汇编器 | 测试代码可读性好，无需额外构建步骤 |
| D2-11 | Phase 2 范围 | ICore + RegisterFile + ToyCore；CortexM3Core 放 Phase 3 | 先验证抽象层可靠，再做真实 CPU |

---

## 目标目录结构

```
include/
  cpu/                              # 🆕 CPU 子系统
    icore.hpp                       # 🆕 ICore 抽象接口
    register_file.hpp               # 🆕 RegisterFile<N> 模板
    toy/                            # 🆕 ToyCore
      core.hpp                      # 🆕 ToyCore 类声明
      isa.hpp                       # 🆕 指令编码/解码 constexpr 辅助
src/
  cpu/                              # 🆕
    toy/
      core.cpp                      # 🆕 ToyCore 实现（step/decode/execute）
test/
  test_register_file.cpp            # 🆕 RegisterFile 单元测试
  test_toy_core.cpp                 # 🆕 ToyCore 指令级测试
```

注意：`src/cpu/toy/core.cpp` 会被 CMakeLists.txt 现有的 glob 自动收录（`file(GLOB_RECURSE ... src/*.cpp)`）。

---

## Step 1: ICore 接口

**文件**: `include/cpu/icore.hpp`

```cpp
#pragma once
#include "core/types.hpp"
#include <cstdint>
#include <string_view>

namespace micro_forge {

class ICore {
public:
    virtual ~ICore() = default;

    // ── 生命周期 ──
    virtual void      reset()                          = 0;
    virtual void      step()                           = 0;   // state != Running 时 no-op
    virtual CoreState state()                    const = 0;

    // ── 寄存器 ──
    // 索引空间 CPU 自定义：0..reg_count()-1 为通用寄存器
    // 超出 reg_count() 的索引由各 CPU 自行定义（特殊寄存器）
    virtual data_t    reg(size_t idx)           const = 0;
    virtual void      set_reg(size_t idx, data_t val)  = 0;
    virtual size_t    reg_count()               const = 0;
    virtual std::string_view reg_name(size_t idx) const = 0;

    // ── PC（独立于 reg 索引） ──
    virtual addr_t    pc()                       const = 0;
    virtual void      set_pc(addr_t addr)              = 0;

    // ── 中断 ──
    virtual void      raise_irq(uint32_t irq_num)      = 0;

    // ── 周期 ──
    virtual uint64_t  cycles()                   const = 0;
};

}  // namespace micro_forge
```

**要点**：
- `step()` 语义：`state() == Running` 时执行一条指令后返回；否则 no-op
- `reg_name()` 返回 `std::string_view`，避免堆分配
- `raise_irq(uint32_t)` 用 `uint32_t` 而非 `uint8_t`，ARM Cortex-M3 支持 240+ 个中断
- 不暴露 Bus——Bus 是实现细节，由构造函数注入

---

## Step 2: RegisterFile<N> 模板

**文件**: `include/cpu/register_file.hpp`

```cpp
#pragma once
#include "core/types.hpp"
#include <array>
#include <cassert>
#include <cstddef>

namespace micro_forge {

template <size_t N>
class RegisterFile {
    std::array<data_t, N> regs_{};

public:
    data_t read(size_t idx) const {
        assert(idx < N);
        return regs_[idx];
    }

    void write(size_t idx, data_t val) {
        assert(idx < N);
        regs_[idx] = val;
    }

    void reset() { regs_.fill(0); }

    static constexpr size_t size() { return N; }
};

}  // namespace micro_forge
```

**要点**：
- 纯存储，无虚函数，无 side-effect
- 方法名用 `read/write` 而非 `get/set`，避免和 ICore 的 `set_reg()` 混淆
- `assert` 做越界检查（debug build 有效，release 无开销）
- PC 不在里面——由各 CPU 类自行管理

**使用示例**：
- ToyCore: `RegisterFile<8>` for R0-R7
- CortexM3Core (Phase 3): `RegisterFile<16>` for R0-R15，特殊行为在 `reg()/set_reg()` 中处理
- RISC-V (未来): `RegisterFile<32>` for x0-x31

---

## Step 3: ToyCore ISA 编码

**文件**: `include/cpu/toy/isa.hpp`

### 指令集定义

8 个寄存器（R0-R7），独立 PC，Z/N 标志位。所有指令 32-bit 固定长度。

```
编码格式：
  [31:28] opcode    (4 bits, 最多 16 条指令)
  [23:21] Rd        (3 bits, 目标/第一寄存器)
  [20:18] Rs        (3 bits, 源寄存器 A)
  [17:15] Rt        (3 bits, 源寄存器 B，或 0)
  [14:0]  imm15     (15-bit 立即数/偏移，或 0)

指令表：
  0x0  NOP                        无操作
  0x1  ADD  Rd, Rs, Rt            Rd = Rs + Rt，更新 Z/N
  0x2  SUB  Rd, Rs, Rt            Rd = Rs - Rt，更新 Z/N
  0x3  AND  Rd, Rs, Rt            Rd = Rs & Rt，更新 Z/N
  0x4  LDI  Rd, imm15             Rd = imm15 (零扩展到 32-bit)
  0x5  LDW  Rd, [Rs + imm5]       Rd = mem32[Rs + imm5*4]
  0x6  STW  Rt, [Rs + imm5]       mem32[Rs + imm5*4] = Rt
  0x7  BZ   imm8                  if Z flag: PC += sign_ext(imm8) * 4
  0x8  JMP  imm15                 PC = imm15 * 4
  0x9  CALL imm15                 push(PC + 4); PC = imm15 * 4
  0xA  RET                        PC = pop()
  0xB  INT  imm4                  raise_irq(imm4)
  0xC  HALT                       state → Halted
```

### 栈约定
- R7 默认作为 SP（约定，非硬件强制）
- 栈向低地址增长
- CALL: `mem[SP-4] = PC+4; SP -= 4; PC = target`
- RET:  `PC = mem[SP]; SP += 4`
- INT 的中断响应也用同一套栈：`mem[SP-4] = PC; SP -= 4; PC = vectors[irq_n]`
- INT handler 末尾用 RET 返回

### constexpr 编码辅助

```cpp
namespace micro_forge::cpu::toy {

// ── 编码 ──
constexpr uint32_t encode_nop() {
    return 0x0u << 28;
}

constexpr uint32_t encode_add(uint32_t rd, uint32_t rs, uint32_t rt) {
    return (0x1u << 28) | (rd << 21) | (rs << 18) | (rt << 15);
}

constexpr uint32_t encode_sub(uint32_t rd, uint32_t rs, uint32_t rt) {
    return (0x2u << 28) | (rd << 21) | (rs << 18) | (rt << 15);
}

constexpr uint32_t encode_and(uint32_t rd, uint32_t rs, uint32_t rt) {
    return (0x3u << 28) | (rd << 21) | (rs << 18) | (rt << 15);
}

constexpr uint32_t encode_ldi(uint32_t rd, uint32_t imm15) {
    return (0x4u << 28) | (rd << 21) | (imm15 & 0x7FFFu);
}

constexpr uint32_t encode_ldw(uint32_t rd, uint32_t rs, uint32_t imm5) {
    return (0x5u << 28) | (rd << 21) | (rs << 18) | (imm5 & 0x1Fu);
}

constexpr uint32_t encode_stw(uint32_t rt, uint32_t rs, uint32_t imm5) {
    return (0x6u << 28) | (rt << 15) | (rs << 18) | (imm5 & 0x1Fu);
}

constexpr uint32_t encode_bz(int32_t offset) {
    // offset 以指令为单位（正=向前，负=向后），imm8 = offset & 0xFF
    return (0x7u << 28) | (static_cast<uint32_t>(offset) & 0xFFu);
}

constexpr uint32_t encode_jmp(uint32_t target) {
    // target 以指令为单位（绝对地址 = target * 4）
    return (0x8u << 28) | (target & 0x7FFFu);
}

constexpr uint32_t encode_call(uint32_t target) {
    return (0x9u << 28) | (target & 0x7FFFu);
}

constexpr uint32_t encode_ret() {
    return 0xAu << 28;
}

constexpr uint32_t encode_int(uint32_t irq_num) {
    return (0xBu << 28) | (irq_num & 0xFu);
}

constexpr uint32_t encode_halt() {
    return 0xCu << 28;
}

// ── 解码 ──
constexpr uint32_t opcode(uint32_t insn) { return (insn >> 28) & 0xFu; }
constexpr uint32_t rd(uint32_t insn)     { return (insn >> 21) & 0x7u; }
constexpr uint32_t rs(uint32_t insn)     { return (insn >> 18) & 0x7u; }
constexpr uint32_t rt(uint32_t insn)     { return (insn >> 15) & 0x7u; }
constexpr uint32_t imm15(uint32_t insn)  { return insn & 0x7FFFu; }
constexpr uint32_t imm8(uint32_t insn)   { return insn & 0xFFu; }
constexpr uint32_t imm5(uint32_t insn)   { return insn & 0x1Fu; }
constexpr uint32_t imm4(uint32_t insn)   { return insn & 0xFu; }

}  // namespace micro_forge::cpu::toy
```

---

## Step 4: ToyCore 类声明

**文件**: `include/cpu/toy/core.hpp`

```cpp
#pragma once
#include "cpu/icore.hpp"
#include "cpu/register_file.hpp"
#include "memory/bus.hpp"
#include <array>

namespace micro_forge::cpu::toy {

class Core : public ICore {
    memory::Bus& bus_;
    RegisterFile<8> regs_;
    addr_t pc_ = 0;
    bool z_flag_ = false;
    bool n_flag_ = false;
    CoreState state_ = CoreState::Halted;
    uint64_t cycles_ = 0;

    // 中断
    std::array<addr_t, 16> interrupt_vectors_{};
    uint32_t pending_irq_ = 0;  // bit mask，最多 16 个 IRQ

    // 内部方法
    Expected<data_t> fetch();
    void execute(data_t insn);
    void check_interrupts();
    void push_stack(data_t val);
    data_t pop_stack();

public:
    explicit Core(memory::Bus& bus);

    // ICore
    void reset() override;
    void step() override;
    CoreState state() const override;
    data_t reg(size_t idx) const override;
    void set_reg(size_t idx, data_t val) override;
    size_t reg_count() const override;
    std::string_view reg_name(size_t idx) const override;
    addr_t pc() const override;
    void set_pc(addr_t addr) override;
    void raise_irq(uint32_t irq_num) override;
    uint64_t cycles() const override;

    // ToyCore 特有：配置中断向量表
    void set_interrupt_vector(uint32_t irq_num, addr_t handler_addr);
};

}  // namespace micro_forge::cpu::toy
```

### 关键实现细节

#### step() 流程
```
1. if (state_ != Running) return;
2. check_interrupts()     // 检查 pending_irq_，如有 → 压栈 PC → 跳转向量表
3. auto insn = fetch()    // bus_.read(pc_, Width::Word)
4. if (!insn) → state_ = Faulted; return;
5. execute(insn)          // switch on opcode
6. if (opcode != JMP/BZ-taken/CALL/RET) pc_ += 4;
7. cycles_++;
```

#### 中断处理
```
check_interrupts():
  if (pending_irq_ == 0) return;
  irq_num = ctz(pending_irq_)   // 找最低位的 pending IRQ
  pending_irq_ &= ~(1u << irq_num);
  push_stack(pc_);              // 保存返回地址
  pc_ = interrupt_vectors_[irq_num];
```

#### 栈操作（R7 约定为 SP）
```
push_stack(val):
  addr_t sp = regs_.read(7);
  sp -= 4;
  bus_.write(sp, val, Width::Word);
  regs_.write(7, sp);

pop_stack() → data_t:
  addr_t sp = regs_.read(7);
  auto val = bus_.read(sp, Width::Word);
  regs_.write(7, sp + 4);
  return val.value();  // 注意：忽略 BusError 简化处理
```

#### raise_irq 实现
```cpp
void Core::raise_irq(uint32_t irq_num) {
    if (irq_num < 16) {
        pending_irq_ |= (1u << irq_num);
    }
}
```

#### INT 指令执行
```
execute() 中 opcode == 0xB (INT):
  uint32_t irq_num = imm4(insn);
  raise_irq(irq_num);  // 复用 raise_irq，下次 step() 开头 check_interrupts 处理
```

---

## Step 5: 测试计划

### test_register_file.cpp

```
- 构造后所有寄存器为 0
- write + read 对称
- 越界 assert（debug build）
- reset() 清零
- size() 返回正确值
```

### test_toy_core.cpp

**基础测试**：
- reset 后 PC=0, regs=0, state=Halted
- set_pc + pc 对称
- reg_name 返回 "R0".."R7"
- cycles() 随 step() 递增

**指令级测试**（每条至少 2 个向量）：
```
LDI:  R0 = 42 → reg(0) == 42
ADD:  R0=5, R1=3, ADD R2,R0,R1 → R2=8, Z=0
ADD:  R0=0, R1=0, ADD R2,R0,R1 → R2=0, Z=1
SUB:  R0=5, R1=3, SUB R2,R0,R1 → R2=2
AND:  R0=0xFF, R1=0x0F, AND R2,R0,R1 → R2=0x0F
LDW/STW: 存 0xDEAD 到内存，读回验证
BZ:   Z=1 时跳转，Z=0 时不跳转
JMP:  无条件跳转到目标地址
HALT: step() 后 state == Halted
```

**CALL/RET 测试**：
```
CALL func → SP 减少 4，PC 跳转到 func
RET → SP 恢复，PC 回到 CALL 之后
嵌套调用：call_a → call_b → ret_b → ret_a
```

**INT 测试**：
```
set_interrupt_vector(3, handler_addr)
INT 3 → PC 跳转到 handler_addr，栈上有返回地址
handler 中 RET → PC 恢复
```

**raise_irq 测试**：
```
set_interrupt_vector(5, handler)
raise_irq(5)
next step() → PC 跳转到 handler
handler 中 RET → PC 恢复
```

**错误处理测试**：
```
PC 指向未映射地址 → fetch 失败 → state == Faulted
未知 opcode → state == Faulted
```

**集成测试：斐波那契**
```cpp
// 计算 fib(10)，结果应在 R1
std::vector<uint32_t> fib_program = {
    encode_ldi(0, 0),        // 0: R0 = 0 (fib_prev)
    encode_ldi(1, 1),        // 1: R1 = 1 (fib_curr)
    encode_ldi(2, 10),       // 2: R2 = 10 (counter)
    encode_ldi(3, 1),        // 3: R3 = 1 (constant)
    // loop:
    encode_add(4, 0, 1),    // 4: R4 = R0 + R1
    encode_and(0, 4, 4),    // 5: R0 = R4 (MOV via AND)
    // 需要交换 R0 和 R1...
    // 或者更简洁：
    encode_add(0, 0, 1),    // 4: R0 += R1
    encode_sub(4, 0, 1),    // 5: R4 = R0 - R1 (old R0)
    encode_and(1, 4, 4),    // 6: R1 = R4 (MOV via AND Rd,Rs,Rs)
    encode_sub(2, 2, 3),    // 7: R2 -= 1
    encode_bz(2),            // 8: if Z (R2==0): skip
    encode_jmp(4),           // 9: jump back to loop
    encode_halt(),           // 10: done
};
// 执行后 R0 应为 fib(10) = 55
```

---

## 实施顺序

```
Step 1 ──→ Step 2 ──→ Step 3 ──→ Step 4 ──→ Step 5
ICore      RegFile    isa.hpp   ToyCore     测试
```

1. 创建 `include/cpu/icore.hpp`（ICore 接口）
2. 创建 `include/cpu/register_file.hpp`（RegisterFile 模板）
3. 创建 `test/test_register_file.cpp`，验证编译 + 测试绿色
4. 创建 `include/cpu/toy/isa.hpp`（编码/解码 constexpr 辅助）
5. 创建 `include/cpu/toy/core.hpp`（ToyCore 声明）
6. 创建 `src/cpu/toy/core.cpp`（ToyCore 实现）
7. 创建 `test/test_toy_core.cpp`（指令级测试）
8. 更新 `test/CMakeLists.txt` 添加新测试文件
9. 全部 ctest 绿色

## 验收标准

- [ ] ICore 接口语法正确，无修改即可被 ToyCore 实现
- [ ] RegisterFile<N> 模板可实例化，read/write 对称
- [ ] ToyCore 实现全部 ICore 虚方法
- [ ] ADD/SUB/AND/LDI 指令测试通过
- [ ] LDW/STW 指令测试通过（CPU ↔ Bus 交互）
- [ ] BZ/JMP 跳转测试通过
- [ ] CALL/RET 调用链测试通过（含嵌套）
- [ ] INT 指令触发中断 → handler 执行 → RET 返回
- [ ] raise_irq 外部触发 → 同样路径正确
- [ ] HALT 后 state() == Halted
- [ ] 斐波那契集成测试通过
- [ ] 全部 ctest 绿色
