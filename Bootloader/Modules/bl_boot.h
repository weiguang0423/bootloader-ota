#ifndef BL_BOOT_H
#define BL_BOOT_H

/* USER CODE BEGIN Header */
/**
	******************************************************************************
	* @file    bl_boot.h
	* @brief   Bootloader 主流程模块对外接口声明。
	******************************************************************************
	* @attention
	*
	* 这个模块负责两件事：
	* 1. 决定当前是启动有效应用槽位，还是进入升级模式。
	* 2. 在满足条件时，安全地跳转到被选中的应用程序入口。
	*
	******************************************************************************
	*/
/* USER CODE END Header */

/**
	* @brief  运行 Bootloader 主流程。
	* @retval None
	*/
void BL_Boot_Run(void);

/**
	* @brief  跳转到当前选中的应用程序入口地址。
	* @retval None
	*/
void BL_Boot_JumpToApplication(void);

#endif
