#include <stdint.h>

#define USART1_SR   (*((volatile uint32_t*)0x40013800))
#define USART1_DR   (*((volatile uint32_t*)0x40013804))
#define USART1_CR1  (*((volatile uint32_t*)0x4001380C))
#define RCC_APB2ENR (*((volatile uint32_t*)0x40021018))

void usart_init(void) {
    RCC_APB2ENR |= (1 << 14);  /* Enable USART1 clock */
    USART1_CR1 = 0x000C;       /* TE + RE */
    USART1_CR1 |= 0x2000;     /* UE */
}

void usart_putc(char c) {
    while (!(USART1_SR & 0x80))
        ;
    USART1_DR = c;
}

void usart_puts(const char *s) {
    while (*s)
        usart_putc(*s++);
}

void reset_handler(void) {
    usart_init();
    usart_puts("Hello from micro-forge!");
    while (1)
        __asm volatile("nop");
}
