/* generated thread header file - do not edit */
#ifndef SCREEN_INTERACT_H_
#define SCREEN_INTERACT_H_
#include "bsp_api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hal_data.h"
#ifdef __cplusplus
                extern "C" void screen_interact_entry(void * pvParameters);
                #else
extern void screen_interact_entry(void *pvParameters);
#endif
#include "r_iic_master.h"
#include "r_i2c_master_api.h"
#include "r_dmac.h"
#include "r_transfer_api.h"
#include "r_spi.h"
FSP_HEADER
/* I2C Master on IIC Instance. */
extern const i2c_master_instance_t g_i2c_master2;

/** Access the I2C Master instance using these structures when calling API functions directly (::p_api is not used). */
extern iic_master_instance_ctrl_t g_i2c_master2_ctrl;
extern const i2c_master_cfg_t g_i2c_master2_cfg;

#ifndef i2c_master2_callback
void i2c_master2_callback(i2c_master_callback_args_t *p_args);
#endif
/* Transfer on DMAC Instance. */
extern const transfer_instance_t g_transfer7;

/** Access the DMAC instance using these structures when calling API functions directly (::p_api is not used). */
extern dmac_instance_ctrl_t g_transfer7_ctrl;
extern const transfer_cfg_t g_transfer7_cfg;

#ifndef g_spi1_rx_transfer_callback
void g_spi1_rx_transfer_callback(transfer_callback_args_t *p_args);
#endif
/* Transfer on DMAC Instance. */
extern const transfer_instance_t g_transfer1;

/** Access the DMAC instance using these structures when calling API functions directly (::p_api is not used). */
extern dmac_instance_ctrl_t g_transfer1_ctrl;
extern const transfer_cfg_t g_transfer1_cfg;

#ifndef g_spi1_tx_transfer_callback
void g_spi1_tx_transfer_callback(transfer_callback_args_t *p_args);
#endif
/** SPI on SPI Instance. */
extern const spi_instance_t g_spi1;

/** Access the SPI instance using these structures when calling API functions directly (::p_api is not used). */
extern spi_instance_ctrl_t g_spi1_ctrl;
extern const spi_cfg_t g_spi1_cfg;

/** Callback used by SPI Instance. */
#ifndef spi1_callback
void spi1_callback(spi_callback_args_t *p_args);
#endif

#define RA_NOT_DEFINED (1)
#if (RA_NOT_DEFINED == g_transfer1)
    #define g_spi1_P_TRANSFER_TX (NULL)
#else
#define g_spi1_P_TRANSFER_TX (&g_transfer1)
#endif
#if (RA_NOT_DEFINED == g_transfer7)
    #define g_spi1_P_TRANSFER_RX (NULL)
#else
#define g_spi1_P_TRANSFER_RX (&g_transfer7)
#endif
#undef RA_NOT_DEFINED
FSP_FOOTER
#endif /* SCREEN_INTERACT_H_ */
