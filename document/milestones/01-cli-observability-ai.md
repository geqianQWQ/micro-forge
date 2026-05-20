# Phase 7.1 · CLI Observability And AI Interface

> 日期: 2026-05-20
> 阶段: v0.2.0 / v0.3.0
> 状态: 待评审
> 依赖: Machine/SoC 运行入口、MMIO trace、FaultRecord、memory dump

---

## 目标

建立 micro-forge 的命令行入口、结构化诊断出口和 AI 可感知接口，让用户和自动化工具能稳定理解模拟器底层发生了什么。

第一阶段采用 stdout/file JSON snapshot 和 event 输出，不承诺 HTTP 服务、WebSocket、MCP 或远程调试协议。

## 当前基线

- `Machine::load_elf()`、`Machine::load_bin()` 和 `Machine::run()` 已提供基础运行骨架。
- `Stm32f103Soc` 已封装 STM32F103 创建、加载和运行。
- `Bus` 已有 trace callback，`tools::enable_mmio_trace()` 能把访问转成结构化 `MmioAccess`。
- CPU 已保存最近一次 `FaultRecord`，包含 PC、LR、SP、xPSR、opcode、bus error、访问地址和访问宽度。
- `tools::memory_dump()` 已能通过 Bus 读取内存并输出文本。

## 能力需求

CLI 首批命令面向 STM32F103：

```text
micro-forge run --chip stm32f103 firmware.elf
micro-forge run --chip stm32f103 firmware.bin --base 0x08000000
micro-forge run --chip stm32f103 firmware.elf --max-steps 100000
micro-forge run --chip stm32f103 firmware.elf --trace-mmio
micro-forge run --chip stm32f103 firmware.elf --snapshot-json snapshot.json
micro-forge dump-mem --chip stm32f103 firmware.elf --addr 0x20000000 --len 256
```

CLI 输出分两类：

- 人类可读输出：运行状态、USART stdout、fault 摘要、关键提示。
- 机器可读输出：JSON snapshot、MMIO events、fault report、register dump。

JSON snapshot 至少表达这些信息：

- CPU：PC、LR、SP、xPSR、通用寄存器、运行状态、handler/thread mode。
- Fault：最近 fault kind、opcode、访问地址、访问宽度、底层 BusError。
- Events：最近 N 条 MMIO 读写，包含地址、宽度、值、设备名、成功/失败。
- Peripherals：USART 输出缓存、GPIO 输出位、SysTick CTRL/LOAD/VAL、NVIC pending/enabled 摘要。
- Run：已执行 steps、CPU cycles、停止原因、ELF/BIN 加载信息。

AI 接口的目标不是让 AI 控制模拟器，而是让 AI 能回答：

- 程序是 fault、halt、timeout，还是仍在运行。
- 最近一次失败 MMIO 是什么。
- UART 没输出是因为没写 DR、TXE 没置位，还是程序没走到 UART。
- SysTick 是否配置并触发。
- GPIO/RCC/AFIO 是否完成常见 HAL 初始化。
- fault 前的 PC、LR、SP 和最近 MMIO 是否足够定位缺失外设或缺失指令。

## 非目标

- 不在第一阶段实现交互式 GDB remote protocol。
- 不在第一阶段实现 HTTP/WebSocket 常驻服务。
- 不在第一阶段实现 MCP server。
- 不要求 JSON schema 覆盖所有未来外设；先覆盖当前 STM32F103 MVP 和 HAL 调试路径。
- 不把 CLI 绑定到 GUI，GUI 后续复用同一套 introspection 数据。

## 验收场景

- 运行 HAL UART ELF，CLI 输出 USART 字符串，并返回运行完成或 max-steps 到达状态。
- 对启用 `--trace-mmio` 的运行，输出能包含 USART1/GPIO/RCC 访问事件。
- 对故意访问 unmapped 地址的固件，CLI 退出时输出 fault report，包含 PC、opcode、BusError 和访问地址。
- `--snapshot-json` 生成可解析 JSON，包含 CPU、fault、recent_mmio、peripherals、run 五个顶层区域。
- JSON 输出中地址和值统一使用十六进制字符串，便于人类阅读和 AI 对齐。
- 没有启用 trace 时，正常运行不产生大量噪音；启用 trace 时能保留最近事件用于 fault 诊断。

## 风险与取舍

- 过早定义庞大 JSON schema 会拖慢迭代；第一版只承诺调试必需字段，字段可向后兼容扩展。
- CLI 需要稳定错误码，但内部错误仍应保留 enum/struct，字符串只在边界层生成。
- recent event ring 的大小会影响内存和诊断质量；默认值应覆盖 HAL 初始化末尾到 fault 的常见窗口。
- AI 适合读结构化状态，不适合从散乱日志推断；因此 JSON snapshot 优先级高于美化文本日志。

## 下一步

完成 CLI 和 JSON snapshot 后，GUI dashboard 只需要读取同一套 snapshot/events，不再重复设计状态采集路径。
