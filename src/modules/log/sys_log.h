/**
 * @file  sys_log.h
 * @brief 系统日志模块公共接口（UART 输出 + Flash 存储）
 * @date  2026-01-26
 * @author Ma Ziteng
 */

#ifndef SYS_LOG_H_
#define SYS_LOG_H_

#include <stdint.h>

/** @brief 日志等级 */
typedef enum {
    log_lvl_debug = 0,
    log_lvl_info,
    log_lvl_warn,
    log_lvl_error,
    log_lvl_none
} log_level_t;

#define LOG_MAX_LINE_LEN  128  /**< 单条日志最大长度 */

/** @brief 核心打印函数（类似 printf） */
void log_print(log_level_t level, const char *fmt, ...);

/* ---- 快捷宏 ---- */
#define LOG_D(...) log_print(log_lvl_debug, __VA_ARGS__)
#define LOG_I(...) log_print(log_lvl_info,  __VA_ARGS__)
#define LOG_W(...) log_print(log_lvl_warn,  __VA_ARGS__)
#define LOG_E(...) log_print(log_lvl_error, __VA_ARGS__)

#endif /* SYS_LOG_H_ */
