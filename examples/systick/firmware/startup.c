#include <stdint.h>

extern void reset_handler(void);
extern void systick_handler(void);

/* Standard ARM Cortex-M3 vector table layout:
   SysTick is system exception 15 at vector table index 15 (offset 0x3C). */
__attribute__((section(".isr_vector"))) const uint32_t vector_table[] = {
    0x20005000,              /* 0:  Initial SP */
    (uint32_t)reset_handler, /* 1:  Reset */
    0,                       /* 2:  NMI */
    0,                       /* 3:  HardFault */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,                        /* 4-11 */
    0,                        /* 12: SVCall */
    0,                        /* 13: Debug Monitor */
    0,                        /* 14: reserved */
    (uint32_t)systick_handler /* 15: SysTick */
};
