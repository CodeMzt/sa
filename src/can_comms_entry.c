/**
 * @file can_comms_entry.c
 * @brief 旧版 CAN 运控任务兼容入口
 */

/* -------------------------------------------------------------------------- */
/* 旧模块说明                                                                  */
/*                                                                            */
/* 本文件对应旧版 CAN 运控任务入口。当前项目已经切换到 `servo_bus_entry.c`     */
/* 承担实际运控调度，本入口仅保留任务符号与工程兼容性，不再执行任何电机控制。   */
/* 不适用原因：底层电机与协议已整体切换为串口舵机方案。                         */
/* -------------------------------------------------------------------------- */

#include "can_comms_entry.h"
#include "shared_data.h"
#include "sys_log.h"
#include "test_mode.h"

#define LOG_READY_WAIT_SLICE_MS   10U
#define LOG_READY_WAIT_MAX_MS   1000U

/* -------------------------------------------------------------------------- */
/* 旧兼容入口说明                                                               */
/* 保留旧 can_comms 任务入口，避免 FSP 任务表和旧引用失效；实际运行时该任务仅   */
/* 等待日志系统后自删，新的运控主链已经迁移到 servo_bus_entry。                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief 旧版 CAN 运控任务入口
 * @param pvParameters FreeRTOS 任务参数
 * @note  当前实现仅输出日志并自删除，避免旧任务继续参与运行时控制。
 */
void can_comms_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED(pvParameters);

#if TEST_MODE_ACTIVE && !TEST_KEEP_CAN_COMMS
    LOG_I("Test mode: can_comms thread disabled.");
    vTaskDelete(NULL);
    return;
#endif

    uint32_t wait_ms = 0U;
    while ((!g_log_system_ready) && (wait_ms < LOG_READY_WAIT_MAX_MS)) {
        vTaskDelay(pdMS_TO_TICKS(LOG_READY_WAIT_SLICE_MS));
        wait_ms += LOG_READY_WAIT_SLICE_MS;
    }

    LOG_I("Legacy can_comms thread retired, deleting self.");
    vTaskDelete(NULL);
}
