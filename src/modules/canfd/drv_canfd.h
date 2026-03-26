/**
 * @file drv_canfd.h
 * @brief CAN FD 驱动头文件（扩展帧，1Mbps）
 * @date 2026-01-23
 * @author Ma Ziteng
 */

#ifndef SURGIDELIVER_ARM_CANFD_H_
#define SURGIDELIVER_ARM_CANFD_H_

/* -------------------------------------------------------------------------- */
/* 旧模块说明                                                                  */
/*                                                                            */
/* 本模块属于旧版 CANFD 电机通信驱动。当前运行时运控已切换到串口舵机链路，     */
/* 该模块仅保留供历史代码、旧数据结构和兼容引用使用，不再作为实际电机通信入口。 */
/* -------------------------------------------------------------------------- */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "can_comms.h"

extern volatile uint16_t g_can_rx_count;

/* 力传感器接收帧：扩展帧 + 8字节原始数据 */
#define CANFD_FORCE_SENSOR_CMD_DATA   (0x1AU)
#define CANFD_FORCE_SENSOR_CAN_ID     (0x10U)
#define CANFD_FORCE_SENSOR_HOST_ID    (0xFDU)
#define CANFD_FORCE_SENSOR_DATA_LEN   (8U)

typedef struct {
	uint8_t  sensor_can_id;
	uint8_t  host_can_id;
	uint8_t  sub_type;
	uint8_t  values[CANFD_FORCE_SENSOR_DATA_LEN];
	bool     valid;
	uint32_t rx_count;
	uint32_t last_update_tick;
} canfd_force_sensor_data_t;

extern volatile canfd_force_sensor_data_t g_force_sensor_data;

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
