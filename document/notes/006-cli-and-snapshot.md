# 006 - CLI 入口与 JSON snapshot(B 条 B1/B2)

> 日期: 2026-06-19
> 阶段: v0.2.0(对应 [01-cli-observability-ai](../milestones/01-cli-observability-ai.md))
> 状态: B1/B2 完成(手测通过,全量 222 回归);B3(events/peripherals)/B4(测试)待续

---

## 背景

之前只有各 example 独立的 `runner.cpp`,没有统一入口,诊断只有人类文本。[01-cli-observability-ai](../milestones/01-cli-observability-ai.md) 要求 `micro-forge run` CLI + JSON snapshot(5 区),让模拟器可被使用、可诊断、AI 可感知。这是产品化(「能用 + 社区反馈」)的关键一步。

## B1 · CLI run 入口

- 新建 `micro-forge` 可执行(`src/cli/main.cpp`),`run` 子命令。
- 自研参数解析(零依赖):`--chip`(仅 stm32f103)/ `<firmware.{elf,bin}>` 位置参数 / `--base`(BIN)/ `--max-steps` / `--trace-mmio` / `--snapshot-json`。
- ELF/BIN 自动识别(ELF magic `0x7F 'ELF'`)。
- **stdout = 固件输出(USART),stderr = 诊断**(状态/fault 摘要)——stdout 可被管道/测试直接断言。
- 退出码:fault/StepError → 1;否则 0;参数错误 → 2。

设计决策:CLI 解析自研、不引 CLI 库(项目零外部依赖倾向)。

## B2 · JSON snapshot

- `src/cli/snapshot.cpp` + `include/cli/snapshot.hpp`,手写序列化(零依赖,不引 nlohmann)。
- 5 区:`cpu`(state/mode/pc/lr/sp/regs R0-R12)/ `fault`(null 或 kind/pc/lr/sp/is_32bit)/ `run`(cycles)/ `peripherals`(B3)/ `events`(B3)。
- 地址/值用小写十六进制字符串,数字(cycles)十进制。
- `--snapshot-json FILE` 写文件。

陷阱:iostream `std::hex` 是 sticky——`hex_kv` 设 hex 后,寄存器编号 `r`(int)输出被污染(r10→"ra")。修法:编号前显式 `std::dec`。

## 附带修正:load_bin reset+launch

SoC `load_bin` 原本只加载不 reset/launch(与 `load_elf` 不一致),BIN 固件不会跑。改为同样 reset+launch。全量 222 测试无回归。

## 验证

- `micro-forge run hello.elf --max-steps N` → stdout `Hello from micro-forge!`,state=Running。
- `micro-forge run hal_uart.elf` → stdout `Hello from STM32 HAL UART`。
- `--snapshot-json` → 有效 JSON(`python json.load` 解析通过),regs r0–r12、fault 区正确。
- 故意 unmapped PC(0x10000000)→ state=Faulted,fault 区 kind=InstructionFetchFault/pc/lr/sp。
- `ctest` 全量 222/222 通过(load_bin 改动无回归)。

## 下一步

- B3:`--trace-mmio` events ring(最近 N 条 MMIO)+ peripherals 区(USART/GPIO/SysTick/NVIC 摘要)。
- B4:CLI E2E 测试(纳入 ctest)。
