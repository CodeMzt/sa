/**
 * @file motor_state.c
 * @brief 电机公共镜像实现
 */

#include "motor_state.h"

static inline float clamp_f(float v, float lo, float hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

motor_t g_motors[MOTOR_NUM] = {
    { .id = MOTOR_ID_JOINT1,  .feedback = {0} },
    { .id = MOTOR_ID_JOINT2,  .feedback = {0} },
    { .id = MOTOR_ID_JOINT3,  .feedback = {0} },
    { .id = MOTOR_ID_JOINT4,  .feedback = {0} },
    { .id = MOTOR_ID_JOINT5,  .feedback = {0} },
    { .id = MOTOR_ID_GRIPPER, .feedback = {0} },
};

uint8_t motor_get_index(uint8_t motor_id) {
    for (uint8_t i = 0U; i < MOTOR_NUM; ++i) {
        if (g_motors[i].id == motor_id) {
            return i;
        }
    }
    return MOTOR_NUM;
}

bool motor_id_is_valid(uint8_t motor_id) {
    return motor_get_index(motor_id) < MOTOR_NUM;
}

bool motor_id_is_joint(uint8_t motor_id) {
    return (motor_id >= MOTOR_ID_JOINT1) && (motor_id <= MOTOR_ID_JOINT5);
}

float motor_clamp_position_cmd(uint8_t motor_id, float position_cmd) {
    if (motor_id_is_joint(motor_id)) {
        return clamp_f(position_cmd, -MOTOR_JOINT_POSITION_LIMIT_RAD, MOTOR_JOINT_POSITION_LIMIT_RAD);
    }
    return position_cmd;
}

