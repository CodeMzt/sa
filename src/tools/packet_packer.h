/*
 * packet_packer.h
 *
 *  Created on: 2026年1月25日
 *      Author: Ma Ziteng
 */

#ifndef PACKET_PACKER_H_
#define PACKET_PACKER_H_

#include <stdint.h>
#include <string.h> // for memcpy
#include "shared_data.h"

#define MOTOR_COUNT 5
#define PACKET_SIZE 44 /* 2 + 20 + 20 + 1 + 1 */

/* 数据包结构体 */
typedef struct __attribute__((packed)) {
    uint8_t  header[2];           
    float    angles[MOTOR_COUNT]; 
    float    torques[MOTOR_COUNT];
    uint8_t  checksum;            
    uint8_t  tail;
} motor_packet_t;

/**
 * @brief 打包电机数据（从 g_motors 数组）
 * @param out_buf 指向发送缓冲区的指针
 */
void pack_motor_data(uint8_t *out_buf);

#endif /* PACKET_PACKER_H_ */
