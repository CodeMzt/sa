/* generated thread header file - do not edit */
#ifndef SERVO_BUS_H_
#define SERVO_BUS_H_
#include "bsp_api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hal_data.h"
#ifdef __cplusplus
                extern "C" void servo_bus_entry(void * pvParameters);
                #else
extern void servo_bus_entry(void *pvParameters);
#endif
#include "r_sci_i2c.h"
#include "r_i2c_master_api.h"
#include "r_dtc.h"
#include "r_transfer_api.h"
#include "r_sci_uart.h"
#include "r_uart_api.h"
FSP_HEADER
extern const i2c_master_cfg_t g_i2c_touch_cfg;
/* I2C on SCI Instance. */
extern const i2c_master_instance_t g_i2c_touch;
#ifndef sci_i2c4_master_callback
void sci_i2c4_master_callback(i2c_master_callback_args_t *p_args);
#endif

extern const sci_i2c_extended_cfg_t g_i2c_touch_cfg_extend;
extern sci_i2c_instance_ctrl_t g_i2c_touch_ctrl;
/* Transfer on DTC Instance. */
extern const transfer_instance_t g_transfer8;

/** Access the DTC instance using these structures when calling API functions directly (::p_api is not used). */
extern dtc_instance_ctrl_t g_transfer8_ctrl;
extern const transfer_cfg_t g_transfer8_cfg;
/* Transfer on DTC Instance. */
extern const transfer_instance_t g_transfer5;

/** Access the DTC instance using these structures when calling API functions directly (::p_api is not used). */
extern dtc_instance_ctrl_t g_transfer5_ctrl;
extern const transfer_cfg_t g_transfer5_cfg;
/** UART on SCI Instance. */
extern const uart_instance_t g_uart_servo;

/** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
extern sci_uart_instance_ctrl_t g_uart_servo_ctrl;
extern const uart_cfg_t g_uart_servo_cfg;
extern const sci_uart_extended_cfg_t g_uart_servo_cfg_extend;

#ifndef uart_servo_callback
void uart_servo_callback(uart_callback_args_t *p_args);
#endif
FSP_FOOTER
#endif /* SERVO_BUS_H_ */
