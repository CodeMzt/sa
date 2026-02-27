/**
 * @file  sys_log.c
 * @brief 系统日志模块实现（FreeRTOS 队列 + UART 输出 / Flash 存储）
 * @date  2026-01-26
 * @author Ma Ziteng
 */

#include <log_task.h>
#include "sys_log.h"
#include "nvm_manager.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static log_level_t g_level = log_lvl_debug;
static uart_instance_t log_uart;

extern QueueHandle_t     g_log_queue;
extern SemaphoreHandle_t g_uart_tx_sem;

typedef struct { char buffer[256]; } log_msg_t;

/**
 * @brief 初始化日志系统（打开 UART，释放信号量）
 * @param uart UART 实例
 */
void sys_log_init(uart_instance_t uart) {
    xSemaphoreGive(g_uart_tx_sem);
    xSemaphoreGive(g_log_mutex);
    log_uart = uart;
    log_uart.p_api->open(log_uart.p_ctrl, log_uart.p_cfg);
}

/**
 * @brief 格式化并输出日志
 * @param level 日志级别
 * @param fmt   格式化字符串
 */
void log_print(log_level_t level, const char *fmt, ...) {
        if (level < g_level || g_log_queue == NULL) return;

        if (xSemaphoreTake(g_log_mutex, portMAX_DELAY) == pdTRUE) {
        static const char *tag[] = {"DBG", "INF", "WRN", "ERR"};
        log_msg_t msg = {{0}};
        va_list args;

        int len = snprintf(msg.buffer, LOG_MAX_LINE_LEN, "[%s][%lu] ", tag[level], xTaskGetTickCount());
        va_start(args, fmt);
        vsnprintf(msg.buffer + len, LOG_MAX_LINE_LEN - len, fmt, args);
        va_end(args);

        int total = (int)strlen(msg.buffer);
        if (total > LOG_MAX_LINE_LEN - 3) total = LOG_MAX_LINE_LEN - 3;
        msg.buffer[total]     = '\r';
        msg.buffer[total + 1] = '\n';
        msg.buffer[total + 2] = '\0';

#ifdef DEBUG
        xQueueSend(g_log_queue, &msg, 0);
#else
        nvm_append_log(msg.buffer);
#endif
        xSemaphoreGive(g_log_mutex);
    }
}

/**
 * @brief 日志后台任务（从队列取出并发送 UART）
 */
void logger_task_func(void) {
    log_msg_t rx;
#ifdef DEBUG
    if (xSemaphoreTake(g_uart_tx_sem, portMAX_DELAY) == pdTRUE) {
        if (xQueueReceive(g_log_queue, &rx, portMAX_DELAY) == pdPASS) {
            log_uart.p_api->write(log_uart.p_ctrl, (uint8_t *)rx.buffer, strlen(rx.buffer));
        }
    }
#else
    static uint32_t last_tick = 0;
    uint32_t current_tick = xTaskGetTickCount();
    if (current_tick - last_tick > 10000) {
        nvm_save_logs();
        last_tick = current_tick;
    }
#endif
}

/**
 * @brief UART 发送完成回调（释放信号量）
 * @param p_args UART 事件参数
 */
void uart_log_callback(uart_callback_args_t *p_args) {
    BaseType_t woken = pdFALSE;
    if (p_args->event == UART_EVENT_TX_COMPLETE) {
        xSemaphoreGiveFromISR(g_uart_tx_sem, &woken);
        portYIELD_FROM_ISR(woken);
    }
}
