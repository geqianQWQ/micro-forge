#include <stdint.h>

#define SysTick_CTRL (*((volatile uint32_t*)0xE000E010))
#define SysTick_LOAD (*((volatile uint32_t*)0xE000E014))
#define SysTick_VAL  (*((volatile uint32_t*)0xE000E018))
#define NVIC_ISER0   (*((volatile uint32_t*)0xE000E100))

volatile uint32_t tick_count = 0;

void systick_handler(void) {
    tick_count++;
}

void reset_handler(void) {
    NVIC_ISER0   = (1 << 15);
    SysTick_LOAD = 999;
    SysTick_VAL  = 0;
    SysTick_CTRL = 0x7;

    while (1)
        __asm volatile("nop");
}
