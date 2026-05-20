# Phase 7.5 · Extension Architecture

> 日期: 2026-05-20
> 阶段: v0.9.0 / v1.0.0
> 状态: 待评审
> 依赖: Machine/SoC 边界稳定、CLI/introspection 数据模型稳定

---

## 目标

为未来 RISC-V、AVR、8051、Xtensa 或更多 STM32/GD32 芯片预留清晰扩展边界，但 v1.0.0 不实现新的 CPU 架构。

重点是避免把 Cortex-M、NVIC、STM32F103 的假设固化到通用层。

## 当前基线

- `cpu::CPU` 已提供 reset、step、state、register、pc、raise_irq、cycles 等基础接口。
- `chips::Machine` 持有 Bus、CPU、SimulationCoordinator，已经是芯片无关运行骨架的雏形。
- `Stm32f103Soc` 封装具体内存映射、外设实例、CPU 创建和时钟域连线。
- `memory::Bus` 和 `periph::Device` 已是架构中立的 MMIO 抽象。
- 当前 loader 仍偏 ARM ELF；Cortex-M3 CPU 仍直接依赖 NVIC 具体类型。

## 能力需求

通用层边界：

- `Machine` 不知道 STM32、Cortex-M、NVIC、SysTick。
- `Bus` 只负责地址路由、trace、read/write，不承担架构异常语义。
- `CPU` 接口不暴露 Cortex-M 专用寄存器；专用状态通过架构 snapshot 扩展表达。
- `SimulationCoordinator` 只按 clock domains 驱动 CPU 和 tickable devices。

架构适配：

- Cortex-M reset、异常入口、异常返回、中断屏蔽属于 Cortex-M adapter 或 Cortex-M CPU 内部，不进入 Machine。
- 中断控制器抽象应允许 NVIC、PLIC/CLINT、AVR SFR interrupt、8051 IE/IP 这类模型各自适配。
- `CortexM3CPU::set_nvic()` 作为过渡接口保留时，应在文档中标注为 Cortex-M/STM32 专用 wiring。

SoC / Board 分层：

- SoC package 负责 CPU、内存映射、片上外设、时钟域、中断线。
- Board package 负责 LED、按钮、UART stdout、外部 flash、传感器、晶振等板级连接。
- STM32F103 继续作为第一个 SoC package；BluePill 之类真实板级包可在用户需求稳定后加入。

Loader 扩展：

- ELF parser 保留 machine 字段检查。
- 未来可按 ELF machine 分派到 ARM、RISC-V、AVR 等 loader policy。
- v1 只保证 ARM Cortex-M3 ELF/BIN 路径；其他 machine 返回清晰错误和支持矩阵提示。

进入新架构的条件：

- 有真实用户固件或明确 demo 目标。
- 当前 STM32F103 v1 承诺稳定。
- 新架构不会要求重写 Machine、Bus、Trace、CLI snapshot。
- 至少能定义一个最小 SoC package 和两个 E2E 示例。

## 非目标

- v1.0.0 不实现 RISC-V CPU。
- v1.0.0 不实现 AVR/8051/Xtensa CPU。
- v1.0.0 不设计纯 JSON 描述即可生成完整芯片的系统。
- v1.0.0 不承诺插件 ABI 稳定。
- 不为了未来扩展牺牲当前 STM32F103 的类型安全和调试体验。

## 验收场景

- 文档支持矩阵明确写出 v1 只支持 ARM Cortex-M3 / STM32F103。
- 通用 `Machine` 文档不出现 NVIC、SysTick、STM32 专属职责。
- STM32F103 SoC 文档明确区分 SoC responsibilities 和 Board responsibilities。
- loader 遇到非 ARM ELF 时返回明确错误，而不是崩溃或误加载。
- 新增一个架构扩展设计草图时，不需要修改 Bus、Device、MMIO trace 的核心概念。
- 当前所有 STM32F103 示例继续通过，说明边界清理没有破坏主线。

## 风险与取舍

- 太早做插件化会让项目复杂化；v1 只要求 C++ package 级扩展边界清楚。
- 纯数据驱动芯片描述很诱人，但外设行为和中断语义仍需要代码，暂不作为 v1 方向。
- `CPU` 接口如果过度抽象，会失去 Cortex-M 调试信息；应通过 snapshot 扩展暴露专用状态，而不是污染通用接口。
- 多架构宣传可以保留在愿景层，但 release 承诺必须聚焦当前能验证的 STM32F103 能力。

## 下一步

v1.0.0 发布后，如果项目已有用户关注或真实 RISC-V/AVR 固件样例，再以独立 milestone 评估第二个架构；优先选择能复用现有 CLI、Bus、Trace、Snapshot 的目标。
