/*
 * surgideliver_arm_canfd.c
 *
 *  Created on: 2026年1月23日
 *      Author: Ma Ziteng
 *
 *  CAN FD 驱动层实现（扩展帧，1Mbps）
 *  适配 RobStride EL05 私有协议（29位扩展帧ID）
 *
 *  AFL 过滤器说明：
 *    RobStride 电机反馈帧为扩展帧，通信类型2（Bit28~24=0x02）
 *    过滤器配置为接收所有扩展帧，由上层协议层按通信类型过滤
 */

#include "drv_canfd.h"
#include "robstride_motor.h"

/* ============================================================
 *  AFL 接收过滤器配置
 *  接收所有扩展帧数据帧（mask全0），路由到 RX FIFO0
 * ============================================================ */
const canfd_afl_entry_t p_canfd0_afl[CANFD_CFG_AFL_CH0_RULE_NUM] = {
    {
        .id = {
            .id = 0x00000000U, /* 匹配ID（mask全0时此值无意义） */
            .frame_type = CAN_FRAME_TYPE_DATA, /* 只接收数据帧 */
            .id_mode = CAN_ID_MODE_EXTENDED /* 扩展帧（29位ID） */
        },
        .mask = {
            .mask_id = 0x00000000U, /* 屏蔽位全0：接受所有ID */
            .mask_frame_type = 0, /* 不过滤帧类型 */
            .mask_id_mode = 1 /* 强制匹配扩展帧模式 */
        },
        .destination = {
            .minimum_dlc = CANFD_MINIMUM_DLC_0,
            .rx_buffer = CANFD_RX_MB_NONE,
            .fifo_select_flags = CANFD_RX_FIFO_0 /* 路由到 RX FIFO0 */
        }
    }
};

/* ============================================================
 *  CAN 回调函数
 *  接收完成时将帧传递给协议层解析
 * ============================================================ */
void canfd0_callback(can_callback_args_t *p_args) {
    switch (p_args->event)
    {
        case CAN_EVENT_RX_COMPLETE:
            /* 将接收帧路由到 RobStride 协议层解析 */
            robstride_parse_feedback(p_args->frame);
            break;

        case CAN_EVENT_TX_COMPLETE:
            break;

        default:
            break;
    }
}

/* ============================================================
 *  初始化函数
 * ============================================================ */

/**
 * @brief  初始化 CANFD0 外设，切换到正常工作模式
 */
fsp_err_t canfd0_init(void){
    fsp_err_t err;

    err = g_canfd0.p_api->open(g_canfd0.p_ctrl, g_canfd0.p_cfg);
    if (err != FSP_SUCCESS) {
        return err;
    }

    err = g_canfd0.p_api->modeTransition(g_canfd0.p_ctrl,
                                          CAN_OPERATION_MODE_NORMAL,
                                          CAN_TEST_MODE_DISABLED);
    return err;
}

/* ============================================================
 *  发送函数
 * ============================================================ */

/**
 * @brief  发送一帧扩展帧 CAN 报文
 * @param  tx_frame  已填好字段的帧结构体
 */
fsp_err_t canfd0_send_ext_frame(can_frame_t tx_frame) {
    return g_canfd0.p_api->write(g_canfd0.p_ctrl, CANFD_TX_MB_0, &tx_frame);
}

/**
 * @brief  检查 CANFD 链接状态
 * @return 链接状态（CANFD_LINK_OK/CANFD_LINK_ERROR）
 */
void canfd_link_check(void) {
    
}
