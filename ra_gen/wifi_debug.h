/* generated thread header file - do not edit */
#ifndef WIFI_DEBUG_H_
#define WIFI_DEBUG_H_
#include "bsp_api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hal_data.h"
#ifdef __cplusplus
                extern "C" void wifi_debug_entry(void * pvParameters);
                #else
extern void wifi_debug_entry(void *pvParameters);
#endif
#include "r_dtc.h"
#include "r_transfer_api.h"
#include "r_sci_uart.h"
#include "r_uart_api.h"
FSP_HEADER
/* Transfer on DTC Instance. */
extern const transfer_instance_t g_transfer4;

/** Access the DTC instance using these structures when calling API functions directly (::p_api is not used). */
extern dtc_instance_ctrl_t g_transfer4_ctrl;
extern const transfer_cfg_t g_transfer4_cfg;
/* Transfer on DTC Instance. */
extern const transfer_instance_t g_transfer3;

/** Access the DTC instance using these structures when calling API functions directly (::p_api is not used). */
extern dtc_instance_ctrl_t g_transfer3_ctrl;
extern const transfer_cfg_t g_transfer3_cfg;
/** UART on SCI Instance. */
extern const uart_instance_t g_uart_wifi;

/** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
extern sci_uart_instance_ctrl_t g_uart_wifi_ctrl;
extern const uart_cfg_t g_uart_wifi_cfg;
extern const sci_uart_extended_cfg_t g_uart_wifi_cfg_extend;

#ifndef wifi_uart_callback
void wifi_uart_callback(uart_callback_args_t *p_args);
#endif
FSP_FOOTER
#endif /* WIFI_DEBUG_H_ */
