/**
 * @file  nvm_types.h
 * @brief NVM data layout for motion and system configuration
 */

#ifndef NVM_TYPES_H_
#define NVM_TYPES_H_

#include <stdbool.h>
#include <stdint.h>

#define MOTION_JOINT_COUNT        5U
#define MOTION_LEGACY_JOINT_COUNT 4U
#define MOTION_CONFIG_VERSION     0x00020000UL
#define MOTION_JOINT_MASK_ALL     0x1FU
#define MOTION_JOINT_MASK_LEGACY  0x0FU
#define MOTION_DEFAULT_CURRENT_LIMIT_MA 4500U

/* ---- Motion configuration ---- */

typedef enum {
    MOVE_ONLY = 0,
    ACTION_GRIP = 1,
    ACTION_RELEASE = 2
} action_type_t;

typedef struct __attribute__((packed)) {
    float angle_m1;
    float angle_m2;
    float angle_m3;
    float angle_m4;
    float angle_m5;
    uint16_t duration_ms;
    uint8_t action;
} motion_frame_t;

#define MAX_FRAMES_PER_SEQ    10U
#define TOTAL_ACTION_GROUPS   7U
#define TEACH_FRAMES_PER_GROUP 3U

typedef struct __attribute__((packed)) {
    uint32_t frame_count;
    uint8_t joint_mask;
    char name[15];
    motion_frame_t frames[MAX_FRAMES_PER_SEQ];
} action_sequence_t;

typedef struct __attribute__((packed)) {
    uint32_t version;
    action_sequence_t groups[TOTAL_ACTION_GROUPS];
    uint32_t crc32;
} motion_config_t;

/* ---- System configuration ---- */

typedef struct __attribute__((packed)) {
    uint32_t magic_word;
    uint32_t version;

    uint8_t mac_addr[6];
    uint8_t ip_addr[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t server_ip[4];
    uint16_t server_port;

    char wifi_ssid[32];
    char wifi_psk[32];
    uint16_t debug_port;

    int16_t angle_min[MOTION_JOINT_COUNT];
    int16_t angle_max[MOTION_JOINT_COUNT];
    uint16_t current_limit[MOTION_JOINT_COUNT];
    uint16_t grip_force_max;

    float zero_offset[MOTION_JOINT_COUNT];

    uint32_t write_count;
    uint32_t crc32;
} sys_config_t;

#endif /* NVM_TYPES_H_ */
