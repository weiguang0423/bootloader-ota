#ifndef BL_CONFIG_H
#define BL_CONFIG_H

/* USER CODE BEGIN Header */
/**
	******************************************************************************
	* @file    bl_config.h
	* @brief   Bootloader 工程统一配置文件。
	******************************************************************************
	* @attention
	*
	* 这份文件只放和项目、板子、分区布局强相关的配置项。
	* 迁移到新板子时，优先修改这里，而不是去改业务逻辑代码。
	*
	******************************************************************************
	*/
/* USER CODE END Header */

#include "stm32f4xx_hal.h"

/* Flash 分区配置 ------------------------------------------------------------ */
#define BL_FLASH_BASE_ADDR            0x08000000UL
#define BL_FLASH_END_ADDR             0x080FFFFFUL
#define BL_BOOT_START_ADDR            BL_FLASH_BASE_ADDR
#define BL_BOOT_MAX_SIZE              0x00020000UL
#define BL_APP1_START_ADDR            0x08020000UL
#define BL_APP1_MAX_SIZE              0x00060000UL
#define BL_APP2_START_ADDR            0x08080000UL
#define BL_APP2_MAX_SIZE              0x00060000UL
#define BL_BOOT_INFO_START_ADDR       0x080E0000UL
#define BL_BOOT_INFO_SIZE             0x00020000UL
#define BL_FLASH_VOLTAGE_RANGE        FLASH_VOLTAGE_RANGE_3

/* 串口参数 ------------------------------------------------------------------ */
#define BL_UART_BAUDRATE              115200UL
#define BL_UART_TIMEOUT_MS            1000UL

/* 升级入口策略 -------------------------------------------------------------- */
#define BL_UPDATE_REQUEST_CHAR        'U'
#define BL_AUTO_BOOT_TIMEOUT_MS       3000UL

/* YModem 参数 --------------------------------------------------------------- */
#define BL_YMODEM_MAX_ERRORS          10U
#define BL_YMODEM_PACKET_TIMEOUT_MS   1000UL
#define BL_YMODEM_SESSION_TIMEOUT_MS  60000UL
#define BL_YMODEM_FILE_NAME_LEN       64U
#define BL_YMODEM_USE_1K_PACKET       1U

/* 板级串口配置 -------------------------------------------------------------- */
/* 默认使用 USART1 / PA9 / PA10，若硬件不同，只需要在这里替换。 */
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
#define BL_LED_GPIO_CLK_ENABLE()      __HAL_RCC_GPIOC_CLK_ENABLE()
#define BL_LED_PORT                   GPIOC
#define BL_LED_PIN                    GPIO_PIN_13
#define BL_LED_ACTIVE_LEVEL           GPIO_PIN_RESET

#endif
