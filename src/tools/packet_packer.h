/**
 * @file    packet_packer.h
 * @brief   数据包打包工具头文件（协议结构定义）
 * @date    2026-01-25
 * @author  Ma Ziteng
 */

#ifndef PACKET_PACKER_H_
#define PACKET_PACKER_H_

#include <stdint.h>
#include <string.h> // for memcpy
#include "shared_data.h"

#define MOTOR_PACKET_MOTOR_COUNT   MOTOR_ACTIVE_MOTOR_NUM
#define MOTOR_PACKET_HEADER_SIZE   2U
#define MOTOR_PACKET_FLOAT_SIZE    4U
#define MOTOR_PACKET_CHECKSUM_SIZE 1U
#define MOTOR_PACKET_TAIL_SIZE     1U
#define PACKET_SIZE                (MOTOR_PACKET_HEADER_SIZE + \
                                   (MOTOR_PACKET_MOTOR_COUNT * MOTOR_PACKET_FLOAT_SIZE * 2U) + \
                                   MOTOR_PACKET_CHECKSUM_SIZE + \
                                   MOTOR_PACKET_TAIL_SIZE)

/* 数据包结构体 */
typedef struct __attribute__((packed)) {
    uint8_t  header[2];           
    float    angles[MOTOR_PACKET_MOTOR_COUNT];
    float    torques[MOTOR_PACKET_MOTOR_COUNT];
    uint8_t  checksum;            
    uint8_t  tail;
} motor_packet_t;

/**
 * @brief 打包电机数据（从 g_motors 数组）
 * @param out_buf 指向发送缓冲区的指针
 */
void pack_motor_data(uint8_t *out_buf);

#endif /* PACKET_PACKER_H_ */
