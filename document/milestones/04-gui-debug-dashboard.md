# Phase 7.4 · GUI Debug Dashboard

> 日期: 2026-05-20
> 阶段: v0.7.0
> 状态: 待评审
> 依赖: CLI JSON snapshot、recent event ring、fault report

---

## 目标

规划一个轻量 GUI 调试仪表盘，让用户不用读大量日志，也能看见 CPU、内存、MMIO、外设和 fault 的实时状态。

GUI 只消费统一 introspection 数据，不直接复制模拟器内部状态采集逻辑。

## 当前基线

- CLI 和 JSON snapshot 尚未实现，但已有 MMIO trace、FaultRecord、memory dump、USART output callback 等数据来源。
- STM32F103 SoC 已有强类型 `parts()`，可以读取 GPIO、USART、SysTick、NVIC 等状态。
- 现有 examples 和 tests 已能提供 GUI 初期演示固件：hello、GPIO blink、SysTick、HAL UART。

## 能力需求

首版 GUI 是 debug dashboard，不是 IDE：

- 运行控制：load firmware、run、pause、step、reset、stop at max-steps。
- CPU 面板：R0-R15、PC、LR、SP、xPSR、handler/thread mode、current state。
- 指令面板：当前 PC、当前 opcode、最近执行位置；反汇编可后续接入。
- 内存窗口：按地址读取、十六进制显示、支持 Flash/SRAM 常用范围。
- MMIO 日志：最近 N 条读写，按设备名、地址、成功/失败过滤。
- 外设面板：GPIO pin 状态、USART 输出/RX 注入、SysTick CTRL/LOAD/VAL、NVIC pending/enabled 摘要。
- Fault 面板：fault kind、PC、LR、SP、opcode、BusError、访问地址、最近 MMIO。
- Snapshot 导入：能打开 CLI 生成的 JSON snapshot 做离线分析。

GUI 数据刷新策略：

- 运行中按固定 UI tick 读取 snapshot，而不是每条指令刷新。
- step/pause/fault 时立即刷新完整 snapshot。
- MMIO 日志和 USART 输出使用 recent event ring 增量更新。

## 非目标

- 首版不实现完整源码级调试。
- 首版不实现断点、watchpoint、反汇编跳转图或时间旅行调试。
- 首版不绑定具体前端框架；需求层只要求能消费 snapshot/events。
- 首版不要求远程连接真实硬件。
- 首版不绕过 CLI/introspection API 直接访问私有模拟器状态。

## 验收场景

- 运行 GPIO blink 示例，GPIOA pin 5 在 GUI 中随固件翻转。
- 运行 HAL UART 示例，USART 输出面板显示完整字符串。
- 运行 SysTick 示例，SysTick VAL/COUNTFLAG 和 tick 计数能观察到变化。
- 运行故意 fault 的固件，GUI 自动停在 fault 状态并显示 fault 面板。
- 打开 CLI 生成的 JSON snapshot，GUI 能展示 CPU、fault、MMIO、外设摘要。
- GUI 和 CLI 对同一次 fault 的 PC、opcode、最近 MMIO 一致。

## 风险与取舍

- GUI 容易提前吸走精力；必须先稳定 JSON snapshot 和 event 模型。
- 如果 GUI 直接读 C++ 内部对象，未来 CLI/AI/GUI 会出现三套事实来源；统一 introspection 是硬约束。
- 首版 dashboard 可以朴素，但信息密度要高，避免做成营销页或静态展示页。
- 远程服务 API 很适合 GUI，但第一阶段可以用本地进程或文件 snapshot 验证体验。

## 下一步

完成首版 dashboard 需求后，评估是否需要本地服务 API；只有当 GUI 交互需要持续双向通信时，再把 HTTP/WebSocket 纳入后续 milestone。
