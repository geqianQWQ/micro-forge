#include <stdint.h>

extern void reset_handler(void);

__attribute__((section(".isr_vector"))) const uint32_t vector_table[] = {
    0x20005000,             /* Initial SP (top of 20KB SRAM) */
    (uint32_t)reset_handler /* Reset Handler */
};
