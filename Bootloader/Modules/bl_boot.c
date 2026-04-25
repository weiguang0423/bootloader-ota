#include "bl_boot.h"

/* USER CODE BEGIN Header */
/**
    ******************************************************************************
    * @file    bl_boot.c
    * @brief   Bootloader 主流程实现。
    ******************************************************************************
    * @attention
    *
    * 这个模块负责启动阶段的两个关键决策：
    * 1. 当前是进入升级模式，还是启动一个有效应用槽位。
    * 2. 如果不升级，如何从 App1/App2 中选择并安全跳转。
    *
    * 为了让控制流保持清晰，这里不直接处理协议细节，只调用 YModem 模块。
    *
    ******************************************************************************
    */
/* USER CODE END Header */

#include <stdbool.h>
#include <string.h>

#include "bl_board.h"
#include "bl_flash.h"
#include "bl_ymodem.h"

typedef void (*bl_app_entry_t)(void);

/**
    * @brief  返回槽位名称文本。
    * @param  slot  应用槽位
    * @retval const char* 文本描述
    */
static const char *BL_Boot_GetSlotName(bl_app_slot_t slot)
{
    const bl_app_partition_t *partition = BL_Flash_GetAppPartition(slot);

    if (partition == NULL)
    {
        return "未配置";
    }

    return partition->name;
}

/**
    * @brief  打印当前 Flash 分区布局。
    * @retval None
    */
static void BL_Boot_PrintPartitionLayout(void)
{
    BL_Board_Printf("Bootloader: 0x%08lX - 0x%08lX\r\n",
                    (unsigned long)BL_BOOT_START_ADDR,
                    (unsigned long)(BL_BOOT_START_ADDR + BL_BOOT_MAX_SIZE - 1UL));
    BL_Board_Printf("App1      : 0x%08lX - 0x%08lX\r\n",
                    (unsigned long)BL_APP1_START_ADDR,
                    (unsigned long)(BL_APP1_START_ADDR + BL_APP1_MAX_SIZE - 1UL));
    BL_Board_Printf("App2      : 0x%08lX - 0x%08lX\r\n",
                    (unsigned long)BL_APP2_START_ADDR,
                    (unsigned long)(BL_APP2_START_ADDR + BL_APP2_MAX_SIZE - 1UL));
}

/**
    * @brief  判断接收到的字符是否为升级请求字符。
    * @param  value  接收到的字符
    * @retval bool true 表示请求进入升级模式，false 表示不是
    */
static bool BL_Boot_IsUpdateRequest(uint8_t value)
{
    uint8_t upper = (uint8_t)BL_UPDATE_REQUEST_CHAR;
    uint8_t lower = upper;

    if ((upper >= (uint8_t)'A') && (upper <= (uint8_t)'Z'))
    {
        lower = (uint8_t)(upper + ((uint8_t)'a' - (uint8_t)'A'));
    }

    return (value == upper) || (value == lower);
}

/**
    * @brief  将 YModem 状态值转换为可打印文本。
    * @param  status  YModem 状态码
    * @retval const char* 状态文本
    */
static const char *BL_Boot_GetYmodemStatusText(bl_ymodem_status_t status)
{
    switch (status)
    {
        case BL_YMODEM_OK:
            return "成功";
        case BL_YMODEM_TIMEOUT:
            return "超时";
        case BL_YMODEM_ABORTED:
            return "用户取消";
        case BL_YMODEM_PROTOCOL_ERROR:
            return "协议错误";
        case BL_YMODEM_CRC_ERROR:
            return "CRC 错误";
        case BL_YMODEM_FLASH_ERROR:
            return "Flash 读写错误";
        case BL_YMODEM_SIZE_ERROR:
            return "文件超出目标分区大小";
        default:
            return "未知状态";
    }
}

/**
    * @brief  在自动跳转窗口内等待用户请求升级。
    * @param  timeout_ms  等待窗口长度，单位毫秒
    * @retval bool true 表示收到升级请求，false 表示超时未收到
    */
static bool BL_Boot_WaitForUpdateWindow(uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();
    uint8_t value = 0U;

    while ((HAL_GetTick() - start_tick) < timeout_ms)
    {
        if (BL_Board_ReadByte(&value, 20U) == HAL_OK)
        {
            if (BL_Boot_IsUpdateRequest(value))
            {
                return true;
            }
        }
    }

    return false;
}

/**
    * @brief  跳转到当前选中的应用程序入口。
    * @retval None
    */
void BL_Boot_JumpToApplication(void)
{
    bl_app_partition_t boot_partition;
    uint32_t app_stack_pointer;
    uint32_t app_reset_handler;
    bl_app_entry_t app_entry;
    uint32_t index;

    if (!BL_Flash_GetBootPartition(&boot_partition))
    {
        BL_Board_Printf("没有可启动的应用程序。\r\n");
        return;
    }

    app_stack_pointer = *(__IO uint32_t *)boot_partition.start_addr;
    app_reset_handler = *(__IO uint32_t *)(boot_partition.start_addr + 4U);
    app_entry = (bl_app_entry_t)app_reset_handler;

    BL_Board_Printf("启动 %s @ 0x%08lX...\r\n",
                    boot_partition.name,
                    (unsigned long)boot_partition.start_addr);
    BL_Board_Delay(20U);

    /*
     * 跳转前先关闭中断并撤销当前 Bootloader 运行环境，避免把 Boot 阶段的
     * 外设状态、中断挂起位和时基状态带到应用程序里。
     */
    __disable_irq();

    (void)HAL_UART_DeInit(BL_Board_GetUart());
    HAL_RCC_DeInit();
    HAL_DeInit();

    /*
     * HAL_DeInit() 通过 AHB1 总线复位把所有 GPIO 恢复成默认状态，
     * PA9（TX）变为浮空输入，终端接收器可能把电平漂移当作起始位，
     * 导致后续 APP 首包帧同步错位。
     *
     * 修复：先通过 BSRR 锁存输出寄存器 = 1，再切换引脚为推挽输出，
     * 保证引脚从浮空直接跳到驱动高电平，不会出现短暂拉低的毛刺。
     * GPIOA 时钟仍然使能（AHB1ENR 不受 HAL_DeInit 影响）。
     */
    BL_UART_TX_PORT->BSRR = BL_UART_TX_PIN;
    {
        GPIO_InitTypeDef gpio_keep_high = {0};
        gpio_keep_high.Pin = BL_UART_TX_PIN;
        gpio_keep_high.Mode = GPIO_MODE_OUTPUT_PP;
        gpio_keep_high.Pull = GPIO_NOPULL;
        gpio_keep_high.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(BL_UART_TX_PORT, &gpio_keep_high);
    }

    /* 清空所有外部中断使能和挂起状态。 */
    for (index = 0U; index < 8U; index++)
    {
        NVIC->ICER[index] = 0xFFFFFFFFUL;
        NVIC->ICPR[index] = 0xFFFFFFFFUL;
    }

    /* 关闭系统节拍，避免应用启动后立刻受到旧 SysTick 影响。 */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    /* 切换到应用自己的中断向量表和主栈。 */
    SCB->VTOR = boot_partition.start_addr;
    __set_MSP(app_stack_pointer);
    __DSB();
    __ISB();

    /* 最终跳转到应用 Reset_Handler。 */
    app_entry();

    while (1)
    {
    }
}

/**
    * @brief  运行 Bootloader 主流程。
    * @retval None
    */
void BL_Boot_Run(void)
{
    bl_app_partition_t boot_partition;
    bl_app_partition_t upgrade_partition;
    bl_ymodem_file_t file_info;
    bl_ymodem_status_t ymodem_status;
    bl_flash_status_t flash_status;
    bool boot_available;

    BL_Board_Printf("\r\n========================================\r\n");
    BL_Board_Printf("双区 STM32 YModem Bootloader\r\n");
    BL_Boot_PrintPartitionLayout();
    BL_Board_Printf("升级串口: USART1 @ %lu\r\n", (unsigned long)BL_UART_BAUDRATE);
    BL_Board_Printf("活动槽位: %s\r\n", BL_Boot_GetSlotName(BL_Flash_GetActiveSlot()));
    if (!BL_Flash_IsBootConfirmed())
    {
        BL_Board_Printf("警告: 活动槽位未确认，首次启动失败将自动回滚。\r\n");
    }
    BL_Board_Printf("========================================\r\n");

    boot_available = BL_Flash_GetBootPartition(&boot_partition);
    (void)BL_Flash_GetUpgradePartition(&upgrade_partition);

    if (boot_available)
    {
        /*
         * 如果应用有效，则优先给应用启动机会。
         * 只有在用户明确请求升级时，才停留在 Bootloader 中。
         */
        BL_Board_Printf("检测到可启动应用: %s @ 0x%08lX\r\n",
                        boot_partition.name,
                        (unsigned long)boot_partition.start_addr);
        BL_Board_Printf("%lu ms 内发送字符 %c 进入升级，否则自动启动 %s。\r\n",
                        (unsigned long)BL_AUTO_BOOT_TIMEOUT_MS,
                        (char)BL_UPDATE_REQUEST_CHAR,
                        boot_partition.name);
        BL_Board_Printf("本次升级目标: %s @ 0x%08lX\r\n",
                        upgrade_partition.name,
                        (unsigned long)upgrade_partition.start_addr);

        if (!BL_Boot_WaitForUpdateWindow(BL_AUTO_BOOT_TIMEOUT_MS))
        {
            BL_Boot_JumpToApplication();
        }
    }
    else
    {
        /* 没有有效应用时，Bootloader 直接承担恢复入口角色。 */
        BL_Board_Printf("未检测到有效应用，直接进入升级模式。\r\n");
        BL_Board_Printf("默认写入目标: %s @ 0x%08lX\r\n",
                        upgrade_partition.name,
                        (unsigned long)upgrade_partition.start_addr);
    }

    while (1)
    {
        if (!BL_Flash_GetUpgradePartition(&upgrade_partition))
        {
            BL_Board_Printf("未找到可写入的应用分区。\r\n");
            BL_Board_Delay(1000U);
            continue;
        }

        memset(&file_info, 0, sizeof(file_info));
        BL_Board_Printf("\r\n等待 YModem 发送，目标分区: %s @ 0x%08lX\r\n",
                        upgrade_partition.name,
                        (unsigned long)upgrade_partition.start_addr);

        ymodem_status = BL_Ymodem_Receive(upgrade_partition.start_addr,
                                          upgrade_partition.max_size,
                                          &file_info);
        if (ymodem_status == BL_YMODEM_OK)
        {
            if (!BL_Flash_IsSlotValid(upgrade_partition.slot))
            {
                BL_Board_Printf("升级完成，但 %s 向量表无效，保持当前活动槽位不变。\r\n",
                                upgrade_partition.name);
                continue;
            }

            flash_status = BL_Flash_SetActiveSlot(upgrade_partition.slot);
            if (flash_status != BL_FLASH_OK)
            {
                BL_Board_Printf("镜像已写入 %s，但活动槽位更新失败。\r\n",
                                upgrade_partition.name);
                BL_Board_Printf("请重新执行升级后再复位。\r\n");
                continue;
            }

            /* 升级成功后直接复位，让系统重新按完整上电流程启动。 */
            BL_Board_Printf("升级成功。文件名: %s, 大小: %lu 字节\r\n",
                            file_info.file_name,
                            (unsigned long)file_info.file_size);
            BL_Board_Printf("已切换活动槽位到 %s。\r\n", upgrade_partition.name);
            BL_Board_Printf("系统即将复位。\r\n");
            BL_Board_Delay(800U);
            NVIC_SystemReset();
        }

        /* 失败后不死机，允许上位机直接重新发起下一次传输。 */
        BL_Board_Printf("本次升级未完成：%s\r\n", BL_Boot_GetYmodemStatusText(ymodem_status));
        BL_Board_Printf("可重新发送文件继续升级。\r\n");
    }
}
