/**
 * @file  drv_wifi.c
 * @brief W800 WiFi 驱动：AT 初始化、SKTRPT 解析、JSON 协议分发
 * @date  2026-02-13
 * @author Ma Ziteng
 */

#include "drv_wifi.h"
#include "wifi_debug.h"
#include "nvm_manager.h"
#include "nvm_config.h"
#include "sys_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

/* ---- 常量 ---- */
#define RX_BUF_SIZE    4096
#define TX_BUF_SIZE    8192
#define CHUNK_MAX      500      /**< W800 单次 SKSND 上限 512B，留余量 */

/* ---- 静态缓冲 ---- */
static volatile char     rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_idx;
static volatile uint8_t  rx_state;          /**< 2 = 收到完整 \\r\\n 帧 */
static volatile uint32_t isr_rx_count;
static volatile bool     tx_done = true;

static char  tx_buf[TX_BUF_SIZE];
static char  line[RX_BUF_SIZE];             /**< static 避免爆栈 */
static int   active_sock = -1;

/* ---- 前置声明 ---- */
static bool        send_at(const char *cmd, const char *expect, uint32_t ms);
static void        clear_rx(void);
static void        wait_tx(uint32_t ms);
static void        handle_json(const char *js);
static void        send_json(const char *json_str);
static void        send_error(const char *cmd, const char *msg);
static bool        match_cmd(const char *js, const char *cmd);
static const char* parse_str(const char *js, const char *key, char *out, size_t max);
static bool        parse_ip(const char *js, const char *key, uint8_t ip[4]);
static bool        parse_uint(const char *js, const char *key, uint32_t *val);
static bool        parse_decimal_i16(const char *js, const char *key, int16_t *val);
static bool        parse_decimal_u16(const char *js, const char *key, uint16_t *val);
static bool        parse_decimal_i16_array(const char *js, const char *key, int16_t *arr, size_t n);
static bool        parse_decimal_u16_array(const char *js, const char *key, uint16_t *arr, size_t n);
static void        encode_hex(const uint8_t *data, uint32_t len, char *hex, size_t max);

/* ===========================================================
 *  底层
 * =========================================================== */

/**
 * @brief UART 中断回调：RX 累积到 rx_buf，CRLF 标记帧完成；TX 完成
 * @param p_args UART 事件参数
 */
void wifi_uart_callback(uart_callback_args_t *p_args) {
    if (p_args->event == UART_EVENT_RX_CHAR) {
        char c = (char)p_args->data;
        isr_rx_count++;
        if (rx_idx >= RX_BUF_SIZE - 1) rx_idx = 0;
        rx_buf[rx_idx++] = c;
        rx_buf[rx_idx]   = '\0';
        if (rx_idx >= 2 && rx_buf[rx_idx - 2] == '\r' && rx_buf[rx_idx - 1] == '\n')
            rx_state = 2;
    }
    if (p_args->event == UART_EVENT_TX_COMPLETE) tx_done = true;
}

uint32_t wifi_get_isr_rx_count(void) { return isr_rx_count; }

/**
 * @brief 清空接收缓存（关中断保护）
 */
static void clear_rx(void) {
    R_BSP_IrqDisable(g_uart_wifi_cfg.rxi_irq);
    rx_idx = 0; rx_buf[0] = '\0'; rx_state = 0;
    R_BSP_IrqEnable(g_uart_wifi_cfg.rxi_irq);
}

/**
 * @brief 阻塞等待 TX 完成
 * @param ms 超时时间（毫秒）
 */
static void wait_tx(uint32_t ms) {
    for (uint32_t t = 0; !tx_done && t < ms; t++)
        vTaskDelay(pdMS_TO_TICKS(1));
}

/**
 * @brief 发送 AT 指令并等待期望关键字
 * @param cmd    AT 指令字符串
 * @param expect 期望回复关键字（NULL 则不等待）
 * @param ms     超时时间（毫秒）
 * @return true 表示成功
 */
static bool send_at(const char *cmd, const char *expect, uint32_t ms) {
    wait_tx(200);
    clear_rx();
    tx_done = false;
    g_uart_wifi.p_api->write(g_uart_wifi.p_ctrl, (uint8_t *)cmd, strlen(cmd));
    if (!expect) return true;

    for (uint32_t t = 0; t < ms; t += 10) {
        if (strstr((const char *)rx_buf, expect)) return true;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    LOG_W("AT timeout. rx: %s", rx_buf);
    return false;
}

/* ===========================================================
 *  公共 API
 * =========================================================== */

bool wifi_link_check(void) { return send_at("AT+\r\n", "+OK", 500); }

/**
 * @brief 初始化 W800 SoftAP + TCP Server (port 8080)
 * @return 初始化成功返回 true，否则返回 false
 */
uint8_t wifi_init_ap_server(void) {
    const sys_config_t *cfg = nvm_get_sys_config();
    const char *ssid = (cfg->wifi_ssid[0] != '\0') ? cfg->wifi_ssid : "SurgicalArm_Debug";
    const char *psk = (cfg->wifi_psk[0] != '\0') ? cfg->wifi_psk : "fdudebug";
    uint16_t debug_port = (cfg->debug_port != 0U) ? cfg->debug_port : 8080U;
    char cmd_apssid[96];
    char cmd_apkey[128];
    char cmd_apnip[128];
    char cmd_skct[96];

    snprintf(cmd_apssid, sizeof(cmd_apssid), "AT+APSSID=%s\r\n", ssid);
    snprintf(cmd_apkey, sizeof(cmd_apkey), "AT+APKEY=1,0,%s\r\n", psk);
    snprintf(cmd_apnip, sizeof(cmd_apnip),
             "AT+APNIP=1,%u.%u.%u.%u,%u.%u.%u.%u,%u.%u.%u.%u,%u.%u.%u.%u\r\n",
             cfg->ip_addr[0], cfg->ip_addr[1], cfg->ip_addr[2], cfg->ip_addr[3],
             cfg->netmask[0], cfg->netmask[1], cfg->netmask[2], cfg->netmask[3],
             cfg->gateway[0], cfg->gateway[1], cfg->gateway[2], cfg->gateway[3],
             cfg->gateway[0], cfg->gateway[1], cfg->gateway[2], cfg->gateway[3]);
    snprintf(cmd_skct, sizeof(cmd_skct), "AT+SKCT=0,1,0,%u,%u\r\n", debug_port, debug_port);

    if (g_uart_wifi.p_api->open(g_uart_wifi.p_ctrl, g_uart_wifi.p_cfg) != FSP_SUCCESS)
        return false;
    LOG_D("WIFI UART opened.");

    for (uint8_t n = 10; n; n--)
        if (send_at("AT+\r\n", "+OK", 500)) { LOG_D("W800 ready."); break; }
        else if (n == 1) return false;

    bool ok = true;
    ok &= send_at("AT+WPRT=2\r\n",          "+OK", 1000);
    ok &= send_at(cmd_apssid, "+OK", 1000);
    ok &= send_at("AT+APENCRY=4\r\n",       "+OK", 1000);
    ok &= send_at(cmd_apkey, "+OK", 1000);
    ok &= send_at(cmd_apnip, "+OK", 1000);
    ok &= send_at("AT+PMTF\r\n",            "+OK", 1000);
    send_at("AT+Z\r\n", NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));
    ok &= send_at("AT+WJOIN\r\n",           "+OK", 5000);
    ok &= send_at("AT+SKRPTM=1\r\n",        "+OK", 1000);
    ok &= send_at(cmd_skct, "+OK", 2000);
    clear_rx();
    return ok;
}

/**
 * @brief 轮询处理 W800 上报的 +SKTRPT 并分发 JSON 命令
 *
 * W800 格式: +SKTRPT=<sock>,<dlen>,<ip>,<port>\r\n\r\n<json>\r\n
 */
void wifi_process_commands(void) {
    if (rx_state != 2) return;

    /* peek：SKTRPT 需要等数据到齐 */
    const char *buf = (const char *)rx_buf;
    if (strncmp(buf, "+SKTRPT=", 8) == 0) {
        int dlen = 0;
        sscanf(buf + 8, "%*d,%d", &dlen);
        const char *hdr_end = strstr(buf, "\r\n");
        if (!hdr_end) return;
        size_t expected = (size_t)(hdr_end - buf) + 4 + (size_t)dlen; /* hdr+\r\n+\r\n+data */
        if (rx_idx < expected) { rx_state = 0; return; }
    }

    /* 消费 buffer */
    R_BSP_IrqDisable(g_uart_wifi_cfg.rxi_irq);
    strncpy(line, (const char *)rx_buf, RX_BUF_SIZE - 1);
    line[RX_BUF_SIZE - 1] = '\0';
    rx_state = 0; rx_idx = 0; rx_buf[0] = '\0';
    R_BSP_IrqEnable(g_uart_wifi_cfg.rxi_irq);

    /* 去除尾部 CRLF */
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\r' || line[len - 1] == '\n')) line[--len] = '\0';
    if (!len) return;

    LOG_D("[RX] len=%d", (int)len);

    /* 提取 SKTRPT → JSON */
    if (strncmp(line, "+SKTRPT=", 8) == 0) {
        sscanf(line + 8, "%d", &active_sock);
        char *sep = strstr(line, "\r\n\r\n");
        if (sep) {
            char *js = sep + 4;
            size_t jlen = strlen(js);
            while (jlen && (js[jlen - 1] == '\r' || js[jlen - 1] == '\n')) js[--jlen] = '\0';
            if (jlen) { handle_json(js); return; }
        }
        LOG_W("SKTRPT: no JSON body");
        return;
    }
    LOG_D("[RX] ignored: %.60s", line);
}

/* ===========================================================
 *  JSON 命令处理
 * =========================================================== */

/**
 * @brief 匹配 JSON 中 cmd 字段（兼容有/无空格）
 * @param js  JSON 字符串
 * @param cmd 要匹配的指令名称
 * @return true 表示匹配成功
 */
static bool match_cmd(const char *js, const char *cmd) {
    char p[48];
    snprintf(p, sizeof(p), "\"cmd\":\"%s\"", cmd);
    if (strstr(js, p)) return true;
    snprintf(p, sizeof(p), "\"cmd\": \"%s\"", cmd);
    return strstr(js, p) != NULL;
}

/**
 * @brief 分发 JSON 命令
 * @param js JSON 字符串
 */
static void handle_json(const char *js) {
    LOG_D("[CMD] %.80s", js);

    if (match_cmd(js, "ping")) {
        send_json("{\"type\":\"pong\",\"ok\":true}");
        return;
    }

    if (match_cmd(js, "read_sys_cfg")) {
        const sys_config_t *c = nvm_get_sys_config();
        snprintf(tx_buf, TX_BUF_SIZE,
            "{\"type\":\"sys_cfg\","
            "\"ip\":\"%u.%u.%u.%u\",\"netmask\":\"%u.%u.%u.%u\","
            "\"gateway\":\"%u.%u.%u.%u\",\"server_ip\":\"%u.%u.%u.%u\","
            "\"server_port\":%u,\"debug_port\":%u,"
            "\"wifi_ssid\":\"%s\",\"wifi_psk\":\"%s\",\"log_offset\":%lu,"
            "\"angle_min\":[%d.%02d,%d.%02d,%d.%02d,%d.%02d],"
            "\"angle_max\":[%d.%02d,%d.%02d,%d.%02d,%d.%02d],"
            "\"current_limit\":[%u.%02u,%u.%02u,%u.%02u,%u.%02u],"
            "\"grip_force_max\":%u.%02u}"
            ,
            c->ip_addr[0],  c->ip_addr[1],  c->ip_addr[2],  c->ip_addr[3],
            c->netmask[0],  c->netmask[1],  c->netmask[2],  c->netmask[3],
            c->gateway[0],  c->gateway[1],  c->gateway[2],  c->gateway[3],
            c->server_ip[0],c->server_ip[1],c->server_ip[2],c->server_ip[3],
            c->server_port, c->debug_port, c->wifi_ssid, c->wifi_psk,
            (unsigned long)nvm_get_log_offset(),
            c->angle_min[0]/100, (c->angle_min[0] < 0 ? -c->angle_min[0] : c->angle_min[0]) % 100,
            c->angle_min[1]/100, (c->angle_min[1] < 0 ? -c->angle_min[1] : c->angle_min[1]) % 100,
            c->angle_min[2]/100, (c->angle_min[2] < 0 ? -c->angle_min[2] : c->angle_min[2]) % 100,
            c->angle_min[3]/100, (c->angle_min[3] < 0 ? -c->angle_min[3] : c->angle_min[3]) % 100,
            c->angle_max[0]/100, c->angle_max[0] % 100,
            c->angle_max[1]/100, c->angle_max[1] % 100,
            c->angle_max[2]/100, c->angle_max[2] % 100,
            c->angle_max[3]/100, c->angle_max[3] % 100,
            c->current_limit[0]/100, c->current_limit[0] % 100,
            c->current_limit[1]/100, c->current_limit[1] % 100,
            c->current_limit[2]/100, c->current_limit[2] % 100,
            c->current_limit[3]/100, c->current_limit[3] % 100,
            c->grip_force_max/100, c->grip_force_max % 100);
        send_json(tx_buf);
        return;
    }

    if (match_cmd(js, "write_sys_cfg")) {
        sys_config_t cfg = *nvm_get_sys_config();
        uint32_t v;
        bool ok = parse_ip(js, "ip", cfg.ip_addr)
                & parse_ip(js, "netmask", cfg.netmask)
                & parse_ip(js, "gateway", cfg.gateway)
                & parse_ip(js, "server_ip", cfg.server_ip);
        ok &= parse_uint(js, "server_port", &v); cfg.server_port = (uint16_t)v;
        ok &= parse_uint(js, "debug_port",  &v); cfg.debug_port  = (uint16_t)v;
        ok &= parse_str(js, "wifi_ssid", cfg.wifi_ssid, sizeof(cfg.wifi_ssid)) != NULL;
        ok &= parse_str(js, "wifi_psk",  cfg.wifi_psk,  sizeof(cfg.wifi_psk))  != NULL;
        ok &= parse_decimal_i16_array(js, "angle_min", cfg.angle_min, 4);
        ok &= parse_decimal_i16_array(js, "angle_max", cfg.angle_max, 4);
        ok &= parse_decimal_u16_array(js, "current_limit", cfg.current_limit, 4);
        ok &= parse_decimal_u16(js, "grip_force_max", &cfg.grip_force_max);
        if (!ok) { send_error("write_sys_cfg", "invalid_fields"); return; }
        send_json(nvm_save_sys_config(&cfg) == FSP_SUCCESS
            ? "{\"type\":\"ack\",\"cmd\":\"write_sys_cfg\",\"ok\":true}"
            : "{\"type\":\"error\",\"cmd\":\"write_sys_cfg\",\"msg\":\"save_failed\"}");
        return;
    }

    if (match_cmd(js, "read_log")) {
        /* 先将 RAM 缓存 flush 到 Flash，确保读取最新日志 */
        nvm_save_logs();
        extern void wait_ready(void);
        wait_ready(); // 确保Flash写入完成

        uint32_t offset = 0, length = 256;
        parse_uint(js, "offset", &offset);
        parse_uint(js, "length", &length);
        if (length > 512) length = 512;
        uint8_t tmp[512];
        nvm_read_logs(offset, tmp, length);
        char hexbuf[512 * 2 + 1];
        encode_hex(tmp, length, hexbuf, sizeof(hexbuf));
        snprintf(tx_buf, TX_BUF_SIZE,
            "{\"type\":\"log_data\",\"offset\":%lu,\"len\":%lu,\"hex\":\"%s\"}",
            (unsigned long)offset, (unsigned long)length, hexbuf);
        send_json(tx_buf);
        return;
    }

    if (match_cmd(js, "clear_log")) {
        send_json(nvm_clear_logs() == FSP_SUCCESS
            ? "{\"type\":\"ack\",\"cmd\":\"clear_log\",\"ok\":true}"
            : "{\"type\":\"error\",\"cmd\":\"clear_log\",\"msg\":\"erase_failed\"}");
        return;
    }

    if (match_cmd(js, "read_motion_cfg")) {
        static char hex_tmp[sizeof(motion_config_t) * 2 + 1];
        encode_hex((const uint8_t *)nvm_get_motion_config(),
                   sizeof(motion_config_t), hex_tmp, sizeof(hex_tmp));
        snprintf(tx_buf, TX_BUF_SIZE, "{\"type\":\"motion_cfg\",\"hex\":\"%s\"}", hex_tmp);
        send_json(tx_buf);
        return;
    }

    send_error("unknown", "unsupported_cmd");
}

/* ===========================================================
 *  TCP 发送（AT+SKSND 分块）
 * =========================================================== */

/**
 * @brief 通过 AT+SKSND 向客户端发送 JSON（自动追加 CRLF、自动分块）
 * @param json_str 要发送的 JSON 字符串
 */
static void send_json(const char *json_str) {
    if (active_sock < 0) { LOG_W("send_json: no socket"); return; }

    static char send_buf[TX_BUF_SIZE];
    size_t jlen = strlen(json_str);
    if (jlen + 2 > TX_BUF_SIZE) { LOG_W("send_json: too large"); jlen = TX_BUF_SIZE - 2; }
    memcpy(send_buf, json_str, jlen);
    send_buf[jlen] = '\r'; send_buf[jlen + 1] = '\n';
    size_t total = jlen + 2;

    for (size_t sent = 0; sent < total; ) {
        size_t chunk = total - sent;
        if (chunk > CHUNK_MAX) chunk = CHUNK_MAX;

        char at[64];
        snprintf(at, sizeof(at), "AT+SKSND=%d,%u\r\n", active_sock, (unsigned)chunk);
        wait_tx(200); clear_rx(); tx_done = false;
        g_uart_wifi.p_api->write(g_uart_wifi.p_ctrl, (uint8_t *)at, strlen(at));

        uint32_t w = 0;
        while (w < 2000 && !strstr((const char *)rx_buf, "+OK")) { vTaskDelay(pdMS_TO_TICKS(5)); w += 5; }
        if (w >= 2000) { LOG_W("SKSND +OK timeout @%u", (unsigned)sent); return; }

        wait_tx(200); clear_rx(); tx_done = false;
        g_uart_wifi.p_api->write(g_uart_wifi.p_ctrl, (uint8_t *)(send_buf + sent), chunk);
        wait_tx(500);
        sent += chunk;
    }
}

/** @brief 发送 JSON 错误响应
 * @param cmd 命令名称
 * @param msg 错误消息
 */
static void send_error(const char *cmd, const char *msg) {
    snprintf(tx_buf, TX_BUF_SIZE, "{\"type\":\"error\",\"cmd\":\"%s\",\"msg\":\"%s\"}", cmd, msg);
    send_json(tx_buf);
}

/* ===========================================================
 *  JSON 轻量解析工具
 * =========================================================== */

/**
 * @brief 解析字符串字段 "key":"value"（兼容冒号后空格）
 * @param js  JSON 字符串
 * @param key 字段名
 * @param out 输出缓冲区
 * @param max 输出缓冲区最大长度
 * @return 解析成功返回 out，失败返回 NULL
 */
static const char* parse_str(const char *js, const char *key, char *out, size_t max) {
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(js, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (*p != '"') return NULL;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < max) out[i++] = *p++;
    out[i] = '\0';
    return out;
}

/**
 * @brief 简单整数解析
 * @param p   输入字符串
 * @param end 解析结束位置（输出参数，可为 NULL）
 * @return 解析得到的整数值
 */
static int simple_atoi(const char *p, const char **end) {
    if (!p) { if (end) *end = NULL; return 0; }
    int sign = 1, val = 0;
    while (*p == ' ') p++;
    if (*p == '\0') { if (end) *end = p; return 0; }
    if (*p == '-') { sign = -1; p++; }
    else if (*p == '+') p++;
    while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
    if (end) *end = p;
    return sign * val;
}

/**
 * @brief 解析 "key":"a.b.c.d" → uint8_t ip[4]
 * @param js  JSON 字符串
 * @param key 字段名
 * @param ip  IP 地址输出数组（4 字节）
 * @return true 表示解析成功
 */
static bool parse_ip(const char *js, const char *key, uint8_t ip[4]) {
    if (!js || !key || !ip) return false;
    char buf[32];
    if (!parse_str(js, key, buf, sizeof(buf))) return false;
    const char *p = buf;
    for (int i = 0; i < 4; i++) {
        const char *end;
        int v = simple_atoi(p, &end);
        if (end == p || v < 0 || v > 255) return false;
        ip[i] = (uint8_t)v;
        p = end;
        if (i < 3 && *p != '.') return false;
        p++;
    }
    return true;
}

/**
 * @brief 解析无符号整数（兼容带引号和不带引号）
 * @param js  JSON 字符串
 * @param key 字段名
 * @param val 输出值
 * @return true 表示解析成功
 */
static bool parse_uint(const char *js, const char *key, uint32_t *val) {
    if (!js || !key || !val) return false;
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(js, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (*p == '"') p++;
    if (*p == '\0' || !(*p >= '0' && *p <= '9')) return false;
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (uint32_t)(*p - '0');
        p++;
    }
    *val = v;
    return true;
}

/**
 * @brief 解析定点数（乘以100，返回int16_t）
 * @param js  JSON 字符串
 * @param key 字段名
 * @param val 输出值
 * @return true 表示解析成功
 */
static bool parse_decimal_i16(const char *js, const char *key, int16_t *val) {
    if (!js || !key || !val) return false;
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(js, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (*p == '"') p++;
    if (*p == '\0') return false;
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    else if (*p == '+') p++;
    int32_t v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    int32_t frac = 0;
    int32_t frac_digits = 0;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9' && frac_digits < 2) {
            frac = frac * 10 + (*p - '0');
            frac_digits++;
            p++;
        }
    }
    while (*p >= '0' && *p <= '9') p++;
    if (frac_digits == 1) {
        frac *= 10;
    }
    v = v * 100 + frac;
    int32_t scaled = sign * v;
    if (scaled < INT16_MIN || scaled > INT16_MAX) {
        return false;
    }
    *val = (int16_t)scaled;
    return true;
}

/**
 * @brief 解析定点数（乘以100，返回uint16_t）
 * @param js  JSON 字符串
 * @param key 字段名
 * @param val 输出值
 * @return true 表示解析成功
 */
static bool parse_decimal_u16(const char *js, const char *key, uint16_t *val) {
    if (!js || !key || !val) return false;
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(js, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (*p == '"') p++;
    if (*p == '\0') return false;
    int32_t v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    int32_t frac = 0;
    int32_t frac_digits = 0;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9' && frac_digits < 2) {
            frac = frac * 10 + (*p - '0');
            frac_digits++;
            p++;
        }
    }
    while (*p >= '0' && *p <= '9') p++;
    if (frac_digits == 1) {
        frac *= 10;
    }
    v = v * 100 + frac;
    if (v < 0 || v > UINT16_MAX) {
        return false;
    }
    *val = (uint16_t)v;
    return true;
}

/**
 * @brief 解析定点数数组（乘以100，返回int16_t）
 * @param js  JSON 字符串
 * @param key 字段名
 * @param arr 输出数组
 * @param n   数组元素个数
 * @return true 表示解析成功
 */
static bool parse_decimal_i16_array(const char *js, const char *key, int16_t *arr, size_t n) {
    if (!js || !key || !arr || n == 0) return false;
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\":[", key);
    const char *p = strstr(js, pat);
    if (!p) return false;
    p += strlen(pat);
    for (size_t i = 0; i < n; i++) {
        while (*p == ' ') p++;
        if (*p == '\0' || *p == ']') return false;
        int sign = 1;
        if (*p == '-') { sign = -1; p++; }
        else if (*p == '+') { p++; }
        int32_t v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        int32_t frac = 0;
        int32_t frac_digits = 0;
        if (*p == '.') {
            p++;
            while (*p >= '0' && *p <= '9' && frac_digits < 2) {
                frac = frac * 10 + (*p - '0');
                frac_digits++;
                p++;
            }
        }
        while (*p >= '0' && *p <= '9') p++;
        if (frac_digits == 1) {
            frac *= 10;
        }
        v = v * 100 + frac;
        int32_t scaled = sign * v;
        if (scaled < INT16_MIN || scaled > INT16_MAX) {
            return false;
        }
        arr[i] = (int16_t)scaled;
        p++;
        while (*p == ' ' || *p == ',') p++;
    }
    return true;
}

/**
 * @brief 解析定点数数组（乘以100，返回uint16_t）
 * @param js  JSON 字符串
 * @param key 字段名
 * @param arr 输出数组
 * @param n   数组元素个数
 * @return true 表示解析成功
 */
static bool parse_decimal_u16_array(const char *js, const char *key, uint16_t *arr, size_t n) {
    if (!js || !key || !arr || n == 0) return false;
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\":[", key);
    const char *p = strstr(js, pat);
    if (!p) return false;
    p += strlen(pat);
    for (size_t i = 0; i < n; i++) {
        while (*p == ' ') p++;
        if (*p == '\0' || *p == ']') return false;
        int32_t v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        int32_t frac = 0;
        int32_t frac_digits = 0;
        if (*p == '.') {
            p++;
            while (*p >= '0' && *p <= '9' && frac_digits < 2) {
                frac = frac * 10 + (*p - '0');
                frac_digits++;
                p++;
            }
        }
        while (*p >= '0' && *p <= '9') p++;
        if (frac_digits == 1) {
            frac *= 10;
        }
        v = v * 100 + frac;
        if (v < 0 || v > UINT16_MAX) {
            return false;
        }
        arr[i] = (uint16_t)v;
        p++;
        while (*p == ' ' || *p == ',') p++;
    }
    return true;
}

/**
 * @brief 二进制 → 大写十六进制字符串
 * @param data 输入数据
 * @param len  数据长度
 * @param hex  输出十六进制字符串
 * @param max  输出缓冲区最大长度
 */
static void encode_hex(const uint8_t *data, uint32_t len, char *hex, size_t max) {
    static const char tbl[] = "0123456789ABCDEF";
    size_t j = 0;
    for (uint32_t i = 0; i < len && j + 2 < max; i++) {
        hex[j++] = tbl[data[i] >> 4];
        hex[j++] = tbl[data[i] & 0x0F];
    }
    hex[j] = '\0';
}
