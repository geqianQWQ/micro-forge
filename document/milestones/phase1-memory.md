# Phase 1 · 内存子系统

> 预计工期：1 周 | 依赖：Phase 0 | 状态：待实施

## 目标

地址空间能正确路由读写。FlatMemory 支持多宽度访问，MemoryBus 区间映射正确，STM32F103 地址映射配置可用。

---

## 设计决策

### D1-1：FlatMemory 底层存储用 `std::vector<uint8_t>`

字节级存储，所有宽度访问都分解为字节操作。小端字节序。
构造时指定大小，内容初始化为零。

```cpp
class FlatMemory : public IPeripheral {
    std::vector<uint8_t> data_;
    uint32_t size_;
public:
    explicit FlatMemory(uint32_t size);

    std::expected<uint32_t, BusError> read(uint32_t offset, uint32_t width) override;
    std::expected<void, BusError>     write(uint32_t offset, uint32_t val, uint32_t width) override;
    std::string_view name() const override { return "FlatMemory"; }

    // 便捷方法：直接加载二进制数据（固件加载用）
    std::expected<void, BusError> load(uint32_t offset, std::span<const uint8_t> data);
};
```

**小端处理**：
- `read(offset, 4)` → `data_[offset] | (data_[offset+1] << 8) | (data_[offset+2] << 16) | (data_[offset+3] << 24)`
- `write(offset, val, 4)` → 逐字节写入 val 的低字节到高字节

### D1-2：MemoryBus 区间映射

```cpp
struct MemoryRegion {
    uint32_t base;
    uint32_t size;
    IPeripheral* peripheral;
};

class MemoryBus {
    std::vector<MemoryRegion> regions_;
public:
    std::expected<void, BusError> map(uint32_t base, uint32_t size, IPeripheral& p);
    std::expected<uint32_t, BusError> read(uint32_t addr, uint32_t width);
    std::expected<void, BusError>     write(uint32_t addr, uint32_t val, uint32_t width);
};
```

**映射策略**：
- 线性扫描 regions_ 找匹配区间（性能足够，模拟器场景无热点）
- `map()` 检测重叠：新区间与已有区间重叠时返回 `BusError::Fault`
- 找不到区间时返回 `BusError::Unmapped`

**地址转换**：
- `addr` 落入 `[base, base+size)` → `offset = addr - base` → 转发到 `peripheral->read(offset, width)`

### D1-3：非对齐访问处理

ARMv7-M 规范：
- `LDR/STR`（word）：支持非 4 对齐地址，行为是地址对 4 取模决定旋转
- `LDRH/STRH`（halfword）：地址必须 2 对齐，否则 MemManage fault
- `LDRB/STRB`（byte）：任何地址均可

**模拟器简化策略**：
- FlatMemory 在字节级操作，天然支持任意地址
- 不做 ARM 规范的旋转行为，直接按小端拼接字节
- 如果后续需要精确模拟，在 CortexM3Core 的 load/store 指令中处理旋转逻辑

### D1-4：STM32F103 地址映射

```cpp
// stm32f103.hpp
void configure_stm32f103(MemoryBus& bus,
                          FlatMemory& flash,    // 128KB
                          FlatMemory& sram);    // 20KB
// 外设区域在 Phase 4 添加
```

**STM32F103 内存映射**：
```
0x0000_0000 – 0x0007_FFFF   Code/Alias (Boot)     → Flash alias
0x0800_0000 – 0x0801_FFFF   Flash (128KB)          → FlatMemory 128KB
0x2000_0000 – 0x2000_4FFF   SRAM (20KB)            → FlatMemory 20KB
0x4000_0000 – 0x4002_33FF   Peripherals (APB1/2/AHB) → Phase 4 添加
0xE000_0000 – 0xE00F_FFFF   Cortex-M3 Internal     → Phase 3B 添加
```

---

## 讨论点和待定事项

| # | 问题 | 何时决定 |
|---|------|---------|
| T1-1 | Boot alias（0x00000000）是否映射到 Flash 的同一份 FlatMemory？还是独立区域？ | Phase 1 实施时 |
| T1-2 | MemoryBus 是否支持 unmap（运行时移除映射）？ | Phase 4（动态外设映射时） |
| T1-3 | Flash 是否应标记为 ReadOnly？写 Flash 应返回 BusError::ReadOnly？ | Phase 4（加载器需要写 Flash，此处有矛盾） |

---

## 隐藏风险

### R1-1：IPeripheral 多宽度的外设适配
不是所有外设都支持所有宽度。比如 UART 的 DR 寄存器可能只支持 byte 访问。
当前方案：各外设自己在 read/write 中检查 width，不支持时返回 `BusError::Fault`。

### R1-2：0xE000_0000 范围
NVIC/SysTick/SCB 在这个范围。这些不是普通外设——NVIC 需要与 CPU 深度交互。
Phase 3B 实施时需要决定：是把它们当 IPeripheral 挂在 MemoryBus 上，还是让 CPU 直接处理这个范围的访问。

---

## 测试计划

### test_flat_memory.cpp
- 写 word → 读 word，验证值一致
- 写 word → 读 byte/halfword，验证小端字节序
- 写 byte → 读 byte，验证不影响相邻字节
- offset 越界 → BusError::Fault
- load() 加载二进制数据 → 读回验证

### test_memory_bus.cpp
- 映射一个 FlatMemory → 通过绝对地址读写 → 验证路由
- 映射两个不重叠区域 → 各自路由正确
- 重叠映射 → 返回错误
- 未映射地址 → BusError::Unmapped
- 边界地址（区域的第一个和最后一个字节）正确

### test_stm32f103_map.cpp
- configure 后，向 0x08000000 写 → 读回一致（Flash）
- 向 0x20000000 写 → 读回一致（SRAM）
- 向 0x40000000 读写 → BusError::Unmapped（外设未映射）
- 向 0xE0000000 读写 → BusError::Unmapped（NVIC 未映射）

## 验收标准

- [ ] FlatMemory read8/16/32 + write8/16/32 全部测试通过
- [ ] 小端字节序测试通过
- [ ] MemoryBus 路由正确，重叠映射被拒绝
- [ ] 未映射地址返回 `BusError::Unmapped`
- [ ] STM32F103 Flash/SRAM 区域可正确读写
- [ ] 所有测试 `ctest` 绿色通过
