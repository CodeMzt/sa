/**
 * @file  nvm_manager.h
 * @brief NVM 管理器公共接口（QSPI Flash 读写、日志环形缓冲）
 * @date  2026-02-11
 * @author Ma Ziteng
 */

#ifndef NVM_MANAGER_H_
#define NVM_MANAGER_H_

#include "hal_data.h"
#include "nvm_types.h"

/** @brief 初始化 QSPI 并从 Flash 加载配置到 RAM（空白则写默认值） */
fsp_err_t nvm_init(void);

/** @brief 获取系统配置指针（RAM 镜像，零拷贝） */
const sys_config_t* nvm_get_sys_config(void);

/** @brief 保存系统配置到 Flash（阻塞，~几十 ms） */
fsp_err_t nvm_save_sys_config(const sys_config_t *new_cfg);

/** @brief 获取动作配置指针 */
const motion_config_t* nvm_get_motion_config(void);

/** @brief 保存动作配置到 Flash */
fsp_err_t nvm_save_motion_config(const motion_config_t *new_motion);

/** @brief 追加日志到环形缓冲（满时自动写 Flash） */
void nvm_append_log(char *log_msg);

/** @brief 读取 Flash 日志区 */
void nvm_read_logs(uint32_t addr_offset, uint8_t *buffer, uint32_t length);

/** @brief 获取当前日志写入偏移（相对日志区起始） */
uint32_t nvm_get_log_offset(void);

/** @brief 擦除全部日志区 */
fsp_err_t nvm_clear_logs(void);

/** @brief 强制将日志缓存写入 Flash */
fsp_err_t nvm_save_logs(void);

#endif /* NVM_MANAGER_H_ */
