/**
 * @file  wifi_debug_entry.c
 * @brief WiFi 调试任务入口（初始化 WiFi 模块 + NVM，循环处理 JSON 命令）
 * @date  2026-02-11
 * @author Ma Ziteng
 */

#include <wifi_debug.h>
#include "sys_log.h"
#include "drv_wifi.h"
#include "nvm_manager.h"
#include "shared_data.h"
#include "modules/servo/drv_servo.h"
#include "test_mode.h"

static void reset_teach_jog_cmd_with_stop(void) {
    teach_jog_hold_cmd_t hold = {0};
    teach_jog_hold_read(&hold);
    uint8_t motor_id = hold.motor_id;
    teach_jog_hold_clear();

    if (hold.active && motor_id_is_valid(motor_id)) {
        (void)servo_stop_motor(motor_id, true);
    }
}

void wifi_debug_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED(pvParameters);

#if TEST_MODE_ACTIVE && !TEST_KEEP_WIFI_DEBUG
    LOG_I("Test mode: wifi_debug thread disabled.");
    vTaskDelete(NULL);
    return;
#endif

    vTaskDelay(pdMS_TO_TICKS(10));

    fsp_err_t err = nvm_init();
    if (err != FSP_SUCCESS) {
        LOG_E("NVM init error: %d", err);
        g_sys_status.is_wifi_connected = false;
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    g_sys_status.is_wifi_connected = false;
    g_sys_status.is_debug_mode_active = false;
    LOG_I("WiFi debug task started (on-demand mode).");

    bool wifi_running = false;

    while (1) {
        bool should_run = g_sys_status.is_debug_mode_active;

        if (should_run && !wifi_running) {
            if (wifi_start_service()) {
                reset_teach_jog_cmd_with_stop();
                wifi_running = true;
                g_sys_status.is_wifi_connected = true;
                LOG_I("WiFi service started for DEBUG mode.");
            } else {
                reset_teach_jog_cmd_with_stop();
                g_sys_status.is_wifi_connected = false;
                LOG_E("WiFi service start failed.");
            }
        } else if (!should_run && wifi_running) {
            reset_teach_jog_cmd_with_stop();
            if (!wifi_stop_service()) {
                LOG_W("WiFi service stop reported warnings.");
            }
            wifi_running = false;
            g_sys_status.is_wifi_connected = false;
            LOG_I("WiFi service stopped (exit DEBUG mode).");
        }

        if (wifi_running) {
            wifi_process_commands();
            vTaskDelay(pdMS_TO_TICKS(20));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
