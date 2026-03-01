/**
 * @file  wifi_debug_entry.c
 * @brief WiFi 调试任务入口（初始化 WiFi 模块 + NVM，循环处理 JSON 命令）
 */

#include <wifi_debug.h>
#include "sys_log.h"
#include "drv_wifi.h"
#include "nvm_manager.h"
#include "shared_data.h"

void wifi_debug_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED(pvParameters);
    vTaskDelay(pdMS_TO_TICKS(10));

    fsp_err_t err = nvm_init();
    if (err != FSP_SUCCESS) {
        LOG_E("NVM init error: %d", err);
        g_sys_status.is_wifi_connected = false;
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    if (!wifi_init_ap_server()) {
        LOG_E("WIFI init error.");
        g_sys_status.is_wifi_connected = false;
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    g_sys_status.is_wifi_connected = true;
    LOG_I("WiFi debug task started.");

    R_BSP_IrqDisable(g_uart_wifi_cfg.rxi_irq);
    R_BSP_IrqDisable(g_uart_wifi_cfg.txi_irq);
    R_BSP_IrqDisable(g_uart_wifi_cfg.tei_irq);
    R_BSP_IrqDisable(g_uart_wifi_cfg.eri_irq);

    while (1) {
        wifi_process_commands();
        vTaskDelay(100);
    }
}
