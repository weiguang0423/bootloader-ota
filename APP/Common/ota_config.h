#ifndef BL_CONFIG_H
#define BL_CONFIG_H

/* USER CODE BEGIN Header */
/**
    ******************************************************************************
    * @file    ota_config.h
    * @brief   系统配置参数定义。
    ******************************************************************************
    * @attention
    *
    * 集中管理 Flash 分区地址、串口引脚、波特率、
    * OTA 超时时间等所有可配置参数，方便移植到不同硬件。
    *
    ******************************************************************************
    */
/* USER CODE END Header */

#include "stm32f4xx_hal.h"

/* 应用镜像配置 ---------------------------------------------------------- */
#define BL_APP1_START_ADDR            0x08020000UL
#define BL_APP1_MAX_SIZE              0x00060000UL
#define BL_APP2_START_ADDR            0x08080000UL
#define BL_APP2_MAX_SIZE              0x00060000UL

/* 启动信息区配置（首次启动确认） ---------------------------------------- */
#define BL_BOOT_INFO_START_ADDR       0x080E0000UL
#define BL_BOOT_INFO_SIZE             0x00020000UL
#define BL_FLASH_VOLTAGE_RANGE        FLASH_VOLTAGE_RANGE_3

/* 调试串口配置 -------------------------------------------------------------- */
#define BL_UART_BAUDRATE              115200UL
#define BL_UART_TIMEOUT_MS            1000UL

/* 默认使用 USART1 / PA9 / PA10，硬件不同时需要修改以下宏 */
#define BL_UART_INSTANCE              USART1
#define BL_UART_CLK_ENABLE()          __HAL_RCC_USART1_CLK_ENABLE()
#define BL_UART_TX_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOA_CLK_ENABLE()
#define BL_UART_RX_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOA_CLK_ENABLE()
#define BL_UART_TX_PORT               GPIOA
#define BL_UART_TX_PIN                GPIO_PIN_9
#define BL_UART_RX_PORT               GPIOA
#define BL_UART_RX_PIN                GPIO_PIN_10
#define BL_UART_AF                    GPIO_AF7_USART1

/* 可选状态灯配置 ------------------------------------------------------------ */
#define BL_USE_STATUS_LED             1U
#define BL_LED_GPIO_CLK_ENABLE()      __HAL_RCC_GPIOF_CLK_ENABLE()
#define BL_LED_PORT                   GPIOF
#define BL_LED_PIN                    GPIO_PIN_9
#define BL_LED_ACTIVE_LEVEL           GPIO_PIN_RESET

/* DTU 串口配置 (USART3 / PB10-TX / PB11-RX，对应 TAS-LTE-892D) ------------ */
#define BL_DTU_UART_INSTANCE          USART3
#define BL_DTU_UART_CLK_ENABLE()      __HAL_RCC_USART3_CLK_ENABLE()
#define BL_DTU_UART_TX_GPIO_CLK()     __HAL_RCC_GPIOB_CLK_ENABLE()
#define BL_DTU_UART_RX_GPIO_CLK()     __HAL_RCC_GPIOB_CLK_ENABLE()
#define BL_DTU_UART_TX_PORT           GPIOB
#define BL_DTU_UART_TX_PIN            GPIO_PIN_10
#define BL_DTU_UART_RX_PORT           GPIOB
#define BL_DTU_UART_RX_PIN            GPIO_PIN_11
#define BL_DTU_UART_AF                GPIO_AF7_USART3
#define BL_DTU_UART_BAUDRATE          115200UL
#define BL_DTU_UART_IRQn              USART3_IRQn
#define BL_DTU_UART_IRQ_PRIORITY      5U

/* 远程 OTA 配置 ------------------------------------------------------------- */
#define BL_OTA_RINGBUF_SIZE           16384U  /* DTU 接收环形缓冲区大小 */
#define BL_OTA_AT_TIMEOUT_MS          2000UL  /* AT 指令响应超时 */
#define BL_OTA_HTTP_CHANNEL           1U      /* OTA 下载使用的 DTU 通道号 */
#define BL_OTA_URL_BUF_SIZE           512U    /* URL 输入缓存 */
#define BL_OTA_HTTP_HOST_BUF_SIZE     192U    /* HTTP Host 缓存 */
#define BL_OTA_HTTP_PATH_BUF_SIZE     320U    /* HTTP Path 缓存 */
#define BL_OTA_HTTP_HEADER_BUF_SIZE   1024U   /* HTTP 头缓存 */
#define BL_OTA_HTTP_BODY_CHUNK_SIZE   256U    /* HTTP Body 单次读取大小 */
#define BL_OTA_URL_IDLE_MS            300UL   /* 无缓存时 URL 判定超时 */
#define BL_OTA_HTTP_HEADER_TIMEOUT_MS 20000UL /* HTTP 头接收超时 */
#define BL_OTA_RX_STALL_TIMEOUT_MS    45000UL /* 下载过程无数据超时 */
#define BL_OTA_TCP_CONNECT_TIMEOUT_MS 30000UL /* TCP 连接超时 */

#endif
