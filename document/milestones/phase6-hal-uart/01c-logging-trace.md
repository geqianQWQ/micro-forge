# Phase 6.0c · Logging And Trace

## 目标

建立轻量、可复用、可测试的日志和 trace 体系，替换散落的 `fprintf(stderr, ...)`。不引入重型日志框架，先满足模拟器调试和测试捕获。

## 设计原则

- 编译期日志等级过滤继续保留。
- 默认 sink 可以是 stderr，但必须能替换。
- 普通日志和结构化诊断事件分开。
- trace 记录成功和失败访问，不能只记录成功路径。
- 日志系统不拥有核心语义，错误传播仍通过 `expected`。

## 日志 API 建议

| 能力 | 要求 |
|------|------|
| level | `trace/debug/info/warn/error/off` |
| module | `cpu`、`bus`、`gpio`、`loader`、`irq`、`fault` 等 |
| sink | 默认 stderr，测试可替换为 vector/string sink |
| 编译期过滤 | `MF_LOG_LEVEL` 继续可用 |
| 运行时过滤 | 可后续加入，首批可不做 |
| 格式化 | 首批可用 `snprintf` 包装，避免直接散落 `fprintf` |

## Trace 事件建议

MMIO/bus trace 不建议只是字符串。建议定义结构化事件：

| 字段 | 说明 |
|------|------|
| `is_write` | 读或写 |
| `addr` | 绝对地址 |
| `value` | 写入值或读取值 |
| `width` | byte/halfword/word |
| `ok` | 是否成功 |
| `error` | 失败时的 `BusErrorKind` |
| `device` | 可选，外设名或 region 名 |

首批目标：失败访问也能进入 trace sink。

## 迁移范围

| 当前散落输出 | 迁移方向 |
|--------------|----------|
| bus write fail | bus trace failure event + 可选 log |
| CPU fault print | fault record + fault log |
| GPIO pin change | gpio debug/info log，默认可关闭 |
| reset print | cpu/reset debug log |
| runner 输出 | runner 可以保留 stdout/stderr，库代码尽量不直接打印 |

## 必须避免

- 不引入 spdlog/log4cxx 等重依赖。
- 不让日志宏只接受字符串字面量，至少要有格式化出口。
- 不在库代码里新增裸 `fprintf(stderr, ...)`。
- 不让测试依赖 stderr 文本断言。

## 验收

- `rg -n "fprintf\\(|printf\\(|std::fprintf|stderr" include src` 中库代码裸输出显著减少；剩余点必须有理由。
- `include/util/logger.hpp` 支持可替换 sink，测试能捕获日志。
- bus trace 测试覆盖成功 read/write 和失败 unmapped/read-only。
- 默认关闭 GPIO debug 后，运行 GPIO blink 不再刷屏。
- 开启 fault 日志时，CPU fault 输出包含 PC、opcode、fault kind。
- `ctest --test-dir build --output-on-failure` 通过。

## 建议验证命令

```bash
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "fprintf\\(|printf\\(|std::fprintf|stderr" include src
rg -n "LOG_TRACE|LOG_DEBUG|LOG_INFO|LOG_WARN|LOG_ERROR" include src test
```

## 相关文件

| 工作 | 文件 |
|------|------|
| logger API | `include/util/logger.hpp` |
| 可选实现 | `src/util/logger.cpp` |
| MMIO trace | `include/tools/mmio_trace.hpp`、`src/tools/mmio_trace.cpp` |
| bus 接入 | `include/memory/bus.hpp`、`src/memory/bus.cpp` |
| CPU fault 日志 | `src/arch/arm/cortex_m3/cortex_m3.cpp` |
| 测试 | `test/test_tools.cpp`、`test/test_memory_bus.cpp`、`test/test_cortex_m3.cpp` |
