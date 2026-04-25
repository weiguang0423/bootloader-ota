#ifndef BL_FLASH_H
#define BL_FLASH_H

/* USER CODE BEGIN Header */
/**
    *****************************************************************************
    * @file    bl_flash.h
    * @brief   Bootloader Flash 访问接口声明。
    *****************************************************************************
    * @attention
    *
    * 这个模块统一封装双应用分区和启动信息区的访问能力。
    * 上层模块不直接操作 HAL Flash 接口，避免分区与槽位逻辑散落在各处。
    *
    *****************************************************************************
    */
/* USER CODE END Header */

#include <stdbool.h>
#include <stdint.h>

#include "bl_config.h"

/**
    * @brief Flash 操作返回状态。
    */
typedef enum
{
        BL_FLASH_OK = 0,          /* Flash 操作成功 */
        BL_FLASH_INVALID_ADDRESS, /* 地址不在受管应用分区内，或长度非法 */
        BL_FLASH_ERASE_ERROR,     /* 擦除失败 */
        BL_FLASH_WRITE_ERROR,     /* 写入失败 */
} bl_flash_status_t;

/**
    * @brief 应用槽位编号。
    */
typedef enum
{
        BL_APP_SLOT_NONE = 0,
        BL_APP_SLOT_1 = 1,
        BL_APP_SLOT_2 = 2,
} bl_app_slot_t;

/**
    * @brief 受 Bootloader 管理的应用分区描述。
    */
typedef struct
{
        bl_app_slot_t slot;
        uint32_t start_addr;
        uint32_t max_size;
        const char *name;
} bl_app_partition_t;

/**
    * @brief  获取指定槽位的分区描述。
    * @param  slot  应用槽位
    * @retval const bl_app_partition_t* 分区描述，未找到返回 NULL
    */
const bl_app_partition_t *BL_Flash_GetAppPartition(bl_app_slot_t slot);

/**
    * @brief  判断指定槽位中的应用是否有效。
    * @param  slot  应用槽位
    * @retval bool true 表示有效，false 表示无效
    */
bool BL_Flash_IsSlotValid(bl_app_slot_t slot);

/**
    * @brief  获取当前记录的活动槽位。
    * @retval bl_app_slot_t 活动槽位，未配置时返回 BL_APP_SLOT_NONE
    */
bl_app_slot_t BL_Flash_GetActiveSlot(void);

/**
    * @brief  获取当前应启动的应用分区。
    * @param  partition  输出分区描述
    * @retval bool true 表示找到可启动分区，false 表示没有有效应用
    */
bool BL_Flash_GetBootPartition(bl_app_partition_t *partition);

/**
    * @brief  获取当前升级应写入的应用分区。
    * @param  partition  输出分区描述
    * @retval bool true 表示找到目标分区，false 表示失败
    */
bool BL_Flash_GetUpgradePartition(bl_app_partition_t *partition);

/**
    * @brief  将指定槽位记录为新的活动槽位。
    * @param  slot  应用槽位
    * @retval bl_flash_status_t Flash 操作状态
    */
bl_flash_status_t BL_Flash_SetActiveSlot(bl_app_slot_t slot);

/**
    * @brief  确认当前活动槽位启动成功，阻止下次回滚。
    * @retval bl_flash_status_t Flash 操作状态
    */
bl_flash_status_t BL_Flash_ConfirmBoot(void);

/**
    * @brief  查询当前活动槽位是否已确认。
    * @retval bool true 表示已确认，false 表示未确认或无有效启动信息
    */
bool BL_Flash_IsBootConfirmed(void);

/**
    * @brief  判断一段地址范围是否完全位于受管理应用分区内。
    * @param  address  起始地址
    * @param  length   长度，单位字节
    * @retval bool true 表示合法，false 表示非法
    */
bool BL_Flash_IsAddressInApp(uint32_t address, uint32_t length);

/**
    * @brief  判断指定应用分区是否存在有效向量表。
    * @param  app_address  应用程序起始地址
    * @retval bool true 表示应用有效，false 表示应用无效
    */
bool BL_Flash_IsApplicationValid(uint32_t app_address);

/**
    * @brief  擦除指定范围覆盖到的所有应用分区扇区。
    * @param  address  起始地址
    * @param  length   长度，单位字节
    * @retval bl_flash_status_t Flash 操作状态
    */
bl_flash_status_t BL_Flash_Erase(uint32_t address, uint32_t length);

/**
    * @brief  向指定应用分区地址写入一段数据。
    * @param  address  目标地址
    * @param  data     数据缓存指针
    * @param  length   数据长度，单位字节
    * @retval bl_flash_status_t Flash 操作状态
    */
bl_flash_status_t BL_Flash_Write(uint32_t address, const uint8_t *data, uint32_t length);

/**
    * @brief  从 Flash 中读取一段数据。
    * @param  address  源地址
    * @param  data     输出缓存指针
    * @param  length   数据长度，单位字节
    * @retval None
    */
void BL_Flash_Read(uint32_t address, uint8_t *data, uint32_t length);

#endif
