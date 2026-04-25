/* USER CODE BEGIN Header */
/**
    ******************************************************************************
    * @file    ota_confirm.c
    * @brief   启动确认功能实现。
    ******************************************************************************
    * @attention
    *
    * 应用成功启动后向启动信息区写入 confirmed 标记，
    * 防止 Bootloader 因未确认而自动回滚到旧槽位。
    *
    ******************************************************************************
    */
/* USER CODE END Header */
#include "ota_confirm.h"

#include <string.h>

#include "boot_state.h"
#include "bsp_flash.h"

/**
    * @brief  确认当前启动成功，写入确认标记到启动信息区。
    * @retval HAL_StatusTypeDef HAL_OK 表示确认成功，HAL_ERROR 表示写入失败
    */
HAL_StatusTypeDef BL_Confirm_Boot(void)
{
    const bl_boot_state_t *stored = (const bl_boot_state_t *)BL_BOOT_INFO_START_ADDR;
    bl_boot_state_t state;
    FLASH_EraseInitTypeDef erase_init = {0};
    uint32_t sector_error = 0U;
    const uint8_t *raw_bytes;
    uint32_t index;

    /* 启动信息无效时无需确认 */
    if ((stored->magic != BL_BOOT_STATE_MAGIC) ||
        (stored->version != BL_BOOT_STATE_VERSION) ||
        (stored->checksum != BL_BootState_CalcChecksum(stored)))
    {
        return HAL_OK;
    }

    /* 已确认过，无需重复写入 */
    if (stored->confirmed == 1U)
    {
        return HAL_OK;
    }

    state = *stored;
    state.confirmed = 1U;
    state.checksum = BL_BootState_CalcChecksum(&state);

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return HAL_ERROR;
    }

    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.VoltageRange = BL_FLASH_VOLTAGE_RANGE;
    erase_init.Sector = BL_FlashApp_GetSector(BL_BOOT_INFO_START_ADDR);
    erase_init.NbSectors = 1U;

    if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
    {
        (void)HAL_FLASH_Lock();
        return HAL_ERROR;
    }

    raw_bytes = (const uint8_t *)&state;
    for (index = 0U; index < sizeof(state); index++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE,
                              BL_BOOT_INFO_START_ADDR + index,
                              raw_bytes[index]) != HAL_OK)
        {
            (void)HAL_FLASH_Lock();
            return HAL_ERROR;
        }
    }

    (void)HAL_FLASH_Lock();
    return HAL_OK;
}
