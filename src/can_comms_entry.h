/**
 * @file  can_comms_entry.h
 * @brief CAN 通信任务入口头文件
 * @date  2026-01-24
 * @author Ma Ziteng
 */

#ifndef CAN_COMMS_ENTRY_H_
#define CAN_COMMS_ENTRY_H_

#include "can_comms.h"

enum {
    MOTOR_DISABLE,
    MOTOR_ENABLE,
    MOTOR_SET_ZERO,
    MOTOR_CLEAR_ERR
};

/* 主控发送给从机的控制包结构 */
typedef struct {
    uint8_t command_mode;
    float target_angle;   // 目标角度
} motor_cmd_packet;

typedef struct {
    uint8_t id; // 注意，这里的id是motor_id
    motor_cmd_packet data;
} can_msg_t;

#endif /* CAN_COMMS_ENTRY_H_ */
