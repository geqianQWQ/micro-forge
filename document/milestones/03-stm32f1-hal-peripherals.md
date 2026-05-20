# Phase 7.3 · STM32F1 HAL Peripherals

> 日期: 2026-05-20
> 阶段: v0.6.0 / v0.8.0
> 状态: 待评审
> 依赖: Cortex-M 正确性、CLI/MMIO trace、当前 STM32F103 SoC

---

## 目标

扩展 STM32F103 外设模型，让更多常见 bare-metal 和 STM32 HAL polling/interrupt 示例可运行、可观察、可诊断。

外设路线优先“深度足够支撑真实路径”，不追求一次性铺满 STM32F103 全部外设。

## 当前基线

- RCC 支持常见 clock enable 和 ready flags 的最小模型。
- GPIO 支持 CRL/CRH/IDR/ODR/BSRR/BRR/LCKR 和 pin change callback。
- USART1 支持 SR/DR/BRR/CR1/CR2/CR3，TX 写 DR 可输出字符。
- TIM2 支持基础计数、prescaler、ARR、UIF。
- SysTick、NVIC、SCB、AFIO、FLASH 已能支撑 HAL UART 初始化路径。
- HAL UART polling E2E 已验证 `HAL_Init()`、GPIO init、USART init、`HAL_UART_Transmit()`。

## 能力需求

EXTI / AFIO：

- 支持 EXTI IMR、EMR、RTSR、FTSR、SWIER、PR 的基本读写。
- 支持 PR write-one-to-clear。
- AFIO EXTICR 能选择 GPIO port 到 EXTI line。
- GPIO 输入变化或软件触发能产生对应 pending，并经 NVIC 进入 handler。

USART：

- 支持 TX polling、RX polling、TXE/RXNE/TC 状态位。
- 支持注入 RX 字节，供测试和未来 GUI/CLI 输入使用。
- 支持 RXNE interrupt 和 TXE interrupt 的最小行为。
- 输出通道保留 callback，CLI/GUI 可捕获 USART stdout。

Timer：

- TIM2 update event 能在 DIER.UIE 使能时产生 NVIC 中断。
- SR.UIF clear 行为符合常见 HAL 轮询/中断路径。
- PSC/ARR/CNT 的 tick 行为与 VirtualClock 域联动。
- PWM/capture 可以作为后续扩展，不进入 v1 必需清单。

RCC / FLASH：

- 常见 HSI/HSE/PLL SystemClock_Config 不 timeout。
- CFGR/SWS 能反映系统时钟切换结果。
- FLASH ACR latency/prefetch 写入不 fault。
- FLASH unlock/lock、erase/program 流程至少能按 HAL 状态机完成；真实写 flash 可先限制在受控地址和测试场景。

SPI polling：

- 作为 v1 候选外设，优先 SPI1。
- 支持 CR1/CR2/SR/DR/BRR 的 polling transmit/receive 最小路径。
- 提供外部设备 callback 或 loopback 模式，便于 E2E 验收。

DMA / I2C：

- 标为 v1 stretch 能力。
- DMA 若进入 v1，只做 UART/SPI 内存到外设的最小通道模型。
- I2C 状态机复杂，除非有真实用户固件需求，否则不作为 v1 必需。

## 非目标

- 不模拟 USB、ETH、CAN、SDIO、FSMC 等大外设。
- 不追求 STM32F103 全寄存器位精确。
- 不实现模拟电气特性、真实 baud timing、analog ADC waveform。
- 不要求 HAL 所有模块都可链接并运行。
- 不为了让 HAL 不 fault 而静默吞掉明确非法访问；兼容 no-op 必须可解释并覆盖测试。

## 验收场景

- HAL GPIO EXTI 示例：GPIO 输入变化触发 EXTI handler，handler 内清 PR 并更新计数。
- HAL UART RX polling 示例：CLI 注入字节，固件从 USART1 DR 读出并回显。
- HAL UART IRQ 示例：RXNE interrupt 进入 handler，输出接收到的字符。
- HAL TIM interrupt 示例：TIM2 update interrupt 周期触发，固件计数递增。
- HAL SystemClock_Config 示例：常见 HSE/PLL 配置不 timeout，SystemCoreClock 更新路径可观测。
- FLASH 流程示例：HAL unlock、erase、program、lock 返回成功，并在受控 flash 区读回数据。
- SPI polling 示例：主机发送固定字节序列，loopback 或 mock device 返回预期响应。

## 风险与取舍

- 外设越多，浅模型越容易误导用户；v1 文档必须明确每个外设支持的 HAL 路径。
- EXTI、USART RX/IRQ、Timer IRQ 能显著扩大真实固件覆盖面，优先级高于铺设冷门外设。
- DMA 能力很有价值，但会引入总线仲裁和时序问题；v1 若做，只做最小可解释模型。
- I2C 对状态位顺序敏感，早做容易陷入 HAL 细节；除非有明确样例，不作为 v1 主线。

## 下一步

先用 CLI/MMIO trace 固化每个外设的“可观察验收样例”，再扩展寄存器行为，避免外设实现变成不可诊断的黑箱。
