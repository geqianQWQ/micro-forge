# Phase 3A · ARM Cortex-M3 指令集

> 预计工期：2-3 周 | 依赖：Phase 2 | 状态：待实施

## 目标

实现 ARM Cortex-M3 的 Thumb 指令子集，使裸机 ARM 汇编程序能在模拟器上正确执行。
同步搭建调试工具（断点、反汇编），确保 Thumb 解码过程可观测。

---

## 设计决策

### D3A-1：CortexM3Core 架构

```cpp
class CortexM3Core : public ICore {
    // 寄存器
    RegisterFile<16> regfile_;   // R0-R12, SP(R13), LR(R14), PC(R15)
    uint32_t xpsr_ = 0;         // xPSR: N/Z/C/V/Q/ICI/IT/T/ExceptionNumber
    uint32_t primask_ = 0;      // 中断屏蔽
    uint32_t control_ = 0;      // 控制寄存器

    // 内部状态
    MemoryBus& bus_;
    CoreState state_ = CoreState::Halted;
    uint64_t cycles_ = 0;

    // IT 块状态（延后实现，此处预留）
    uint8_t it_state_ = 0;

    // 内部方法
    uint16_t fetch16(uint32_t addr);
    uint32_t fetch32(uint32_t addr);   // Thumb-2 双半字
    void step_impl();
    void execute_16bit(uint16_t insn);
    void execute_32bit(uint16_t hw1, uint16_t hw2);

    // 标志位
    void update_flags_add(uint32_t a, uint32_t b, uint32_t result);
    void update_flags_sub(uint32_t a, uint32_t b, uint32_t result);
    bool condition_passed(uint8_t cond);

public:
    explicit CortexM3Core(MemoryBus& bus);

    // ICore 接口实现
    void reset() override;
    void step() override;
    CoreState state() const override;
    uint32_t reg(uint8_t idx) const override;
    void set_reg(uint8_t idx, uint32_t v) override;
    uint8_t reg_count() const override;
    std::string reg_name(uint8_t idx) const override;
    uint32_t pc() const override;
    void set_pc(uint32_t addr) override;
    void raise_irq(uint8_t irq_n) override;  // Phase 3B 完整实现
    uint64_t cycles() const override;

    // Thumb bit 管理
    bool is_thumb_mode() const;
};
```

### D3A-2：Thumb 解码策略

16-bit 指令按 bits[15:10] 分为几大类：

```
bits[15:10]  类别
─────────────────────
000xxx       Shift + Add/Sub/Cmp/Mov (immediate)
001xxx       Add/Sub/Cmp/Mov (large immediate)
010000       Data processing (register)
010001       Special data + branch exchange
01001x       LDR (literal, PC-relative)
0101xx       Load/store (register offset)
011xx x      Load/store (word/byte immediate offset)
100xx x      Load/store (halfword immediate offset / SP-relative)
1010x x      Add to SP/PC / miscellaneous
1011xx       Miscellaneous (PUSH/POP/CPS/etc.)
1100xx       Multiple load/store / unconditional branch
1101xxx      Conditional branch + SVC
11100x       B (unconditional)
11101/10/11  Thumb-2 32-bit instruction prefix
```

实现方式：switch-case 按 bits[15:10] 分派到大类处理函数，每个函数内部进一步解码。

### D3A-3：标志位实现

ARM 的 N/Z/C/V 标志位编码在 xPSR 的 [31:28]：

```cpp
// xPSR flag bits
static constexpr uint32_t PSR_N = 1u << 31;  // Negative
static constexpr uint32_t PSR_Z = 1u << 30;  // Zero
static constexpr uint32_t PSR_C = 1u << 29;  // Carry
static constexpr uint32_t PSR_V = 1u << 28;  // Overflow

void update_flags_add(uint32_t a, uint32_t b, uint32_t result) {
    uint32_t psr = xpsr_ & ~(PSR_N | PSR_Z | PSR_C | PSR_V);
    if (result & 0x80000000) psr |= PSR_N;
    if (result == 0)         psr |= PSR_Z;
    if (result < a)          psr |= PSR_C;  // unsigned overflow
    if (((a ^ ~b) & (a ^ result)) & 0x80000000) psr |= PSR_V;  // signed overflow
    xpsr_ = psr;
}
```

### D3A-4：Thumb-2 32-bit 检测

```cpp
void step_impl() {
    uint32_t pc_val = regfile_.pc;
    uint16_t hw1 = fetch16(pc_val);

    if ((hw1 & 0xE000) == 0xE000 && (hw1 & 0x1800) != 0x0000) {
        // 32-bit Thumb-2 instruction
        uint16_t hw2 = fetch16(pc_val + 2);
        execute_32bit(hw1, hw2);
        regfile_.pc += 4;
    } else {
        // 16-bit Thumb instruction
        execute_16bit(hw1);
        regfile_.pc += 2;
    }
}
```

### D3A-5：ThumbDisassembler

```cpp
class ThumbDisassembler : public IDisassembler {
public:
    std::string disasm(uint32_t addr, const MemoryBus& mem) const override;
    uint32_t insn_length(uint32_t addr, const MemoryBus& mem) const override;
    // 返回 2 或 4，取决于是否是 Thumb-2 32-bit 指令
};
```

---

## 指令实现计划

### 第一批：线性代码能跑（~5-6 天）

| 指令 | 编码范围 | 注意事项 |
|------|---------|---------|
| MOV (immediate) | 00100 xxx | Rd ← imm8 |
| MOV (register) | 01000110 | 高寄存器变体，不更新标志位 |
| MOVS | 00000 xxx | 更新 N/Z |
| ADD (immediate) | 00110 xxx | Rd ← Rd + imm8 |
| ADD (register) | 0001100 xxx | 3 寄存器 / 2 寄存器变体 |
| ADD (high reg) | 01000100 | 不更新标志位 |
| SUB (immediate) | 00111 xxx | Rd ← Rd - imm8 |
| SUB (register) | 0001101 xxx | |
| CMP (immediate) | 00101 xxx | 只更新标志位 |
| CMP (register) | 01000101 | |
| LDR (immediate) | 01101 xxx | imm5 偏移，word |
| LDR (register) | 0101 100 xxx | Rm 偏移 |
| LDR (literal) | 01001 xxx | PC 相对 |
| STR (immediate) | 01100 xxx | imm5 偏移 |
| STR (register) | 0101 000 xxx | |
| PUSH | 1011 0 10 xxx | 寄存器列表 |
| POP | 1011 1 10 xxx | 寄存器列表 |
| B (unconditional) | 11100 xxx | 11-bit 偏移 |
| BL | 11110 xxx + 11111 xxx | 32-bit，双半字编码 |
| BX | 01000111 0 xxx | |
| NOP | 10111111 00000000 | |

### 第二批：条件 + 移位（~3-4 天）

| 指令 | 编码范围 | 注意事项 |
|------|---------|---------|
| B<cond> | 1101 cond xxx | 8-bit 偏移，条件码 |
| LSL (immediate) | 00000 xxx | 移位后更新标志位 |
| LSR (immediate) | 00001 xxx | |
| ASR (immediate) | 00010 xxx | |
| LDRB/STRB | 0111x xxx / 0101 x 10 | byte 访问 |
| LDRH/STRH | 1000x xxx / 0101 x 01 | halfword 访问 |

### 第三批：Thumb-2（~3-4 天）

| 指令 | 编码 | 注意事项 |
|------|------|---------|
| MOVW | 11110 i 10 0100 XXXX | 16-bit 立即数，编码不连续 |
| MOVT | 11110 i 10 1100 XXXX | 高 16-bit，与 MOVW 配合加载 32-bit 常量 |
| LDR.W | 11111 00 xxx | 32-bit 编码的 LDR |
| STR.W | 11111 00 xxx | 32-bit 编码的 STR |
| MRS | 11110 0 111 011 XXXX | 读特殊寄存器 |
| MSR | 11110 0 110 XXXX | 写特殊寄存器 |

---

## 讨论点和待定事项

| # | 问题 | 何时决定 |
|---|------|---------|
| T3A-1 | SP (R13) 在 RegisterFile 中还是独立字段？ARM 的 SP 有特殊行为（最低 2 bit 始终为 0） | 本 Phase 开始时 |
| T3A-2 | PC 读取时返回当前指令地址 + 4（ARM 流水线行为）还是当前指令地址？ | 本 Phase 开始时（建议 +4，匹配真实 ARM 行为） |
| T3A-3 | `raise_irq()` 在本 Phase 的实现：简单忽略（延迟到 Phase 3B）还是记录 pending？ | 本 Phase 开始时（建议简单忽略） |
| T3A-4 | IT 块：是否在第三批指令中附带实现？还是严格延后？ | 视进度决定 |
| T3A-5 | 指令周期数：每条指令=1 cycle 还是参考真实周期表？ | 建议 1 cycle，Phase 5 评估是否需要调整 |

---

## 隐藏风险

### R3A-1：Thumb 16-bit 编码空间重叠（最高风险）
Thumb 16-bit 编码不是正交的。bits[15:10] 的某些值范围对应多种指令，
需要按 bits[9:6] 甚至更低位进一步区分。解码表实现时需要对照 ARM Architecture Reference Manual
的 Table A5-1 ~ A5-13 逐条验证。

### R3A-2：MOVW/MOVT 立即数编码
MOVW 的 16-bit 立即数 `imm16` 分散在编码的多个位域中：
`imm16 = imm4:i:imm3:imm8`（非连续）。
MOVT 同理。实现时需要仔细拼合。

### R3A-3：BL 双半字编码
BL 是 32-bit 指令，编码为两个 16-bit 半字。
第一个半字包含偏移的高位，第二个半字包含低位。
PC 更新必须在两个半字都解码后进行。
如果第一个半字被当作独立指令解码，会出错。

### R3A-4：高寄存器操作
MOV/ADD/CMP/BX 可以使用高寄存器（R8-R15），
但这些变体通常不更新标志位。混淆「更新标志位」和「不更新标志位」变体会导致条件分支行为错误。

---

## 测试计划

### 指令级测试向量

每个指令至少 2-3 个测试用例：

```
格式：{初始寄存器, 初始flags, 指令} → {期望寄存器, 期望flags}

例：ADDS R0, R1, R2
  R0=0, R1=5, R2=3, flags=0 → R0=8, flags=N=0,Z=0,C=0,V=0
  R0=0, R1=0xFFFFFFFF, R2=1, flags=0 → R0=0, flags=N=0,Z=1,C=1,V=0
```

标志位测试向量参考 ARM Architecture Reference Manual 的 Table A7-6（ADD/SUB 真值表）。

### 集成测试

**测试 1：线性计算**
```asm
  MOVS R0, #3
  MOVS R1, #4
  ADDS R0, R0, R1    ; R0 = 7
  STR  R0, [SP, #-4] ; 存到栈
  B    hang
hang: B hang
```
验证 R0=7，内存对应位置值为 7。

**测试 2：函数调用链**
```asm
  LDR  R0, =3
  LDR  R1, =4
  BL   add_func
  B    hang
add_func:
  ADDS R0, R0, R1
  BX   LR
hang: B hang
```
验证 R0=7，LR 被正确保存/恢复。

**测试 3：循环 + 条件分支**
```asm
  MOVS R0, #0
  MOVS R1, #10
loop:
  ADDS R0, R0, #1
  SUBS R1, R1, #1
  BNE  loop
  ; R0 = 10, R1 = 0
```

## 验收标准

- [ ] CortexM3Core 实现 ICore 全部虚方法
- [ ] 第一批指令全部通过指令级测试向量
- [ ] 第二批指令全部通过
- [ ] 第三批（MOVW/MOVT/LDR.W/STR.W/MRS/MSR）通过
- [ ] 标志位 N/Z/C/V 按 ARM ARM 真值表验证
- [ ] ThumbDisassembler 反汇编输出可读
- [ ] 集成测试（线性计算、函数调用链、循环）通过
- [ ] 断点功能可用
- [ ] 所有测试 ctest 绿色
