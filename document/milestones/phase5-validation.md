# Phase 5 · 验收

> 预计工期：1 周 | 依赖：Phase 4 | 状态：待实施

## 目标

用真实 STM32F1 固件（裸机寄存器操作）跑出有意义的输出，验证整个模拟器管线。

---

## 验收程序 A · GPIO LED 翻转

```c
// firmware/test_blink/main.c
#define RCC_APB2ENR  (*(volatile uint32_t*)0x40021018)
#define GPIOA_CRL    (*(volatile uint32_t*)0x40010800)
#define GPIOA_ODR    (*(volatile uint32_t*)0x4001080C)

void delay(int count) {
    for (volatile int i = 0; i < count; i++);
}

int main(void) {
    RCC_APB2ENR |= (1 << 2);          // 使能 GPIOA 时钟
    GPIOA_CRL   &= ~(0xF << 20);
    GPIOA_CRL   |=  (0x1 << 20);      // PA5 推挽输出
    while(1) {
        GPIOA_ODR ^= (1 << 5);        // 翻转 PA5
        delay(100000);
    }
}
```

**预期输出**（终端持续打印）：
```
[GPIO] GPIOA.PIN5 → HIGH
[GPIO] GPIOA.PIN5 → LOW
[GPIO] GPIOA.PIN5 → HIGH
[GPIO] GPIOA.PIN5 → LOW
...
```

**需要的工作**：
- arm-none-eabi-gcc 编译上述代码为 ELF
- 准备最小 startup.s（Reset_Handler → 设置 SP → 跳 main）
- 准备最小链接脚本（Flash 从 0x08000000，SRAM 从 0x20000000）

---

## 验收程序 B · USART Hello World

```c
// firmware/test_usart/main.c
#define RCC_APB2ENR  (*(volatile uint32_t*)0x40021018)
#define USART1_SR    (*(volatile uint32_t*)0x40013800)
#define USART1_DR    (*(volatile uint32_t*)0x40013804)
#define USART1_BRR   (*(volatile uint32_t*)0x40013808)
#define USART1_CR1   (*(volatile uint32_t*)0x4001380C)

void usart_init(void) {
    RCC_APB2ENR |= (1 << 14);  // 使能 USART1 时钟
    USART1_BRR = 0x1D4C;       // 72MHz / 9600 baud
    USART1_CR1 |= (1 << 13);   // UE: USART Enable
    USART1_CR1 |= (1 << 3);    // TE: Transmitter Enable
}

void usart_send(char c) {
    while (!(USART1_SR & (1 << 7)));  // 等待 TXE
    USART1_DR = c;
}

int main(void) {
    usart_init();
    const char* msg = "Hello Virtual STM32!\n";
    while (*msg) {
        usart_send(*msg++);
    }
    while(1);
}
```

**预期输出**：
```
Hello Virtual STM32!
```

**关键验证点**：
- USART1_SR.TXE 始终为 1 → 轮询不阻塞
- USART1_DR 写入 → 字符输出到 stdout
- 多字符连续发送正确

---

## 验收程序 C · SysTick 中断

```c
// firmware/test_systick/main.c
#include <stdint.h>

#define SCB_VTOR     (*(volatile uint32_t*)0xE000ED08)
#define SysTick_CTRL (*(volatile uint32_t*)0xE000E010)
#define SysTick_LOAD (*(volatile uint32_t*)0xE000E014)
#define SysTick_VAL  (*(volatile uint32_t*)0xE000E018)

volatile uint32_t tick_count = 0;

void SysTick_Handler(void) {
    tick_count++;
    // 这里需要在模拟器中输出日志
    // 方案 1：通过 USART 输出
    // 方案 2：通过特殊 MMIO 地址触发日志（调试用）
}

int main(void) {
    SysTick_LOAD = 1000 - 1;          // 每 1000 cycles 触发
    SysTick_VAL = 0;
    SysTick_CTRL = 0x7;               // ENABLE + TICKINT + CLKSOURCE

    while(1) {
        // 主循环空转，等待 SysTick
    }
}
```

**预期输出**：
```
[TICK] SysTick fired, count=1
[TICK] SysTick fired, count=2
[TICK] SysTick fired, count=3
...
```

**关键验证点**：
- SysTick 按 cycles 递减，归零触发中断
- NVIC 正确路由 SysTick 中断到 handler
- handler 执行后正确返回（EXC_RETURN）
- tick_count 正确递增

---

## 验收固件构建

### 最小 Startup 文件

```asm
// startup.s
.syntax unified
.cpu cortex-m3
.thumb

.global Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
    ldr sp, =_estack       // 栈顶
    bl main
hang:
    b hang

.global SysTick_Handler
.type SysTick_Handler, %function
SysTick_Handler:
    // handler code
    bx lr

// 向量表
.section .isr_vector
.word _estack
.word Reset_Handler
.word 0   // NMI
.word 0   // HardFault
// ... 填充到 SysTick 位置
.word SysTick_Handler
```

### 链接脚本

```ld
MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) } > FLASH
    .rodata : { *(.rodata*) } > FLASH
    .data : { *(.data*) } > SRAM AT > FLASH
    .bss : { *(.bss*) } > SRAM
    _estack = ORIGIN(SRAM) + LENGTH(SRAM);
}
```

### 编译命令

```bash
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -nostdlib -T stm32f103.ld \
    startup.s main.c -o test_blink.elf
```

---

## 验收检查清单

### 程序 A（GPIO 翻转）
- [ ] ELF 加载成功
- [ ] Reset 序列正确（SP + PC 从向量表读取）
- [ ] RCC 使能 GPIOA 时钟（寄存器写入无错误）
- [ ] GPIOA_CRL 配置正确
- [ ] GPIOA_ODR 翻转 → 终端输出 `[GPIO] GPIOA.PIN5 → HIGH/LOW`
- [ ] 循环持续运行

### 程序 B（USART Hello World）
- [ ] USART1 初始化正确
- [ ] SR.TXE 轮询不阻塞
- [ ] USART1_DR 写入 → stdout 输出正确字符
- [ ] 完整字符串 "Hello Virtual STM32!" 输出

### 程序 C（SysTick 中断）
- [ ] SysTick 配置正确（LOAD/CTRL）
- [ ] 每 1000 cycles VAL 归零
- [ ] 中断触发 → SysTick_Handler 被调用
- [ ] 中断返回正确（PC 恢复，寄存器恢复）
- [ ] tick_count 正确递增

### 整体
- [ ] 三个程序全部通过
- [ ] 无 BusError / Fault
- [ ] 内存映射正确（无非法访问）
- [ ] 模拟器运行稳定（无崩溃）
