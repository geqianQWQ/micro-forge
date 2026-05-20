# 001 - CPU 与指令集

> v0.1.0 开发笔记 | 2026-05-20

## 1. CPU 接口设计

`cpu::CPU` 是所有 CPU 实现的抽象基类（`include/cpu/cpu.hpp`），设计遵循最小接口原则：

- 不暴露 Bus、不暴露 `run()`、不暴露特殊寄存器访问
- Bus 通过构造函数注入，CPU 内部持有 `WeakPtr<Bus>`
- 方法统一返回 `CPUExpected<T>` = `std::expected<T, CPUError>`

**核心虚方法**：`reset()`, `step()`, `state()`, `pc()`, `set_pc()`, `raise_irq()`, `cycles()`, `register_value()`, `set_register_value()`, `register_name()`, `register_count()`

**CPUError 枚举**：`NotRunning`, `RegisterIndexOverflow`, `FailedPollIntr`, `InstructionFetchFault`, `DataAccessFault`, `IllegalInstruction`, `ExceptionEntryFault`, `ExceptionReturnFault`, `InvalidPc`

**CPU::State 枚举**：`Running`, `Halted`, `Faulted`

**RegisterFile**（`include/cpu/regfile.hpp`）：模板类 `Registers<N>`，`std::array<data_t, N>` 存储，带边界检查的 `read/write`，返回 `RegisterExpected<T>`。PC 不在 RegisterFile 中——各 CPU 实现独立管理。

## 2. ToyCore 参考实现

ToyCore 是 ICore/CPU 接口的第一个消费者，用于验证抽象层合理性（`phase2`）。12 条指令的自制 ISA：

`NOP`, `ADD`, `SUB`, `AND`, `LDI`, `LDW`, `STW`, `BZ`, `JMP`, `CALL`, `RET`, `INT/HALT`

- 32 位固定长度编码，`[31:28]` opcode
- 8 个通用寄存器，PC 独立，Z/N 标志位，R7 约定为 SP
- 中断：`pending_irq_` 位掩码 + 向量表跳转 + `raise_irq()` 统一路径
- Fibonacci 程序验证

ToyCore 现在作为回归测试基线保留，证明了 CPU 抽象层不需要为真实 ISA 做修改。

## 3. Cortex-M3 架构

### 类层次

`cpu::arm::cortex_m3::CortexM3CPU` 继承 `cpu::CPU`（`include/arch/arm/cortex_m3/cortex_m3.hpp`）。

### 寄存器

- `reg::Registers<16>`：R0-R15（R13=SP, R14=LR, R15=PC）
- 独立特殊寄存器：`xpsr_`, `primask_`, `basepri_`, `faultmask_`, `control_`, `msp_`, `psp_`
- xPSR `[31:28]`：N/Z/C/V 标志位

### PC 行为

- `read_pc_raw()` 返回实际执行地址
- `pc()` 返回 ARM 语义的 PC 值（受流水线偏移影响）
- PC 位 0 必须为 1（Thumb 模式指示），复位时从向量表加载

### SP (R13)

低 2 位在写入时屏蔽（ARM 规范要求 SP 对齐到 4 字节）。

### step() 流程

```
check_and_handle_interrupt() → fetch16(addr) → 判断16/32位 → execute_16bit/32bit → cycles_++
```

### 关键设计决策

- SP 放在 RegisterFile 中（R13），不做特殊处理——arm SP 行为在 write_reg 中屏蔽低 2 位
- 统一 1 周期指令——不模拟流水线/多周期，简化时序模型
- NVIC 指针通过 `set_nvic()` 注入，不持有引用避免循环依赖

## 4. Thumb/Thumb-2 解码架构

### 三层架构

1. **位域提取**：辅助函数提取 opcode 各字段
2. **解码分派**：`execute_16bit()` / `execute_32bit()` 的 switch 语句
3. **执行处理器**：各 case 分支直接执行

### 16 位检测

`bits[15:10]` 分为 15 个组（移位、算术、数据传输、加载/存储、栈、SP 操作、分支、CBZ/CBNZ 等）。

### 32 位检测

`(hw1 & 0xE000) == 0xE000 && (hw1 & 0x1800) != 0`。两个半字一起解码。

### 为什么用 switch

跳转表优化 + 分支预测友好，对模拟器热路径最优。不用虚分派或函数指针表。

### 代码组织（4 文件拆分）

| 文件 | 行数 | 内容 |
|------|------|------|
| `cortex_m3.cpp` | ~401 | 核心逻辑、复位、标志位更新 |
| `cortex_m3_thumb16.cpp` | ~663 | 16 位 Thumb 指令 |
| `cortex_m3_thumb32.cpp` | ~743 | 32 位 Thumb-2 指令 |
| `cortex_m3_interrupt.cpp` | ~233 | 中断/异常处理 |

总计 ~2040 行。质量约束：单文件不超过 700 行。

## 5. 指令覆盖现状

### 16 位 Thumb（基本完整）

- 移位：LSL/LSR/ASR（立即数 + 寄存器）
- 算术：ADD/SUB（立即数/寄存器/高寄存器）、MUL、ADC/SBC
- 数据传输：MOV（高/低寄存器）
- 加载/存储：LDR/STR（立即数/寄存器偏移）、LDR PC/SP、PUSH/POP
- SP 操作：ADD SP、SUB SP
- 分支：B、B<cond>、BL（序言）、CBZ/CBNZ、BX、BLX
- 其他：NOP、CPS（部分）、SVC

### 32 位 Thumb-2

- 分支：BL/BLX（完整）、B.W、CBZ/CBNZ.W
- 数据处理：MOVW/MOVT、ADD/SUB/MUL（立即数 + 寄存器 + 移位）
- 加载/存储：LDR.B/W、STR.B/W、LDRD/STRD、LDM/STM、PUSH.W/POP.W
- 系统：MRS/MSR（部分）
- 除法：UDIV/SDIV（SDIV 存在已知问题）
- 其他：TBB/TBH、SXTB/SXTH/UXTB/UXTH

### v0.1.0 缺口

- 屏障指令：DMB/DSB/ISB
- CPSIE/CPSID（中断使能/禁用）
- IT 块（部分实现，`it_conditions_` 向量 + `it_condition_pos_`，优先级较低）
- 扩展 MRS/MSR（PRIMASK/BASEPRI/FAULTMASK/CONTROL 完整访问）
- 位域操作：BFI/BFC/SBFX/UBFX
- 乘法扩展：MLA/MLS/SMULL/UMULL
- REV 字节反转
- ADC/SBC 进位修复

## 6. 标志位与条件执行

### xPSR 标志位（[31:28]）

- N（Negative）：结果 bit 31 为 1
- Z（Zero）：结果为 0
- C（Carry）：加法进位/减法借位
- V（oVerflow）：有符号溢出

### 更新方法

- `update_nz(result)`：只更新 N 和 Z
- `update_flags(FlagPostOperation::Add|Sub, a, b, result)`：完整更新 NZCV

### 条件执行

`condition_need_execute(cond)` 覆盖全部 16 种 ARM 条件码（EQ/NE/CS/CC/MI/PL/VS/VC/HI/LS/GE/LT/GT/LE/AL）。

ADDS/SUBS/MOVS 等带 S 后缀的指令更新标志位；不带 S 的（如高寄存器 MOV）不更新。

## 7. Fault 模型

### FaultRecord 结构体（`include/cpu/fault_record.hpp`）

```cpp
struct FaultRecord {
    addr_t pc, lr, sp;          // 故障时 CPU 上下文
    data_t xpsr;
    uint16_t opcode16, opcode16_2;  // 指令操作码
    bool is_32bit;
    CPU::CPUError kind;
    std::optional<BusError> bus_error;   // 总线错误详情
    std::optional<addr_t> access_addr;   // 故障访问地址
    std::optional<Width> access_width;   // 访问宽度
};
```

### Fault 流程

1. 非法指令 → `IllegalInstruction` → `trigger_hardfault()` → `exception_entry_system(3)`
2. 总线错误 → `record_bus_fault()` 保存上下文 → `DataAccessFault`
3. 故障无法恢复 → `try_escalate_fault()` → CPU 进入 `Faulted` 状态
4. `last_fault()` 返回 `std::optional<FaultRecord>` 供外部查询

### ARM 异常编号

Reset=1, NMI=2, HardFault=3, MemManage=4, BusFault=5, UsageFault=6, SVC=11, PendSV=14, SysTick=15, External IRQ n = 16+n

### VTOR 联动

`vector_table_base_` 默认 `0x08000000`（STM32F103 Flash 基地址），SCB VTOR 写入时同步更新 CPU 的 `vector_table_base_`。

## 8. 中断与异常处理

### Pull 模式

NVIC 不持有 CPU 引用。CPU 在每步 `step()` 开始时调用 `check_and_handle_interrupt()` 主动轮询 NVIC。避免循环引用。

### 中断入口流程

1. `has_pending_irq()` → 获取最高优先级 pending IRQ
2. 检查 PRIMASK 和当前优先级
3. 上下文保存：压栈 `{R0, R1, R2, R3, R12, LR, PC, xPSR}`（8 个字），遵循 ARM 规范
4. 从向量表加载 handler 地址，设置 `in_handler_mode_ = true`
5. 返回地址（压栈的 PC）的 bit 0 清零

### EXC_RETURN 检测

`write_pc(value)` 统一检测 EXC_RETURN（`0xFFFFFFF1`, `0xFFFFFFF9` 等），`BX LR` 和 `POP {PC}` 都经过此路径。检测到后触发 `interrupt_return()`。

### 中断返回流程

1. 从栈中恢复 8 个字的上下文
2. 清除 `in_handler_mode_`，恢复 `current_priority_`
3. CPU 继续从恢复的 PC 执行

### HardFault 触发

`trigger_hardfault()` → `exception_entry_system(3)`。用于非法指令等不可恢复错误。`try_escalate_fault()` 在异常入口自身失败时升级为 HardFault。

## 9. 探针模式与质量约束

### 探针模式

`enable_probe_mode(true)` 后，遇到非法指令不触发 HardFault，而是跳过并记录操作码到 `missing_opcodes_`。通过 `missing_opcodes()` 查询缺失指令列表，用于有针对性地实现新指令。

### 质量约束

- 单个 Cortex-M3 `.cpp` 文件不超过 700 行
- 任何优化/重构必须通过全部现有测试
- `rg` 扫描：无裸 `fprintf(stderr, ...)`，无 `value_or(0)` 掩盖错误

## 10. 调试案例

### CBNZ/CBZ 误解码

`CBNZ`（`0xB908`）被误解码为 `POP`，导致 SP 被破坏、PC 跳转到错误地址。根因：16 位解码 switch 中 CBNZ/CBZ 的 case 分支与 POP 编码区间重叠。修复后添加 `CbzAndCbnzBranchWithoutTouchingStack` 回归测试。

### Thumb-2 寄存器移位缺失

`LSL.W r3, r3, ip`（`0xFA03 F30C`）——32 位指令中 Rm 字段需要额外的寄存器移位解码，最初实现遗漏了此路径。修复后 hal_blink 固件正常运行，GPIO PA5 正确翻转。
