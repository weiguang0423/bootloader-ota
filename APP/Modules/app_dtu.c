/* USER CODE BEGIN Header */
/**
    ******************************************************************************
    * @file    app_dtu.c
    * @brief   DTU（4G 数传单元）驱动实现。
    ******************************************************************************
    * @attention
    *
    * 实现环形缓冲区中断接收、AT 指令收发、透传模式切换、
    * TCP 连接建立与状态检测。
    *
    ******************************************************************************
    */
/* USER CODE END Header */
#include "app_dtu.h"

#include <stdio.h>
#include <string.h>

#include "bsp_board.h"

#define BL_DTU_SR_ERROR_MASK   (USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE)
#define BL_OTA_RINGBUF_MASK    (BL_OTA_RINGBUF_SIZE - 1U)

static uint8_t  s_ring_buf[BL_OTA_RINGBUF_SIZE];
static volatile uint16_t s_ring_head;
static volatile uint16_t s_ring_tail;

static UART_HandleTypeDef s_dtu_uart;

/**
    * @brief  复位环形缓冲区（头尾指针归零）。
    * @retval None
    */
static void RingBuf_Reset(void)
{
    s_ring_head = 0U;
    s_ring_tail = 0U;
}

/**
    * @brief  获取环形缓冲区当前数据量。
    * @retval uint16_t 缓冲区中有效字节数
    */
static uint16_t RingBuf_Count(void)
{
    uint16_t h = s_ring_head;
    uint16_t t = s_ring_tail;
    return (uint16_t)((h >= t) ? (h - t) : (BL_OTA_RINGBUF_SIZE - t + h));
}

/**
    * @brief  向环形缓冲区写入一个字节。
    * @param  byte  要写入的字节
    * @retval bool true 表示写入成功，false 表示缓冲区已满
    */
static bool RingBuf_Put(uint8_t byte)
{
    uint16_t next = (uint16_t)((s_ring_head + 1U) & BL_OTA_RINGBUF_MASK);
    if (next == s_ring_tail)
    {
        return false;
    }
    s_ring_buf[s_ring_head] = byte;
    s_ring_head = next;
    return true;
}

/**
    * @brief  从环形缓冲区读取一个字节。
    * @param  byte  输出字节指针
    * @retval bool true 表示读取成功，false 表示缓冲区为空
    */
static bool RingBuf_Get(uint8_t *byte)
{
    if (s_ring_head == s_ring_tail)
    {
        return false;
    }
    *byte = s_ring_buf[s_ring_tail];
    s_ring_tail = (uint16_t)((s_ring_tail + 1U) & BL_OTA_RINGBUF_MASK);
    return true;
}

/**
    * @brief  初始化 DTU 串口及环形缓冲区，使能接收中断。
    * @retval None
    */
void BL_DTU_Init(void)
{
    RingBuf_Reset();

    s_dtu_uart.Instance          = BL_DTU_UART_INSTANCE;
    s_dtu_uart.Init.BaudRate     = BL_DTU_UART_BAUDRATE;
    s_dtu_uart.Init.WordLength   = UART_WORDLENGTH_8B;
    s_dtu_uart.Init.StopBits     = UART_STOPBITS_1;
    s_dtu_uart.Init.Parity       = UART_PARITY_NONE;
    s_dtu_uart.Init.Mode         = UART_MODE_TX_RX;
    s_dtu_uart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    s_dtu_uart.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&s_dtu_uart) != HAL_OK)
    {
        return;
    }

    HAL_NVIC_SetPriority(BL_DTU_UART_IRQn, BL_DTU_UART_IRQ_PRIORITY, 0U);   //设置 USART3 中断的抢占优先级（由 BL_DTU_UART_IRQ_PRIORITY 配置）和子优先级（0）
    HAL_NVIC_EnableIRQ(BL_DTU_UART_IRQn);                                   //使能 USART3 中断（NVIC 层面），CPU 才能响应这个外设的中断请求

    __HAL_UART_CLEAR_OREFLAG(&s_dtu_uart);            //清除溢出错误标志（OverRun Error）——防止之前残留的溢出标志导致无法正确接收新数据
    __HAL_UART_ENABLE_IT(&s_dtu_uart, UART_IT_RXNE);  //使能 RXNE 中断（Receive Not Empty）——收到一个字节就触发中断，实现逐字节接收
    __HAL_UART_ENABLE_IT(&s_dtu_uart, UART_IT_ERR);   //使能 错误中断——帧错误、噪声、溢出等错误发生时触发中断
}

/**
    * @brief  DTU 串口中断处理函数，将接收数据放入环形缓冲区。
    * @retval None
    */
void BL_DTU_IRQHandler(void)
{
    USART_TypeDef *instance = s_dtu_uart.Instance;        // 拿到 USART3 的寄存器基地址
    uint32_t sr = instance->SR;                           // 读取状态寄存器 SR

    if ((sr & USART_SR_RXNE) != 0U)                       // RXNE = Receive Not Empty
    {
        uint8_t byte = (uint8_t)(instance->DR & 0x00FFU); // 从数据寄存器 DR 读取收到的字节
        (void)RingBuf_Put(byte);
        return;
    }

    if ((sr & BL_DTU_SR_ERROR_MASK) != 0U)
    {
        volatile uint32_t dummy = instance->DR;           // 读 DR 清掉错误标志（手册要求）
        (void)dummy;                                      // 防止编译器警告未使用
    }
}

/**
    * @brief  获取 DTU 环形缓冲区中可读取的字节数。
    * @retval uint16_t 可读取的字节数
    */
uint16_t BL_DTU_Available(void)
{
    return RingBuf_Count();
}

/**
    * @brief  从 DTU 环形缓冲区读取指定长度的数据。
    * @param  buf  接收缓存指针
    * @param  len  期望读取的字节数
    * @retval uint16_t 实际读取的字节数
    */
uint16_t BL_DTU_Read(uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0U; i < len; i++)
    {
        if (!RingBuf_Get(&buf[i]))
        {
            break;
        }
    }
    return i;
}

/**
    * @brief  从 DTU 读取一行数据（以 \n 结尾）。
    * @param  buf         接收缓存指针
    * @param  max_len     缓存最大长度
    * @param  timeout_ms  超时时间，单位毫秒
    * @retval uint16_t 实际读取的字节数（不含 '\0'）
    */
uint16_t BL_DTU_ReadLine(char *buf, uint16_t max_len, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint16_t pos = 0U;
    uint8_t ch;

    while (1)
    {
        if ((HAL_GetTick() - start) >= timeout_ms)
        {
            break;
        }
        if (RingBuf_Get(&ch))
        {
            if (pos < (max_len - 1U))
            {
                buf[pos++] = (char)ch;
            }
            if (ch == '\n')             //以 \n 结尾
            {
                break;
            }
        }
    }
    buf[pos] = '\0';
    return pos;
}

/**
    * @brief  向 DTU 串口发送指定长度的数据。
    * @param  data        发送数据指针
    * @param  len         发送字节数
    * @param  timeout_ms  超时时间，单位毫秒
    * @retval HAL_StatusTypeDef HAL 层返回状态
    */
HAL_StatusTypeDef BL_DTU_Write(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    return HAL_UART_Transmit(&s_dtu_uart, (uint8_t *)data, len, timeout_ms);
}

/**
    * @brief  向 DTU 发送字符串。
    * @param  str  以 '\0' 结尾的字符串
    * @retval HAL_StatusTypeDef HAL 层返回状态
    */
HAL_StatusTypeDef BL_DTU_SendString(const char *str)
{
    uint16_t len = (uint16_t)strlen(str);
    return BL_DTU_Write((const uint8_t *)str, len, BL_OTA_AT_TIMEOUT_MS);
}

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
                    char *resp_buf, uint16_t resp_max, uint32_t timeout_ms)
{
    char line[256];
    uint32_t start;

    if ((resp_buf != NULL) && (resp_max > 0U))
    {
        resp_buf[0] = '\0';
    }

    BL_DTU_FlushRx();
    BL_DTU_SendString(cmd);

    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (BL_DTU_ReadLine(line, sizeof(line), 200U) > 0U)
        {
            if ((resp_buf != NULL) && (resp_max > 0U))
            {
                uint16_t copy_len = (uint16_t)strlen(line);
                if (copy_len >= resp_max) copy_len = resp_max - 1U;
                memcpy(resp_buf, line, copy_len);
                resp_buf[copy_len] = '\0';
            }
            if ((expect == NULL) || (strstr(line, expect) != NULL))
            {
                return true;
            }
        }
    }
    return false;
}

/**
    * @brief  退出 DTU 透传模式（发送 +++ 并等待 AT 响应）。
    * @retval bool true 表示退出成功
    */
bool BL_DTU_ExitTransparent(void)
{
    HAL_Delay(1100U);
    BL_DTU_FlushRx();
    BL_DTU_SendString("+++");
    HAL_Delay(1100U);

    return BL_DTU_SendAT("AT\r\n", "OK", NULL, 0U, BL_OTA_AT_TIMEOUT_MS);
}

/**
    * @brief  进入 DTU 透传模式（发送 ATO）。
    * @retval bool true 表示进入成功
    */
bool BL_DTU_EnterTransparent(void)
{
    return BL_DTU_SendAT("ATO\r\n", "OK", NULL, 0U, BL_OTA_AT_TIMEOUT_MS);
}

/**
    * @brief  清空 DTU 环形缓冲区。
    * @retval None
    */
void BL_DTU_FlushRx(void)
{
    RingBuf_Reset();
}

/* ---- DTU TCP 传输配置（OTA 专用） ---- */

/**
    * @brief  发送 AT 指令（不检查响应内容，仅确保 AT 通道正常）。
    * @param  cmd        AT 指令字符串
    * @param  expect     期望响应关键字
    * @param  timeout_ms 超时时间，单位毫秒
    * @retval None
    */
static void DTU_SendATOptional(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    (void)BL_DTU_SendAT(cmd, expect, NULL, 0U, timeout_ms);
}

/**
    * @brief  查询 DTU 的 ASKCONNECT 状态。
    * @param  connected_out  输出当前连接状态
    * @retval bool true 表示查询成功，false 表示查询失败
    */
static bool DTU_ReadAskConnectState(bool *connected_out)
{
    char resp[128];
    const char *p;
    uint32_t value;

    if (connected_out == NULL)
    {
        return false;
    }

    *connected_out = false;
    if (!BL_DTU_SendAT("AT+ASKCONNECT?\r\n", "+ASKCONNECT:",
                       resp, sizeof(resp), BL_OTA_AT_TIMEOUT_MS))
    {
        return false;
    }

    p = strchr(resp, ':');
    if (p == NULL)
    {
        return false;
    }
    p++;

    while ((*p == ' ') || (*p == '\t'))
    {
        p++;
    }

    value = 0U;
    while ((*p >= '0') && (*p <= '9'))
    {
        value = (value * 10U) + (uint32_t)(*p - '0');
        p++;
    }

    *connected_out = (value != 0U);
    return true;
}

/**
    * @brief  等待 DTU TCP 连接建立。
    * @param  timeout_ms  超时时间，单位毫秒
    * @retval bool true 表示 TCP 已连接，false 表示超时
    */
static bool DTU_WaitTcpConnected(uint32_t timeout_ms)
{
    uint32_t start;
    bool connected;
    bool got_state;

    start = HAL_GetTick();
    got_state = false;
    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (DTU_ReadAskConnectState(&connected))
        {
            got_state = true;
            if (connected)
            {
                return true;
            }
        }

        HAL_Delay(500U);
    }

    return !got_state;
}

/**
    * @brief  配置 DTU TCP 传输通道并等待连接建立。
    * @param  host  目标服务器域名或 IP
    * @param  port  目标端口
    * @retval bool true 表示配置成功且 TCP 已连接
    */
bool BL_DTU_ConfigTcpTransport(const char *host, uint16_t port)
{
    char cmd[256];
    int len;

    if ((host == NULL) || (*host == '\0') || (port == 0U))
    {
        return false;
    }

    /* 关闭状态上报，避免 +STATUS 在透传下载过程中混入固件字节流。 */
    DTU_SendATOptional("AT+AUTOSTATUS=0,0\r\n", "OK", BL_OTA_AT_TIMEOUT_MS);
    DTU_SendATOptional("AT+AUTOATO=0\r\n", "OK", BL_OTA_AT_TIMEOUT_MS);
    DTU_SendATOptional("AT+POLL=0,10,1\r\n", "OK", BL_OTA_AT_TIMEOUT_MS);
    DTU_SendATOptional("AT+SENDID=0\r\n", "OK", BL_OTA_AT_TIMEOUT_MS);
    DTU_SendATOptional("AT+CACHE=0\r\n", "OK", BL_OTA_AT_TIMEOUT_MS);

    len = snprintf(cmd, sizeof(cmd), "AT+TCPHEX=0,%u\r\n", (unsigned int)BL_OTA_HTTP_CHANNEL);
    if ((len > 0) && (len < (int)sizeof(cmd)))
    {
        DTU_SendATOptional(cmd, "OK", BL_OTA_AT_TIMEOUT_MS);
    }

    /* 配置目标服务器 */
    len = snprintf(cmd, sizeof(cmd), "AT+DSCADDR=%u,\"TCP\",\"%s\",%u\r\n",
                   (unsigned int)BL_OTA_HTTP_CHANNEL,
                   host,
                   (unsigned int)port);
    if ((len <= 0) || (len >= (int)sizeof(cmd)))
    {
        return false;
    }
    if (!BL_DTU_SendAT(cmd, "OK", NULL, 0U, BL_OTA_AT_TIMEOUT_MS))
    {
        return false;
    }

    /* 1. 保存当前配置 */
    if (!BL_DTU_SendAT("AT&W\r\n", "OK", NULL, 0U, BL_OTA_AT_TIMEOUT_MS))
    {
        return false;
    }

    /* 2. 设置透传模式 */
    len = snprintf(cmd, sizeof(cmd), "AT+DTUMODE=1,%u\r\n", (unsigned int)BL_OTA_HTTP_CHANNEL);
    if ((len <= 0) || (len >= (int)sizeof(cmd)))
    {
        return false;
    }
    if (!BL_DTU_SendAT(cmd, "OK", NULL, 0U, BL_OTA_AT_TIMEOUT_MS))
    {
        return false;
    }

    /* 3. 退出透传 */
    (void)BL_DTU_ExitTransparent();

    /* 4. 重启 DTU */
    BL_DTU_FlushRx();
    (void)BL_DTU_SendAT("AT+CFUN=1,1\r\n", "OK", NULL, 0U, BL_OTA_AT_TIMEOUT_MS);

    HAL_Delay(3500U);
    {
        uint32_t boot_start = HAL_GetTick();
        while ((HAL_GetTick() - boot_start) < BL_OTA_TCP_CONNECT_TIMEOUT_MS)
        {
            if (BL_DTU_ExitTransparent())
            {
                break;
            }
            HAL_Delay(500U);
        }
        if ((HAL_GetTick() - boot_start) >= BL_OTA_TCP_CONNECT_TIMEOUT_MS)
        {
            return false;
        }
    }

    if (!DTU_WaitTcpConnected(BL_OTA_TCP_CONNECT_TIMEOUT_MS))
    {
        return false;
    }

    return true;
}
