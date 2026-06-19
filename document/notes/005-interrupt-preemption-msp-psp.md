# 005 - 中断抢占与 MSP/PSP 双栈 实现指导

> 日期: 2026-06-19
> 阶段: v0.4.0 / v0.5.0(对应 [02-cortex-m-correctness](../milestones/02-cortex-m-correctness.md))
> 状态: ✅ 已实施(2026-06-19)—— 抢占判定 / MSP-PSP / PRIGROUP / NVIC 缓存全部落地,新增 5 测试,全量 222/222 通过
> 节奏: correctness-first —— 先做对(每步扫描),再立刻上「显然已知优化」(NVIC 增量最高-pending 缓存);tail-chaining 列后续

---

## 目标与范围

让 Cortex-M3 从「单级中断」升级到「可信的抢占式异常模型」,覆盖 ARMv7-M 的抢占/嵌套语义和 MSP/PSP 双栈。这是 [06 进度校准](../milestones/06-progress-assessment-and-replan.md) 第一波 A 条。

**本次做**:① 抢占判定 ② MSP/PSP 双栈 ③ PRIGROUP 优先级分组 ④(做对后立刻)NVIC 增量最高-pending 缓存。

**本次不做 / 后续**:tail-chaining(纯优化,pop-push 行为已正确);late-arrival(单线程同步模拟器天然退化,无需处理,见下)。

## 现状基线(代码证据)

- [cortex_m3_interrupt.cpp:20-22](../../src/arch/arm/cortex_m3/cortex_m3_interrupt.cpp#L20-L22):handler mode 直接 `return {}`,**不抢占**。
- `current_priority_` 在 [:129](../../src/arch/arm/cortex_m3/cortex_m3_interrupt.cpp#L129) 设置、[:199](../../src/arch/arm/cortex_m3/cortex_m3_interrupt.cpp#L199) 复位,**从不参与抢占判定**(死变量)。
- [exception_entry_common:96](../../src/arch/arm/cortex_m3/cortex_m3_interrupt.cpp#L96):`exc_return = in_handler_mode_ ? 0xFFFFFFF1 : 0xFFFFFFF9` —— handler 分支(`0xFFFFFFF1`)其实是对的;**缺的是 thread 分支按 CONTROL.SPSEL 二选 `0xFFFFFFF9`(MSP)/`0xFFFFFFFD`(PSP)**。
- [cortex_m3.hpp:126-127](../../include/arch/arm/cortex_m3/cortex_m3.hpp#L126-L127):`msp_/psp_/control_` 字段都在,但切换逻辑不存在;SP 当前直接存在 RegisterFile R13([笔记 001](001-cpu-and-instruction-set.md) §3)。
- 已就绪、不用动:PRIMASK / FAULTMASK / BASEPRI 三种屏蔽([:23-49](../../src/arch/arm/cortex_m3/cortex_m3_interrupt.cpp#L23-L49))、EXC_RETURN 三值的识别([write_pc:9-17](../../src/arch/arm/cortex_m3/cortex_m3_interrupt.cpp#L9-L17))、`exception_entry_common` 的压栈顺序与 handler 分支 EXC_RETURN。

## 设计

### 台阶 1 · 抢占判定

把 `check_and_handle_interrupt()` 的 handler-mode early return 改造成「活动优先级 vs 候选优先级」比较,让 `current_priority_` 真正干活。

核心规则:
- 定义 **活动优先级** `active_pri = in_handler_mode_ ? current_priority_ : 0xFF`。
- 候选异常(含 SysTick 系统异常与外部 IRQ)的**抢占优先级数值 < active_pri** 时才进入(数值越小优先级越高)。
- **同优先级不抢占**(留 pending,等当前返回后由 tail-chaining 或下一轮处理)。
- PRIMASK / FAULTMASK / BASEPRI 的屏蔽语义保持不变,但注意:它们只屏蔽**可屏蔽异常**(SysTick、外部 IRQ);NMI / HardFault 不可屏蔽。当前 `trigger_hardfault()` 是直接调用不走 `check`,所以 HardFault 路径天然不受影响 —— 改造时不要把 HardFault 也塞回屏蔽判定里。

注意区分「是否屏蔽」与「是否抢占」是两道闸:先过屏蔽闸(PRIMASK/FAULTMASK/BASEPRI/enabled),再过抢占闸(`< active_pri`)。

### 台阶 2 · MSP/PSP 双栈(本条最难点)

#### 不变量(必须用代码守住)

> **R13(RegisterFile[13])在任何时刻都等于「当前活动 SP」。**
> 活动栈 = `in_handler_mode_ ? MSP : (CONTROL.SPSEL ? PSP : MSP)`。

为守住这个不变量,**所有 SP 的读写一律经过一个统一访问器,禁止裸访问 `regs_[13]`**(push/pop/write_reg(13) 全部改道):

```
get_active_sp()  -> in_handler ? msp_ : (control_ & 2 ? psp_ : msp_)
set_active_sp(v) -> 写对应影子;并同步回写到 regs_[13] 保持一致
```

有了访问器,mode / SPSEL 切换的同步就归约成「在切换点 swap 影子与 R13」。切换点只有两处:

1. **异常入口**(thread → handler,活动栈强制切 MSP):
   - 若 thread 当前用 PSP(`CONTROL.SPSEL=1`):先 `psp_ = get_active_sp()`(保存),再 `in_handler_mode_ = true`(此后活动栈=MSP),压栈在 MSP 上进行。
   - 若 thread 当前用 MSP:不用 swap,直接压栈。
   - 设 EXC_RETURN:`0xFFFFFFF9`(thread/MSP)或 `0xFFFFFFFD`(thread/PSP);handler→handler 抢占仍是 `0xFFFFFFF1`(本就对,不动)。
2. **异常返回**(按 EXC_RETURN 恢复活动栈):
   - 弹栈完成后,若 `EXC_RETURN == 0xFFFFFFFD`:save `msp_ = get_active_sp()`,然后切回 PSP(`R13 = psp_`,`in_handler_mode_ = false`)。
   - 若 `0xFFFFFFF9`:活动栈本就是 MSP,只需 `in_handler_mode_ = false`,不 swap。
   - 若 `0xFFFFFFF1`(返回到嵌套 handler):保持 `in_handler_mode_ = true`。

`MSR PSP/MSP/CONTROL` 写入后,若写的是「当前活动栈」对应影子,必须经访问器把 R13 也同步过去(否则 R13 与影子脱节)。MRS 读则直接读影子即可。

> 提示:`push_stack/pop_stack` 当前直接动 R13。改造后它们应调用 `get_active_sp()/set_active_sp()`,从而自然落在正确影子上。这是把双栈做对、且改动可控的关键。

### 台阶 3 · PRIGROUP 优先级分组(抢占判定的前置正确性)

不做分组,抢占判定会用「原始优先级字节」比较,导致**同一抢占组内的中断互相错误抢占**。必须用「分组后的抢占优先级」做抢占比较。

- 抢占优先级 = 原始优先级按 PRIGROUP 右移并掩码后的值。
- STM32F103 优先级实现 4 位(`__NVIC_PRIO_BITS=4`,有效位 `[7:4]`);AIRCR.PRIGROUP(`SCB` 寄存器,[笔记 003](003-peripherals-and-soc.md) §4,复位值 `0xFA050000`)取 `[10:8]`。
- 提供统一 helper,所有抢占比较都走它:
  - `preempt_priority(raw)` = 按 PRIGROUP 计算抢占部分。
  - 抢占闸用 `preempt_priority(候选) < preempt_priority(active)`;同抢占组内的排序(子优先级)只在「决定先服务哪个 pending」时用,**不触发抢占**。
- 最小实现:从 SCB 读 `AIRCR.PRIGROUP`,按 ARMv7-M 规范 B1.5.5 的公式算抢占位数 `preempt_bits = min(4, 7 - PRIGROUP)`(超出的 sub 位在 4 位实现下归零)。建议先跑通默认分组(HAL 默认 `PRIORITYGROUP_4` = 全抢占),再加多分组用例。

### late-arrival:无需处理(写明,免得日后误加)

硬件里 late-arrival 指「压栈 8 字期间更高优先级异常到达时直接服务高的」。我们是单线程同步模拟器,`exception_entry_common` 是一次性原子完成的,压栈中途不会被任何中断打断 —— 所以该场景**天然退化为台阶 1 的普通抢占**,自动正确。**不要为它加任何特殊代码**,加了反而可能引入「压栈到一半改状态」的 bug。

## 阶段 2 · 显然已知优化(做对后立刻上)

把「每步扫描 ISER/ISPR 找最高 pending」换成 NVIC 端增量缓存:

- 在 `NvicPeripheral` 维护 `(highest_pending_irq, highest_pending_pri)`,在 `set_pending(n)` / `clear_pending(n)` / `set_priority` / enable 写入时**增量更新**(比较、取最小优先级)。
- `check_and_handle_interrupt()` 改为读这个缓存做 O(1) 比对,不再每步扫描。
- 注意一致性:缓存失效条件(无 pending 时缓存清空;多个同优先级 pending 时取其一即可,同优先级顺序在 v1 不强求严格 round-robin)。
- 这是纯加速,**不改外部行为**;优化前后跑同一套中断测试,结果必须 bit 一致(可作为优化的回归门槛)。

> 为什么放阶段 2 而不是和台阶 1 同时做:抢占判定的正确性要先被「每步扫描」的简单实现 + 测试钉死,再换缓存实现才有对照基准。先优化再验证正确性,出问题分不清是抢占逻辑错还是缓存错。

## tail-chaining(后续,本次不做)

异常返回时若有 pending 且优先级够,不 pop 直接续(handler→handler)。当前不实现的话会「pop 8 字再 push 8 字」—— **结果正确**,只浪费栈空间和周期。等抢占/双栈稳定、且出现对栈深度敏感的真实固件(如 RTOS 任务多时)再做,优先级低于阶段 2 优化。

## 要改的文件

| 文件 | 改动 |
|------|------|
| [cortex_m3_interrupt.cpp](../../src/arch/arm/cortex_m3/cortex_m3_interrupt.cpp) | `check_and_handle_interrupt` 抢占闸;`exception_entry_common` 入口栈切换 + EXC_RETURN PSP 分支;`interrupt_return` 返回栈切换 |
| [cortex_m3.cpp](../../src/arch/arm/cortex_m3/cortex_m3.cpp) | 引入 `get_active_sp()/set_active_sp()` 访问器;`push_stack/pop_stack/write_reg(13)` 改道;`MSR PSP/MSP/CONTROL` 同步 R13 |
| [cortex_m3.hpp](../../include/arch/arm/cortex_m3/cortex_m3.hpp) | 声明访问器;`preempt_priority()` helper;可能暴露 active SP 来源给 snapshot(见下) |
| [nvic.hpp/.cpp](../../include/periph/nvic.hpp) | 阶段 2 增量缓存;`highest_pending_irq()` 语义不变 |
| [scb.hpp/.cpp](../../src/periph/scb.cpp) | 暴露 `AIRCR.PRIGROUP` 读取(供 `preempt_priority` 用) |

## 测试策略

参照 [02 验收场景](../milestones/02-cortex-m-correctness.md),建议至少:

1. **抢占**:低优先级 TIM2 IRQ handler 中到达高优先级 USART1 IRQ,高优先级 handler 先执行并正确嵌套返回(验证 `current_priority_` 在嵌套层的保存/恢复)。
2. **BASEPRI**:设 BASEPRI 后低优先级 IRQ 保持 pending、高优先级仍可进入。
3. **PSP/SVC**:Thread mode 用 PSP(`CONTROL.SPSEL=1`)的固件触发 SVC,通过 `0xFFFFFFFD` 返回 PSP 栈(验证台阶 2 切换正确,这条最易暴露 R13↔影子脱节)。
4. **PRIGROUP**:同抢占组两个中断不互相抢占;不同组按抢占优先级抢占。
5. **优化回归**:阶段 2 前后跑同一套中断测试,行为 bit 一致。
6. **现有回归**:`-O0/-O2/-Os` 编译的最小 HAL 初始化固件不 fault(抢占/栈改动不应破坏 HAL UART E2E)。

## 验收(对应 02)

- [ ] 低优先级 handler 执行中被高优先级 IRQ 抢占,嵌套返回后原 handler 正确继续。
- [ ] 设 BASEPRI 后低优先级 pending 不丢、高优先级可进。
- [ ] Thread+PSP 固件经 SVC 用 PSP 栈正确返回。
- [ ] PRIGROUP 分组下抢占行为符合规范。
- [ ] 阶段 2 优化前后,中断测试 bit 一致。
- [ ] 既有 217 测试 + HAL UART E2E 全绿。

## 结论 / 下一步

A 条口径已收敛:**抢占判定 + MSP/PSP(访问器不变量)+ PRIGROUP** 先做对,随后立刻上 **NVIC 增量最高-pending 缓存**;tail-chaining / late-arrival 本次不动。实现完成后回到 [06](../milestones/06-progress-assessment-and-replan.md) 把 A 的完成度从 ~20%/40% 推到验收通过,再聊第 2 条(CLI + snapshot)。

> 本文件为指导层,不含完整实现代码,由用户手工实现。
