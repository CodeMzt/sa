/**
 * @file  nvm_types.h
 * @brief NVM 数据类型定义（动作序列 / 系统配置）
 * @date  2026-02-11
 * @author Ma Ziteng
 */

#ifndef NVM_TYPES_H_
#define NVM_TYPES_H_

#include <stdint.h>
#include <stdbool.h>

/* ---- 动作序列 (Motion) ---- */

/** @brief 关键帧动作类型 */
typedef enum {
    MOVE_ONLY,          /**< 纯移动（过渡点） */
    ACTION_GRIP,        /**< 到达后抓取 */
    ACTION_RELEASE      /**< 到达后释放 */
} action_type_t;

/** @brief 单个关键帧 */
typedef struct __attribute__((packed)) {
    float    angle_m1;      /**< 关节1角度 */
    float    angle_m2;      /**< 关节2角度 */
    float    angle_m3;      /**< 关节3角度 */
    float    angle_m4;      /**< 关节4角度 */
    uint16_t duration_ms;   /**< 运动耗时 ms */
    uint8_t  action;        /**< action_type_t */
} motion_frame_t;

#define MAX_FRAMES_PER_SEQ  10  /**< 单组最大帧数 */
#define TOTAL_ACTION_GROUPS  7  /**< 动作组数量 */
#define TEACH_FRAMES_PER_GROUP  3  /**< 示教模式每组固定帧数 */

/** @brief 单个动作序列 */
typedef struct __attribute__((packed)) {
    uint32_t       frame_count;
    char           name[16];
    motion_frame_t frames[MAX_FRAMES_PER_SEQ];
} action_sequence_t;

/** @brief 动作配置块（含 CRC） */
typedef struct __attribute__((packed)) {
    /**
     * 七组动作说明：
     * 第1、2组代表抓取和释放FORCEPS（镊子）
     * 第3、4组代表抓取和释放HEMOSTAT（止血钳）
     * 第5、6组代表抓取和释放SCALPEL（解剖刀）
     * 第7组代表回复初始位置
     */
    action_sequence_t groups[TOTAL_ACTION_GROUPS];
    uint32_t crc32;
} motion_config_t;

/* ---- 系统配置 (System Config) ---- */

/** @brief 系统配置结构体（存储于 Flash 首扇区） */
typedef struct __attribute__((packed)) {
    /* Header */
    uint32_t magic_word;
    uint32_t version;

    /* 以太网 */
    uint8_t  mac_addr[6];
    uint8_t  ip_addr[4];        /**< 本机 IP */
    uint8_t  netmask[4];
    uint8_t  gateway[4];
    uint8_t  server_ip[4];      /**< 上位机 IP */
    uint16_t server_port;

    /* WiFi 调试 */
    char     wifi_ssid[32];
    char     wifi_psk[32];
    uint16_t debug_port;

    /* 安全限位 定点数，乘以100存储 */
    int16_t  angle_min[4];
    int16_t  angle_max[4];
    uint16_t current_limit[4];
    uint16_t grip_force_max;

    /* 校准 */
    float    zero_offset[4];    /**< 零点偏移 */

    /* Footer */
    uint32_t write_count;       /**< 累计写入次数 */
    uint32_t crc32;
} sys_config_t;

#endif /* NVM_TYPES_H_ */
