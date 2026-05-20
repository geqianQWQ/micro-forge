# Phase 7.0 · v1 Roadmap

> 日期: 2026-05-20
> 阶段: v0.1.0 → v1.0.0
> 状态: 待评审
> 依赖: Phase 6 HAL UART fixture、当前 STM32F103 SoC 基线

---

## 目标

把 micro-forge 从“可以跑 selected STM32F103 binary/HAL 示例”的 v0.1.0 基线，推进到一个可信、可观察、可调试、可自动化的 Cortex-M MCU binary simulator v1.0.0。

v1.0.0 的承诺范围是 STM32F103 / Cortex-M3 纵向打磨，不把 RISC-V、AVR、8051 或 Xtensa 作为交付目标。多架构只保留架构入口和边界设计。

## 当前基线

- 已有 `Stm32f103Soc::create()`，能组装 Flash/SRAM、Bus、Cortex-M3 CPU、NVIC、SCB、SysTick、RCC、GPIO、USART、TIM2、AFIO、FLASH。
- 已有 ELF loader 和裸 binary loader，可以把真实 Cortex-M3 固件装入模拟地址空间。
- 已有 bare-metal hello、GPIO blink、SysTick 和 HAL UART E2E 示例。
- 已有 MMIO trace、fault record、memory dump 等诊断雏形。
- 当前外设和异常模型偏 MVP：可支撑受控示例，不等于完整 STM32F103 或完整 ARMv7-M。

## 能力需求

v1.0.0 应让用户能完成以下工作：

- 用一条 CLI 命令运行 STM32F103 ELF 或 BIN 固件。
- 在固件 fault、HAL timeout、MMIO unmapped、UART 无输出时，拿到结构化诊断。
- 观察 CPU 寄存器、PC、xPSR、handler mode、最近 MMIO、最近 fault、关键外设状态。
- 运行一组文档化的 HAL/bare-metal 示例，并知道每个示例覆盖了什么能力。
- 在不理解内部源码的情况下，根据支持矩阵判断自己的固件是否可能运行。
- 在未来新增 SoC 或架构时，不需要推翻 Machine、Bus、Loader、Trace 的公共边界。

## 非目标

- v1.0.0 不承诺周期精确模拟。
- v1.0.0 不承诺完整 STM32F103 外设全集。
- v1.0.0 不承诺 FreeRTOS 完整任务调度和上下文切换。
- v1.0.0 不实现 RISC-V、AVR、8051、Xtensa CPU。
- v1.0.0 不把 GUI 作为唯一入口；CLI 和结构化 introspection 是 GUI 的前置基础。

## 版本路线

| 版本 | 主轴 | 验收标准 |
|------|------|----------|
| v0.2.0 | CLI + 诊断出口 | `micro-forge run` 可运行 STM32F103 ELF；fault report、MMIO trace、register dump 可从 CLI 获取 |
| v0.3.0 | Introspection / AI snapshot | JSON snapshot 和 recent event ring 稳定；CLI 可输出机器可读状态 |
| v0.4.0 | Cortex-M 异常语义 | MSP/PSP、EXC_RETURN、BASEPRI/FAULTMASK/CONTROL、HardFault/BusFault 基础语义有验收 |
| v0.5.0 | NVIC 与中断可信化 | 优先级、屏蔽、嵌套、SysTick/external IRQ roundtrip 有行为测试 |
| v0.6.0 | STM32F1 HAL 外设扩展 | EXTI、USART RX/IRQ、Timer IRQ、RCC clock config、FLASH 流程跑通受控示例 |
| v0.7.0 | 调试体验 | GUI dashboard 原型读同一份 snapshot/event 数据；CLI 与 GUI 诊断一致 |
| v0.8.0 | HAL 示例扩展 | SPI polling 或 DMA 最小模型择一进入验收；多优化级别固件回归 |
| v0.9.0 | API / 文档冻结 | 支持矩阵、示例、限制、公共入口稳定；清理含混接口和过渡命名 |
| v1.0.0 | 稳定发布 | 文档化承诺全部可测试；受控 STM32F103 固件可运行、可观察、可诊断 |

## 验收场景

- `ctest` 全量通过，且 E2E 示例覆盖 bare-metal 和 HAL 两类固件。
- CLI 能运行 HAL UART 示例并输出预期字符串。
- CLI 能在故意 unmapped MMIO 的固件上输出 fault record、最近 MMIO 和寄存器快照。
- 文档包含支持矩阵：CPU 指令/异常、中断、loader、外设、CLI、GUI、AI snapshot。
- release notes 明确写出支持范围和非目标，避免用户把 v1 理解成完整通用 MCU 仿真器。

## 风险与取舍

- 外设广度和 CPU 正确性会互相争时间；v1 优先 CPU/诊断可信度，外设只做高频 HAL 路径。
- GUI 很适合展示项目价值，但必须复用 CLI/introspection 数据，避免出现两套观测模型。
- 多架构叙事可以吸引关注，但过早实现会稀释 STM32F103 主线；v1 只清理边界和保留入口。
- 诊断输出如果只有人类文本，会限制 AI 分析；v1 必须把 JSON snapshot 作为一等能力。

## 下一步

先落地 `01-cli-observability-ai.md` 和 `02-cortex-m-correctness.md` 中的需求，因为它们共同决定后续外设、GUI 和多架构扩展的基础形状。
