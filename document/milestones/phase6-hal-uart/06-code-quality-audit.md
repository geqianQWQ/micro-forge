# Phase 6.5 · Modern C++ Code Quality Audit

## 目标

建立一套可重复执行的代码质量审查门，覆盖抽象、错误传播、生命周期、边界安全、重复实现、性能和构建卫生。

这篇不是立即重写清单。它用于后续质量修复阶段：每个问题先分级、补测试或复现，再做小步重构，避免在 HAL UART 通过后继续累积隐性技术债。

当前基线：

- `ctest --test-dir build --output-on-failure` 已通过 203/203。
- `clang-tidy` 对代表文件扫描噪声较大，但已确认 swappable parameters、magic numbers、`[[nodiscard]]`、unnecessary copy 等问题类型。

## 扫描入口

先用 `rg` 做稳定、低成本的项目扫描：

```bash
rg -n "configure_peripherals|Expected<void> .*\\([^)]*,[^)]*,[^)]*,[^)]*,[^)]*" include src test examples
rg -n "value_or\\(0\\)|value_or\\(0x|return \\{\\};.*Reserved|std::expected<.*std::string" include src test examples
rg -n "static_cast<.*CPU\\*>|new cpu::|assert\\(|\\|\\| true|const_cast|reinterpret_cast" include src test examples
rg -n "if \\(w != Width::Word\\)|switch \\(offset\\)|PeripheralFault|Reserved" include src test examples
find examples -path '*/build/*' -print
```

代表文件可用 `clang-tidy` 交叉验证，但不要求一次清完所有风格项：

```bash
clang-tidy -p build src/chips/stm32f1/stm32f103_soc.cpp --checks='bugprone-*,performance-*,modernize-*,readability-*'
clang-tidy -p build src/chips/stm32f1/stm32f1_gpio.cpp --checks='bugprone-*,performance-*,modernize-*,readability-*'
clang-tidy -p build src/loader/elf_loader.cpp --checks='bugprone-*,performance-*,modernize-*,readability-*'
clang-tidy -p build src/sim/virtual_clock.cpp --checks='bugprone-*,performance-*,modernize-*,readability-*'
```

## 本次扫到的高优先级风险

| 优先级 | 问题 | 要求 |
|--------|------|------|
| P0 correctness | `Stm32f1Gpio::name()` 对非 `A-E` 端口可能访问 `names[-1]` | 先补非法端口测试，再修正索引类型和范围检查 |
| P0 correctness | `NvicPeripheral::is_enabled()` 未检查 `irq_n < kMaxIrq` | 对所有 public IRQ 查询入口统一边界策略 |
| P0 correctness | `VirtualClock::consume_ticks/set_domain_freq/domain_freq_hz` 只靠 `assert` 防越界 | release 下不能 UB；改为返回错误、忽略并可诊断，或明确不可达并封闭调用入口 |
| P0 correctness | binary/ELF loader 手写 4 字节分块，3 字节尾块和 BSS 非 4 对齐有写错风险 | 新增 payload 1/2/3/4/5 字节和非对齐 BSS 测试 |
| P1 diagnostics/API | `Machine::load_bin()` 抹掉底层 `LoadError`，多处返回 `std::expected<..., std::string>` | 边界层可转字符串，内部优先 typed error |
| P1 diagnostics/API | CPU load/store 把 bus failure 塌缩成 `DataAccessFault` | fault record 至少保留底层 `BusError` 或访问地址 |
| P1 diagnostics/API | `AFIO/FLASH` reserved 写入无条件成功，SCB AIRCR 错 key 静默成功 | 保留硬件兼容行为时必须写明原因并覆盖测试 |
| P1 maintainability | `configure_peripherals` 参数过长且映射样板重复 | 改为 `Stm32f103Parts&` 或 descriptor/table-driven mapping，并引入 `map_checked` |
| P1 coupling | SoC 使用裸 `new`、`static_cast<CortexM3CPU*>`、回调捕获裸指针 | 收敛到窄接口或持有明确 typed accessor，避免隐式生命周期契约 |
| P2 performance | `Stm32f1Timer::tick()` 丢弃 prescaler 余数，`VirtualClock::total_ns` 整数 ns 截断，`it_conditions_.erase(begin())` 热路径移动 | 有测试或 profiling 证据后分批修 |
| P2 build hygiene | `examples/hal_uart/firmware/build/` 出现在源码树，HAL firmware CMake 有 `objcopy ... || true` 和无效 self-copy | `.gitignore` 覆盖嵌套 build；构建失败不吞错；copy 目标必须有效 |

## 检查要点

### 抽象和接口

- 5 个以上参数的函数必须审查是否应该引入上下文对象、descriptor 或窄接口。
- SoC 组装层不得把所有具体外设散成位置参数；芯片部件应由 `Stm32f103Parts` 或映射表统一描述。
- `addr_t`、`data_t` 连续参数容易互换，重要 public API 需要命名结构或 helper 降低误用。

### 错误传播和诊断

- 不允许在核心库用 `value_or(0)` 掩盖本应不可能的错误，除非旁边说明原因并有测试。
- 内部错误优先使用 enum/struct；字符串只在 runner、CLI、test failure message 等边界层生成。
- 保留 reserved register read-as-zero/write-ignore 时，要标注硬件兼容或 HAL 兼容依据，不写“为了不 fault”。

### 生命周期和耦合

- `WeakPtr` dereference 前必须有清晰有效性检查，不能只依赖 assert。
- 避免裸 `new` 和向下 `static_cast` 作为长期接口；如果 SoC 需要 Cortex-M3 特有能力，提供明确 accessor 或专用连接函数。
- 回调捕获裸指针时必须证明被捕获对象生命周期长于回调，或改用 `WeakPtr`/引用封装。

### 边界和 UB

- public 方法不能只靠 `assert` 防非法索引。
- 数组索引必须先转换为无符号安全范围或显式检查，避免 `char`、`uint8_t`、`size_t` 混用产生绕回。
- loader、memory、bus 的地址加法需要检查 overflow 和尾部粒度。

### 重复实现

- 外设 `Width::Word` 检查、寄存器 offset switch、read/write 保存寄存器模式应抽成局部 inline helper 或 constexpr register descriptor。
- RCC 地址区间和位号不应在 `is_clock_enabled()`、`enable_clock()`、测试和映射层重复造轮子。
- CPU `rr/br/bw` lambda 在 16-bit 和 32-bit decoder 中重复，应评估移为 private inline helper。

### 性能和时钟准确性

- hot path 上的容器头部 erase、线性 bus lookup、`std::function` 回调只在证据充分时优化，但必须记录风险。
- 计时逻辑不能因整数截断持续漂移；如果当前只是调试输出，要在接口注释中说明精度边界。
- prescaler、residual、tick 累积应有跨多次调用的测试。

### 构建卫生

- 源码树中不应保留 `build/`、`CMakeFiles/`、`.elf`、`.bin` 等生成产物。
- CMake custom command 不允许用 `|| true` 吞掉关键产物失败。
- 示例 firmware 的构建目录应只落在主 build tree 下，避免 in-source build 被误提交。

## 验收清单

- [ ] 新增本 milestone 文档并通过 `git diff --check`。
- [ ] P0 项均有单测或最小复现。
- [ ] 修复后 `ctest --test-dir build --output-on-failure` 通过。
- [ ] `configure_peripherals` 不再依赖长参数列表，外设映射职责集中。
- [ ] loader 覆盖 payload 1/2/3/4/5 字节和非 4 对齐 BSS。
- [ ] GPIO 非法端口、NVIC invalid IRQ、VirtualClock invalid domain 有明确行为。
- [ ] HAL firmware 构建失败不再被 `|| true` 吞掉。
- [ ] 若保留 reserved register 宽松行为，测试和注释能说明兼容原因。

## 执行建议

建议按提交拆分：

| 提交 | 内容 |
|------|------|
| 1 | P0 边界和 loader correctness 测试 |
| 2 | P0 修复，不改变 public 行为以外的语义 |
| 3 | SoC/peripheral mapping 抽象收敛 |
| 4 | 错误传播和诊断增强 |
| 5 | 构建卫生和低风险性能修正 |

每个提交说明里写清楚：

- 修了哪一类质量问题。
- 跑过哪些测试和扫描命令。
- 哪些宽松行为是有意保留。
- 剩余风险是什么。
