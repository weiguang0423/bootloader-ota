#ifndef BL_BOARD_H
#define BL_BOARD_H

/* USER CODE BEGIN Header */
/**
	******************************************************************************
	* @file    bl_board.h
	* @brief   板级驱动抽象层接口声明。
	******************************************************************************
	* @attention
	*
	* 这里把和具体硬件板子相关的内容集中封装：
	* - 串口初始化与收发
	* - 指示灯控制
	* - 延时与调试输出
	*
	* 这样上层 Bootloader 和 YModem 逻辑只依赖统一接口，方便移植。
	*
	******************************************************************************
	*/
/* USER CODE END Header */

#include <stdbool.h>
#include <stdint.h>

#include "bl_config.h"

/**
	* @brief  初始化板级硬件资源。
	* @retval None
	*/
void BL_Board_Init(void);

/**
	* @brief  获取 Bootloader 使用的串口句柄。
	* @retval UART_HandleTypeDef* 串口句柄指针
	*/
UART_HandleTypeDef *BL_Board_GetUart(void);

/**
	* @brief  从升级串口读取指定长度的数据。
	* @param  data        接收缓存指针
	* @param  length      期望接收的字节数
	* @param  timeout_ms  超时时间，单位毫秒
	* @retval HAL_StatusTypeDef HAL 层返回状态
	*/
HAL_StatusTypeDef BL_Board_Read(uint8_t *data, uint16_t length, uint32_t timeout_ms);

/**
	* @brief  从升级串口读取 1 个字节。
	* @param  byte        接收字节指针
	* @param  timeout_ms  超时时间，单位毫秒
	* @retval HAL_StatusTypeDef HAL 层返回状态
	*/
HAL_StatusTypeDef BL_Board_ReadByte(uint8_t *byte, uint32_t timeout_ms);

/**
	* @brief  向升级串口发送指定长度的数据。
	* @param  data        发送缓存指针
	* @param  length      发送字节数
	* @param  timeout_ms  超时时间，单位毫秒
	* @retval HAL_StatusTypeDef HAL 层返回状态
	*/
HAL_StatusTypeDef BL_Board_Write(const uint8_t *data, uint16_t length, uint32_t timeout_ms);

/**
	* @brief  通过升级串口输出格式化调试信息。
	* @param  format  格式化字符串
	* @retval None
	*/
void BL_Board_Printf(const char *format, ...);

/**
	* @brief  设置状态灯开关状态。
	* @param  on  true 表示点亮，false 表示熄灭
	* @retval None
	*/
void BL_Board_SetLed(bool on);

/**
	* @brief  翻转状态灯当前状态。
	* @retval None
	*/
void BL_Board_ToggleLed(void);

/**
	* @brief  毫秒级阻塞延时。
	* @param  ms  延时时间，单位毫秒
	* @retval None
	*/
void BL_Board_Delay(uint32_t ms);

#endif
