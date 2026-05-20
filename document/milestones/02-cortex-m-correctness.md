# Phase 7.2 · Cortex-M Correctness

> 日期: 2026-05-20
> 阶段: v0.4.0 / v0.5.0
> 状态: 待评审
> 依赖: 当前 Cortex-M3 CPU、SCB、NVIC、SysTick、FaultRecord

---

## 目标

把 Cortex-M3 从“能跑受控示例”的 MVP 推进到行为可信的 ARMv7-M 子集，重点补齐异常、栈、中断屏蔽、NVIC 优先级和 bit-band。

## 当前基线

- CPU 已能执行常见 Thumb/Thumb-2 指令，并覆盖 HAL UART 所需的大量编译器输出。
- 已有 SVC、HardFault 升级、VTOR 联动、SysTick system exception、external IRQ roundtrip。
- 已有 PRIMASK、BASEPRI、FAULTMASK、CONTROL、MSP、PSP 的读写保存路径。
- NVIC 已支持 enable、pending、clear、priority MMIO 和基础 pending 查询。
- 当前异常模型仍偏简化：handler mode 下不进入新中断，MSP/PSP 影子状态和 R13 一致性需要补强。

## 能力需求

异常与栈：

- Reset 后 MSP 从向量表读取，PC 清除 Thumb bit 后进入 Reset_Handler。
- Thread mode 默认使用 MSP；CONTROL.SPSEL 切换后使用 PSP。
- Handler mode 始终使用 MSP。
- EXC_RETURN `0xFFFFFFF9` 返回 Thread/MSP，`0xFFFFFFFD` 返回 Thread/PSP，`0xFFFFFFF1` 返回 Handler。
- 异常入口保存 R0-R3、R12、LR、PC、xPSR，并设置 LR 为正确 EXC_RETURN。
- 异常返回恢复寄存器、PC、xPSR 和活动栈指针。

中断与屏蔽：

- PRIMASK 屏蔽可屏蔽异常。
- FAULTMASK 屏蔽除 NMI 外的异常；HardFault 语义按 ARM 规则收敛。
- BASEPRI 屏蔽优先级数值大于等于阈值的外部中断。
- AIRCR.PRIGROUP 影响 NVIC 优先级分组，至少在行为排序上可验证。
- 高优先级外部中断可抢占低优先级 handler。
- 同优先级或被屏蔽中断保持 pending，不丢失。

Fault 分流：

- 指令 fetch 失败、data access 失败、illegal instruction 不直接粗暴停机。
- 有对应 fault handler 或 HardFault handler 时进入 handler。
- handler 缺失、vector 为 0、异常入口/返回失败时，CPU 状态进入 Faulted。
- FaultRecord 记录原始原因，不被 HardFault 入口覆盖掉关键信息。

bit-band：

- SRAM bit-band alias `0x22000000-0x23FFFFFF` 映射到 `0x20000000-0x200FFFFF`。
- Peripheral bit-band alias `0x42000000-0x43FFFFFF` 映射到 `0x40000000-0x400FFFFF`。
- alias word 写 0/非 0 改变目标 bit；alias word 读返回目标 bit。

指令覆盖：

- 持续用真实编码单测覆盖新增 Thumb/Thumb-2 指令。
- 对真实 HAL/GCC 产物使用 probe missing op 流程，新增指令以固件触发证据排序。
- 不追求 ARMv7-M 全指令全集；优先覆盖 HAL、CMSIS、常见裸机编译输出。

## 非目标

- 不做周期精确异常 entry/return/tail-chaining 周期模拟。
- 不实现 MPU、FPU 或 TrustZone。
- 不承诺完整 debug monitor、DWT、ITM、ETM。
- 不为了通过单个固件临时吞掉 illegal instruction；缺失指令必须有诊断和测试。
- 不把 Cortex-M 专用异常概念泄漏到通用 Machine 层。

## 验收场景

- Thread mode 使用 PSP 的固件能触发 SVC，并通过 EXC_RETURN 返回 PSP 栈。
- 一个低优先级 TIM2 IRQ handler 中到达高优先级 USART1 IRQ，高优先级 handler 能先执行并返回。
- 设置 BASEPRI 后，低优先级 IRQ 保持 pending，高优先级 IRQ 仍可进入。
- 访问 unmapped data 地址时，有 HardFault handler 的固件继续运行，并可读到 FaultRecord。
- SRAM bit-band 写入能改变目标 SRAM word 的单个 bit；外设 bit-band 能改变 GPIO ODR 或可控测试外设位。
- `-O0`、`-O2`、`-Os` 编译的最小 HAL 初始化固件不因高频指令缺失而 fault。

## 风险与取舍

- MSP/PSP 和 R13 影子状态若没有统一规则，会导致异常返回偶发错误；该项优先级高于新增外设。
- NVIC 完整规范很大；v1 只要求行为足以支撑嵌套、屏蔽、优先级排序，不追周期精确。
- Fault 分流如果过早细分所有 configurable fault，可能扩大范围；先保证 HardFault 语义和诊断可信。
- bit-band 可以在 Bus 层或特殊 Region 层实现；需求层只要求外部行为一致。

## 下一步

完成本阶段后，HAL 外设扩展可以更放心地判断失败原因：外设缺失就是外设缺失，而不是 CPU/异常底层语义漂移。
