/**
 * @file  nvm_manager.c
 * @brief NVM 管理器实现（QSPI Flash + RAM 缓存 + 日志环形缓冲）
 * @date  2026-02-11
 * @author Ma Ziteng
 */

#include "nvm_manager.h"
#include "nvm_config.h"
#include "sys_log.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

#define NVM_CFG_SAVE_LOCK_TIMEOUT_MS    (200U)

/* ---- RAM 缓存 ---- */
static sys_config_t    g_sys_cfg;
static motion_config_t g_motion_cfg;
static volatile uint8_t g_nvm_init_state = 0U; /* 0:未初始化 1:初始化中 2:已完成 */
extern SemaphoreHandle_t g_cfg_save_mutex;

/* ---- 日志缓冲 ---- */
#define LOG_CACHE_SIZE  256               /**< 页单位 */
static uint8_t  g_log_cache[LOG_CACHE_SIZE];
static uint16_t g_log_idx   = 0;          /**< 缓存内偏移 */
static uint32_t g_flash_wr  = NVM_ADDR_LOG_START;  /**< Flash 下一写入位置 */

/* 辅助宏：扇区对齐 */
#define SECTOR_BASE(a)  ((a) & ~(NVM_SECTOR_SIZE - 1))
#define SECTOR_END(a)   (SECTOR_BASE(a) + NVM_SECTOR_SIZE)

/* ---- 内部函数 ---- */
static void        load_defaults(void);
static fsp_err_t   qspi_erase_write(uint32_t addr, uint8_t *data, uint32_t len);
static fsp_err_t   qspi_write_only(uint32_t addr, uint8_t *data, uint32_t len);
static fsp_err_t   qspi_read(uint32_t addr, uint8_t *data, uint32_t len);
void               wait_ready(void);
static uint32_t    calc_crc32(const uint8_t *data, uint32_t len);
static void        scan_log_head(void);
static uint32_t    get_device_id(void);

/* ===========================================================
 *  初始化
 * =========================================================== */

/**
 * @brief 上电初始化：打开 CRC/QSPI，加载配置，扫描日志头
 * @return FSP_SUCCESS 表示成功，其他表示错误
 */
fsp_err_t nvm_init(void) {
    if (g_nvm_init_state == 2U) return FSP_SUCCESS;

    taskENTER_CRITICAL();
    if (g_nvm_init_state == 0U) {
        g_nvm_init_state = 1U;
        taskEXIT_CRITICAL();
    } else {
        taskEXIT_CRITICAL();
        while (g_nvm_init_state == 1U) vTaskDelay(pdMS_TO_TICKS(10));
        return (g_nvm_init_state == 2U) ? FSP_SUCCESS : FSP_ERR_INTERNAL;
    }

    fsp_err_t err;

    err = g_crc0.p_api->open(g_crc0.p_ctrl, g_crc0.p_cfg);
    if (err != FSP_SUCCESS) {
        g_nvm_init_state = 0U;
        return err;
    }

    err = g_qspi0.p_api->open(g_qspi0.p_ctrl, g_qspi0.p_cfg);
    if (err != FSP_SUCCESS) {
        g_nvm_init_state = 0U;
        return err;
    }

    if (g_cfg_save_mutex == NULL) {
        g_cfg_save_mutex = xSemaphoreCreateMutex();
        if (g_cfg_save_mutex == NULL) {
            g_nvm_init_state = 0U;
            return FSP_ERR_OUT_OF_MEMORY;
        }
    }

    /* 系统配置 */
    err = qspi_read(NVM_ADDR_SYS_CONFIG, (uint8_t *)&g_sys_cfg, sizeof(sys_config_t));
    if (err != FSP_SUCCESS) {
        g_nvm_init_state = 0U;
        return err;
    }

    uint32_t crc = calc_crc32((uint8_t *)&g_sys_cfg, sizeof(sys_config_t) - 4);
    if (g_sys_cfg.magic_word != NVM_MAGIC_WORD || g_sys_cfg.crc32 != crc) {
        LOG_D("Load default config.");
        load_defaults();
        err = nvm_save_sys_config(&g_sys_cfg);
        if (err != FSP_SUCCESS) {
            g_nvm_init_state = 0U;
            return err;
        }
    }

    /* 动作配置 */
    err = qspi_read(NVM_ADDR_MOTION_SEQ, (uint8_t *)&g_motion_cfg, sizeof(motion_config_t));
    if (err != FSP_SUCCESS) {
        g_nvm_init_state = 0U;
        return err;
    }

    uint32_t motion_crc = calc_crc32((uint8_t *)&g_motion_cfg, sizeof(motion_config_t) - 4);
    if (g_motion_cfg.crc32 != motion_crc) {
        LOG_D("Motion config invalid or erased, reset to default.");
        memset(&g_motion_cfg, 0, sizeof(motion_config_t));
        nvm_save_motion_config(&g_motion_cfg);
    }

    scan_log_head();
    LOG_D("Device ID: 0x%.8lx", (unsigned long)get_device_id());

    g_nvm_init_state = 2U;
    return FSP_SUCCESS;
}

/* ===========================================================
 *  配置存取
 * =========================================================== */

/**
 * @brief 获取系统配置
 * @return 系统配置指针
 */
const sys_config_t* nvm_get_sys_config(void) { return &g_sys_cfg; }

/**
 * @brief 保存系统配置
 * @param new_cfg 新的系统配置
 * @return FSP_SUCCESS 表示成功
 */
fsp_err_t nvm_save_sys_config(const sys_config_t *new_cfg) {
    if (new_cfg == NULL) return FSP_ERR_ASSERTION;

    if (g_cfg_save_mutex != NULL) {
        if (xSemaphoreTake(g_cfg_save_mutex, pdMS_TO_TICKS(NVM_CFG_SAVE_LOCK_TIMEOUT_MS)) != pdTRUE) {
            LOG_W("nvm_save_sys_config: cfg mutex timeout");
            return FSP_ERR_TIMEOUT;
        }
    }

    memcpy(&g_sys_cfg, new_cfg, sizeof(sys_config_t));
    g_sys_cfg.magic_word = NVM_MAGIC_WORD;
    g_sys_cfg.write_count++;
    g_sys_cfg.crc32 = calc_crc32((uint8_t *)&g_sys_cfg, sizeof(sys_config_t) - 4);

    fsp_err_t err = qspi_erase_write(NVM_ADDR_SYS_CONFIG, (uint8_t *)&g_sys_cfg, sizeof(sys_config_t));

    if (g_cfg_save_mutex != NULL) {
        (void)xSemaphoreGive(g_cfg_save_mutex);
    }

    return err;
}

/**
 * @brief 获取动作配置
 * @return 动作配置指针
 */
const motion_config_t* nvm_get_motion_config(void) { return &g_motion_cfg; }

/**
 * @brief 保存动作配置
 * @param new_motion 新的动作配置
 * @return FSP_SUCCESS 表示成功
 */
fsp_err_t nvm_save_motion_config(const motion_config_t *new_motion) {
    if (new_motion == NULL) return FSP_ERR_ASSERTION;

    if (g_cfg_save_mutex != NULL) {
        if (xSemaphoreTake(g_cfg_save_mutex, pdMS_TO_TICKS(NVM_CFG_SAVE_LOCK_TIMEOUT_MS)) != pdTRUE) {
            LOG_W("nvm_save_motion_config: cfg mutex timeout");
            return FSP_ERR_TIMEOUT;
        }
    }

    memcpy(&g_motion_cfg, new_motion, sizeof(motion_config_t));
    g_motion_cfg.crc32 = calc_crc32((uint8_t *)&g_motion_cfg, sizeof(motion_config_t) - 4);

    fsp_err_t err = qspi_erase_write(NVM_ADDR_MOTION_SEQ, (uint8_t *)&g_motion_cfg, sizeof(motion_config_t));

    if (g_cfg_save_mutex != NULL) {
        (void)xSemaphoreGive(g_cfg_save_mutex);
    }

    return err;
}

/* ===========================================================
 *  日志系统（字节级精确管理 + 追加写入）
 * =========================================================== */

/**
 * @brief 内部：将缓存 flush 到 Flash（处理跨扇区）
 * @return FSP_SUCCESS 表示成功
 */
static fsp_err_t flush_log_cache(void) {
    if (g_log_idx == 0) return FSP_SUCCESS;

    uint32_t wr_addr = g_flash_wr;
    uint8_t *src = g_log_cache;
    uint32_t to_write = g_log_idx;
    fsp_err_t err = FSP_SUCCESS;

    // 预擦除下一个扇区
    uint32_t ea = (wr_addr / NVM_SECTOR_SIZE + 1) * NVM_SECTOR_SIZE;
    wait_ready();
    err = g_qspi0.p_api->erase(g_qspi0.p_ctrl, (uint8_t *)(QSPI_DEVICE_START_ADDRESS + ea), NVM_SECTOR_SIZE);
    if (err != FSP_SUCCESS) return err;

    // 分页写入（256字节对齐，不能跨页）
    while (to_write > 0) {
        uint32_t page_off = wr_addr % 256;
        uint32_t page_remain = 256 - page_off;
        uint32_t chunk = (to_write < page_remain) ? to_write : page_remain;

        err = qspi_write_only(wr_addr, src, chunk);
        if (err != FSP_SUCCESS) return err;

        wr_addr += chunk;
        src += chunk;
        to_write -= chunk;

        if (wr_addr >= NVM_ADDR_LOG_END) wr_addr = NVM_ADDR_LOG_START;
    }

    g_flash_wr = wr_addr;
    g_log_idx = 0;
    return FSP_SUCCESS;
}

/**
 * @brief 追加日志到缓存
 * @param log_msg 日志消息
 */
void nvm_append_log(char *log_msg) {
    uint16_t len = (uint16_t)(strlen(log_msg) + 1);

    /* 缓存放不下则先 flush */
    if (g_log_idx + len > LOG_CACHE_SIZE) flush_log_cache();

    /* 如果单条日志超过缓存大小，截断 */
    if (len > LOG_CACHE_SIZE) len = LOG_CACHE_SIZE;

    memcpy(&g_log_cache[g_log_idx], log_msg, len);
    g_log_idx = (uint16_t)(g_log_idx + len);
}

/**
 * @brief 读取日志
 * @param addr_offset 地址偏移
 * @param buffer 输出缓冲区
 * @param length 读取长度
 */
void nvm_read_logs(uint32_t addr_offset, uint8_t *buffer, uint32_t length) {
    qspi_read(NVM_ADDR_LOG_START + addr_offset, buffer, length);
}

/**
 * @brief 获取日志当前偏移
 * @return 日志偏移量（包含缓存中未写入的）
 */
uint32_t nvm_get_log_offset(void) {
    return (g_flash_wr - NVM_ADDR_LOG_START) + g_log_idx;
}

/**
 * @brief 清除日志
 * @return FSP_SUCCESS 表示成功
 */
fsp_err_t nvm_clear_logs(void) {
    /* 只擦除首扇区并重置指针 */
    wait_ready();
    fsp_err_t err = g_qspi0.p_api->erase(g_qspi0.p_ctrl,
            (uint8_t *)(QSPI_DEVICE_START_ADDRESS + NVM_ADDR_LOG_START), NVM_SECTOR_SIZE);
    g_flash_wr = NVM_ADDR_LOG_START;
    g_log_idx  = 0;
    memset(g_log_cache, 0, LOG_CACHE_SIZE);
    return err;
}

fsp_err_t nvm_save_logs(void) {
    return flush_log_cache();
}

/* ===========================================================
 *  内部辅助
 * =========================================================== */

/**
 * @brief 等待 QSPI 写操作完成
 */
void wait_ready(void) {
    spi_flash_status_t st = { .write_in_progress = true };
    while (st.write_in_progress) {
        g_qspi0.p_api->statusGet(g_qspi0.p_ctrl, &st);
        vTaskDelay(1);
    }
}

/**
 * @brief 加载出厂默认系统配置
 */
static void load_defaults(void) {
    memset(&g_sys_cfg, 0, sizeof(sys_config_t));
    g_sys_cfg.magic_word = NVM_MAGIC_WORD;
    g_sys_cfg.version    = NVM_VERSION;

    uint8_t ip[]  = {192, 168, 31, 250}; memcpy(g_sys_cfg.ip_addr, ip, 4);
    uint8_t msk[] = {255, 255, 255, 0};  memcpy(g_sys_cfg.netmask, msk, 4);
    uint8_t gw[]  = {192, 168, 31, 1};   memcpy(g_sys_cfg.gateway, gw, 4);
    uint8_t srv[] = {192, 168, 31, 229}; memcpy(g_sys_cfg.server_ip, srv, 4);
    g_sys_cfg.server_port = 2333;
    g_sys_cfg.debug_port  = 8080;

    strncpy(g_sys_cfg.wifi_ssid, "SurgicalArm_Debug", sizeof(g_sys_cfg.wifi_ssid) - 1);
    strncpy(g_sys_cfg.wifi_psk,  "fdudebug",          sizeof(g_sys_cfg.wifi_psk) - 1);

    g_sys_cfg.angle_min[0] = -9000;  g_sys_cfg.angle_max[0] =  9000;
    g_sys_cfg.angle_min[1] = -4500;  g_sys_cfg.angle_max[1] = 13500;
    g_sys_cfg.angle_min[2] = -6000;  g_sys_cfg.angle_max[2] = 12000;
    g_sys_cfg.angle_min[3] = -6000;  g_sys_cfg.angle_max[3] = 12000;

    g_sys_cfg.current_limit[0] = 100;
    g_sys_cfg.current_limit[1] = 100;
    g_sys_cfg.current_limit[2] = 100;
    g_sys_cfg.current_limit[3] = 100;
    g_sys_cfg.grip_force_max   = 500;
}

/**
 * @brief 擦除后写入 Flash（自动对齐扇区）
 * @param addr 目标地址
 * @param data 数据指针
 * @param len  数据长度
 * @return FSP_SUCCESS 表示成功
 */
static fsp_err_t qspi_erase_write(uint32_t addr, uint8_t *data, uint32_t len) {
    uint32_t erase_start = addr / NVM_SECTOR_SIZE * NVM_SECTOR_SIZE;
    uint32_t erase_end = (addr + len - 1) / NVM_SECTOR_SIZE * NVM_SECTOR_SIZE;
    fsp_err_t err;
    for (uint32_t ea = erase_start; ea <= erase_end; ea += NVM_SECTOR_SIZE) {
        wait_ready();
        err = g_qspi0.p_api->erase(g_qspi0.p_ctrl, (uint8_t *)(QSPI_DEVICE_START_ADDRESS + ea), NVM_SECTOR_SIZE);
        if (err != FSP_SUCCESS) return err;
    }

    uint32_t wr_addr = addr;
    uint8_t *src = data;
    uint32_t to_write = len;
    while (to_write > 0) {
        uint32_t page_off = wr_addr % 256;
        uint32_t page_remain = 256 - page_off;
        uint32_t chunk = (to_write < page_remain) ? to_write : page_remain;
        err = qspi_write_only(wr_addr, src, chunk);
        if (err != FSP_SUCCESS) return err;
        wr_addr += chunk;
        src += chunk;
        to_write -= chunk;
    }
    return FSP_SUCCESS;
}

/**
 * @brief 仅写入 Flash（不擦除，用于追加写入已擦除区域）
 * @param addr 目标地址
 * @param data 数据指针
 * @param len  数据长度
 * @return FSP_SUCCESS 表示成功
 */
static fsp_err_t qspi_write_only(uint32_t addr, uint8_t *data, uint32_t len) {
    fsp_err_t err = FSP_SUCCESS;
    uint32_t wr_addr = addr;
    uint8_t *src = data;
    uint32_t to_write = len;
    while (to_write > 0) {
        uint32_t page_off = wr_addr % 256;
        uint32_t page_remain = 256 - page_off;
        uint32_t chunk = (to_write < page_remain) ? to_write : page_remain;
        wait_ready();
        err = g_qspi0.p_api->write(g_qspi0.p_ctrl, src, (uint8_t *)(wr_addr + QSPI_DEVICE_START_ADDRESS), chunk);
        if (err != FSP_SUCCESS) return err;
        wr_addr += chunk;
        src += chunk;
        to_write -= chunk;
    }
    return FSP_SUCCESS;
}

/**
 * @brief Memory-mapped 读取
 * @param addr 源地址
 * @param data 数据缓冲区
 * @param len  读取长度
 * @return FSP_SUCCESS 表示成功
 */
static fsp_err_t qspi_read(uint32_t addr, uint8_t *data, uint32_t len) {
    memcpy(data, (void *)(addr + QSPI_DEVICE_START_ADDRESS), len);
    return FSP_SUCCESS;
}

/**
 * @brief 使用硬件 CRC 外设计算 CRC-32
 * @param data 数据指针
 * @param len  数据长度
 * @return CRC32 校验值
 */
static uint32_t calc_crc32(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    crc_input_t in = { .crc_seed = 0xFFFFFFFF, .num_bytes = len, .p_input_buffer = data };
    g_crc0.p_api->calculate(g_crc0.p_ctrl, &in, &crc);
    return crc;
}

/**
 * @brief 扫描日志区找到首个 0xFF 位置（字节级精确）
 */
static void scan_log_head(void) {
    uint8_t *base = (uint8_t *)(QSPI_DEVICE_START_ADDRESS + NVM_ADDR_LOG_START);
    uint32_t log_size = NVM_ADDR_LOG_END - NVM_ADDR_LOG_START;

    /* 按扇区扫描，找到第一个非全满扇区 */
    for (uint32_t sector = 0; sector < log_size; sector += NVM_SECTOR_SIZE) {
        uint8_t *p_sector = base + sector;
        /* 检查扇区首字节 */
        if (*p_sector == 0xFF) {
            /* 扇区已擦除，从这里开始 */
            g_flash_wr = NVM_ADDR_LOG_START + sector;
            return;
        }
        /* 扇区内逐字节找 0xFF */
        for (uint32_t i = 0; i < NVM_SECTOR_SIZE; i++) {
            if (p_sector[i] == 0xFF) {
                g_flash_wr = NVM_ADDR_LOG_START + sector + i;
                return;
            }
        }
    }

    /* 日志区全满，从头开始，擦除首扇区 */
    g_flash_wr = NVM_ADDR_LOG_START;
    wait_ready();
    g_qspi0.p_api->erase(g_qspi0.p_ctrl,
            (uint8_t *)(QSPI_DEVICE_START_ADDRESS + NVM_ADDR_LOG_START), NVM_SECTOR_SIZE);
}

/**
 * @brief 读取 Flash JEDEC ID（0x9F）
 * @return 设备 ID
 */
static uint32_t get_device_id(void) {
    uint8_t cmd = 0x9F, id[3] = {0};
    g_qspi0.p_api->directWrite(g_qspi0.p_ctrl, &cmd, 1, true);
    g_qspi0.p_api->directRead(g_qspi0.p_ctrl, id, 3);
    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
}
