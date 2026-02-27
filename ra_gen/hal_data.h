/* generated HAL header file - do not edit */
#ifndef HAL_DATA_H_
#define HAL_DATA_H_
#include <stdint.h>
#include "bsp_api.h"
#include "common_data.h"
#include "r_qspi.h"
#include "r_spi_flash_api.h"
#include "r_crc.h"
#include "r_crc_api.h"
FSP_HEADER
extern const spi_flash_instance_t g_qspi0;
extern qspi_instance_ctrl_t g_qspi0_ctrl;
extern const spi_flash_cfg_t g_qspi0_cfg;
extern const crc_instance_t g_crc0;
extern crc_instance_ctrl_t g_crc0_ctrl;
extern const crc_cfg_t g_crc0_cfg;
void hal_entry(void);
void g_hal_init(void);
FSP_FOOTER
#endif /* HAL_DATA_H_ */
