#include "ota_core.h"

/**
 ******************************************************************************
 * @file    bl_ota.c
 * @brief   远程 OTA：接收 URL → TCP HTTP GET → 写 Flash → 切槽位 → 复位。
 ******************************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_board.h"
#include "app_dtu.h"
#include "bsp_flash.h"

static bl_ota_state_t s_state = BL_OTA_IDLE;
static char s_pending_url[BL_OTA_URL_BUF_SIZE];
static char s_url_collect_buf[BL_OTA_URL_BUF_SIZE];
static uint16_t s_url_collect_len;
static uint32_t s_url_last_tick;

static const char HTTP_SCHEME[]        = "http://";
static const char HDR_CONTENT_LENGTH[] = "content-length:";
static const char HTTP_STATUS_200[]    = " 200 ";

#define HTTP_DEFAULT_PORT        80U

/* ---- STM32F4 硬件 CRC（HAL 库方式） ---- */

static CRC_HandleTypeDef s_hcrc;

/**
    * @brief  HAL CRC 底层 MSP 初始化（使能 CRC 时钟）。
    * @param  hcrc  CRC 句柄
    * @retval None
    */
void HAL_CRC_MspInit(CRC_HandleTypeDef *hcrc)
{
    (void)hcrc;
    __HAL_RCC_CRC_CLK_ENABLE();
}

/**
    * @brief  初始化硬件 CRC 外设。
    * @retval bool true 表示初始化成功
    */
static bool OTA_HwCrcInit(void)
{
    s_hcrc.Instance = CRC;
    return HAL_CRC_Init(&s_hcrc) == HAL_OK;
}

/**
    * @brief  使用硬件 CRC 计算 Flash 指定区域校验值。
    * @param  flash_addr  Flash 起始地址
    * @param  len         数据长度（字节）
    * @retval uint32_t CRC32 校验值
    */
static uint32_t OTA_HwCrcCalculate(uint32_t flash_addr, uint32_t len)
{
    uint32_t words = len / 4U;
    uint32_t tail  = len % 4U;
    uint32_t crc;

    if (words > 0U)
    {
        crc = HAL_CRC_Calculate(&s_hcrc, (uint32_t *)flash_addr, words);
    }
    else
    {
        __HAL_CRC_DR_RESET(&s_hcrc);
        crc = CRC->DR;
    }

    if (tail > 0U)
    {
        uint32_t last_word = 0U;
        const uint8_t *p = (const uint8_t *)(flash_addr + words * 4U);
        uint32_t i;

        for (i = 0U; i < tail; i++)
        {
            last_word |= (uint32_t)p[i] << (i * 8U);
        }
        crc = HAL_CRC_Accumulate(&s_hcrc, &last_word, 1U);
    }

    return crc;
}

/**
    * @brief  验证固件的硬件 CRC（最后 4 字节为附加 CRC）。
    * @param  flash_addr 固件 Flash 起始地址
    * @param  total_size 固件总大小（含 CRC 尾）
    * @retval bool true 表示 CRC 校验通过
    */
static bool OTA_VerifyFirmwareHwCrc(uint32_t flash_addr, uint32_t total_size)
{
    uint32_t firmware_size;
    uint32_t expected_crc;
    uint32_t actual_crc;

    if (total_size < 4U)
    {
        BL_Board_Printf("OTA: total_size too small (%lu)\r\n", total_size);
        return false;
    }

    /* 最后 4 字节为小端序 CRC，由 append_crc32.py 附加 */
    expected_crc = *(__IO uint32_t *)(flash_addr + total_size - 4U);
    firmware_size = total_size - 4U;

    if (!OTA_HwCrcInit())
    {
        BL_Board_Printf("OTA: CRC init failed\r\n");
        return false;
    }

    actual_crc = OTA_HwCrcCalculate(flash_addr, firmware_size);

    bool ok = (actual_crc == expected_crc);
    BL_Board_Printf("OTA: CRC %s, expected=0x%08X, actual=0x%08X\r\n",
                    ok ? "OK" : "FAIL",
                    (unsigned int)expected_crc, (unsigned int)actual_crc);
    return ok;
}

/**
    * @brief  判断字符是否为空白字符。
    * @param  ch  待判断字符
    * @retval bool true 表示是空白字符
    */
static inline bool OTA_IsSpace(char ch)
{
    return (ch == ' ') || (ch == '\r') || (ch == '\n') || (ch == '\t');
}

/**
    * @brief  在文本中不区分大小写查找子串。
    * @param  text     被搜索文本
    * @param  pattern  要查找的模式串
    * @retval const char* 匹配位置指针，未找到返回 NULL
    */
static const char *OTA_FindIgnoreCase(const char *text, const char *pattern)
{
    const char *t;

    if ((text == NULL) || (pattern == NULL) || (*pattern == '\0'))
    {
        return NULL;
    }

    for (t = text; *t != '\0'; t++)
    {
        const char *a = t;
        const char *b = pattern;

        while ((*a != '\0') && (*b != '\0'))
        {
            char ca = *a;
            char cb = *b;
            if ((ca >= 'a') && (ca <= 'z')) ca -= ('a' - 'A');
            if ((cb >= 'a') && (cb <= 'z')) cb -= ('a' - 'A');
            if (ca != cb) break;
            a++;
            b++;
        }

        if (*b == '\0')
        {
            return t;
        }
    }

    return NULL;
}

/**
    * @brief  复位 URL 收集器状态。
    * @retval None
    */
static void OTA_ResetUrlCollector(void)
{
    s_url_collect_len = 0U;
    s_url_collect_buf[0] = '\0';
}

/**
    * @brief  提交收集到的 URL 字符串（去除首尾空白并提取 http://）。
    * @param  url      输出 URL 缓存
    * @param  max_len  输出缓存最大长度
    * @retval bool true 表示成功提取有效 URL
    */
static bool OTA_CommitCollectedUrl(char *url, uint16_t max_len)
{
    const char *start;
    const char *end;
    const char *http_pos;
    const char *p;
    uint16_t len;

    if ((url == NULL) || (max_len < 2U) || (s_url_collect_len == 0U))
    {
        OTA_ResetUrlCollector();
        return false;
    }

    /* 去除首尾空白 */
    start = s_url_collect_buf;
    while (OTA_IsSpace(*start)) start++;

    end = s_url_collect_buf + s_url_collect_len;
    while ((end > start) && OTA_IsSpace(end[-1])) end--;

    if (end <= start)
    {
        OTA_ResetUrlCollector();
        return false;
    }

    /* 仅提取真正的 http:// URL，忽略 +STATUS 等状态行噪声。 */
    http_pos = OTA_FindIgnoreCase(start, HTTP_SCHEME);
    if ((http_pos == NULL) || (http_pos >= end))
    {
        OTA_ResetUrlCollector();
        return false;
    }

    /* 扫描 URL 结束位置（遇到空白或引号类字符截断） */
    for (p = http_pos; p < end; p++)
    {
        char ch = *p;
        if (OTA_IsSpace(ch) || (ch == '"') || (ch == '\'') || (ch == '}') || (ch == ','))
            break;
    }

    len = (uint16_t)(p - http_pos);
    if ((len == 0U) || (len >= max_len))
    {
        OTA_ResetUrlCollector();
        return false;
    }

    memcpy(url, http_pos, len);
    url[len] = '\0';
    OTA_ResetUrlCollector();
    return true;
}

/**
    * @brief  从 DTU 读取一条 URL 命令。
    * @param  url      输出 URL 缓存
    * @param  max_len  输出缓存最大长度
    * @retval bool true 表示读取到有效 URL
    */
static bool OTA_ReadUrlCommand(char *url, uint16_t max_len)
{
    uint8_t byte;

    while (BL_DTU_Available() > 0U)
    {
        if (BL_DTU_Read(&byte, 1U) != 1U)
        {
            break;
        }

        s_url_last_tick = HAL_GetTick();

        if ((byte == '\r') || (byte == '\n'))
        {
            if (s_url_collect_len > 0U)
            {
                return OTA_CommitCollectedUrl(url, max_len);
            }
            continue;
        }

        if ((s_url_collect_len == 0U) && OTA_IsSpace((char)byte)) continue;

        if (s_url_collect_len < (BL_OTA_URL_BUF_SIZE - 1U))
        {
            s_url_collect_buf[s_url_collect_len++] = (char)byte;
            s_url_collect_buf[s_url_collect_len] = '\0';
        }
        else
        {
            OTA_ResetUrlCollector();
        }
    }

    if (s_url_collect_len > 0U)
    {
        if ((HAL_GetTick() - s_url_last_tick) >= BL_OTA_URL_IDLE_MS)
        {
            return OTA_CommitCollectedUrl(url, max_len);
        }
    }

    return false;
}

/**
    * @brief  解析 http:// URL 为主机、端口、路径。
    * @param  input     输入 URL 字符串
    * @param  host      输出主机名缓存
    * @param  host_max  主机名缓存最大长度
    * @param  port      输出端口号
    * @param  path      输出路径缓存
    * @param  path_max  路径缓存最大长度
    * @retval bool true 表示解析成功
    */
static bool OTA_ParseUrl(const char *input,
                         char *host,
                         uint16_t host_max,
                         uint16_t *port,
                         char *path,
                         uint16_t path_max)
{
    const char *p;
    const char *host_begin;
    const char *host_end;
    const char *path_begin;
    const char *colon;
    uint16_t host_len;
    uint16_t path_len;
    uint16_t name_len;
    uint32_t port_val;

    p = OTA_FindIgnoreCase(input, HTTP_SCHEME);
    if (p == NULL) return false;

    p += sizeof(HTTP_SCHEME) - 1U;
    host_begin = p;
    path_begin = strchr(p, '/');
    host_end = (path_begin != NULL) ? path_begin : (p + strlen(p));

    host_len = (uint16_t)(host_end - host_begin);
    if ((host_len == 0U) || (host_len >= host_max))
        return false;

    /* 反向查找最后一个 ':' 作为端口分隔符（兼顾 IPv6 [::1]:8080） */
    colon = NULL;
    for (p = host_end; p > host_begin; )
    {
        p--;
        if (*p == ':') { colon = p; break; }
    }

    port_val = HTTP_DEFAULT_PORT;
    name_len = host_len;

    if (colon != NULL)
    {
        port_val = (uint32_t)atoi(colon + 1);
        if ((port_val == 0U) || (port_val > 65535U))
            return false;
        name_len = (uint16_t)(colon - host_begin);
    }

    memcpy(host, host_begin, name_len);
    host[name_len] = '\0';

    if (path_begin != NULL)
    {
        path_len = (uint16_t)strlen(path_begin);
        if ((path_len == 0U) || (path_len >= path_max))
            return false;
        memcpy(path, path_begin, path_len + 1U);
    }
    else
    {
        path[0] = '/';
        path[1] = '\0';
    }

    *port = (uint16_t)port_val;
    return true;
}

/**
    * @brief  发送 AT 指令（不检查响应，仅发送）。
    * @param  cmd        AT 指令字符串
    * @param  expect     期望响应关键字
    * @param  timeout_ms 超时时间
    * @retval None
    */
static void OTA_SendATOptional(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    (void)BL_DTU_SendAT(cmd, expect, NULL, 0U, timeout_ms);
}

/* ---- HTTP header reader ---- */

/**
    * @brief  从 DTU 读取 HTTP 响应头，提取 Content-Length。
    * @param  content_length_out  输出 Content-Length 值
    * @retval bool true 表示成功读取且包含 200 状态码
    */
static bool OTA_ReadHttpHeaders(uint32_t *content_length_out)
{
    static char hdr_buf[BL_OTA_HTTP_HEADER_BUF_SIZE];
    uint16_t filled;
    uint32_t start;
    uint8_t b;
    const char *cl_pos;
    const char *p;

    if (content_length_out == NULL)
    {
        return false;
    }

    filled = 0U;
    start  = HAL_GetTick();

    while (1)
    {
        if (BL_DTU_Read(&b, 1U) == 1U)
        {
            if (filled >= BL_OTA_HTTP_HEADER_BUF_SIZE - 1U)
            {
                return false;
            }
            hdr_buf[filled++] = (char)b;
            if ((filled >= 4U) &&
                (hdr_buf[filled - 4U] == '\r') &&
                (hdr_buf[filled - 3U] == '\n') &&
                (hdr_buf[filled - 2U] == '\r') &&
                (hdr_buf[filled - 1U] == '\n'))
            {
                if (filled == 4U)
                {
                    continue; /* 跳过前导空行 */
                }
                break;
            }
        }
        if ((HAL_GetTick() - start) > BL_OTA_HTTP_HEADER_TIMEOUT_MS)
        {
            return false;
        }
    }

    hdr_buf[filled] = '\0';

    /* 检查 HTTP 200 */
    if (OTA_FindIgnoreCase(hdr_buf, HTTP_STATUS_200) == NULL)
    {
        return false;
    }

    /* 解析 Content-Length */
    cl_pos = OTA_FindIgnoreCase(hdr_buf, HDR_CONTENT_LENGTH);
    if (cl_pos == NULL)
    {
        return false;
    }

    p = cl_pos + (sizeof(HDR_CONTENT_LENGTH) - 1U);
    while (*p == ' ') p++;
    if ((*p < '0') || (*p > '9')) return false;

    *content_length_out = 0U;
    while ((*p >= '0') && (*p <= '9'))
    {
        *content_length_out = (*content_length_out * 10U) + (uint32_t)(*p - '0');
        p++;
    }
    return true;
}

/* ---- Main download orchestrator ---- */

/**
    * @brief  执行完整 OTA 下载流程：解析 URL、TCP 连接、HTTP GET、写 Flash、校验、切槽位。
    * @param  url  固件下载 URL
    * @retval bool true 表示升级成功（成功后会自动复位）
    */
static bool OTA_DownloadFirmware(const char *url)
{
    char host[BL_OTA_HTTP_HOST_BUF_SIZE];
    char path[BL_OTA_HTTP_PATH_BUF_SIZE];
    uint16_t port;
    uint32_t flash_addr;
    uint32_t flash_max_size;
    uint32_t total_size = 0U;
    bool tcp_configured = false;
    bool in_transparent = false;
    bool ok = false;
    char cmd[64];
    int cmd_len;

    if (url == NULL)
    {
        return false;
    }

    port = 80U;

    if (!OTA_ParseUrl(url, host, sizeof(host), &port, path, sizeof(path)))
    {
        return false;
    }

    if (!BL_FlashApp_GetUpgradePartition(&flash_addr, &flash_max_size))
    {
        return false;
    }

    /* 退出当前透传 */
    (void)BL_DTU_ExitTransparent();

    /* 配置 TCP 传输 */
    if (!BL_DTU_ConfigTcpTransport(host, port))
    {
        return false;
    }
    tcp_configured = true;

    /* ===== 获取固件总大小 ===== */
    {
        char http_req[256];

        BL_DTU_FlushRx();
        HAL_Delay(200U);

        if (!BL_DTU_EnterTransparent())
        {
            goto cleanup;
        }
        in_transparent = true;

        /* 发送 HEAD 请求获取文件大小 */
        if (snprintf(http_req, sizeof(http_req),
                     "HEAD %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                     path, host) >= (int)sizeof(http_req))
        {
            goto cleanup;
        }
        if (BL_DTU_SendString(http_req) != HAL_OK)
        {
            goto cleanup;
        }

        if (!OTA_ReadHttpHeaders(&total_size) ||
            (total_size == 0U) || (total_size > flash_max_size))
        {
            goto cleanup;
        }

        /* 退出透传，关闭 HEAD 连接，准备擦除和下载 */
        (void)BL_DTU_ExitTransparent();
        in_transparent = false;
        HAL_Delay(500U);
    }

    if (!BL_FlashApp_BeginSession() || !BL_FlashApp_Erase(flash_addr, total_size))
    {
        goto cleanup;
    }

    /* ===== 一次性下载固件 ===== */
    {
        char http_req[BL_OTA_HTTP_HOST_BUF_SIZE + BL_OTA_HTTP_PATH_BUF_SIZE + 128U];
        uint8_t buf[BL_OTA_HTTP_BODY_CHUNK_SIZE];
        uint32_t received = 0U;
        uint32_t last_rx_tick;
        int req_len;

        BL_DTU_FlushRx();
        HAL_Delay(100U);

        if (!BL_DTU_EnterTransparent())
        {
            goto cleanup;
        }
        in_transparent = true;

        /* 发送 GET 请求（不带 Range，一次性下载完整文件） */
        req_len = snprintf(http_req, sizeof(http_req),
                           "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                           path, host);
        if ((req_len <= 0) || (req_len >= (int)sizeof(http_req)))
        {
            goto cleanup;
        }

        if (BL_DTU_SendString(http_req) != HAL_OK)
        {
            goto cleanup;
        }

        /* 读取响应头（Content-Length 应与 HEAD 结果一致） */
        {
            uint32_t resp_len = 0U;
            if (!OTA_ReadHttpHeaders(&resp_len))
            {
                goto cleanup;
            }
        }

        /* 持续读取 body 并写入 Flash */
        last_rx_tick = HAL_GetTick();
        while (received < total_size)
        {
            uint32_t remaining = total_size - received;
            uint16_t want = (remaining > BL_OTA_HTTP_BODY_CHUNK_SIZE)
                                ? BL_OTA_HTTP_BODY_CHUNK_SIZE
                                : (uint16_t)remaining;
            uint16_t got;

            got = BL_DTU_Read(buf, want);
            if (got > 0U)
            {
                if (!BL_FlashApp_Write(flash_addr + received, buf, got))
                {
                    goto cleanup;
                }
                received += got;
                last_rx_tick = HAL_GetTick();
            }
            else
            {
                if ((HAL_GetTick() - last_rx_tick) > BL_OTA_RX_STALL_TIMEOUT_MS)
                {
                    goto cleanup;
                }
            }
        }

        if (received != total_size)
        {
            goto cleanup;
        }
    }

    /* ===== 验证和切换 ===== */
    if (!BL_FlashApp_IsAppValid(flash_addr))
    {
        goto cleanup;
    }

    if (!OTA_VerifyFirmwareHwCrc(flash_addr, total_size))
    {
        goto cleanup;
    }

    if (!BL_FlashApp_SetActiveSlot(flash_addr))
    {
        goto cleanup;
    }

    ok = true;

cleanup:
    if (ok)
    {
        /* 成功路径：立即复位，由 Bootloader 接管并启动新固件。
         * 不需要恢复 DTU 状态，因为系统即将重启。 */
        (void)BL_FlashApp_EndSession();
        HAL_Delay(500U);
        NVIC_SystemReset();
    }
    else
    {
        BL_Board_Printf("OTA: download or verify failed\r\n");

        if (in_transparent)
        {
            (void)BL_DTU_ExitTransparent();
        }

        if (tcp_configured)
        {
            cmd_len = snprintf(cmd, sizeof(cmd), "AT+DTUMODE=0,%u\r\n",
                               (unsigned int)BL_OTA_HTTP_CHANNEL);
            if ((cmd_len > 0) && (cmd_len < (int)sizeof(cmd)))
            {
                OTA_SendATOptional(cmd, "OK", BL_OTA_AT_TIMEOUT_MS);
            }

            OTA_SendATOptional("AT+AUTOSTATUS=0,0\r\n", "OK", BL_OTA_AT_TIMEOUT_MS);
            OTA_SendATOptional("AT&W\r\n", "OK", BL_OTA_AT_TIMEOUT_MS);
        }

        (void)BL_FlashApp_EndSession();
        BL_DTU_FlushRx();
        (void)BL_DTU_EnterTransparent();
    }

    return ok;
}

/**
    * @brief  初始化 OTA 模块，复位状态机并清空缓冲区。
    * @retval None
    */
void BL_OTA_Init(void)
{
    s_state = BL_OTA_IDLE;
    s_pending_url[0] = '\0';
    OTA_ResetUrlCollector();

    BL_DTU_FlushRx();
}

/**
    * @brief  OTA 主处理函数，在主循环中周期调用以驱动状态机。
    * @retval None
    */
void BL_OTA_Process(void)
{
    if (s_state == BL_OTA_IDLE)
    {
        if (OTA_ReadUrlCommand(s_pending_url, sizeof(s_pending_url)))
        {
            s_state = BL_OTA_DOWNLOADING;
        }
    }
    else if (s_state == BL_OTA_DOWNLOADING)
    {
        if (!OTA_DownloadFirmware(s_pending_url))
        {
            BL_Board_Printf("OTA: firmware download failed, abort\r\n");
        }
        s_pending_url[0] = '\0';
        s_state = BL_OTA_IDLE;
    }
}

/**
    * @brief  获取当前 OTA 状态。
    * @retval bl_ota_state_t 当前 OTA 状态
    */
bl_ota_state_t BL_OTA_GetState(void)
{
    return s_state;
}
