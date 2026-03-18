/**
 * @file  drv_wifi.h
 * @brief W800 WiFi 模块驱动头文件（AT 指令与 TCP JSON 协议接口）
 * @date  2026-02-11
 * @author Ma Ziteng
 */

#ifndef DRV_WIFI_H_
#define DRV_WIFI_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 初始化 W800 SoftAP 并启动 TCP Server
 * @return 成功返回 true
 */
uint8_t wifi_init_ap_server(void);

/**
 * @brief AT 链路检测（AT+ → +OK）
 * @return true 表示链路正常
 */
bool wifi_link_check(void);

/**
 * @brief 启动 WiFi 调试服务（打开 UART + 启动 AP/监听）
 * @return true 表示启动成功
 */
bool wifi_start_service(void);

/**
 * @brief 停止 WiFi 调试服务（关闭监听/AP，并关闭 UART）
 * @return true 表示停止成功
 */
bool wifi_stop_service(void);

/**
 * @brief 轮询处理 TCP 收到的 JSON 命令（需在任务循环中调用）
 */
void wifi_process_commands(void);

/**
 * @brief 进入调试交互前重置 WiFi 接收/会话状态
 * 清空串口接收缓存并重置活动 socket，避免非调试阶段残留数据影响后续解析。
 */
void wifi_reset_debug_session(void);

/**
 * @brief 获取 ISR 累计接收字节数（诊断用）
 * @return 接收字节数
 */
uint32_t wifi_get_isr_rx_count(void);

#endif /* DRV_WIFI_H_ */
