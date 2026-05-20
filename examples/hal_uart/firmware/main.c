#include "stm32f1xx_hal.h"

uint32_t SystemCoreClock = 8000000U;
const uint8_t AHBPrescTable[16U] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
const uint8_t APBPrescTable[8U]  = {0, 0, 0, 0, 1, 2, 3, 4};

void SystemInit(void) {
    /* Run on HSI 8 MHz default */
}

void SysTick_Handler(void) {
    HAL_IncTick();
}

static UART_HandleTypeDef huart1;

static void SystemClock_Config(void) {
    /* In simulation: keep HSI 8 MHz default */
}

static void MX_GPIO_Init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};

    /* PA9: USART1_TX — Alternate Function Push-Pull */
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PA10: USART1_RX — Input floating */
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static void MX_USART1_UART_Init(void) {
    huart1.Instance        = USART1;
    huart1.Init.BaudRate   = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits   = UART_STOPBITS_1;
    huart1.Init.Parity     = UART_PARITY_NONE;
    huart1.Init.Mode       = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    const uint8_t msg[] = "Hello from STM32 HAL UART\n";
    HAL_UART_Transmit(&huart1, msg, sizeof(msg) - 1, HAL_MAX_DELAY);

    while (1) {
        __asm volatile("nop");
    }
}
