# 004 - 仿真框架与工具

> v0.1.0 开发笔记 | 2026-05-20

## 1. 构建系统

### 根 CMakeLists.txt

- CMake 3.25+，C++23（严格 `-std=c++23`）
- 编译选项：`-Wall -Wextra -Werror`
- `file(GLOB_RECURSE)` 自动发现 `src/*.cpp`
- `export_compile_commands` 支持 IDE
- 自动生成架构细节头文件（`arch_details.hpp`）

### 依赖

GoogleTest v1.14.0 通过 FetchContent 获取。

### 示例构建

每个示例有独立的 `firmware/CMakeLists.txt`，使用 `arm-none-eabi-gcc` 交叉编译。编译宏：`STM32F103xB`, `USE_HAL_DRIVER`。

### 并发编译

强制要求 `-j${nproc}`。构建命令：`cmake --build build -j$(nproc)`。

## 2. 日志系统

### 实现（`include/util/logger.hpp`）

宏基础：`LOG_TRACE`, `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`。

### 配置

- 编译时级别过滤：`MF_LOG_LEVEL`（0=Trace → 5=Off）
- 可替换 sink（默认 stderr）：`set_log_sink(LogSink)`
- 每条日志包含级别 + 模块名 + 格式化消息

### 约定

- 库代码不使用裸 `fprintf(stderr, ...)`
- 结构化 trace 事件优先于原始字符串输出
- 测试可注入字符串/vector sink 用于断言

## 3. VirtualClock 与时钟域

### 架构（`include/sim/virtual_clock.hpp`）

```cpp
struct DomainConfig {
    uint32_t freq_hz;  // 域频率
};
```

`VirtualClock` 管理多个时钟域，维护每个域的累积 tick 和余数。

### advance() 算法

```cpp
void advance(uint64_t cpu_cycles) {
    for (auto& domain : domains_) {
        uint128_t ticks = (uint128_t)cpu_cycles * domain.freq_hz / sysclk_freq;
        domain.elapsed_ticks += (uint64_t)ticks;
        domain.residual += (uint128_t)cpu_cycles * domain.freq_hz % sysclk_freq;
        if (domain.residual >= sysclk_freq) {
            domain.elapsed_ticks++;
            domain.residual -= sysclk_freq;
        }
    }
    total_ns_ += cpu_cycles * 1000000000ULL / sysclk_freq;
}
```

使用 `__uint128_t` 避免乘法溢出。余数累积实现零漂移 tick 计算。

### 域操作

- `consume_ticks(domain_index)` — 返回并重置该域自上次调用以来的累积 tick
- `set_domain_freq(domain_index, freq_hz)` — 运行时更新频率（来自 RCC CFGR 写入）
- `domain_freq_hz(domain_index)` — 查询当前频率
- 无效域索引安全返回 0

### STM32F103 时钟域

`ClockDomain` 枚举：`Sysclk=0, Apb1=1, Apb2=2`

默认所有域 8 MHz（复位条件）。SysTick → Sysclk 域，TIM2 → Apb1 域。RCC CFGR 写入触发 `coordinator.clock().set_domain_freq()` 更新。

## 4. SimulationCoordinator

### 实现（`include/sim/coordinator.hpp`）

```cpp
struct Tickable {
    WeakPtr<periph::Device> device;
    size_t domain_index;
};
```

### step() 流程

```
1. last_cycles = cpu_->cycles()
2. cpu_->step()
3. delta = cpu_->cycles() - last_cycles
4. clock_.advance(delta)
5. for each tickable:
     ticks = clock_.consume_ticks(tickable.domain_index)
     if ticks > 0:
       tickable.device->tick(ticks)
```

### run(max_steps)

循环调用 `step()` 直到：
- CPU 状态变为 Halted 或 Faulted
- step() 返回 StepError
- 达到 max_steps

返回 `RunResult`：`Running`, `Halted`, `Faulted`, `StepError`

## 5. 调试工具

### memory_dump（`include/tools/memory_dump.hpp`）

从 Bus 读取指定地址范围，以十六进制格式输出。用于手动检查内存内容（固件加载验证、寄存器状态等）。

### mmio_trace（`include/tools/mmio_trace.hpp`）

通过 `Bus::set_trace()` 捕获所有 MMIO 访问序列。输出格式：`[R/W] addr=0x... val=0x... dev=NAME [OK/ERR:reason]`

用于分析固件的外设访问模式、调试 MMIO 行为异常。

### CPU 探针模式

`cortex_m3_cpu.enable_probe_mode(true)` 后：
- 遇到非法指令不触发 HardFault，跳过并继续
- 记录缺失操作码到 `missing_opcodes_`
- `missing_opcodes()` 返回 `(addr, opcode16, opcode16_2)` 列表
- 用于有针对性地实现新指令

### FaultRecord 查询

`cpu->last_fault()` 返回 `std::optional<FaultRecord>`，包含完整的故障上下文（PC/LR/SP/xPSR/opcode/bus error）。

## 6. 测试策略

### 测试套件（13 个可执行文件，~20 个测试文件）

| 类别 | 测试 | 覆盖范围 |
|------|------|---------|
| 基础设施 | `test_infra` | 类型系统、BusError、CoreState、日志 |
| CPU 抽象 | `test_cpu` | ICore、RegisterFile、ToyCore |
| Cortex-M3 | `test_cortex_m3`, `test_cortex_m3_advanced`, `test_cortex_m3_faults` | 生命周期、基本/高级指令、故障 |
| 中断 | `test_nvic`, `test_systick`, `test_interrupt` | NVIC API、SysTick、端到端中断 |
| 芯片平台 | `test_chip` | STM32F103 内存映射、外设地址 |
| 外设 | `test_stm32f1_periph` | RCC/GPIO/USART/Timer/AFIO/FLASH |
| SoC 集成 | `test_stm32f103_soc` | SoC 创建、内存/外设可达性、复位序列 |
| 加载器 | `test_elf_loader` | ELF 解析、PT_LOAD 段、BSS |
| 仿真 | `test_virtual_clock`, `test_coordinator` | 时钟精度、tick 传播 |
| 端到端 | `test_e2e` | 真实固件执行（GPIO blink、SysTick、HAL UART） |

### 验证规则

- 任何 Cortex-M3 `.cpp` 修改必须通过全部 CPU 测试
- `rg` 扫描禁止：裸 `fprintf(stderr, ...)`、`value_or(0)` 掩盖错误、`BusError::Fault` 滥用
- 运行命令：`ctest --test-dir build --output-on-failure`

## 7. 示例程序

每个示例包含：
- `firmware/` — ARM 固件源码 + CMakeLists.txt + 链接脚本
- `runner.cpp` — 主机端仿真驱动（创建 SoC、加载固件、运行仿真）

### hello_world

裸机 USART 轮询。通过 MMIO 寄存器直接访问 USART1 DR，输出 "Hello"。

### gpio_blink

裸机 GPIO 翻转。RCC 使能 GPIOA 时钟 → 配置 CRL → ODR/BSRR 翻转 PA5。循环控制翻转次数。

### systick

裸机 SysTick 中断。设置向量表（SP + Reset handler + SysTick handler）→ 配置 SysTick CTRL/LOAD → 中断处理中翻转 LED。

### hal_blink

基于 HAL 的 GPIO 翻转。完整 HAL 初始化链（`HAL_Init → SystemClock_Config → MX_GPIO_Init`），使用 `HAL_GPIO_WritePin/HAL_GPIO_TogglePin`。成功输出：`GPIO PA5 toggled 2006 times`。

### hal_uart

基于 HAL 的 UART 传输。完整 HAL 初始化链 + `MX_USART1_UART_Init`，使用 `HAL_UART_Transmit`。输出 `Hello from STM32 HAL UART`。依赖 STM32CubeF1 子模块。

## 8. 代码质量审计摘要

### P0 修复（正确性）

- GPIO `name()` 边界检查（非法 port_id 不越界）
- NVIC `is_enabled()` IRQ 号边界检查
- VirtualClock 无效域索引安全返回
- ELF Loader 1-5 字节尾部块和非 4 对齐 BSS 处理

### P1 修复（健壮性）

- `Machine::load_bin()` 错误传播（LoadError → 用户可见消息）
- DataAccessFault 中保留总线错误上下文（`record_bus_fault`）
- SoC 移除 `static_cast`，使用接口方法
- 外设映射重构为 `configure_peripherals(Bus&, Parts&)` 独立函数

### P2 跟踪项（精度）

- Timer 预分频器余数保留（避免长期漂移）
- VirtualClock `total_ns` 精度提升
- IT 条件队列热路径优化

### 构建卫生

- 固件构建无 `|| true` 掩盖错误
- 示例无源内构建（out-of-source）
