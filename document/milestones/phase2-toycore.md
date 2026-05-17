# Phase 2 · ToyCore（自制 ISA）

> 预计工期：1-1.5 周 | 依赖：Phase 1 | 状态：待实施

## 目标

用约 10 条自制指令的简单 CPU 验证 ICore 接口的硬件无关性。
ToyCore 是 ICore 的第一个实现——如果 ICore 不需要修改就能支持 ToyCore，则接口设计成功。

---

## 设计决策

### D2-1：自制 ISA 指令集

约 10 条指令，全部 32-bit 固定长度，寄存器-寄存器或寄存器-立即数格式。

```
指令          编码格式                    语义
─────────────────────────────────────────────────────
MOV Rd, imm   [OPC|Rd|imm16]            Rd ← imm (16-bit)
MOV Rd, Rs    [OPC|Rd|Rs|0000]          Rd ← Rs
ADD Rd, Rs    [OPC|Rd|Rs|0001]          Rd ← Rd + Rs，更新 Z/N 标志
SUB Rd, Rs    [OPC|Rd|Rs|0010]          Rd ← Rd - Rs，更新 Z/N 标志
LOAD Rd, [Rs] [OPC|Rd|Rs|0011]          Rd ← mem[Rs]（读 32-bit）
STORE [Rd],Rs [OPC|Rd|Rs|0100]          mem[Rd] ← Rs（写 32-bit）
JMP addr      [OPC|addr24]              PC ← addr
JZ  addr      [OPC|addr24]              if Z flag: PC ← addr
CALL addr     [OPC|addr24]              push PC; PC ← addr
RET           [OPC|000...0]             PC ← pop
INT n         [OPC|n|000...0]           软件中断 n
HALT          [OPC|000...0]             停机，state → Halted
```

**寄存器**：8 个通用寄存器 R0-R7，PC 独立（在 RegisterFile 外）。
**标志位**：Z（零）、N（负），存储在 ToyCore 内部（不在 RegisterFile 中）。
**栈**：R7 默认作为 SP（约定，非硬件强制）。栈向低地址增长。

### D2-2：跳过汇编器

测试程序以 `std::vector<uint32_t>` 硬编码指令序列。12 条指令手动编码很快。
如果后续需要，可以写一个简单的文本→二进制转换器，但不作为本 Phase 交付物。

### D2-3：ToyCore : ICore 结构

```cpp
class ToyCore : public ICore {
    RegisterFile<8> regfile_;
    MemoryBus& bus_;
    CoreState state_ = CoreState::Halted;
    uint64_t cycles_ = 0;
    bool flag_z_ = false;
    bool flag_n_ = false;

    // 中断向量表（简单数组）
    std::array<uint32_t, 16> interrupt_vectors_{};

    // 内部方法
    uint32_t fetch();
    void execute(uint32_t insn);
    void push_stack(uint32_t val);
    uint32_t pop_stack();
public:
    explicit ToyCore(MemoryBus& bus);

    // ICore
    void reset() override;
    void step() override;
    CoreState state() const override;

    uint32_t reg(uint8_t idx) const override;
    void set_reg(uint8_t idx, uint32_t v) override;
    uint8_t reg_count() const override;
    std::string reg_name(uint8_t idx) const override;

    uint32_t pc() const override;
    void set_pc(uint32_t addr) override;

    void raise_irq(uint8_t irq_n) override;
    uint64_t cycles() const override;

    // 中断向量表配置
    void set_interrupt_vector(uint8_t irq_n, uint32_t handler_addr);
};
```

### D2-4：ToyIsaDisassembler : IDisassembler

```cpp
class ToyIsaDisassembler : public IDisassembler {
public:
    std::string disasm(uint32_t addr, const MemoryBus& mem) const override;
    uint32_t insn_length(uint32_t addr, const MemoryBus& mem) const override;
    // 固定 4 字节指令，insn_length 始终返回 4
};
```

### D2-5：调试工具初版

实现一个极简的命令行调试器，用于驱动 ToyCore 单步执行：
- `step` — 执行一条指令，打印 PC + 寄存器 + 反汇编
- `regs` — 打印所有寄存器 + 标志位
- `run` — 连续执行直到 Halted 或 Faulted
- `quit` — 退出

此调试器在后续 Phase 中会复用和增强。

---

## 讨论点和待定事项

| # | 问题 | 何时决定 |
|---|------|---------|
| T2-1 | RegisterFile 的 pc 字段 vs ICore 的 pc() 独立实现？如果 pc 独立于 regs，RegisterFile 只管通用寄存器。 | 本 Phase 实施时 |
| T2-2 | 标志位是否应该进入 RegisterFile？不同架构标志位差异大（ARM 有 NZCV，ToyCore 只有 ZN）。 | 本 Phase 实施时 |
| T2-3 | 指令编码具体 bit layout 需要定稿。上表是草案，实施时细化。 | 本 Phase 开始时 |
| T2-4 | 调试器用独立可执行文件 vs 库函数+测试内调用？ | 本 Phase 实施时 |

---

## 隐藏风险

### R2-1：ICore 接口缺陷暴露
如果 ToyCore 实现中发现 ICore 缺少必要方法（比如需要 `halted()` 而不仅是 `state()`），
这是好事——在 ARM 实现之前发现比之后发现好得多。
**应对**：如果需要修改 ICore，立即修改，不要绕过。

### R2-2：MemoryBus 访问方式
ToyCore 的 fetch() 需要从 MemoryBus 读指令字节。
CPU 内部调用 `bus_.read(pc, 4)` 取 32-bit 指令。
确认 MemoryBus 的 read() 接口能满足 CPU 取指需求。

---

## 验证程序：斐波那契

```cpp
// 手编二进制（伪代码表示）
std::vector<uint32_t> program = {
    MOV(R0, 0),       // R0 = 0 (fib_prev)
    MOV(R1, 1),       // R1 = 1 (fib_curr)
    MOV(R2, 10),      // R2 = 10 (counter)
    // loop:
    ADD(R0, R1),      // R0 = R0 + R1
    MOV(R3, R1),      // R3 = R1 (temp)
    MOV(R1, R0),      // R1 = R0
    MOV(R0, R3),      // R0 = R3 (swap)
    SUB(R2, 1_const), // R2--  (需要 sub immediate 支持)
    JZ(done),
    JMP(loop),
    // done:
    HALT,
};
// 加载到内存地址 0，PC 从 0 开始
// 执行后 R1 应包含第 10 个斐波那契数
```

## 验收标准

- [ ] ToyCore 编译通过，实现 ICore 全部虚方法
- [ ] ICore 接口无需修改即支持 ToyCore（关键验证点）
- [ ] 寄存器读写正确（reg/get/set）
- [ ] MOV/ADD/SUB 指令测试通过
- [ ] LOAD/STORE 指令测试通过（内存交互）
- [ ] JMP/JZ 条件跳转测试通过
- [ ] CALL/RET 嵌套调用测试通过
- [ ] INT 软件中断跳转测试通过
- [ ] HALT 后 state() 返回 Halted
- [ ] 斐波那契程序在 ToyCore 上输出正确结果
- [ ] ToyIsaDisassembler 反汇编输出可读
- [ ] cycles() 随 step() 递增
