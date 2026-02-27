/**
 * @file  log_task_entry.c
 * @brief 日志任务入口（初始化 UART，循环调度日志输出）
 */

#include <log_task.h>
#include "sys_log.h"

extern void sys_log_init(uart_instance_t uart);
extern void logger_task_func(void);

void log_task_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED(pvParameters);

    sys_log_init(g_uart_log);
    g_ioport.p_api->pinWrite(g_ioport.p_ctrl, BSP_IO_PORT_04_PIN_00, BSP_IO_LEVEL_LOW);
    LOG_I("Log task started.");
    while (1) {
        logger_task_func();
        vTaskDelay(10);
    }
}
