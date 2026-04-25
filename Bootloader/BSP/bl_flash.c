#include "bl_flash.h"

/* USER CODE BEGIN Header */
/**
    ******************************************************************************
    * @file    bl_flash.c
    * @brief   Bootloader Flash 操作实现。
    ******************************************************************************
    * @attention
    *
    * 这个模块负责双应用分区和启动信息区的最小必需能力：
    * - 应用分区地址合法性判断
    * - 分区向量表有效性判断
    * - 活动槽位读写
    * - 扇区擦除和数据读写
    *
    * 上层逻辑不要直接调用 HAL Flash 接口，而是统一走这里，方便后续扩展
    * 地址保护、校验和错误处理策略。
    *
    ******************************************************************************
    */
/* USER CODE END Header */

#include <string.h>

#define BL_ARRAY_SIZE(array)          (sizeof(array) / sizeof((array)[0]))
#define BL_BOOT_STATE_MAGIC           0x424C5354UL
#define BL_BOOT_STATE_VERSION         0x00000002UL

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t active_slot;
    uint32_t confirmed;     /* 0 = 待确认（首次启动），1 = 已确认 */
    uint32_t checksum;
} bl_boot_state_t;

static const bl_app_partition_t bl_app_partitions[] =
{
    {BL_APP_SLOT_1, BL_APP1_START_ADDR, BL_APP1_MAX_SIZE, "App1"},
    {BL_APP_SLOT_2, BL_APP2_START_ADDR, BL_APP2_MAX_SIZE, "App2"},
};

static uint32_t BL_Flash_GetSector(uint32_t address);

/**
    * @brief  计算启动信息结构的校验值。
    * @param  state  启动信息指针
    * @retval uint32_t 校验结果
    */
static uint32_t BL_Flash_CalculateBootStateChecksum(const bl_boot_state_t *state)
{
    return state->magic ^ state->version ^ state->active_slot ^ state->confirmed ^ 0x5A5AA5A5UL;
}

/**
    * @brief  判断地址范围是否完全位于指定应用分区内。
    * @param  address    起始地址
    * @param  length     数据长度
    * @param  partition  分区描述
    * @retval bool true 表示位于分区内，false 表示不在分区内
    */
static bool BL_Flash_IsRangeInPartition(uint32_t address, uint32_t length, const bl_app_partition_t *partition)
{
    uint32_t partition_end_addr;

    if ((partition == NULL) || (length == 0U))
    {
        return false;
    }

    partition_end_addr = partition->start_addr + partition->max_size;

    return (address >= partition->start_addr) &&
           (address < partition_end_addr) &&
           (length <= (partition_end_addr - address));
}

/**
    * @brief  根据地址定位所属的应用分区。
    * @param  address  Flash 地址
    * @retval const bl_app_partition_t* 对应分区，未命中返回 NULL
    */
static const bl_app_partition_t *BL_Flash_GetPartitionByAddress(uint32_t address)
{
    uint32_t index;

    for (index = 0U; index < BL_ARRAY_SIZE(bl_app_partitions); index++)
    {
        if (BL_Flash_IsRangeInPartition(address, 1U, &bl_app_partitions[index]))
        {
            return &bl_app_partitions[index];
        }
    }

    return NULL;
}

/**
    * @brief  读取并校验启动信息。
    * @param  state  输出启动信息
    * @retval bool true 表示读取有效，false 表示无效
    */
static bool BL_Flash_ReadBootState(bl_boot_state_t *state)
{
    const bl_boot_state_t *stored_state = (const bl_boot_state_t *)BL_BOOT_INFO_START_ADDR;

    if (state == NULL)
    {
        return false;
    }

    if ((stored_state->magic != BL_BOOT_STATE_MAGIC) ||
        (stored_state->version != BL_BOOT_STATE_VERSION))
    {
        return false;
    }

    if (stored_state->checksum != BL_Flash_CalculateBootStateChecksum(stored_state))
    {
        return false;
    }

    if ((stored_state->active_slot != (uint32_t)BL_APP_SLOT_1) &&
        (stored_state->active_slot != (uint32_t)BL_APP_SLOT_2))
    {
        return false;
    }

    *state = *stored_state;
    return true;
}

/**
    * @brief  选择当前应启动的槽位。
    * @retval bl_app_slot_t 槽位编号，没有有效应用时返回 BL_APP_SLOT_NONE
    */
static bl_app_slot_t BL_Flash_SelectBootSlot(void)
{
    bl_boot_state_t state = {0};
    bl_app_slot_t active_slot;
    bl_app_slot_t fallback_slot;

    if (!BL_Flash_ReadBootState(&state))
    {
        /* 没有有效启动信息，按 App1 > App2 顺序选择。 */
        if (BL_Flash_IsSlotValid(BL_APP_SLOT_1))
        {
            return BL_APP_SLOT_1;
        }
        if (BL_Flash_IsSlotValid(BL_APP_SLOT_2))
        {
            return BL_APP_SLOT_2;
        }
        return BL_APP_SLOT_NONE;
    }

    active_slot = (bl_app_slot_t)state.active_slot;

    if (BL_Flash_IsSlotValid(active_slot))
    {
        /*
         * confirmed == 0 表示这是首次启动新固件，应该给它机会运行。
         * App 启动成功后必须调用 BL_ConfirmBoot() 将 confirmed 置 1。
         * 回滚机制由外部看门狗或后续启动策略实现，
         * Bootloader 不做 preemptive 回滚，否则新固件永远得不到首次启动机会。
         */
        (void)fallback_slot;  /*  unused, 保留变量避免警告  */
        return active_slot;
    }

    if (BL_Flash_IsSlotValid(BL_APP_SLOT_1))
    {
        return BL_APP_SLOT_1;
    }

    if (BL_Flash_IsSlotValid(BL_APP_SLOT_2))
    {
        return BL_APP_SLOT_2;
    }

    return BL_APP_SLOT_NONE;
}

/**
    * @brief  选择下一次升级应写入的槽位。
    * @retval bl_app_slot_t 槽位编号
    */
static bl_app_slot_t BL_Flash_SelectUpgradeSlot(void)
{
    bl_app_slot_t boot_slot = BL_Flash_SelectBootSlot();

    if (boot_slot == BL_APP_SLOT_1)
    {
        return BL_APP_SLOT_2;
    }

    if (boot_slot == BL_APP_SLOT_2)
    {
        return BL_APP_SLOT_1;
    }

    /* 没有有效槽位时，默认写入 App1。 */
    return BL_APP_SLOT_1;
}

/**
    * @brief  将启动信息写入专用信息分区。
    * @param  state  启动信息指针
    * @retval bl_flash_status_t 写入结果
    */
static bl_flash_status_t BL_Flash_WriteBootState(const bl_boot_state_t *state)
{
    FLASH_EraseInitTypeDef erase_init = {0};
    bl_boot_state_t verify_state = {0};
    const uint8_t *raw_bytes;
    uint32_t sector_error = 0U;
    uint32_t index;

    if (state == NULL)
    {
        return BL_FLASH_INVALID_ADDRESS;
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return BL_FLASH_WRITE_ERROR;
    }

    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.VoltageRange = BL_FLASH_VOLTAGE_RANGE;
    erase_init.Sector = BL_Flash_GetSector(BL_BOOT_INFO_START_ADDR);
    erase_init.NbSectors = 1U;

    if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
    {
        (void)HAL_FLASH_Lock();
        return BL_FLASH_ERASE_ERROR;
    }

    raw_bytes = (const uint8_t *)state;
    for (index = 0U; index < sizeof(*state); index++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE,
                              BL_BOOT_INFO_START_ADDR + index,
                              raw_bytes[index]) != HAL_OK)
        {
            (void)HAL_FLASH_Lock();
            return BL_FLASH_WRITE_ERROR;
        }
    }

    (void)HAL_FLASH_Lock();

    if (!BL_Flash_ReadBootState(&verify_state) ||
        (verify_state.active_slot != state->active_slot))
    {
        return BL_FLASH_WRITE_ERROR;
    }

    return BL_FLASH_OK;
}

/**
    * @brief  获取指定槽位的分区描述。
    * @param  slot  应用槽位
    * @retval const bl_app_partition_t* 分区描述，未命中返回 NULL
    */
const bl_app_partition_t *BL_Flash_GetAppPartition(bl_app_slot_t slot)
{
    uint32_t index;

    for (index = 0U; index < BL_ARRAY_SIZE(bl_app_partitions); index++)
    {
        if (bl_app_partitions[index].slot == slot)
        {
            return &bl_app_partitions[index];
        }
    }

    return NULL;
}

/**
    * @brief  根据地址获取对应的 Flash 扇区编号。
    * @param  address  Flash 地址
    * @retval uint32_t 扇区编号
    */
static uint32_t BL_Flash_GetSector(uint32_t address)
{
    if (address < 0x08004000UL)
    {
        return FLASH_SECTOR_0;
    }
    if (address < 0x08008000UL)
    {
        return FLASH_SECTOR_1;
    }
    if (address < 0x0800C000UL)
    {
        return FLASH_SECTOR_2;
    }
    if (address < 0x08010000UL)
    {
        return FLASH_SECTOR_3;
    }
    if (address < 0x08020000UL)
    {
        return FLASH_SECTOR_4;
    }
    if (address < 0x08040000UL)
    {
        return FLASH_SECTOR_5;
    }
    if (address < 0x08060000UL)
    {
        return FLASH_SECTOR_6;
    }
    if (address < 0x08080000UL)
    {
        return FLASH_SECTOR_7;
    }
    if (address < 0x080A0000UL)
    {
        return FLASH_SECTOR_8;
    }
    if (address < 0x080C0000UL)
    {
        return FLASH_SECTOR_9;
    }
    if (address < 0x080E0000UL)
    {
        return FLASH_SECTOR_10;
    }

    return FLASH_SECTOR_11;
}

/**
    * @brief  判断地址范围是否位于受 Bootloader 管理的应用分区内。
    * @param  address  起始地址
    * @param  length   数据长度，单位字节
    * @retval bool true 表示合法，false 表示非法
    */
bool BL_Flash_IsAddressInApp(uint32_t address, uint32_t length)
{
    uint32_t index;

    for (index = 0U; index < BL_ARRAY_SIZE(bl_app_partitions); index++)
    {
        if (BL_Flash_IsRangeInPartition(address, length, &bl_app_partitions[index]))
        {
            return true;
        }
    }

    return false;
}

/**
    * @brief  判断指定应用分区入口是否有效。
    * @param  app_address  应用程序起始地址
    * @retval bool true 表示应用存在且向量表有效，false 表示无效
    */
bool BL_Flash_IsApplicationValid(uint32_t app_address)
{
    const bl_app_partition_t *partition = BL_Flash_GetPartitionByAddress(app_address);
    uint32_t stack_pointer;
    uint32_t reset_handler;

    if ((partition == NULL) || (partition->start_addr != app_address))
    {
        return false;
    }

    stack_pointer = *(__IO uint32_t *)app_address;
    reset_handler = *(__IO uint32_t *)(app_address + 4U);

    return ((stack_pointer & 0x2FFE0000UL) == 0x20000000UL) &&
           (reset_handler >= partition->start_addr) &&
           (reset_handler < (partition->start_addr + partition->max_size));
}

/**
    * @brief  判断指定槽位中的应用是否有效。
    * @param  slot  应用槽位
    * @retval bool true 表示有效，false 表示无效
    */
bool BL_Flash_IsSlotValid(bl_app_slot_t slot)
{
    const bl_app_partition_t *partition = BL_Flash_GetAppPartition(slot);

    if (partition == NULL)
    {
        return false;
    }

    return BL_Flash_IsApplicationValid(partition->start_addr);
}

/**
    * @brief  获取当前记录的活动槽位。
    * @retval bl_app_slot_t 活动槽位，未配置时返回 BL_APP_SLOT_NONE
    */
bl_app_slot_t BL_Flash_GetActiveSlot(void)
{
    bl_boot_state_t state = {0};

    if (!BL_Flash_ReadBootState(&state))
    {
        return BL_APP_SLOT_NONE;
    }

    return (bl_app_slot_t)state.active_slot;
}

/**
    * @brief  获取当前应启动的应用分区。
    * @param  partition  输出分区描述
    * @retval bool true 表示找到可启动分区，false 表示没有有效应用
    */
bool BL_Flash_GetBootPartition(bl_app_partition_t *partition)
{
    const bl_app_partition_t *selected_partition;

    if (partition == NULL)
    {
        return false;
    }

    selected_partition = BL_Flash_GetAppPartition(BL_Flash_SelectBootSlot());
    if (selected_partition == NULL)
    {
        return false;
    }

    *partition = *selected_partition;
    return true;
}

/**
    * @brief  获取当前升级应写入的应用分区。
    * @param  partition  输出分区描述
    * @retval bool true 表示找到目标分区，false 表示失败
    */
bool BL_Flash_GetUpgradePartition(bl_app_partition_t *partition)
{
    const bl_app_partition_t *selected_partition;

    if (partition == NULL)
    {
        return false;
    }

    selected_partition = BL_Flash_GetAppPartition(BL_Flash_SelectUpgradeSlot());
    if (selected_partition == NULL)
    {
        return false;
    }

    *partition = *selected_partition;
    return true;
}

/**
    * @brief  将指定槽位记录为新的活动槽位。
    * @param  slot  应用槽位
    * @retval bl_flash_status_t Flash 操作状态
    */
bl_flash_status_t BL_Flash_SetActiveSlot(bl_app_slot_t slot)
{
    bl_boot_state_t state = {0};

    if (BL_Flash_GetAppPartition(slot) == NULL)
    {
        return BL_FLASH_INVALID_ADDRESS;
    }

    state.magic = BL_BOOT_STATE_MAGIC;
    state.version = BL_BOOT_STATE_VERSION;
    state.active_slot = (uint32_t)slot;
    state.confirmed = 0U;   /* 新槽位默认未确认，等待 App 首次启动后确认。 */
    state.checksum = BL_Flash_CalculateBootStateChecksum(&state);

    return BL_Flash_WriteBootState(&state);
}

/**
    * @brief  确认当前活动槽位启动成功，阻止下次回滚。
    * @retval bl_flash_status_t Flash 操作状态
    */
bl_flash_status_t BL_Flash_ConfirmBoot(void)
{
    bl_boot_state_t state = {0};

    if (!BL_Flash_ReadBootState(&state))
    {
        return BL_FLASH_OK;  /* 无有效启动信息，非升级场景，视为已确认。 */
    }

    if (state.confirmed == 1U)
    {
        return BL_FLASH_OK;  /* 已确认，无需重复写。 */
    }

    state.confirmed = 1U;
    state.checksum = BL_Flash_CalculateBootStateChecksum(&state);

    return BL_Flash_WriteBootState(&state);
}

/**
    * @brief  查询当前活动槽位是否已确认。
    * @retval bool true 表示已确认，false 表示未确认或无有效启动信息
    */
bool BL_Flash_IsBootConfirmed(void)
{
    bl_boot_state_t state = {0};

    if (!BL_Flash_ReadBootState(&state))
    {
        return true;   /* 无有效启动信息，非升级场景，不触发回滚警告。 */
    }

    return (state.confirmed == 1U);
}

/**
    * @brief  擦除目标范围覆盖到的所有 Flash 扇区。
    * @param  address  起始地址
    * @param  length   长度，单位字节
    * @retval bl_flash_status_t 擦除结果
    */
bl_flash_status_t BL_Flash_Erase(uint32_t address, uint32_t length)
{
    FLASH_EraseInitTypeDef erase_init = {0};
    uint32_t sector_error = 0U;
    uint32_t start_sector;
    uint32_t end_sector;

    if (!BL_Flash_IsAddressInApp(address, length))
    {
        return BL_FLASH_INVALID_ADDRESS;
    }

    start_sector = BL_Flash_GetSector(address);
    end_sector = BL_Flash_GetSector(address + length - 1U);

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return BL_FLASH_ERASE_ERROR;
    }

    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.VoltageRange = BL_FLASH_VOLTAGE_RANGE;
    erase_init.Sector = start_sector;
    erase_init.NbSectors = (end_sector - start_sector) + 1U;

    if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
    {
        (void)HAL_FLASH_Lock();
        return BL_FLASH_ERASE_ERROR;
    }

    (void)HAL_FLASH_Lock();
    return BL_FLASH_OK;
}

/**
    * @brief  向 Flash 写入一段数据。
    * @param  address  目标地址
    * @param  data     源数据指针
    * @param  length   数据长度，单位字节
    * @retval bl_flash_status_t 写入结果
    */
bl_flash_status_t BL_Flash_Write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t index;

    if ((data == NULL) || !BL_Flash_IsAddressInApp(address, length))
    {
        return BL_FLASH_INVALID_ADDRESS;
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return BL_FLASH_WRITE_ERROR;
    }

    /*
     * 最小版本直接按字节编程，换来最少的对齐约束。
     * 后续如果想提升速度，再改成按字或双字写入即可。
     */
    for (index = 0U; index < length; index++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, address + index, data[index]) != HAL_OK)
        {
            (void)HAL_FLASH_Lock();
            return BL_FLASH_WRITE_ERROR;
        }
    }

    (void)HAL_FLASH_Lock();
    return BL_FLASH_OK;
}

/**
    * @brief  从 Flash 读取一段数据到 RAM。
    * @param  address  源地址
    * @param  data     目标缓存指针
    * @param  length   数据长度，单位字节
    * @retval None
    */
void BL_Flash_Read(uint32_t address, uint8_t *data, uint32_t length)
{
    if ((data == NULL) || (length == 0U))
    {
        return;
    }

    memcpy(data, (const void *)address, length);
}
