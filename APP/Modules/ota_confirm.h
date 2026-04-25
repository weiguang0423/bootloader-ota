#ifndef BL_CONFIRM_H
#define BL_CONFIRM_H

/* USER CODE BEGIN Header */
/**
    ******************************************************************************
    * @file    bl_confirm.h
    * @brief   应用端首次启动确认接口声明。
    ******************************************************************************
    * @attention
    *
    * 升级写入新固件后，Bootloader 将活动槽位标记为"未确认"。
    * 应用成功启动后必须调用 BL_Confirm_Boot() 写入确认标记，
    * 否则下次复位时 Bootloader 将自动回滚到旧槽位。
    *
    ******************************************************************************
    */
/* USER CODE END Header */

#include "ota_config.h"

/**
    * @brief  确认当前启动成功，写入确认标记到启动信息区。
    * @retval HAL_StatusTypeDef HAL_OK 表示确认成功
    */
HAL_StatusTypeDef BL_Confirm_Boot(void);

#endif
