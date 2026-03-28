/**
 * @file    packet_packer.c
 * @brief   数据包打包工具实现（将电机反馈数据序列化为二进制包）
 * @date    2026-01-25
 * @author  Ma Ziteng
 */

#include "packet_packer.h"
#include "motion_ctrl.h"

static const uint8_t k_uploaded_motor_ids[MOTOR_PACKET_MOTOR_COUNT] = {
    MOTOR_ID_JOINT1,
    MOTOR_ID_JOINT2,
    MOTOR_ID_JOINT3,
    MOTOR_ID_JOINT4,
    MOTOR_ID_JOINT5,
    MOTOR_ID_GRIPPER
};

/**
 * @brief 打包电机数据 (从全局 g_motors 数组)
 * @param out_buf 指向发送缓冲区的指针
 */
void pack_motor_data(uint8_t *out_buf) {
    motor_packet_t pkt = {0};
    bool use_playback_targets = (g_motion_ctrl.state == MOTION_STATE_PLAYBACK) &&
                                g_motion_ctrl.playback_upload_q_valid;

    pkt.header[0] = 0xA5;
    pkt.header[1] = 0x5A;

    /* 从 g_motors 数组提取角度和力矩数据 */
    for (int i = 0; i < (int) MOTOR_PACKET_MOTOR_COUNT; i++) {
        uint8_t motor_index = motor_get_index(k_uploaded_motor_ids[i]);
        if (motor_index < MOTOR_NUM) {
            bool use_joint_target = use_playback_targets && (i < (int) MOTOR_ACTIVE_JOINT_NUM);
            pkt.angles[i] = use_joint_target ? g_motion_ctrl.playback_upload_q[i]
                                             : g_motors[motor_index].feedback.position;
            pkt.torques[i] = g_motors[motor_index].feedback.torque;
        } else {
            pkt.angles[i] = 0.0f;
            pkt.torques[i] = 0.0f;
        }
    }

    uint8_t sum = 0;
    uint8_t *raw_ptr = (uint8_t *)&pkt;
    for (int i = 2; i < (int)(PACKET_SIZE - 2); i++) {
        sum += raw_ptr[i];
    }
    pkt.checksum = sum;
    pkt.tail = 0xED;

    memcpy(out_buf, &pkt, PACKET_SIZE);
}
