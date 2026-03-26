/**
 * @file  nvm_manager.h
 * @brief NVM manager public APIs
 */

#ifndef NVM_MANAGER_H_
#define NVM_MANAGER_H_

#include "hal_data.h"
#include "nvm_types.h"
#include <stdbool.h>

fsp_err_t nvm_init(void);

const sys_config_t *nvm_get_sys_config(void);
fsp_err_t nvm_save_sys_config(const sys_config_t *new_cfg);

const motion_config_t *nvm_get_motion_config(void);
bool nvm_is_action_sequence_valid(const action_sequence_t *seq, bool allow_empty);
bool nvm_is_motion_config_valid(const motion_config_t *cfg);
fsp_err_t nvm_save_motion_config(const motion_config_t *new_motion);

void nvm_append_log(char *log_msg);
void nvm_read_logs(uint32_t addr_offset, uint8_t *buffer, uint32_t length);
uint32_t nvm_get_log_offset(void);
fsp_err_t nvm_clear_logs(void);
fsp_err_t nvm_save_logs(void);

#endif /* NVM_MANAGER_H_ */
