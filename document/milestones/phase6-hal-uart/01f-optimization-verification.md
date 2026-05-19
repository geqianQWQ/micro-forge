# Phase 6.0f · Optimization Verification Gates

## 目标

给后续小模型一个固定验收门：每做完一个优化阶段，都必须证明行为没有悄悄坏掉，并证明该阶段的目标真的发生了。

这篇不是功能实现文档，而是执行检查清单。

## 通用守门

每个 01b-01e 阶段完成后至少运行：

```bash
cmake --build build
ctest --test-dir build --output-on-failure
git status --short
```

若本地没有可用 build 目录，先配置：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 日志验收

用于确认日志真的收敛，而不是换一种散落方式：

```bash
rg -n "fprintf\\(|printf\\(|std::fprintf|stderr" include src
rg -n "LOG_TRACE|LOG_DEBUG|LOG_INFO|LOG_WARN|LOG_ERROR" include src test
ctest --test-dir build -R "tools|memory_bus|cortex" --output-on-failure
```

期望：

- 库代码裸 `fprintf/stderr` 数量减少。
- 日志 sink 可在测试中捕获。
- MMIO trace 成功和失败访问都有测试。

## 错误传播验收

用于确认错误没有继续被吞掉：

```bash
rg -n "BusError::Fault" include src test examples
rg -n "value_or\\(0\\)|value_or\\(0xDEAD\\)|\\(void\\).*step|\\(void\\).*reset|\\(void\\).*set_register" include src test examples
rg -n "return;|break;" src/sim src/chips
ctest --test-dir build -R "memory_bus|flat_memory|cortex|e2e" --output-on-failure
```

期望：

- `BusError::Fault` 只保留在确实无法细分的位置。
- 核心库不靠 `value_or(0)` 掩盖错误。
- `run()` 类 API 的停止原因可返回或可查询。

## Fault 语义验收

用于确认 fault 是 ARM 异常语义，而不是直接停机：

```bash
ctest --test-dir build -R "cortex|interrupt|systick|e2e" --output-on-failure
rg -n "trigger_hardfault|interrupt_entry_system|vector_table_base|VTOR" include src test
```

期望：

- HardFault 使用 vector index 3。
- SysTick 使用 vector index 15。
- External IRQ n 使用 vector index 16+n。
- 写 SCB `VTOR` 后 CPU exception vector base 生效。
- 有 handler 时 CPU 继续 Running，无 handler 时 Faulted 且 fault record 可读。

## 文件拆分验收

用于确认拆分真的降低文件复杂度：

```bash
find src/arch/arm/cortex_m3 -name '*.cpp' -print0 | xargs -0 wc -l | sort -nr
rg -n "execute_16bit|execute_32bit|interrupt_entry|fetch16|FaultRecord" src/arch/arm/cortex_m3
ctest --test-dir build -R "cortex|interrupt" --output-on-failure
```

期望：

- 单个 Cortex-M3 `.cpp` 文件不超过 700 行。
- decode、exception、fault、fetch 职责分布清晰。
- 拆分阶段不改变已有指令行为。

## 提交流程建议

每个阶段建议单独提交：

| 阶段 | 提交内容 |
|------|----------|
| 01b | 错误 enum、FaultRecord、错误传播测试 |
| 01c | logger/sink/trace failure event、日志迁移测试 |
| 01d | ARM fault/exception 语义和 VTOR 联动 |
| 01e | Cortex-M3 文件拆分，无行为变化 |

每个提交说明里必须写：

- 做了什么。
- 哪些测试跑过。
- 哪些检查命令证明目标完成。
- 剩余风险是什么。
