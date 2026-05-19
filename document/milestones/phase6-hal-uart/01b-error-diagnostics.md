# Phase 6.0b · Error And Diagnostics Model

## 目标

在继续补 HAL 所需指令和外设前，先把错误语义从“全部塌缩成 BusFault/Fault”整理成可传播、可诊断、低开销的模型。

核心原则：

- 热路径错误对象保持小而便宜。
- enum 表达错误语义，诊断上下文只在 fault 汇聚点生成。
- 不在每次 bus/device 访问中构造字符串。
- API 边界不吞错，能返回错误就返回错误。

## 分层模型

| 层 | 数据形态 | 责任 |
|----|----------|------|
| Bus/Device | 小 enum 或小 struct | 表达地址/对齐/权限/设备等访问失败原因 |
| CPU execute | `CPUErrorKind` | 表达 fetch/data access/illegal instruction/exception entry 等 CPU 失败类别 |
| Fault record | 只在 fault 发生时生成 | 汇总 PC、opcode、访问地址、寄存器快照、原始错误 |
| 日志/trace | 可选 sink | 输出人类可读诊断，不作为错误传播本体 |

## BusError 细化建议

当前 `BusError::Fault` 太粗。建议拆出：

| 错误 | 含义 |
|------|------|
| `Unmapped` | 总线找不到 region |
| `Unaligned` | width/address 不满足访问要求 |
| `ReadOnly` | 写只读寄存器或只读区域 |
| `InvalidDevice` | region 设备 weak pointer 失效 |
| `RegionOverlap` | map 阶段 region 重叠 |
| `OutOfRange` | region 内设备 offset 越界 |
| `UnsupportedWidth` | 外设不支持该访问宽度 |
| `PeripheralFault` | 外设内部明确 fault |

第一步可以只改 enum，不急着把 `BusError` 扩成大 struct。若需要字段，优先使用小结构：

```cpp
struct BusError {
    BusErrorKind kind;
};
```

只有确认收益明显时，再加入 `addr`、`width`、`access`。

## CPUError 细化建议

当前 `NextInstructionsUnavaliable` 同时承担 fetch 失败、data bus 失败、exception entry 失败。建议拆出：

| 错误 | 含义 |
|------|------|
| `NotRunning` | CPU 状态不允许 step |
| `RegisterIndexOverflow` | 寄存器索引越界 |
| `InstructionFetchFault` | 取指失败 |
| `DataAccessFault` | 指令执行中的数据访问失败 |
| `IllegalInstruction` | 未实现或非法编码 |
| `ExceptionEntryFault` | exception entry 压栈或取 vector 失败 |
| `ExceptionReturnFault` | exception return 出栈或恢复失败 |
| `InvalidPc` | PC 不可用或不满足执行要求 |

命名中现有拼写错误要顺手修正，例如 `Unavaliable`、`Unavaibale`。

## FaultRecord

建议新增轻量只读诊断记录，放在 CPU 层保存最近一次 fault。

首批字段：

| 字段 | 说明 |
|------|------|
| `pc` | fault 发生时 PC |
| `lr` | fault 发生时 LR |
| `sp` | fault 发生时 SP |
| `xpsr` | fault 发生时 xPSR |
| `opcode16` | 第一 halfword |
| `opcode16_2` | 第二 halfword，可选 |
| `is_32bit` | 是否 32-bit Thumb 指令 |
| `kind` | CPU fault kind |
| `bus_error` | 若来自 bus，保存原始 kind |
| `access_addr` | 若是访存失败，保存访问地址 |
| `access_width` | 若是访存失败，保存 width |

## 必须避免

- 不在 `BusError` 中保存长字符串。
- 不在热路径默认收集完整寄存器数组。
- 不把日志打印当成错误传播。
- 不用 `value_or(0)` 掩盖寄存器读取失败。
- 不在 `run()` 这类上层 API 中静默丢弃 step 错误。

## 实施顺序

1. 扩展 `BusError` enum，修正返回点中明显错误的 `Fault`。
2. 扩展 `CPUError` enum，拆分 fetch/data/exception/illegal。
3. 为 CPU 增加最近一次 `FaultRecord` 查询接口。
4. 修改 `SimulationCoordinator::run()` / `Machine::run()`，让停止原因可返回或可查询。
5. 更新测试断言，避免继续依赖大口袋 `Fault`。

## 验收

- `rg -n "BusError::Fault" include src test examples` 的结果显著减少，剩余使用点必须能解释为真实 generic fault。
- `rg -n "NextInstructionsUnavaliable|PCUnavaibale|Unavaliable|Unavaibale" include src test examples` 无结果。
- 取指 unmapped、数据访问 unmapped、写只读寄存器、region overlap 至少各有一个测试覆盖。
- CPU fault 测试能读取最近一次 `FaultRecord`，并断言 PC/opcode/fault kind。
- `ctest --test-dir build --output-on-failure` 通过。

## 相关文件

| 工作 | 文件 |
|------|------|
| bus 错误类型 | `include/core/types.hpp` |
| CPU 错误类型 | `include/cpu/cpu.hpp` |
| bus 错误来源 | `src/memory/bus.cpp`、`src/memory/flat_memory.cpp` |
| CPU fault 汇聚 | `include/arch/arm/cortex_m3/cortex_m3.hpp`、`src/arch/arm/cortex_m3/cortex_m3.cpp` |
| run 边界 | `include/sim/coordinator.hpp`、`src/sim/coordinator.cpp`、`include/chips/machine.hpp`、`src/chips/machine.cpp` |
| 测试 | `test/test_memory_bus.cpp`、`test/test_cortex_m3.cpp`、`test/test_e2e.cpp` |
