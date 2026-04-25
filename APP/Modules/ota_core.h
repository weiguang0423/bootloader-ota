#ifndef BL_OTA_H
#define BL_OTA_H

/* USER CODE BEGIN Header */
/**
    ******************************************************************************
    * @file    ota_core.h
    * @brief   远程 OTA 升级核心接口声明。
    ******************************************************************************
    * @attention
    *
    * 升级流程：
    *   1. 监听 DTU 透传串口，接收 http:// 固件 URL
    *   2. 解析 URL 并切换 DTU 到 TCP 模式
    *   3. 发送 HTTP GET，下载固件写入对端 Flash 分区
    *   4. 硬件 CRC32 校验 + 向量表合法性检查
    *   5. 切换活动槽位并复位
    *
    ******************************************************************************
    */
/* USER CODE END Header */

#include <stdint.h>
#include "ota_config.h"

/**
 * @brief OTA 状态机状态。
 */
typedef enum
{
    BL_OTA_IDLE = 0,
    BL_OTA_DOWNLOADING,
    BL_OTA_DONE,
    BL_OTA_ERROR,
} bl_ota_state_t;

/**
 * @brief  初始化 OTA 模块。
 */
void BL_OTA_Init(void);

/**
 * @brief  OTA 主处理函数，在主循环中周期调用。
 */
void BL_OTA_Process(void);

/**
 * @brief  获取当前 OTA 状态。
 */
bl_ota_state_t BL_OTA_GetState(void);

#endif
