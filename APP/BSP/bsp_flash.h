#ifndef BL_FLASH_APP_H
#define BL_FLASH_APP_H

#include <stdbool.h>
#include <stdint.h>
#include "ota_config.h"

/**
    * @brief  根据 Flash 地址获取对应的扇区编号。
    * @param  address  Flash 地址
    * @retval uint32_t 扇区编号
    */
uint32_t BL_FlashApp_GetSector(uint32_t address);

/**
    * @brief  开始 Flash 操作会话（解锁 Flash）。
    * @retval bool true 表示解锁成功
    */
bool BL_FlashApp_BeginSession(void);

/**
    * @brief  结束 Flash 操作会话（锁定 Flash）。
    * @retval bool true 表示锁定成功
    */
bool BL_FlashApp_EndSession(void);

/**
    * @brief  获取当前应用的对端升级分区地址和大小。
    * @param  addr      输出参数：升级目标起始地址
    * @param  max_size  输出参数：升级目标最大大小
    * @retval bool true 表示获取成功
    */
bool BL_FlashApp_GetUpgradePartition(uint32_t *addr, uint32_t *max_size);

/**
    * @brief  擦除指定地址和大小的 Flash 区域。
    * @param  addr  起始地址
    * @param  size  擦除大小（字节）
    * @retval bool true 表示擦除成功
    */
bool BL_FlashApp_Erase(uint32_t addr, uint32_t size);

/**
    * @brief  向 Flash 写入一段数据（自动对齐字节/字写入）。
    * @param  addr  目标地址
    * @param  data  源数据指针
    * @param  len   数据长度（字节）
    * @retval bool true 表示写入成功
    */
bool BL_FlashApp_Write(uint32_t addr, const uint8_t *data, uint32_t len);

/**
    * @brief  设置新的活动槽位并写入启动信息区。
    * @param  slot_addr  活动槽位的起始地址
    * @retval bool true 表示设置成功
    */
bool BL_FlashApp_SetActiveSlot(uint32_t slot_addr);

/**
    * @brief  检查指定地址的应用程序是否有效（栈指针合法性）。
    * @param  addr  应用程序起始地址
    * @retval bool true 表示应用有效
    */
bool BL_FlashApp_IsAppValid(uint32_t addr);

#endif
