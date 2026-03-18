/**
 * @file drv_canfd.c
 * @brief CAN FD 驱动实现（RobStride 电机私有协议适配，1Mbps 扩展帧）
 * @date 2026-01-23
 * @author Ma Ziteng
 */

#include "drv_canfd.h"
#include "robstride_motor.h"
#include "shared_data.h"
#include "sys_log.h"

volatile uint16_t g_can_rx_count;
volatile canfd_force_sensor_data_t g_force_sensor_data = {
    .sensor_can_id = CANFD_FORCE_SENSOR_CAN_ID,
    .host_can_id = CANFD_FORCE_SENSOR_HOST_ID,
    .sub_type = 0U,
    .values = {0},
    .valid = false,
    .rx_count = 0U,
    .last_update_tick = 0U,
};

#define CAN_LINK_CHECK_WINDOW_MS     (200U)
#define CAN_LINK_MISS_THRESHOLD      (5U)

/* ============================================================
 *  内部接收分发辅助函数
 * ============================================================ */
static uint8_t canfd_get_cmd_type(uint32_t ext_id) {
    return (uint8_t)((ext_id >> 24) & 0x1FU);
}

static bool canfd_parse_force_sensor_frame(can_frame_t rx_frame) {
    if (rx_frame.id_mode != CAN_ID_MODE_EXTENDED) return false;
    if (rx_frame.type != CAN_FRAME_TYPE_DATA) return false;
    if (rx_frame.data_length_code != CANFD_FORCE_SENSOR_DATA_LEN) return false;

    uint32_t ext_id = rx_frame.id;
    uint8_t sensor_can_id = (uint8_t)((ext_id >> 8) & 0xFFU);
    uint8_t host_can_id = (uint8_t)(ext_id & 0xFFU);

    if (canfd_get_cmd_type(ext_id) != CANFD_FORCE_SENSOR_CMD_DATA) return false;
    if (sensor_can_id != CANFD_FORCE_SENSOR_CAN_ID) return false;
    if (host_can_id != CANFD_FORCE_SENSOR_HOST_ID) return false;

    g_force_sensor_data.sensor_can_id = sensor_can_id;
    g_force_sensor_data.host_can_id = host_can_id;
    g_force_sensor_data.sub_type = (uint8_t)((ext_id >> 16) & 0xFFU);

    for (uint8_t i = 0; i < CANFD_FORCE_SENSOR_DATA_LEN; i++) {
        g_force_sensor_data.values[i] = rx_frame.data[i];
    }

    g_force_sensor_data.valid = true;
    if (g_force_sensor_data.rx_count < UINT32_MAX) g_force_sensor_data.rx_count++;
    g_force_sensor_data.last_update_tick = (uint32_t)xTaskGetTickCountFromISR();

    g_can_rx_count++;
    return true;
}

static void canfd_dispatch_rx_frame(can_frame_t rx_frame) {
    switch (canfd_get_cmd_type(rx_frame.id)) {
        case ROBSTRIDE_CMD_FEEDBACK:
        case ROBSTRIDE_CMD_AUTO_REPORT:
            robstride_parse_feedback(rx_frame);
            break;

        case CANFD_FORCE_SENSOR_CMD_DATA:
            (void)canfd_parse_force_sensor_frame(rx_frame);
            break;

        default:
            break;
    }
}

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
 *  接收完成时将帧按协议类型分发
 * ============================================================ */
void canfd0_callback(can_callback_args_t *p_args) {
    if (p_args == NULL) return;
    if (p_args->event == CAN_EVENT_RX_COMPLETE) canfd_dispatch_rx_frame(p_args->frame);
}

/* ============================================================
 *  初始化函数
 * ============================================================ */

/**
 * @brief  初始化 CANFD0 外设，切换到正常工作模式
 */
fsp_err_t canfd0_init(void) {
    fsp_err_t err;

    err = g_canfd0.p_api->open(g_canfd0.p_ctrl, g_canfd0.p_cfg);
    if (err != FSP_SUCCESS) return err;

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
    return g_canfd0.p_api->write(g_canfd0.p_ctrl, CANFD_TX_BUFFER_FIFO_COMMON_1, &tx_frame);
}

/**
 * @brief  检查 CANFD 链接状态
 */
void canfd_link_check(void) {
    static uint16_t s_last_count = 0U;
    static TickType_t s_last_check_tick = 0;
    static uint8_t s_miss_windows = 0U;

    TickType_t now = xTaskGetTickCount();
    if (s_last_check_tick == 0) {
        s_last_check_tick = now;
        s_last_count = g_can_rx_count;
        g_sys_status.is_can_connected = false;
        return;
    }

    if ((now - s_last_check_tick) < pdMS_TO_TICKS(CAN_LINK_CHECK_WINDOW_MS)) return;

    uint16_t current_count = g_can_rx_count;
    uint16_t rx_delta = (uint16_t)(current_count - s_last_count);

    s_last_count = current_count;
    s_last_check_tick = now;

    bool previous = g_sys_status.is_can_connected;
    if (rx_delta > 0U) {
        s_miss_windows = 0U;
        g_sys_status.is_can_connected = true;
    } else {
        if (s_miss_windows < UINT8_MAX) s_miss_windows++;
        if (s_miss_windows >= CAN_LINK_MISS_THRESHOLD) g_sys_status.is_can_connected = false;
    }

    if (g_sys_status.is_can_connected != previous) {
        LOG_I("CAN link %s (rx_delta=%u, total=%u)",
              g_sys_status.is_can_connected ? "UP" : "DOWN",
              (unsigned int)rx_delta,
              (unsigned int)current_count);
    }
}
