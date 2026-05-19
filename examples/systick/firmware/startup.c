#include <stdint.h>

extern void reset_handler(void);
extern void systick_handler(void);

/* Vector table layout for our simulator:
   SysTick is NVIC IRQ 15 → handler at vector table entry 16+15 = 31. */
__attribute__((section(".isr_vector")))
const uint32_t vector_table[] = {
    0x20005000,              /* 0:  Initial SP */
    (uint32_t)reset_handler, /* 1:  Reset */
    0,                       /* 2:  NMI */
    0,                       /* 3:  HardFault */
    0, 0, 0, 0, 0, 0, 0, 0, /* 4-11 */
    0,                       /* 12: SVCall */
    0,                       /* 13: Debug Monitor */
    0,                       /* 14: reserved */
    0,                       /* 15: PendSV */
    0,                       /* 16: IRQ 0 */
    0,                       /* 17: IRQ 1 */
    0,                       /* 18: IRQ 2 */
    0,                       /* 19: IRQ 3 */
    0,                       /* 20: IRQ 4 */
    0,                       /* 21: IRQ 5 */
    0,                       /* 22: IRQ 6 */
    0,                       /* 23: IRQ 7 */
    0,                       /* 24: IRQ 8 */
    0,                       /* 25: IRQ 9 */
    0,                       /* 26: IRQ 10 */
    0,                       /* 27: IRQ 11 */
    0,                       /* 28: IRQ 12 */
    0,                       /* 29: IRQ 13 */
    0,                       /* 30: IRQ 14 */
    (uint32_t)systick_handler /* 31: IRQ 15 (SysTick) */
};
