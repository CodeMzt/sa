/*
 * surgideliver_arm_canfd.h
 *
 *  Created on: 2026年1月23日
 *      Author: Ma Ziteng
 *
 *  CAN FD 驱动层（扩展帧，1Mbps）
 *  适配 RobStride EL05 私有协议（29位扩展帧ID）
 */

#ifndef SURGIDELIVER_ARM_CANFD_H_
#define SURGIDELIVER_ARM_CANFD_H_

#include <stdint.h>
#include <string.h>
#include "can_comms.h"

extern volatile uint16_t count_can;

/**
 * @brief  初始化 CANFD0 外设，切换到正常工作模式
 * @return FSP_SUCCESS 或错误码
 */
fsp_err_t canfd0_init(void);

/**
 * @brief  发送一帧扩展帧 CAN 报文（供协议层调用）
 * @param  tx_frame  已填好 id/id_mode/type/dlc/data 的帧结构体
 * @return FSP_SUCCESS 或错误码
 */
fsp_err_t canfd0_send_ext_frame(can_frame_t tx_frame);

/**
 * @brief  CAN 接收/发送回调（由 FSP 驱动自动调用，用户勿直接调用）
 */
void canfd0_callback(can_callback_args_t *p_args);

/**
 * @brief  基于接收帧活动进行 CAN 链路联通检查
 */
void canfd_link_check(void);

#endif /* SURGIDELIVER_ARM_CANFD_H_ */
