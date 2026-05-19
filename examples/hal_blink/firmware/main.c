#include "stm32f1xx_hal.h"

uint32_t SystemCoreClock = 8000000U;

void SystemInit(void) {
    /* Run on HSI 8 MHz default */
}

void SysTick_Handler(void) {
    HAL_IncTick();
}

static void delay_ms(uint32_t ms) {
    volatile uint32_t count = ms * 1000;
    while (count--)
        ;
}

int main(void) {
    HAL_Init();

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = GPIO_PIN_5;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    for (int i = 0; i < 3; i++) {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
        delay_ms(100);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
        delay_ms(100);
    }

    while (1) __asm volatile("nop");
}
