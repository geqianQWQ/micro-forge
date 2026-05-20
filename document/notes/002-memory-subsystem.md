# 002 - 内存子系统

> v0.1.0 开发笔记 | 2026-05-20

## 1. 核心类型与错误模型

### 基础类型（`include/core/types.hpp`）

| 类型 | 定义 | 用途 |
|------|------|------|
| `byte_t` | `uint8_t` | 字节级数据 |
| `half_word_t` | `uint16_t` | 半字数据 |
| `Width` | `enum: Byte=1, HalfWord=2, Word=4` | 访问宽度 |
| `Expected<T>` | `std::expected<T, BusError>` | 内存操作统一错误类型 |

`addr_t` 和 `data_t` 由架构细节头文件定义（ARM Cortex-M3 下均为 `uint32_t`）。

### BusError 枚举

```
Unmapped         — 地址无映射
Unaligned        — 不支持的非对齐访问
ReadOnly         — 写只读区域
InvalidDevice    — WeakPtr 过期或空
RegionOverlap    — map() 区域重叠
OutOfRange       — 偏移超出设备/内存范围
PeripheralFault  — 外设寄存器偏移未实现
```

设计原则：热路径错误对象小而廉价。仅在 Fault 收敛点（CPU FaultRecord）生成诊断上下文。

## 2. Device 外设接口

### periph::Device（`include/periph/device.hpp`）

所有外设和内存设备的抽象基类：

```cpp
class Device {
    virtual Expected<data_t> read(addr_t offset, Width w) = 0;
    virtual Expected<void> write(addr_t offset, data_t data, Width w) = 0;
    virtual void tick(uint64_t cycles);  // 默认空实现
    virtual std::string_view name() const noexcept = 0;
};
```

**关键语义**：`offset` 是相对于外设基地址的偏移，不是绝对地址。Bus 负责地址到偏移的转换。`read` 返回高位置零的数据值。

### WeakPtr 生命周期

每个设备拥有 `WeakPtrFactory<Self>`，通过 `GetWeak()` 获取 `WeakPtr<Self>`。Bus 的 `map()` 接受 `WeakPtr<Device>`。

**约束**：`WeakPtrFactory` 必须是最后一个声明的成员（C++ 成员逆序销毁保证工厂先于对象失效）。设备不可移动/复制。

## 3. FlatMemory

### 实现（`include/memory/flat_memory.hpp`）

- `std::vector<uint8_t>` 字节级存储
- 小端序位操作（非 memcpy）：`data_[o] | (data_[o+1] << 8) | (data_[o+2] << 16) | (data_[o+3] << 24)`
- `load(offset, span)` 用于固件加载（本质上 memcpy 进内部存储）
- 边界检查：`offset + width_bytes > size_` → `BusError::OutOfRange`

继承 `periph::Device`，`name()` 返回 `"FlatMemory"`。

## 4. Bus 地址路由

### Region 结构

```cpp
struct Region {
    addr_t start, end;
    WeakPtr<Device> device;
};
```

### 路由逻辑（`include/memory/bus.hpp`）

- `map(Region)`：线性扫描检查重叠，重叠时返回 `BusError::RegionOverlap`
- `read/write(addr, width)`：
  1. `find_region(addr)` 线性扫描所有 regions
  2. 计算 `offset = addr - region.start`
  3. 委托给 `device->read/write(offset, .../data, width)`
- 未找到区域 → `BusError::Unmapped`
- WeakPtr 过期 → `BusError::InvalidDevice`

当前 SoC 配置约 15 个 Region，线性扫描性能足够。

## 5. Bus Trace 与 MMIO 追踪

### BusTraceEvent

```cpp
struct BusTraceEvent {
    bool is_write;
    addr_t addr;
    data_t value;
    Width width;
    bool ok;
    BusError error;
    std::string_view device;
};
```

`Bus::set_trace(TraceCallback)` 注册回调，记录所有成功和失败的访问。用于调试和 MMIO 行为分析。

### mmio_trace 工具（`include/tools/mmio_trace.hpp`）

封装了 Bus trace 的启用/禁用，输出格式化的 MMIO 访问记录（读/写、地址、值、设备名、成功/失败）。

### memory_dump 工具（`include/tools/memory_dump.hpp`）

将 Bus 上指定地址范围的内容以十六进制转储，用于手动检查内存状态。

## 6. STM32F103 地址映射

完整映射表（`src/chips/stm32f1/` 中实现）：

| 起始地址 | 大小/范围 | 设备 |
|----------|----------|------|
| `0x00000000` | 128KB | Boot 别名（映射到同一 Flash FlatMemory 实例） |
| `0x08000000` | 128KB | Flash（FlatMemory 128KB） |
| `0x20000000` | 20KB | SRAM（FlatMemory 20KB） |
| `0x40000000` | — | TIM2 |
| `0x40010000` | — | AFIO |
| `0x40010800` | — | GPIOA |
| `0x40010C00` | — | GPIOB |
| `0x40011000` | — | GPIOC |
| `0x40013800` | — | USART1 |
| `0x40021000` | — | RCC |
| `0x40022000` | — | FLASH 接口 |
| `0xE000E010` | — | SysTick |
| `0xE000E100` | — | NVIC |
| `0xE000ED00` | — | SCB |

Boot 别名设计：`0x00000000` 和 `0x08000000` 映射到同一个 FlatMemory 实例，ARM 复位时从 0x0 取向量表。

## 7. 非对齐访问策略

**模拟器简化**：`FlatMemory` 在字节级操作，天然支持任意对齐。读 Word 时逐字节拼接，不存在对齐限制。

ARM 精确的对齐异常行为（如 ARMv7-M 的 Unaligned Access UsageFault）未模拟。如有需要可在 Bus 层添加对齐检查，将 `Unaligned` BusError 返回给 CPU。
