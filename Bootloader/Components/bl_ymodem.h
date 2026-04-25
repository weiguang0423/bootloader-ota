#ifndef BL_YMODEM_H
#define BL_YMODEM_H

/* USER CODE BEGIN Header */
/**
    ******************************************************************************
    * @file    bl_ymodem.h
    * @brief   YModem 协议接收模块接口声明。
    ******************************************************************************
    * @attention
    *
    * 当前模块负责最小可用的 YModem 接收流程：
    * - 握手
    * - 起始帧解析
    * - 数据帧接收
    * - EOT 结束流程
    *
    * 它不关心槽位选择策略，只关心“把收到的文件可靠写入指定目标分区”。
    *
    ******************************************************************************
    */
/* USER CODE END Header */

#include <stdint.h>

#include "bl_config.h"

/**
    * @brief YModem 接收结果枚举。
    */
typedef enum
{
        BL_YMODEM_OK = 0,         /* 接收成功 */
        BL_YMODEM_TIMEOUT,        /* 接收超时 */
        BL_YMODEM_ABORTED,        /* 上位机或用户主动取消 */
        BL_YMODEM_PROTOCOL_ERROR, /* 协议流程不符合预期 */
        BL_YMODEM_CRC_ERROR,      /* 包级 CRC 校验失败 */
        BL_YMODEM_FLASH_ERROR,    /* Flash 擦写失败 */
        BL_YMODEM_SIZE_ERROR,     /* 文件大小超出目标分区允许范围 */
} bl_ymodem_status_t;

/**
    * @brief 接收到的文件信息。
    */
typedef struct
{
        char file_name[BL_YMODEM_FILE_NAME_LEN]; /* 文件名 */
        uint32_t file_size;                      /* 文件总大小，单位字节 */
        uint32_t received_size;                  /* 当前已接收字节数 */
} bl_ymodem_file_t;

/**
    * @brief  接收一个 YModem 文件并写入指定目标分区。
    * @param  flash_address  目标写入地址
    * @param  max_size       允许接收的最大大小
    * @param  file           输出文件信息结构体
    * @retval bl_ymodem_status_t YModem 接收结果
    */
bl_ymodem_status_t BL_Ymodem_Receive(uint32_t flash_address, uint32_t max_size, bl_ymodem_file_t *file);

#endif
