/* generated thread header file - do not edit */
#ifndef LOG_TASK_H_
#define LOG_TASK_H_
#include "bsp_api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hal_data.h"
#ifdef __cplusplus
                extern "C" void log_task_entry(void * pvParameters);
                #else
extern void log_task_entry(void *pvParameters);
#endif
#include "r_dtc.h"
#include "r_transfer_api.h"
#include "r_sci_uart.h"
#include "r_uart_api.h"
FSP_HEADER
/* Transfer on DTC Instance. */
extern const transfer_instance_t g_transfer2;

/** Access the DTC instance using these structures when calling API functions directly (::p_api is not used). */
extern dtc_instance_ctrl_t g_transfer2_ctrl;
extern const transfer_cfg_t g_transfer2_cfg;
/* Transfer on DTC Instance. */
extern const transfer_instance_t g_transfer0;

/** Access the DTC instance using these structures when calling API functions directly (::p_api is not used). */
extern dtc_instance_ctrl_t g_transfer0_ctrl;
extern const transfer_cfg_t g_transfer0_cfg;
/** UART on SCI Instance. */
extern const uart_instance_t g_uart_log;

/** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
extern sci_uart_instance_ctrl_t g_uart_log_ctrl;
extern const uart_cfg_t g_uart_log_cfg;
extern const sci_uart_extended_cfg_t g_uart_log_cfg_extend;

#ifndef uart_log_callback
void uart_log_callback(uart_callback_args_t *p_args);
#endif
FSP_FOOTER
#endif /* LOG_TASK_H_ */
