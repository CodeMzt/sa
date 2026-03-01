/*
 * robstride_motor.c
 *
 *  Created on: 2026年2月24日
 *      Author: Ma Ziteng
 *
 *  RobStride EL05 关节电机 CAN 通信协议层实现
 */

#include "robstride_motor.h"
#include "drv_canfd.h"

/* ============================================================
 *  全局电机数组
 *  索引 0~3 对应关节1~4，索引 4 对应夹爪
 * ============================================================ */
robstride_motor_t g_motors[ROBSTRIDE_MOTOR_NUM] = {
    { .can_id = ROBSTRIDE_MOTOR_ID_JOINT1,  .feedback = {0} },
    { .can_id = ROBSTRIDE_MOTOR_ID_JOINT2,  .feedback = {0} },
    { .can_id = ROBSTRIDE_MOTOR_ID_JOINT3,  .feedback = {0} },
    { .can_id = ROBSTRIDE_MOTOR_ID_JOINT4,  .feedback = {0} },
    { .can_id = ROBSTRIDE_MOTOR_ID_GRIPPER, .feedback = {0} },
};

/* ============================================================
 *  内部辅助函数
 * ============================================================ */

/**
 * @brief  float 线性映射到 uint16
 *         将 [x_min, x_max] 范围内的 x 映射到 [0, 2^bits - 1]
 */
static uint16_t float_to_uint16(float x, float x_min, float x_max) {
    if (x > x_max) {
        x = x_max;
    }
    if (x < x_min) {
        x = x_min;
    }
    float span = x_max - x_min;
    return (uint16_t)((x - x_min) / span * 65535.0f);
}

/**
 * @brief  uint16 线性映射回 float
 */
static float uint16_to_float(uint16_t x, float x_min, float x_max) {
    float span = x_max - x_min;
    return (float)x / 65535.0f * span + x_min;
}

/**
 * @brief  构造 29 位扩展帧 ID
 *         格式：Bit28~24=通信类型, Bit23~8=数据区2(主机ID等), Bit7~0=目标电机ID
 *
 * @param  cmd_type   通信类型 (5bit, 0~31)
 * @param  data16     数据区2 (16bit)，通常为主机ID或其他控制字
 * @param  motor_id   目标电机 CAN ID (8bit)
 * @return 29位扩展帧 ID（存放在 uint32_t 低29位）
 */
static uint32_t build_ext_id(uint8_t cmd_type, uint16_t data16, uint8_t motor_id) {
    return (((uint32_t)cmd_type & 0x1FU) << 24)
         | (((uint32_t)data16  & 0xFFFFU) << 8)
         | ((uint32_t)motor_id & 0xFFU);
}

/**
 * @brief  发送一帧扩展帧 CAN 报文
 */
static fsp_err_t send_ext_frame(uint32_t ext_id, const uint8_t *data, uint8_t len) {
    can_frame_t tx_frame;
    memset(&tx_frame, 0, sizeof(can_frame_t));

    tx_frame.id             = ext_id;
    tx_frame.id_mode        = CAN_ID_MODE_EXTENDED;
    tx_frame.type           = CAN_FRAME_TYPE_DATA;
    tx_frame.data_length_code = len;

    if (data != NULL && len > 0) memcpy(tx_frame.data, data, len);
    
    return canfd0_send_ext_frame(tx_frame);
}

/* ============================================================
 *  底层通信函数实现
 * ============================================================ */

/**
 * @brief  通信类型0：获取设备ID
 *         发送：ID = (0x00 << 24) | (master_id << 8) | motor_id
 *         数据区全0
 */
fsp_err_t robstride_get_device_id(uint8_t motor_id) {
    uint32_t ext_id = build_ext_id(ROBSTRIDE_CMD_GET_ID,
                                   (uint16_t)ROBSTRIDE_MASTER_ID << 8,
                                   motor_id);
    uint8_t data[8] = {0};
    return send_ext_frame(ext_id, data, 8);
}

/**
 * @brief  通信类型3：电机使能运行
 *         发送：ID = (0x03 << 24) | (master_id << 8) | motor_id
 *         数据区全0
 */
fsp_err_t robstride_enable(uint8_t motor_id) {
    uint32_t ext_id = build_ext_id(ROBSTRIDE_CMD_ENABLE,
                                   (uint16_t)ROBSTRIDE_MASTER_ID << 8,
                                   motor_id);
    uint8_t data[8] = {0};
    return send_ext_frame(ext_id, data, 8);
}

/**
 * @brief  通信类型4：电机停止运行
 *         发送：ID = (0x04 << 24) | (master_id << 8) | motor_id
 *         数据区全0（正常停止）
 */
fsp_err_t robstride_stop(uint8_t motor_id) {
    uint32_t ext_id = build_ext_id(ROBSTRIDE_CMD_STOP,
                                   (uint16_t)ROBSTRIDE_MASTER_ID << 8,
                                   motor_id);
    uint8_t data[8] = {0};
    return send_ext_frame(ext_id, data, 8);
}

/**
 * @brief  通信类型4：清除电机故障
 *         发送：ID = (0x04 << 24) | (master_id << 8) | motor_id
 *         Byte[0] = 1 表示清故障
 */
fsp_err_t robstride_clear_fault(uint8_t motor_id) {
    uint32_t ext_id = build_ext_id(ROBSTRIDE_CMD_STOP,
                                   (uint16_t)ROBSTRIDE_MASTER_ID << 8,
                                   motor_id);
    uint8_t data[8] = {0};
    data[0] = 1U;  /* Byte[0]=1 清故障 */
    return send_ext_frame(ext_id, data, 8);
}

/**
 * @brief  通信类型6：设置电机机械零位
 *         发送：ID = (0x06 << 24) | (master_id << 8) | motor_id
 *         Byte[0] = 1
 */
fsp_err_t robstride_set_zero(uint8_t motor_id) {
    uint32_t ext_id = build_ext_id(ROBSTRIDE_CMD_SET_ZERO,
                                   (uint16_t)ROBSTRIDE_MASTER_ID << 8,
                                   motor_id);
    uint8_t data[8] = {0};
    data[0] = 1U;
    return send_ext_frame(ext_id, data, 8);
}

/**
 * @brief  通信类型17：读取单个参数
 *         数据区：Byte0~1 = index（小端），Byte2~7 = 0
 */
fsp_err_t robstride_read_param(uint8_t motor_id, uint16_t index) {
    uint32_t ext_id = build_ext_id(ROBSTRIDE_CMD_READ_PARAM,
                                   (uint16_t)ROBSTRIDE_MASTER_ID << 8,
                                   motor_id);
    uint8_t data[8] = {0};
    /* 小端序写入 index */
    data[0] = (uint8_t)(index & 0xFFU);
    data[1] = (uint8_t)((index >> 8) & 0xFFU);
    return send_ext_frame(ext_id, data, 8);
}

/**
 * @brief  通信类型18：写入 float 类型参数（掉电丢失）
 *         数据区：Byte0~1 = index（小端），Byte2~3 = 0，Byte4~7 = float值（小端）
 */
fsp_err_t robstride_write_param_float(uint8_t motor_id, uint16_t index, float value) {
    uint32_t ext_id = build_ext_id(ROBSTRIDE_CMD_WRITE_PARAM,
                                   (uint16_t)ROBSTRIDE_MASTER_ID << 8,
                                   motor_id);
    uint8_t data[8] = {0};
    /* 小端序写入 index */
    data[0] = (uint8_t)(index & 0xFFU);
    data[1] = (uint8_t)((index >> 8) & 0xFFU);
    /* 小端序写入 float（IEEE-754，4字节） */
    memcpy(&data[4], &value, 4);
    return send_ext_frame(ext_id, data, 8);
}

/**
 * @brief  通信类型18：写入 uint8 类型参数（掉电丢失）
 *         数据区：Byte0~1 = index（小端），Byte2~3 = 0，Byte4 = uint8值
 */
fsp_err_t robstride_write_param_uint8(uint8_t motor_id, uint16_t index, uint8_t value) {
    uint32_t ext_id = build_ext_id(ROBSTRIDE_CMD_WRITE_PARAM,
                                   (uint16_t)ROBSTRIDE_MASTER_ID << 8,
                                   motor_id);
    uint8_t data[8] = {0};
    data[0] = (uint8_t)(index & 0xFFU);
    data[1] = (uint8_t)((index >> 8) & 0xFFU);
    data[4] = value;
    return send_ext_frame(ext_id, data, 8);
}

/**
 * @brief  通信类型22：保存参数到 Flash
 *         数据区固定为 01 02 03 04 05 06 07 08
 */
fsp_err_t robstride_save_config(uint8_t motor_id) {
    uint32_t ext_id = build_ext_id(ROBSTRIDE_CMD_SAVE_CONFIG,
                                   (uint16_t)ROBSTRIDE_MASTER_ID << 8,
                                   motor_id);
    uint8_t data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    return send_ext_frame(ext_id, data, 8);
}

/**
 * @brief  通信类型1：运控模式电机控制指令（5参数）
 *
 *  29位ID：
 *    Bit28~24 = 0x01
 *    Bit23~8  = float_to_uint16(torque_ff, T_MIN, T_MAX)  （力矩前馈编码进ID）
 *    Bit7~0   = motor_id
 *
 *  数据区（8字节，高字节在前）：
 *    Byte0~1 : 目标角度  [0~65535] -> (-12.57 ~ 12.57 rad)
 *    Byte2~3 : 目标角速度 [0~65535] -> (-50 ~ 50 rad/s)
 *    Byte4~5 : Kp        [0~65535] -> (0 ~ 500)
 *    Byte6~7 : Kd        [0~65535] -> (0 ~ 5)
 */
fsp_err_t robstride_motion_control(uint8_t motor_id,
                                   float   position,
                                   float   velocity,
                                   float   kp,
                                   float   kd,
                                   float   torque_ff) {
    /* 力矩前馈编码进 ID 的 Bit23~8 */
    uint16_t torque_uint = float_to_uint16(torque_ff, ROBSTRIDE_T_MIN, ROBSTRIDE_T_MAX);
    uint32_t ext_id = build_ext_id(ROBSTRIDE_CMD_MOTION_CTRL, torque_uint, motor_id);

    uint16_t pos_uint = float_to_uint16(position, ROBSTRIDE_P_MIN, ROBSTRIDE_P_MAX);
    uint16_t vel_uint = float_to_uint16(velocity, ROBSTRIDE_V_MIN, ROBSTRIDE_V_MAX);
    uint16_t kp_uint  = float_to_uint16(kp,  ROBSTRIDE_KP_MIN, ROBSTRIDE_KP_MAX);
    uint16_t kd_uint  = float_to_uint16(kd,  ROBSTRIDE_KD_MIN, ROBSTRIDE_KD_MAX);

    uint8_t data[8];
    /* 高字节在前（大端） */
    data[0] = (uint8_t)(pos_uint >> 8);
    data[1] = (uint8_t)(pos_uint & 0xFFU);
    data[2] = (uint8_t)(vel_uint >> 8);
    data[3] = (uint8_t)(vel_uint & 0xFFU);
    data[4] = (uint8_t)(kp_uint  >> 8);
    data[5] = (uint8_t)(kp_uint  & 0xFFU);
    data[6] = (uint8_t)(kd_uint  >> 8);
    data[7] = (uint8_t)(kd_uint  & 0xFFU);

    return send_ext_frame(ext_id, data, 8);
}

/* ============================================================
 *  高层控制函数实现
 * ============================================================ */

/**
 * @brief  切换运行模式
 */
fsp_err_t robstride_set_run_mode(uint8_t motor_id, robstride_run_mode_t mode) {
    return robstride_write_param_uint8(motor_id,
                                      ROBSTRIDE_PARAM_RUN_MODE,
                                      (uint8_t)mode);
}

/**
 * @brief  CSP 位置模式：设置目标位置
 *         直接写入 loc_ref 参数
 */
fsp_err_t robstride_set_position_csp(uint8_t motor_id, float position_rad) {
    return robstride_write_param_float(motor_id,
                                      ROBSTRIDE_PARAM_LOC_REF,
                                      position_rad);
}

/**
 * @brief  CSP 位置模式：设置速度限制
 */
fsp_err_t robstride_set_csp_speed_limit(uint8_t motor_id, float limit_spd) {
    return robstride_write_param_float(motor_id,
                                      ROBSTRIDE_PARAM_LIMIT_SPD,
                                      limit_spd);
}

/**
 * @brief  PP 位置模式：设置目标位置（含速度和加速度）
 *         按手册顺序：先写 vel_max，再写 acc_set，最后写 loc_ref
 */
fsp_err_t robstride_set_position_pp(uint8_t motor_id,
                                    float   position_rad,
                                    float   vel_max,
                                    float   acc_set) {
    fsp_err_t err;

    err = robstride_write_param_float(motor_id, ROBSTRIDE_PARAM_VEL_MAX, vel_max);
    if (err != FSP_SUCCESS) return err;

    err = robstride_write_param_float(motor_id, ROBSTRIDE_PARAM_ACC_SET, acc_set);
    if (err != FSP_SUCCESS) return err;

    return robstride_write_param_float(motor_id, ROBSTRIDE_PARAM_LOC_REF, position_rad);
}

/**
 * @brief  速度模式：设置目标速度
 *         按手册顺序：先写 limit_cur（若非0），再写 acc_rad（若非0），最后写 spd_ref
 */
fsp_err_t robstride_set_speed(uint8_t motor_id,
                              float   speed_rps,
                              float   limit_cur,
                              float   acc_rad) {
    fsp_err_t err;

    if (limit_cur > 0.0f) {
        err = robstride_write_param_float(motor_id, ROBSTRIDE_PARAM_LIMIT_CUR, limit_cur);
        if (err != FSP_SUCCESS) return 1;
    }


    if (acc_rad > 0.0f) {
        err = robstride_write_param_float(motor_id, ROBSTRIDE_PARAM_ACC_RAD, acc_rad);
        if (err != FSP_SUCCESS) return 2;
    }

    return robstride_write_param_float(motor_id, ROBSTRIDE_PARAM_SPD_REF, speed_rps);
}

/**
 * @brief  电流模式：设置目标 Iq 电流
 */
fsp_err_t robstride_set_current(uint8_t motor_id, float iq_ref) {
    return robstride_write_param_float(motor_id, ROBSTRIDE_PARAM_IQ_REF, iq_ref);
}

/**
 * @brief  夹爪运控模式：柔顺抓取
 *         控制逻辑：t_ref = Kd*(0 - v_actual) + Kp*(close_position - p_actual) + torque_ff
 *         当夹爪接触物体后，位置误差被物体阻挡，力矩自然被 Kp 限制
 */
fsp_err_t robstride_gripper_grasp(uint8_t motor_id,
                                  float   close_position,
                                  float   kp,
                                  float   kd,
                                  float   torque_ff) {
    /* 速度目标设为0，依靠 Kp 驱动到目标位置，Kd 提供阻尼 */
    return robstride_motion_control(motor_id,
                                   close_position,
                                   0.0f,
                                   kp,
                                   kd,
                                   torque_ff);
}

/**
 * @brief  夹爪运控模式：松开（回到零位）
 */
fsp_err_t robstride_gripper_release(uint8_t motor_id, float kp, float kd) {
    return robstride_motion_control(motor_id,
                                   0.0f,   /* 目标位置：零位 */
                                   0.0f,   /* 速度目标：0 */
                                   kp,
                                   kd,
                                   0.0f);  /* 无力矩前馈 */
}

/* ============================================================
 *  反馈解析函数
 * ============================================================ */

/**
 * @brief  解析电机反馈帧（通信类型2）
 *
 *  29位ID 格式（接收）：
 *    Bit28~24 = 0x02
 *    Bit23~16 = 模式状态(bit23~22) + 故障信息(bit21~16)
 *    Bit15~8  = 当前电机 CAN_ID
 *    Bit7~0   = 主机 CAN_ID
 *
 *  数据区（8字节，高字节在前）：
 *    Byte0~1 : 当前角度  [0~65535] -> (-12.57 ~ 12.57 rad)
 *    Byte2~3 : 当前角速度 [0~65535] -> (-50 ~ 50 rad/s)
 *    Byte4~5 : 当前力矩  [0~65535] -> (-6 ~ 6 Nm)
 *    Byte6~7 : 当前温度  Temp(°C) * 10，高字节在前
 */
void robstride_parse_feedback(can_frame_t rx_frame) {
    uint32_t ext_id = rx_frame.id;

    /* 通信类型 */
    uint8_t cmd_type = (uint8_t)((ext_id >> 24) & 0x1FU);
    if (cmd_type != ROBSTRIDE_CMD_FEEDBACK && cmd_type!= ROBSTRIDE_CMD_AUTO_REPORT) return;
    
    uint8_t motor_can_id = (uint8_t)((ext_id >> 8) & 0xFFU);
    uint8_t mode_state  = (uint8_t)((ext_id >> 22) & 0x03U);
    uint8_t fault_flags = (uint8_t)((ext_id >> 16) & 0x3FU);

    robstride_motor_t *p_motor = NULL;
    for (uint8_t i = 0; i < ROBSTRIDE_MOTOR_NUM; i++) {
        if (g_motors[i].can_id == motor_can_id) {
            p_motor = &g_motors[i];
            break;
        }
    }

    if (p_motor == NULL) return; 
    
    /* 解析数据区（高字节在前） */
    uint16_t pos_raw  = ((uint16_t)rx_frame.data[0] << 8) | rx_frame.data[1];
    uint16_t vel_raw  = ((uint16_t)rx_frame.data[2] << 8) | rx_frame.data[3];
    uint16_t torq_raw = ((uint16_t)rx_frame.data[4] << 8) | rx_frame.data[5];
    uint16_t temp_raw = ((uint16_t)rx_frame.data[6] << 8) | rx_frame.data[7];

    /* 线性映射回物理量 */
    p_motor->feedback.position    = uint16_to_float(pos_raw,  ROBSTRIDE_P_MIN, ROBSTRIDE_P_MAX);
    p_motor->feedback.velocity    = uint16_to_float(vel_raw,  ROBSTRIDE_V_MIN, ROBSTRIDE_V_MAX);
    p_motor->feedback.torque      = uint16_to_float(torq_raw, ROBSTRIDE_T_MIN, ROBSTRIDE_T_MAX);
    p_motor->feedback.temperature = (float)temp_raw / 10.0f;  /* 温度 *10 存储 */
    p_motor->feedback.mode_state  = mode_state;
    p_motor->feedback.fault_flags = fault_flags;

    count_can++;
}

/* ============================================================
 *  主动上报控制函数实现
 * ============================================================ */

/**
 * @brief  开启电机主动上报功能（通信类型24）
 */
fsp_err_t robstride_enable_auto_report(uint8_t motor_id) {
    // 使用通信类型24（0x18）开启主动上报
    uint32_t ext_id = build_ext_id(ROBSTRIDE_CMD_AUTO_REPORT,
                                   (uint16_t)ROBSTRIDE_MASTER_ID << 8,
                                   motor_id);
    uint8_t data[8] = {1,2,3,4,5,6,1,0};// 开启主动上报
    return send_ext_frame(ext_id, data, 8);
}

/**
 * @brief  关闭电机主动上报功能（通信类型24）
 */
fsp_err_t robstride_disable_auto_report(uint8_t motor_id) {
    // 使用通信类型24（0x18）关闭主动上报
    uint32_t ext_id = build_ext_id(ROBSTRIDE_CMD_AUTO_REPORT,
                                   (uint16_t)ROBSTRIDE_MASTER_ID << 8,
                                   motor_id);
    uint8_t data[8] = {1,2,3,4,5,6,0,0};// 关闭主动上报
    return send_ext_frame(ext_id, data, 8);
}

/**
 * @brief  设置电机主动上报时间间隔（通信类型18写入EPScan_time参数）
 */
fsp_err_t robstride_set_auto_report_interval(uint8_t motor_id, uint16_t interval_value) {
    // 使用通信类型18（0x12）写入EPScan_time参数
    return robstride_write_param_uint8(motor_id, ROBSTRIDE_PARAM_EPS_CAN_TIME, (uint8_t)interval_value);
}
