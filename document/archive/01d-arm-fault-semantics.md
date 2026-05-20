# Phase 6.0d · ARM Fault Semantics

## 目标

让 CPU fault 优先按 Cortex-M 异常语义模拟，而不是一遇到错误就把模拟器停成 `Faulted`。

模拟器最终仍然需要 `Faulted` 状态，但它应表示 CPU 已无法继续模拟，例如 exception entry 失败、handler 缺失、嵌套 fault 无法处理，而不是表示“发生了一个可处理的 ARM fault”。

## Exception 编号

必须统一 system exception 和 external IRQ：

| 事件 | Exception number | Vector index |
|------|------------------|--------------|
| Reset | 1 | 1 |
| NMI | 2 | 2 |
| HardFault | 3 | 3 |
| MemManage | 4 | 4 |
| BusFault | 5 | 5 |
| UsageFault | 6 | 6 |
| SVC | 11 | 11 |
| PendSV | 14 | 14 |
| SysTick | 15 | 15 |
| External IRQ n | 16 + n | 16 + n |

`HardFault`、`SVC`、`SysTick` 必须走 system exception entry，不允许走 external IRQ helper。

## 首批 fault 映射

| 来源 | 首批行为 |
|------|----------|
| 取指 unmapped/unaligned | 记录 InstructionFetchFault，尝试 BusFault 或 HardFault |
| 数据访问失败 | 记录 DataAccessFault，尝试 BusFault 或 HardFault |
| 未实现指令 | 记录 IllegalInstruction，尝试 UsageFault 或 HardFault |
| exception entry 压栈失败 | 记录 ExceptionEntryFault，升级 HardFault 或最终 Faulted |
| handler vector 为 0 | 记录 invalid vector，最终 Faulted |

若暂不实现 configurable fault enable 位，可以先统一升级到 HardFault，但 vector index 必须正确。

## FaultRecord 与真实异常

真实 CPU 不会提供调试字符串，但模拟器应该保留最近一次 fault 诊断：

- 进入 handler 前记录 fault cause。
- handler 能正常执行时，CPU state 仍为 `Running`。
- handler 缺失或 entry 失败时，CPU state 才为 `Faulted`。
- runner/test 可读取最近一次 fault record 定位问题。

## VTOR 联动

SCB `VTOR` 写入必须同步 CPU vector table base：

| 操作 | 预期 |
|------|------|
| reset 默认 | 从 boot alias 或 flash vector table 取 SP/PC |
| 写 `SCB->VTOR` | 后续 system exception 和 external IRQ 都使用新 base |
| SysTick | vector index 15 |
| external IRQ 0 | vector index 16 |

## 必须修正的已知风险点

- `trigger_hardfault()` 不能调用 external IRQ entry。
- CPU 内部 `vector_table_base_` 不能与 SCB `VTOR` 脱节。
- `interrupt_entry()` 和 `interrupt_entry_system()` 的重复压栈逻辑需要统一，避免行为漂移。
- fault 诊断不能只打印 stderr，必须可查询。

## 验收

- HardFault 测试证明读取 vector index 3，而不是 index 19。
- SysTick 测试证明读取 vector index 15。
- External IRQ 0 测试证明读取 vector index 16。
- 写 `SCB->VTOR` 后，SysTick/IRQ 使用新 vector base。
- 一个有 HardFault handler 的固件触发非法指令后，CPU 进入 handler 且 state 仍为 `Running`。
- 一个 HardFault vector 为 0 的固件触发非法指令后，CPU state 为 `Faulted`，最近 fault record 可读。
- `ctest --test-dir build --output-on-failure` 通过。

## 相关文件

| 工作 | 文件 |
|------|------|
| CPU exception entry | `include/arch/arm/cortex_m3/cortex_m3.hpp`、`src/arch/arm/cortex_m3/cortex_m3.cpp` |
| reset/vector base | `include/arch/arm/cortex_m3/cortex_m3_reset.hpp`、`src/arch/arm/cortex_m3/cortex_m3_reset.cpp` |
| SCB VTOR | `include/periph/scb.hpp`、`src/periph/scb.cpp` |
| SoC wiring | `src/chips/stm32f1/interrupt_config.cpp`、`src/chips/stm32f1/stm32f103_soc.cpp` |
| 测试 | `test/test_interrupt_roundtrip.cpp`、`test/test_cortex_m3.cpp`、`test/test_e2e.cpp` |
