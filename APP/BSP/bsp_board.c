/* USER CODE BEGIN Header */
/**
    ******************************************************************************
    * @file    bsp_board.c
    * @brief   板级驱动抽象层实现。
    ******************************************************************************
    * @attention
    *
    * 封装调试串口初始化收发、指示灯控制、格式化调试输出，
    * 以及 HAL 层 UART MSP 初始化回调。
    *
    ******************************************************************************
    */
/* USER CODE END Header */
#include "bsp_board.h"

#include <stdarg.h>
#include <stdio.h>

static UART_HandleTypeDef s_board_uart;

/**
    * @brief  初始化板级 GPIO，配置状态灯引脚。
    * @retval None
    */
static void BL_Board_GpioInit(void)
{
#if BL_USE_STATUS_LED
    GPIO_InitTypeDef gpio_init = {0};

    BL_LED_GPIO_CLK_ENABLE();

    gpio_init.Pin = BL_LED_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BL_LED_PORT, &gpio_init);

    BL_Board_SetLed(false);
#endif
}

/**
    * @brief  初始化板级调试串口外设。
    * @retval None
    */
static void BL_Board_UartInit(void)
{
    s_board_uart.Instance = BL_UART_INSTANCE;
    s_board_uart.Init.BaudRate = BL_UART_BAUDRATE;
    s_board_uart.Init.WordLength = UART_WORDLENGTH_8B;
    s_board_uart.Init.StopBits = UART_STOPBITS_1;
    s_board_uart.Init.Parity = UART_PARITY_NONE;
    s_board_uart.Init.Mode = UART_MODE_TX_RX;
    s_board_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_board_uart.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&s_board_uart) != HAL_OK)
    {
        while (1)
        {
        }
    }
}

/**
    * @brief  初始化板级硬件资源（GPIO、串口等）。
    * @retval None
    */
void BL_Board_Init(void)
{
    BL_Board_GpioInit();
    BL_Board_UartInit();
}

/**
    * @brief  获取板级调试串口句柄。
    * @retval UART_HandleTypeDef* 串口句柄指针
    */
UART_HandleTypeDef *BL_Board_GetUart(void)
{
    return &s_board_uart;
}

/**
    * @brief  从调试串口读取指定长度的数据。
    * @param  data        接收缓存指针
    * @param  length      期望接收的字节数
    * @param  timeout_ms  超时时间，单位毫秒
    * @retval HAL_StatusTypeDef HAL 层返回状态
    */
HAL_StatusTypeDef BL_Board_Read(uint8_t *data, uint16_t length, uint32_t timeout_ms)
{
    if ((data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_UART_Receive(&s_board_uart, data, length, timeout_ms);
}

/**
    * @brief  从调试串口读取 1 个字节。
    * @param  byte        接收字节指针
    * @param  timeout_ms  超时时间，单位毫秒
    * @retval HAL_StatusTypeDef HAL 层返回状态
    */
HAL_StatusTypeDef BL_Board_ReadByte(uint8_t *byte, uint32_t timeout_ms)
{
    return BL_Board_Read(byte, 1U, timeout_ms);
}

/**
    * @brief  向调试串口发送指定长度的数据。
    * @param  data        发送缓存指针
    * @param  length      发送字节数
    * @param  timeout_ms  超时时间，单位毫秒
    * @retval HAL_StatusTypeDef HAL 层返回状态
    */
HAL_StatusTypeDef BL_Board_Write(const uint8_t *data, uint16_t length, uint32_t timeout_ms)
{
    if ((data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_UART_Transmit(&s_board_uart, (uint8_t *)data, length, timeout_ms);
}

/**
    * @brief  通过调试串口输出格式化调试信息。
    * @param  format  格式化字符串
    * @retval None
    */
void BL_Board_Printf(const char *format, ...)
{
    char buffer[256];
    va_list args;
    int length;

    if (format == NULL)
    {
        return;
    }

    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (length <= 0)
    {
        return;
    }

    if (length >= (int)sizeof(buffer))
    {
        length = (int)sizeof(buffer) - 1;
    }

    (void)BL_Board_Write((const uint8_t *)buffer, (uint16_t)length, BL_UART_TIMEOUT_MS);
}

/**
    * @brief  设置状态灯开关状态。
    * @param  on  true 表示点亮，false 表示熄灭
    * @retval None
    */
void BL_Board_SetLed(bool on)
{
#if BL_USE_STATUS_LED
#if BL_LED_ACTIVE_LEVEL == GPIO_PIN_SET
    HAL_GPIO_WritePin(BL_LED_PORT, BL_LED_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    HAL_GPIO_WritePin(BL_LED_PORT, BL_LED_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
#endif
#else
    (void)on;
#endif
}

/**
    * @brief  翻转状态灯当前状态。
    * @retval None
    */
void BL_Board_ToggleLed(void)
{
#if BL_USE_STATUS_LED
    HAL_GPIO_TogglePin(BL_LED_PORT, BL_LED_PIN);
#endif
}

/**
    * @brief  毫秒级阻塞延时。
    * @param  ms  延时时间，单位毫秒
    * @retval None
    */
void BL_Board_Delay(uint32_t ms)
{
    HAL_Delay(ms);
}

/**
    * @brief  HAL 串口底层硬件初始化（GPIO、时钟、中断）。
    * @param  huart  串口句柄
    * @retval None
    */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio_init = {0};

    if (huart == NULL)
    {
        return;
    }

    if (huart->Instance == BL_UART_INSTANCE)
    {
        BL_UART_TX_GPIO_CLK_ENABLE();
        BL_UART_RX_GPIO_CLK_ENABLE();
        BL_UART_CLK_ENABLE();

        gpio_init.Mode      = GPIO_MODE_AF_PP;
        gpio_init.Pull      = GPIO_PULLUP;
        gpio_init.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio_init.Alternate = BL_UART_AF;

        gpio_init.Pin = BL_UART_TX_PIN;
        HAL_GPIO_Init(BL_UART_TX_PORT, &gpio_init);

        gpio_init.Pin = BL_UART_RX_PIN;
        HAL_GPIO_Init(BL_UART_RX_PORT, &gpio_init);
    }

    if (huart->Instance == BL_DTU_UART_INSTANCE)
    {
        BL_DTU_UART_TX_GPIO_CLK();
        BL_DTU_UART_RX_GPIO_CLK();
        BL_DTU_UART_CLK_ENABLE();

        gpio_init.Mode      = GPIO_MODE_AF_PP;
        gpio_init.Pull      = GPIO_PULLUP;
        gpio_init.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio_init.Alternate = BL_DTU_UART_AF;

        gpio_init.Pin = BL_DTU_UART_TX_PIN;
        HAL_GPIO_Init(BL_DTU_UART_TX_PORT, &gpio_init);

        gpio_init.Pin = BL_DTU_UART_RX_PIN;
        HAL_GPIO_Init(BL_DTU_UART_RX_PORT, &gpio_init);
    }
}

/**
    * @brief  HAL 串口底层硬件反初始化（GPIO 去初始化）。
    * @param  huart  串口句柄
    * @retval None
    */
void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
    if (huart == NULL)
    {
        return;
    }

    if (huart->Instance == BL_UART_INSTANCE)
    {
        HAL_GPIO_DeInit(BL_UART_TX_PORT, BL_UART_TX_PIN);
        HAL_GPIO_DeInit(BL_UART_RX_PORT, BL_UART_RX_PIN);
    }

    if (huart->Instance == BL_DTU_UART_INSTANCE)
    {
        HAL_GPIO_DeInit(BL_DTU_UART_TX_PORT, BL_DTU_UART_TX_PIN);
        HAL_GPIO_DeInit(BL_DTU_UART_RX_PORT, BL_DTU_UART_RX_PIN);
    }
}
