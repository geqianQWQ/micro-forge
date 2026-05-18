# Phase 3A 设计讨论：ARM Cortex-M3 指令集

> 日期: 2026-05-17
> 阶段: Phase 3A
> 状态: 讨论完成，待实施

---

## 待定问题决议（T3A-1 ~ T3A-5）

| # | 问题 | 决议 |
|---|------|------|
| T3A-1 | SP 在 RegisterFile 还是独立字段？ | 放 RegisterFile（R13），`set_register_value(13, val)` 时自动 mask 低 2 位 |
| T3A-2 | PC 读取返回 +4 还是当前地址？ | 返回 +4（匹配 ARM 流水线行为）。regfile 存实际执行地址，`pc()` 返回 `regfile_[15] + 4` |
| T3A-3 | raise_irq 本 Phase 处理方式？ | 简单忽略（空实现或记录 pending），完整中断逻辑留给 Phase 3B |
| T3A-4 | IT 块是否实现？ | 优先级最低，第三批最后做。`B<cond>` 不依赖 IT 块即可工作 |
| T3A-5 | 指令周期数？ | 1 cycle/指令，Phase 5 再评估 |

## 代码组织

- **CortexM3Core 放在 `chips/stm32f1/`**（和已有的 memory_bus.hpp 同级）
- 反汇编器在本 Phase 做，但优先级在指令执行之后

## 指令解码架构：分层 switch + 委托

放弃 ToyCore 的巨型 switch 内联逻辑，采用三层分离：

### 第 1 层：位域提取工具（`thumb_fields.hpp`）

```cpp
namespace micro_forge::cpu::thumb {
constexpr uint8_t rd3(uint16_t insn)  { return insn & 0x7; }
constexpr uint8_t rn3(uint16_t insn)  { return (insn >> 3) & 0x7; }
constexpr uint8_t rm3(uint16_t insn)  { return (insn >> 6) & 0x7; }
constexpr uint8_t imm8(uint16_t insn) { return insn & 0xFF; }
constexpr uint8_t imm5(uint16_t insn) { return (insn >> 6) & 0x1F; }
constexpr uint8_t cond(uint16_t insn) { return (insn >> 8) & 0xF; }
constexpr bool is_32bit_prefix(uint16_t hw1) {
    return (hw1 & 0xE000) == 0xE000 && (hw1 & 0x1800) != 0;
}
}
```

### 第 2 层：解码分派（`cortexm3_core.cpp`）

switch 体只含一行函数调用，函数名对应 ARM 文档的指令类别名：

```cpp
void execute_16bit(uint16_t insn) {
    switch ((insn >> 10) & 0x3F) {
        case 0 ... 3:   exec_shift_add_sub(*this, insn); break;
        case 4 ... 7:   exec_add_sub_cmp_mov_imm(*this, insn); break;
        case 16:        exec_data_proc(*this, insn); break;
        // ...
    }
}
```

### 第 3 层：执行 handler（`thumb_exec.cpp`）

每个 handler 是独立函数，5-20 行：

```cpp
void exec_mov_imm(CortexM3Core& cpu, uint16_t insn) {
    uint8_t rd = thumb::rd3(insn);
    uint32_t val = thumb::imm8(insn);
    cpu.write_reg(rd, val);
    cpu.update_nz(val);
}
```

### 为什么选 switch 而不是策略模式 / 函数指针表

- **switch**：编译器生成跳转表（O(1)），分支预测友好，CPU 模拟器热路径最优
- **函数指针表**：间接调用无法内联，分支预测差
- **策略模式 / CRTP**：30+ 指令产生大量样板代码，虚调用开销不适合热路径
- 核心改进不是分派机制本身，而是**逻辑分离**（switch 只分派，handler 只执行）

## 文件结构规划

```
include/chips/stm32f1/
  thumb_fields.hpp     -- 位域提取 constexpr 工具函数
  cortexm3_core.hpp    -- CortexM3Core 类定义

src/chips/stm32f1/
  thumb_exec_16.cpp    -- 16-bit 指令 handler（第一、二批）
  thumb_exec_32.cpp    -- 32-bit Thumb-2 handler（第三批）
  cortexm3_core.cpp    -- CortexM3Core 类实现 + 解码分派
```

## 下一步

1. 创建 `thumb_fields.hpp` 位域提取工具
2. 实现 `CortexM3Core` 类骨架（继承 CPU）
3. 按三批指令顺序实施（第一批：线性代码能跑 → 第二批：条件+移位 → 第三批：Thumb-2）
4. 每批指令配套测试
5. 最后补 ThumbDisassembler
