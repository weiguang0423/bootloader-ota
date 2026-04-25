/* USER CODE BEGIN Header */
/**
    ******************************************************************************
    * @file    bsp_flash.c
    * @brief   Flash 操作抽象层实现。
    ******************************************************************************
    * @attention
    *
    * 实现 Flash 扇区映射、擦除写入逻辑、启动信息读写、
    * 活动槽位切换及应用程序合法性检查。
    *
    ******************************************************************************
    */
/* USER CODE END Header */
#include "bsp_flash.h"

#include <string.h>

#include "boot_state.h"

#define SLOT_1  1U
#define SLOT_2  2U

static bool s_flash_unlocked = false;

/**
    * @brief  根据 Flash 地址获取对应的扇区编号。
    * @param  address  Flash 地址
    * @retval uint32_t 扇区编号
    */
uint32_t BL_FlashApp_GetSector(uint32_t address)
{
    if (address < 0x08004000UL) return FLASH_SECTOR_0;
    if (address < 0x08008000UL) return FLASH_SECTOR_1;
    if (address < 0x0800C000UL) return FLASH_SECTOR_2;
    if (address < 0x08010000UL) return FLASH_SECTOR_3;
    if (address < 0x08020000UL) return FLASH_SECTOR_4;
    if (address < 0x08040000UL) return FLASH_SECTOR_5;
    if (address < 0x08060000UL) return FLASH_SECTOR_6;
    if (address < 0x08080000UL) return FLASH_SECTOR_7;
    if (address < 0x080A0000UL) return FLASH_SECTOR_8;
    if (address < 0x080C0000UL) return FLASH_SECTOR_9;
    if (address < 0x080E0000UL) return FLASH_SECTOR_10;
    return FLASH_SECTOR_11;
}

/**
    * @brief  开始 Flash 操作会话（解锁 Flash）。
    * @retval bool true 表示解锁成功
    */
bool BL_FlashApp_BeginSession(void)
{
    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return false;
    }
    s_flash_unlocked = true;
    return true;
}

/**
    * @brief  结束 Flash 操作会话（锁定 Flash）。
    * @retval bool true 表示锁定成功
    */
bool BL_FlashApp_EndSession(void)
{
    s_flash_unlocked = false;
    return HAL_FLASH_Lock() == HAL_OK;
}

/**
    * @brief  获取当前应用的对端升级分区地址和大小。
    * @param  addr      输出参数：升级目标起始地址
    * @param  max_size  输出参数：升级目标最大大小
    * @retval bool true 表示获取成功
    */
bool BL_FlashApp_GetUpgradePartition(uint32_t *addr, uint32_t *max_size)
{
    if ((addr == NULL) || (max_size == NULL))
    {
        return false;
    }

#ifndef APP_IMAGE_START_ADDR
#define APP_IMAGE_START_ADDR BL_APP1_START_ADDR
#endif

    if (APP_IMAGE_START_ADDR == BL_APP1_START_ADDR)
    {
        *addr     = BL_APP2_START_ADDR;
        *max_size = BL_APP2_MAX_SIZE;
    }
    else
    {
        *addr     = BL_APP1_START_ADDR;
        *max_size = BL_APP1_MAX_SIZE;
    }
    return true;
}

/**
    * @brief  擦除指定地址和大小的 Flash 区域。
    * @param  addr  起始地址
    * @param  size  擦除大小（字节）
    * @retval bool true 表示擦除成功
    */
bool BL_FlashApp_Erase(uint32_t addr, uint32_t size)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0U;
    uint32_t start_sector = BL_FlashApp_GetSector(addr);
    uint32_t end_sector   = BL_FlashApp_GetSector(addr + size - 1U);
    uint32_t nb_sectors;

    if (size == 0U)
    {
        return false;
    }

    nb_sectors = end_sector - start_sector + 1U;

    if (!s_flash_unlocked)
    {
        if (HAL_FLASH_Unlock() != HAL_OK)
        {
            return false;
        }
    }

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = BL_FLASH_VOLTAGE_RANGE;
    erase.Sector       = start_sector;
    erase.NbSectors    = nb_sectors;

    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
    {
        if (!s_flash_unlocked)
        {
            (void)HAL_FLASH_Lock();
        }
        return false;
    }

    if (!s_flash_unlocked)
    {
        (void)HAL_FLASH_Lock();
    }
    return true;
}

/**
    * @brief  向 Flash 写入一段数据（自动对齐字节/字写入）。
    * @param  addr  目标地址
    * @param  data  源数据指针
    * @param  len   数据长度（字节）
    * @retval bool true 表示写入成功
    */
bool BL_FlashApp_Write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t word;

    if ((data == NULL) || (len == 0U))
    {
        return false;
    }

    if (!s_flash_unlocked)
    {
        if (HAL_FLASH_Unlock() != HAL_OK)
        {
            return false;
        }
    }

    i = 0U;

    while (((addr + i) & 0x3U) != 0U)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, addr + i, data[i]) != HAL_OK)
        {
            goto write_fail;
        }
        i++;
        if (i >= len)
        {
            goto write_done;
        }
    }

    while ((i + 4U) <= len)
    {
        word = ((uint32_t)data[i]) |
               ((uint32_t)data[i + 1U] << 8) |
               ((uint32_t)data[i + 2U] << 16) |
               ((uint32_t)data[i + 3U] << 24);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word) != HAL_OK)
        {
            goto write_fail;
        }

        i += 4U;
    }

    while (i < len)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, addr + i, data[i]) != HAL_OK)
        {
            goto write_fail;
        }
        i++;
    }

write_done:
    if (!s_flash_unlocked)
    {
        (void)HAL_FLASH_Lock();
    }
    return true;

write_fail:
    if (!s_flash_unlocked)
    {
        (void)HAL_FLASH_Lock();
    }
    return false;
}

/**
    * @brief  设置新的活动槽位并写入启动信息区。
    * @param  slot_addr  活动槽位的起始地址
    * @retval bool true 表示设置成功
    */
bool BL_FlashApp_SetActiveSlot(uint32_t slot_addr)
{
    bl_boot_state_t state;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0U;
    const uint8_t *raw;
    uint32_t i;

    if (slot_addr == BL_APP1_START_ADDR)
    {
        state.active_slot = SLOT_1;
    }
    else if (slot_addr == BL_APP2_START_ADDR)
    {
        state.active_slot = SLOT_2;
    }
    else
    {
        return false;
    }

    state.magic     = BL_BOOT_STATE_MAGIC;
    state.version   = BL_BOOT_STATE_VERSION;
    state.confirmed = 0U;
    state.checksum  = BL_BootState_CalcChecksum(&state);

    if (!s_flash_unlocked)
    {
        if (HAL_FLASH_Unlock() != HAL_OK)
        {
            return false;
        }
    }

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = BL_FLASH_VOLTAGE_RANGE;
    erase.Sector       = BL_FlashApp_GetSector(BL_BOOT_INFO_START_ADDR);
    erase.NbSectors    = 1U;

    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
    {
        goto slot_fail;
    }

    raw = (const uint8_t *)&state;
    for (i = 0U; i < sizeof(state); i++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE,
                              BL_BOOT_INFO_START_ADDR + i,
                              raw[i]) != HAL_OK)
        {
            goto slot_fail;
        }
    }

    if (!s_flash_unlocked)
    {
        (void)HAL_FLASH_Lock();
    }

    /* 回读验证，确保启动信息确实写入成功 */
    {
        const bl_boot_state_t *stored = (const bl_boot_state_t *)BL_BOOT_INFO_START_ADDR;
        if ((stored->magic != BL_BOOT_STATE_MAGIC) ||
            (stored->version != BL_BOOT_STATE_VERSION) ||
            (stored->active_slot != state.active_slot) ||
            (stored->checksum != BL_BootState_CalcChecksum(stored)))
        {
            return false;
        }
    }

    return true;

slot_fail:
    if (!s_flash_unlocked)
    {
        (void)HAL_FLASH_Lock();
    }
    return false;
}

/**
    * @brief  检查指定地址的应用程序是否有效（栈指针合法性）。
    * @param  addr  应用程序起始地址
    * @retval bool true 表示应用有效
    */
bool BL_FlashApp_IsAppValid(uint32_t addr)
{
    uint32_t sp = *(__IO uint32_t *)addr;

    if ((sp < 0x20000000UL) || (sp > 0x20020000UL))
    {
        return false;
    }
    return true;
}
