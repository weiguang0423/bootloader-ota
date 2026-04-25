#ifndef BL_DTU_H
#define BL_DTU_H

#include <stdint.h>
#include <stdbool.h>
#include "ota_config.h"

/**
    * @brief  初始化 DTU 串口及环形缓冲区。
    * @retval None
    */
void BL_DTU_Init(void);

/**
    * @brief  DTU 串口中断处理函数。
    * @retval None
    */
void BL_DTU_IRQHandler(void);

/**
    * @brief  获取环形缓冲区中可读取的字节数。
    * @retval uint16_t 可读取的字节数
    */
uint16_t BL_DTU_Available(void);

/**
    * @brief  从 DTU 环形缓冲区读取指定长度的数据。
    * @param  buf  接收缓存指针
    * @param  len  期望读取的字节数
    * @retval uint16_t 实际读取的字节数
    */
uint16_t BL_DTU_Read(uint8_t *buf, uint16_t len);

/**
    * @brief  从 DTU 读取一行数据（以 \n 结尾）。
    * @param  buf         接收缓存指针
    * @param  max_len     缓存最大长度
    * @param  timeout_ms  超时时间，单位毫秒
    * @retval uint16_t 实际读取的字节数（不含 '\0'）
    */
uint16_t BL_DTU_ReadLine(char *buf, uint16_t max_len, uint32_t timeout_ms);

/**
    * @brief  向 DTU 串口发送指定长度的数据。
    * @param  data        发送数据指针
    * @param  len         发送字节数
    * @param  timeout_ms  超时时间，单位毫秒
    * @retval HAL_StatusTypeDef HAL 层返回状态
    */
HAL_StatusTypeDef BL_DTU_Write(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

/**
    * @brief  向 DTU 发送字符串。
    * @param  str  以 '\0' 结尾的字符串
    * @retval HAL_StatusTypeDef HAL 层返回状态
    */
HAL_StatusTypeDef BL_DTU_SendString(const char *str);

/**
    * @brief  发送 AT 指令并等待期望响应。
    * @param  cmd        AT 指令字符串
    * @param  expect     期望响应关键字（NULL 表示不检查）
    * @param  resp_buf   响应缓存（NULL 表示丢弃）
    * @param  resp_max   响应缓存大小
    * @param  timeout_ms 超时时间，单位毫秒
    * @retval bool true 表示收到期望响应，false 表示超时或无响应
    */
bool BL_DTU_SendAT(const char *cmd, const char *expect,
                    char *resp_buf, uint16_t resp_max, uint32_t timeout_ms);

/**
    * @brief  退出 DTU 透传模式。
    * @retval bool true 表示退出成功
    */
bool BL_DTU_ExitTransparent(void);

/**
    * @brief  进入 DTU 透传模式。
    * @retval bool true 表示进入成功
    */
bool BL_DTU_EnterTransparent(void);

/**
    * @brief  清空 DTU 环形缓冲区。
    * @retval None
    */
void BL_DTU_FlushRx(void);

/**
    * @brief  配置 DTU TCP 传输通道并等待连接建立。
    * @param  host  目标服务器域名或 IP
    * @param  port  目标端口
    * @retval bool true 表示配置成功且 TCP 已连接
    */
bool BL_DTU_ConfigTcpTransport(const char *host, uint16_t port);

#endif
