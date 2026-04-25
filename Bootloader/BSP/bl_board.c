#include "bl_board.h"

/* USER CODE BEGIN Header */
/**
    ******************************************************************************
    * @file    bl_board.c
    * @brief   板级抽象层实现。
    ******************************************************************************
    * @attention
    *
    * 这个文件负责和具体硬件打交道，尽量把"板子差异"挡在这里：
    * - 时钟基础初始化
    * - GPIO 初始化
    * - 串口初始化
    * - 调试输出
    *
    * 这样上层 Bootloader 和 YModem 模块只依赖统一接口，更方便移植。
    *
    ******************************************************************************
    */
/* USER CODE END Header */

#include <stdarg.h>
#include <stdio.h>

/* Private variables --------------------------------------------------------- */
static UART_HandleTypeDef s_boot_uart;

/**
    * @brief  初始化板级 GPIO。
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
    * @brief  初始化 Bootloader 使用的串口外设。
    * @retval None
    */
static void BL_Board_UartInit(void)
{
    s_boot_uart.Instance = BL_UART_INSTANCE;               //所选择的uart的基地址
    s_boot_uart.Init.BaudRate = BL_UART_BAUDRATE;					 //config.h中配置的波特率，此处为115200
    s_boot_uart.Init.WordLength = UART_WORDLENGTH_8B;      //设定8位数据位
    s_boot_uart.Init.StopBits = UART_STOPBITS_1;           //设定1位停止位
    s_boot_uart.Init.Parity = UART_PARITY_NONE;            //设定0位校验位
    s_boot_uart.Init.Mode = UART_MODE_TX_RX;               //设定位收发模式
    s_boot_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;      //不设定流控
    s_boot_uart.Init.OverSampling = UART_OVERSAMPLING_16;  //16倍过采样

    if (HAL_UART_Init(&s_boot_uart) != HAL_OK)
    {
        while (1)
        {
                        /* 初始化失败后停机，方便调试串口配置或时钟问题。 */
        }
    }
}

/**
    * @brief  初始化板级硬件资源。
    * @retval None
    */
void BL_Board_Init(void)
{
    /*
     * 最小版本直接使用复位后的时钟配置即可。
     * STM32F4 上电后默认开启 HSI，足够支持 115200 串口调试。
     * 如果后续需要更高主频，可以在这里切到 PLL。
     */
    HAL_Init();
    BL_Board_GpioInit();
    BL_Board_UartInit();
}

/**
    * @brief  获取 Bootloader 使用的串口句柄。
    * @retval UART_HandleTypeDef* 串口句柄指针
    */
UART_HandleTypeDef *BL_Board_GetUart(void)
{
    return &s_boot_uart;
}

/**
    * @brief  从升级串口读取指定长度的数据。
    * @param  data        接收缓存指针
    * @param  length      接收长度
    * @param  timeout_ms  超时时间，单位毫秒
    * @retval HAL_StatusTypeDef HAL 接口返回值
    */
HAL_StatusTypeDef BL_Board_Read(uint8_t *data, uint16_t length, uint32_t timeout_ms)
{
    if ((data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_UART_Receive(&s_boot_uart, data, length, timeout_ms);
}

/**
    * @brief  从升级串口读取 1 个字节。Ymodem 协议来在正式传输大数据块之前，通常会先发一个单字节的"暗号"。
    * @param  byte        接收字节指针
    * @param  timeout_ms  超时时间，单位毫秒
    * @retval HAL_StatusTypeDef HAL 接口返回值
    */
HAL_StatusTypeDef BL_Board_ReadByte(uint8_t *byte, uint32_t timeout_ms)
{
    return BL_Board_Read(byte, 1U, timeout_ms);
}

/**
    * @brief  向升级串口发送指定长度的数据。
    * @param  data        发送缓存指针
    * @param  length      发送长度
    * @param  timeout_ms  超时时间，单位毫秒
    * @retval HAL_StatusTypeDef HAL 接口返回值
    */
HAL_StatusTypeDef BL_Board_Write(const uint8_t *data, uint16_t length, uint32_t timeout_ms)
{
    if ((data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_UART_Transmit(&s_boot_uart, (uint8_t *)data, length, timeout_ms);
}

/**
    * @brief  输出格式化调试信息。
    * @param  format  printf 风格格式字符串
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
    * @brief  翻转状态灯状态。
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
    * @brief  HAL 串口底层硬件初始化。
    * @param  huart  串口句柄
    * @retval None
    */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio_init = {0};

    if ((huart == NULL) || (huart->Instance != BL_UART_INSTANCE))
    {
        return;
    }

    BL_UART_TX_GPIO_CLK_ENABLE();
    BL_UART_RX_GPIO_CLK_ENABLE();
    BL_UART_CLK_ENABLE();

    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = BL_UART_AF;

    gpio_init.Pin = BL_UART_TX_PIN;
    HAL_GPIO_Init(BL_UART_TX_PORT, &gpio_init);

    gpio_init.Pin = BL_UART_RX_PIN;
    HAL_GPIO_Init(BL_UART_RX_PORT, &gpio_init);
}

/**
    * @brief  HAL 串口底层硬件反初始化。
    * @param  huart  串口句柄
    * @retval None
    */
void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance != BL_UART_INSTANCE))
    {
        return;
    }

    HAL_GPIO_DeInit(BL_UART_TX_PORT, BL_UART_TX_PIN);
    HAL_GPIO_DeInit(BL_UART_RX_PORT, BL_UART_RX_PIN);
}
