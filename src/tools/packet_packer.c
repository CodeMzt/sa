/*
 * packet_packer.c
 *
 *  Created on: 2026年1月25日
 *      Author: Ma Ziteng
 */

#include "packet_packer.h"

/**
 * @brief 打包电机数据 (从全局 g_motors 数组)
 * @param out_buf 指向发送缓冲区的指针
 */
void pack_motor_data(uint8_t *out_buf) {
    motor_packet_t pkt;

    pkt.header[0] = 0xA5;
    pkt.header[1] = 0x5A;

    /* 从 g_motors 数组提取角度和力矩数据 */
    for (int i = 0; i < MOTOR_COUNT; i++) {
        pkt.angles[i] = g_motors[i].feedback.position;
        pkt.torques[i] = g_motors[i].feedback.torque;
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
