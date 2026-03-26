/**
 * @file can_comms_entry.h
 * @brief 旧版 CAN 运控任务兼容头文件
 */

#ifndef CAN_COMMS_ENTRY_H_
#define CAN_COMMS_ENTRY_H_

/* -------------------------------------------------------------------------- */
/* 旧接口说明                                                                  */
/*                                                                            */
/* 本头文件中的命令枚举与 CAN 控制包结构属于旧版 CAN 运控链路的遗留接口。      */
/* 当前串口舵机重构后，这些结构不再参与新的运行时控制，仅保留以避免旧引用编译   */
/* 失败。新增功能请不要再继续依赖这里的包格式。                                 */
/* -------------------------------------------------------------------------- */

#include "can_comms.h"

enum {
    MOTOR_DISABLE,
    MOTOR_ENABLE,
    MOTOR_SET_ZERO,
    MOTOR_CLEAR_ERR
};

/**
 * @brief 旧版 CAN 单电机控制数据包
 * @note  仅作兼容保留，当前串口舵机方案不再使用该结构进行控制。
 */
typedef struct {
    uint8_t command_mode;
    float target_angle;
} motor_cmd_packet;

/**
 * @brief 旧版 CAN 消息封装
 * @note  仅作兼容保留，当前运行时控制路径已经不再使用。
 */
typedef struct {
    uint8_t id;
    motor_cmd_packet data;
} can_msg_t;

#endif /* CAN_COMMS_ENTRY_H_ */
