#include "bl_ymodem.h"

/* USER CODE BEGIN Header */
/**
    ******************************************************************************
    * @file    bl_ymodem.c
    * @brief   YModem 协议接收实现。
    ******************************************************************************
    * @attention
    *
    * 这个模块实现的是“最小可用”的 YModem 接收器，重点目标不是覆盖所有
    * 协议分支，而是保证主流程清晰、可读、可扩展：
    * - 先握手
    * - 再接收起始帧
    * - 然后按包写入目标分区
    * - 最后按标准流程处理 EOT 和结束帧
    *
    * 后续如果要加入断点续传、双镜像确认、签名校验等能力，应在这个模块
    * 外部扩展，而不是把所有槽位策略塞进协议解析函数里。
    *
    ******************************************************************************
    */
/* USER CODE END Header */

#include <stdbool.h>
#include <string.h>

#include "bl_board.h"
#include "bl_flash.h"

/* YModem 控制字符 */
#define BL_YMODEM_SOH                 0x01U
#define BL_YMODEM_STX                 0x02U
#define BL_YMODEM_EOT                 0x04U
#define BL_YMODEM_ACK                 0x06U
#define BL_YMODEM_NAK                 0x15U
#define BL_YMODEM_CAN                 0x18U
#define BL_YMODEM_CRC_REQUEST         0x43U
#define BL_YMODEM_ABORT_UPPER         0x41U
#define BL_YMODEM_ABORT_LOWER         0x61U

/* 数据包尺寸 */
#define BL_YMODEM_PACKET_SIZE_128     128U
#define BL_YMODEM_PACKET_SIZE_1K      1024U

/**
    * @brief 单个 YModem 数据包接收结果。
    */
typedef enum
{
    BL_PACKET_OK = 0,
    BL_PACKET_TIMEOUT,
    BL_PACKET_EOT,
    BL_PACKET_ABORTED,
    BL_PACKET_CRC_ERROR,
    BL_PACKET_SEQUENCE_ERROR,
    BL_PACKET_UNSUPPORTED,
} bl_ymodem_packet_status_t;

/**
    * @brief 接收到的单个 YModem 数据包描述。
    */
typedef struct
{
    uint8_t sequence;
    uint16_t payload_size;
    uint8_t payload[BL_YMODEM_PACKET_SIZE_1K];
} bl_ymodem_packet_t;

/**
    * @brief  计算限定长度字符串的实际长度。
    * @param  text        字符串指针
    * @param  max_length  最大可扫描长度
    * @retval size_t 实际字符串长度
    */
static size_t BL_Ymodem_Strnlen(const char *text, size_t max_length)
{
    size_t length = 0U;

    while ((length < max_length) && (text[length] != '\0'))
    {
        length++;
    }

    return length;
}

/**
    * @brief  将十进制文本解析为 uint32_t。
    * @param  text   输入文本指针
    * @param  value  解析结果输出指针
    * @retval bool true 表示解析成功，false 表示解析失败
    */
static bool BL_Ymodem_ParseUint32(const char *text, uint32_t *value)
{
    uint32_t result = 0U;
    bool has_digit = false;

    if ((text == NULL) || (value == NULL))
    {
        return false;
    }

    while ((*text >= '0') && (*text <= '9'))
    {
        uint32_t digit = (uint32_t)(*text - '0');
        has_digit = true;

        if (result > ((0xFFFFFFFFU - digit) / 10U))
        {
            return false;
        }

        result = (result * 10U) + digit;
        text++;
    }

    if (!has_digit)
    {
        return false;
    }

    *value = result;
    return true;
}

/**
    * @brief  计算 YModem 包使用的 CRC16 值。
    * @param  data    输入数据指针
    * @param  length  数据长度
    * @retval uint16_t CRC16 结果
    */
static uint16_t BL_Ymodem_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0U;
    uint16_t index;
    uint8_t bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= (uint16_t)data[index] << 8;

        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

/**
    * @brief  发送单个协议字节。
    * @param  value  要发送的字节
    * @retval None
    */
static void BL_Ymodem_SendByte(uint8_t value)
{
    (void)BL_Board_Write(&value, 1U, BL_UART_TIMEOUT_MS);
}

/**
    * @brief  发送双 CAN 取消当前传输。
    * @retval None
    */
static void BL_Ymodem_SendCancel(void)
{
    uint8_t cancel_frame[2] = {BL_YMODEM_CAN, BL_YMODEM_CAN};
    (void)BL_Board_Write(cancel_frame, sizeof(cancel_frame), BL_UART_TIMEOUT_MS);
}

/**
    * @brief  解析 YModem 起始帧中的文件名和文件大小。
    * @param  payload       起始帧有效载荷
    * @param  payload_size  有效载荷长度
    * @param  file          文件信息输出结构体
    * @retval bool true 表示解析成功，false 表示解析失败
    */
static bool BL_Ymodem_ParseFileInfo(const uint8_t *payload, uint16_t payload_size, bl_ymodem_file_t *file)
{
    size_t name_length;
    const char *size_text;

    if ((payload == NULL) || (file == NULL) || (payload_size == 0U))
    {
        return false;
    }

    name_length = BL_Ymodem_Strnlen((const char *)payload, payload_size);
    if ((name_length == 0U) || (name_length >= BL_YMODEM_FILE_NAME_LEN))
    {
        return false;
    }

    if ((name_length + 1U) >= payload_size)
    {
        return false;
    }

    memcpy(file->file_name, payload, name_length);
    file->file_name[name_length] = '\0';

    size_text = (const char *)&payload[name_length + 1U];
    if (!BL_Ymodem_ParseUint32(size_text, &file->file_size))
    {
        return false;
    }

    if (file->file_size == 0U)
    {
        return false;
    }

    file->received_size = 0U;
    return true;
}

/**
    * @brief  从串口读取并解析一个完整的 YModem 数据包。
    * @param  packet      输出包结构体
    * @param  timeout_ms  包接收超时时间
    * @retval bl_ymodem_packet_status_t 单包解析结果
    */
static bl_ymodem_packet_status_t BL_Ymodem_ReadPacket(bl_ymodem_packet_t *packet, uint32_t timeout_ms)
{
    uint8_t header = 0U;
    uint8_t seq = 0U;
    uint8_t seq_complement = 0U;
    uint8_t crc_bytes[2] = {0U, 0U};
    uint16_t expected_crc;
    uint16_t packet_crc;

    if (packet == NULL)
    {
        return BL_PACKET_UNSUPPORTED;
    }

    if (BL_Board_ReadByte(&header, timeout_ms) != HAL_OK)
    {
        return BL_PACKET_TIMEOUT;
    }

    switch (header)
    {
        case BL_YMODEM_SOH:
            packet->payload_size = BL_YMODEM_PACKET_SIZE_128;
            break;

        case BL_YMODEM_STX:
#if BL_YMODEM_USE_1K_PACKET
            packet->payload_size = BL_YMODEM_PACKET_SIZE_1K;
            break;
#else
            return BL_PACKET_UNSUPPORTED;
#endif

        case BL_YMODEM_EOT:
            return BL_PACKET_EOT;

        case BL_YMODEM_CAN:
            return BL_PACKET_ABORTED;

        case BL_YMODEM_ABORT_UPPER:
        case BL_YMODEM_ABORT_LOWER:
            return BL_PACKET_ABORTED;

        default:
            return BL_PACKET_UNSUPPORTED;
    }

    if (BL_Board_ReadByte(&seq, timeout_ms) != HAL_OK)
    {
        return BL_PACKET_TIMEOUT;
    }

    if (BL_Board_ReadByte(&seq_complement, timeout_ms) != HAL_OK)
    {
        return BL_PACKET_TIMEOUT;
    }

    if ((uint8_t)(seq + seq_complement) != 0xFFU)
    {
        return BL_PACKET_SEQUENCE_ERROR;
    }

    if (BL_Board_Read(packet->payload, packet->payload_size, timeout_ms) != HAL_OK)
    {
        return BL_PACKET_TIMEOUT;
    }

    if (BL_Board_Read(crc_bytes, 2U, timeout_ms) != HAL_OK)
    {
        return BL_PACKET_TIMEOUT;
    }

    expected_crc = BL_Ymodem_Crc16(packet->payload, packet->payload_size);
    packet_crc = (uint16_t)((uint16_t)crc_bytes[0] << 8) | crc_bytes[1];

    if (expected_crc != packet_crc)
    {
        return BL_PACKET_CRC_ERROR;
    }

    packet->sequence = seq;
    return BL_PACKET_OK;
}

/**
    * @brief  接收一个 YModem 文件并写入指定目标分区。
    * @param  flash_address  应用写入起始地址
    * @param  max_size       允许写入的最大字节数
    * @param  file           文件信息输出结构体
    * @retval bl_ymodem_status_t 会话结果
    */
bl_ymodem_status_t BL_Ymodem_Receive(uint32_t flash_address, uint32_t max_size, bl_ymodem_file_t *file)
{
    bl_ymodem_status_t result = BL_YMODEM_PROTOCOL_ERROR;
    bl_ymodem_packet_status_t packet_status;
    bl_ymodem_packet_t packet;
    uint8_t expected_sequence = 1U;
    uint8_t error_count = 0U;
    uint32_t session_start_tick = HAL_GetTick();
    bool transfer_started = false;

    if (file == NULL)
    {
        return BL_YMODEM_PROTOCOL_ERROR;
    }

    memset(file, 0, sizeof(*file));
    BL_Board_SetLed(true);

    /*
     * 第一阶段：握手并接收起始帧。
     * 起始帧里包含文件名和文件大小。
     */
    while (!transfer_started)
    {
        if ((HAL_GetTick() - session_start_tick) > BL_YMODEM_SESSION_TIMEOUT_MS)
        {
            result = BL_YMODEM_TIMEOUT;
            goto exit;
        }

        BL_Ymodem_SendByte(BL_YMODEM_CRC_REQUEST);

        /* 只有收到块号为 0 的起始帧，才进入真正的数据接收阶段。 */
        packet_status = BL_Ymodem_ReadPacket(&packet, BL_YMODEM_PACKET_TIMEOUT_MS);
        switch (packet_status)
        {
            case BL_PACKET_OK:
                if (packet.sequence != 0U)
                {
                    BL_Ymodem_SendByte(BL_YMODEM_NAK);
                    break;
                }

                if (!BL_Ymodem_ParseFileInfo(packet.payload, packet.payload_size, file))
                {
                    result = BL_YMODEM_PROTOCOL_ERROR;
                    BL_Ymodem_SendCancel();
                    goto exit;
                }

                if (file->file_size > max_size)
                {
                    result = BL_YMODEM_SIZE_ERROR;
                    BL_Ymodem_SendCancel();
                    goto exit;
                }

                if (BL_Flash_Erase(flash_address, file->file_size) != BL_FLASH_OK)
                {
                    result = BL_YMODEM_FLASH_ERROR;
                    BL_Ymodem_SendCancel();
                    goto exit;
                }

                BL_Ymodem_SendByte(BL_YMODEM_ACK);
                BL_Ymodem_SendByte(BL_YMODEM_CRC_REQUEST);
                transfer_started = true;
                error_count = 0U;
                break;

            case BL_PACKET_TIMEOUT:
                break;

            case BL_PACKET_ABORTED:
                result = BL_YMODEM_ABORTED;
                goto exit;

            case BL_PACKET_CRC_ERROR:
                BL_Ymodem_SendByte(BL_YMODEM_NAK);
                break;

            default:
                BL_Ymodem_SendByte(BL_YMODEM_NAK);
                break;
        }
    }

    /*
     * 第二阶段：接收数据包，直到看到 EOT。
     * Flash 偏移只由 received_size 决定，不依赖包号，可避免包号回卷带来的地址错误。
     */
    while (1)
    {
        uint32_t remaining_size;
        uint32_t write_size;

        /* 循环读取数据包，直到接收到 EOT。 */
        packet_status = BL_Ymodem_ReadPacket(&packet, BL_YMODEM_PACKET_TIMEOUT_MS);
        switch (packet_status)
        {
            case BL_PACKET_OK:
                if (packet.sequence == expected_sequence)
                {
                    /*
                     * 对最后一个包，真实写入长度可能小于包长。
                     * 这里严格按文件总长度截断，避免把填充字节也写进有效镜像。
                     */
                    remaining_size = file->file_size - file->received_size;
                    write_size = (packet.payload_size < remaining_size) ? packet.payload_size : remaining_size;

                    if ((write_size == 0U) ||
                        (BL_Flash_Write(flash_address + file->received_size, packet.payload, write_size) != BL_FLASH_OK))
                    {
                        result = BL_YMODEM_FLASH_ERROR;
                        BL_Ymodem_SendCancel();
                        goto exit;
                    }

                    file->received_size += write_size;
                    expected_sequence++;
                    error_count = 0U;
                    BL_Ymodem_SendByte(BL_YMODEM_ACK);
                }
                else if (packet.sequence == (uint8_t)(expected_sequence - 1U))
                {
                    /*
                     * 这是重复包，通常表示上一次 ACK 丢了。
                     * 这里不重复写 Flash，只补一个 ACK 即可。
                     */
                    BL_Ymodem_SendByte(BL_YMODEM_ACK);
                }
                else
                {
                    /* 包号不连续，说明链路中存在丢包或乱序，要求上位机重发。 */
                    error_count++;
                    BL_Ymodem_SendByte(BL_YMODEM_NAK);
                }
                break;

            case BL_PACKET_EOT:
                /*
                 * 标准 YModem 结束握手：
                 * 第一次 EOT 回 NAK，第二次 EOT 回 ACK，再发一个 C 等待结束帧。
                 */
                BL_Ymodem_SendByte(BL_YMODEM_NAK);
                packet_status = BL_Ymodem_ReadPacket(&packet, BL_YMODEM_PACKET_TIMEOUT_MS);
                if (packet_status != BL_PACKET_EOT)
                {
                    result = BL_YMODEM_PROTOCOL_ERROR;
                    BL_Ymodem_SendCancel();
                    goto exit;
                }

                BL_Ymodem_SendByte(BL_YMODEM_ACK);
                BL_Ymodem_SendByte(BL_YMODEM_CRC_REQUEST);

                packet_status = BL_Ymodem_ReadPacket(&packet, BL_YMODEM_PACKET_TIMEOUT_MS);
                if ((packet_status != BL_PACKET_OK) || (packet.sequence != 0U) || (packet.payload[0] != '\0'))
                {
                    result = BL_YMODEM_PROTOCOL_ERROR;
                    BL_Ymodem_SendCancel();
                    goto exit;
                }

                BL_Ymodem_SendByte(BL_YMODEM_ACK);

                if (file->received_size != file->file_size)
                {
                    result = BL_YMODEM_PROTOCOL_ERROR;
                    goto exit;
                }

                result = BL_YMODEM_OK;
                goto exit;

            case BL_PACKET_TIMEOUT:
                /* 包超时后发送 NAK，给上位机一次重传机会。 */
                error_count++;
                BL_Ymodem_SendByte(BL_YMODEM_NAK);
                break;

            case BL_PACKET_ABORTED:
                result = BL_YMODEM_ABORTED;
                goto exit;

            case BL_PACKET_CRC_ERROR:
                error_count++;
                BL_Ymodem_SendByte(BL_YMODEM_NAK);
                break;

            default:
                error_count++;
                BL_Ymodem_SendByte(BL_YMODEM_NAK);
                break;
        }

        if (error_count >= BL_YMODEM_MAX_ERRORS)
        {
            /* 连续错误过多时主动取消会话，避免长时间阻塞。 */
            result = BL_YMODEM_TIMEOUT;
            BL_Ymodem_SendCancel();
            goto exit;
        }
    }

exit:
    /* 无论成功还是失败，离开协议模块前都关闭状态灯。 */
    BL_Board_SetLed(false);
    return result;
}
