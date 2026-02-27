/**
 * @file  nvm_config.h
 * @brief NVM Flash 地址映射与常量定义（W25Q64 8MB）
 * @date  2026-02-11
 * @author Ma Ziteng
 */

#ifndef NVM_CONFIG_H_
#define NVM_CONFIG_H_

/* ---- Flash 物理特性 ---- */
#define NVM_FLASH_SIZE      (8 * 1024 * 1024)   /**< 8 MB */
#define NVM_SECTOR_SIZE     4096                 /**< 4 KB 最小擦除 */
#define NVM_BLOCK_SIZE      (64 * 1024)          /**< 64 KB 块擦除 */

/* ---- 内存映射 ---- */
#define NVM_ADDR_SYS_CONFIG 0x00000000           /**< 系统配置区 (1 sector) */
#define NVM_ADDR_MOTION_SEQ 0x00001000           /**< 动作序列区 */
#define NVM_SIZE_MOTION_SEQ (64 * 1024)          /**< 动作序列大小 64 KB */
#define NVM_ADDR_LOG_START  0x00020000           /**< 日志区起始 */
#define NVM_ADDR_LOG_END    0x000FFFFF           /**< 日志区结束 (1MB-1) */

/* ---- 校验与版本 ---- */
#define NVM_MAGIC_WORD      0x52415F4D           /**< "RA_M" */
#define NVM_VERSION         0x00010000           /**< v1.0.0 */

/* ---- 缓冲 ---- */
#define NVM_LOG_CACHE_SIZE  4096                 /**< 日志缓存*/

#endif /* NVM_CONFIG_H_ */
