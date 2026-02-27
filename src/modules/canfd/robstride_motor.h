/*
 * robstride_motor.h
 *
 *  Created on: 2026年2月24日
 *      Author: Ma Ziteng
 *
 *  RobStride EL05 关节电机 CAN 通信协议层
 *  支持私有协议（扩展帧，1Mbps）
 *
 *  CAN 扩展帧 29位ID 格式：
 *    Bit28~Bit24 : 通信类型 (5bit)
 *    Bit23~Bit8  : 数据区2 / 主机ID等 (16bit)
 *    Bit7~Bit0   : 目标电机 CAN_ID (8bit)
 */

#ifndef ROBSTRIDE_MOTOR_H_
#define ROBSTRIDE_MOTOR_H_

#include <stdint.h>
#include <string.h>
#include "can_comms.h"

/* ============================================================
 *  CAN ID 配置
 * ============================================================ */
/** 主机 CAN ID，可按需修改 */
#define ROBSTRIDE_MASTER_ID         (0xFDU)

/** 5个电机的 CAN ID，可按需修改 */
#define ROBSTRIDE_MOTOR_ID_JOINT1   (1U)
#define ROBSTRIDE_MOTOR_ID_JOINT2   (2U)
#define ROBSTRIDE_MOTOR_ID_JOINT3   (3U)
#define ROBSTRIDE_MOTOR_ID_JOINT4   (4U)
#define ROBSTRIDE_MOTOR_ID_GRIPPER  (5U)

/** 电机总数 */
#define ROBSTRIDE_MOTOR_NUM         (5U)

/* ============================================================
 *  物理量范围（用于 float <-> uint16 线性映射）
 * ============================================================ */
#define ROBSTRIDE_P_MIN     (-12.57f)   /* 位置最小值 rad，约 -4π */
#define ROBSTRIDE_P_MAX     ( 12.57f)   /* 位置最大值 rad，约 +4π */
#define ROBSTRIDE_V_MIN     (-50.0f)    /* 速度最小值 rad/s */
#define ROBSTRIDE_V_MAX     ( 50.0f)    /* 速度最大值 rad/s */
#define ROBSTRIDE_KP_MIN    (  0.0f)    /* 运控模式 Kp 最小值 */
#define ROBSTRIDE_KP_MAX    (500.0f)    /* 运控模式 Kp 最大值 */
#define ROBSTRIDE_KD_MIN    (  0.0f)    /* 运控模式 Kd 最小值 */
#define ROBSTRIDE_KD_MAX    (  5.0f)    /* 运控模式 Kd 最大值 */
#define ROBSTRIDE_T_MIN     ( -6.0f)    /* 力矩最小值 Nm */
#define ROBSTRIDE_T_MAX     (  6.0f)    /* 力矩最大值 Nm */

/* ============================================================
 *  通信类型（Communication Type）
 * ============================================================ */
#define ROBSTRIDE_CMD_GET_ID            (0x00U)  /* 获取设备ID */
#define ROBSTRIDE_CMD_MOTION_CTRL       (0x01U)  /* 运控模式控制指令 */
#define ROBSTRIDE_CMD_FEEDBACK          (0x02U)  /* 电机反馈数据（接收） */
#define ROBSTRIDE_CMD_ENABLE            (0x03U)  /* 电机使能运行 */
#define ROBSTRIDE_CMD_STOP              (0x04U)  /* 电机停止运行 */
#define ROBSTRIDE_CMD_SET_ZERO          (0x06U)  /* 设置机械零位 */
#define ROBSTRIDE_CMD_SET_CAN_ID        (0x07U)  /* 设置电机 CAN_ID */
#define ROBSTRIDE_CMD_READ_PARAM        (0x11U)  /* 单个参数读取 */
#define ROBSTRIDE_CMD_WRITE_PARAM       (0x12U)  /* 单个参数写入（掉电丢失） */
#define ROBSTRIDE_CMD_FAULT_FEEDBACK    (0x15U)  /* 故障反馈帧（接收） */
#define ROBSTRIDE_CMD_SAVE_CONFIG       (0x16U)  /* 电机数据保存帧 */
#define ROBSTRIDE_CMD_SET_BAUDRATE      (0x17U)  /* 波特率修改帧 */
#define ROBSTRIDE_CMD_AUTO_REPORT       (0x18U)  /* 主动上报帧 */
#define ROBSTRIDE_CMD_SET_PROTOCOL      (0x19U)  /* 协议修改帧 */

/* ============================================================
 *  可读写参数 Index（通信类型17/18使用）
 * ============================================================ */
#define ROBSTRIDE_PARAM_RUN_MODE        (0x7005U)  /* 运行模式 uint8 */
#define ROBSTRIDE_PARAM_IQ_REF          (0x7006U)  /* 电流模式 Iq 指令 float */
#define ROBSTRIDE_PARAM_SPD_REF         (0x700AU)  /* 速度模式转速指令 float */
#define ROBSTRIDE_PARAM_LIMIT_TORQUE    (0x700BU)  /* 转矩限制 float */
#define ROBSTRIDE_PARAM_CUR_KP          (0x7010U)  /* 电流 Kp float */
#define ROBSTRIDE_PARAM_CUR_KI          (0x7011U)  /* 电流 Ki float */
#define ROBSTRIDE_PARAM_CUR_FILT_GAIN   (0x7014U)  /* 电流滤波系数 float */
#define ROBSTRIDE_PARAM_LOC_REF         (0x7016U)  /* 位置模式角度指令 float (rad) */
#define ROBSTRIDE_PARAM_LIMIT_SPD       (0x7017U)  /* CSP 速度限制 float */
#define ROBSTRIDE_PARAM_LIMIT_CUR       (0x7018U)  /* 速度/位置模式电流限制 float */
#define ROBSTRIDE_PARAM_MECH_POS        (0x7019U)  /* 负载端计圈机械角度 float (只读) */
#define ROBSTRIDE_PARAM_IQF             (0x701AU)  /* Iq 滤波值 float (只读) */
#define ROBSTRIDE_PARAM_MECH_VEL        (0x701BU)  /* 负载端转速 float (只读) */
#define ROBSTRIDE_PARAM_VBUS            (0x701CU)  /* 母线电压 float (只读) */
#define ROBSTRIDE_PARAM_LOC_KP          (0x701EU)  /* 位置 Kp float */
#define ROBSTRIDE_PARAM_SPD_KP          (0x701FU)  /* 速度 Kp float */
#define ROBSTRIDE_PARAM_SPD_KI          (0x7020U)  /* 速度 Ki float */
#define ROBSTRIDE_PARAM_SPD_FILT_GAIN   (0x7021U)  /* 速度滤波值 float */
#define ROBSTRIDE_PARAM_ACC_RAD         (0x7022U)  /* 速度模式加速度 float */
#define ROBSTRIDE_PARAM_VEL_MAX         (0x7024U)  /* PP 模式速度 float */
#define ROBSTRIDE_PARAM_ACC_SET         (0x7025U)  /* PP 模式加速度 float */
#define ROBSTRIDE_PARAM_EPS_CAN_TIME    (0x7026U)  /* 上报时间设置 uint16 */
#define ROBSTRIDE_PARAM_CAN_TIMEOUT     (0x7028U)  /* CAN 超时阈值 uint32 */
#define ROBSTRIDE_PARAM_ZERO_STA        (0x7029U)  /* 零点标志位 uint8 */
#define ROBSTRIDE_PARAM_ADD_OFFSET      (0x702BU)  /* 零位偏置 float */

/* ============================================================
 *  运行模式枚举
 * ============================================================ */
typedef enum {
    ROBSTRIDE_MODE_MOTION_CTRL   = 0,  /* 运控模式（默认） */
    ROBSTRIDE_MODE_POSITION_PP   = 1,  /* 位置模式 PP（插补位置） */
    ROBSTRIDE_MODE_SPEED         = 2,  /* 速度模式 */
    ROBSTRIDE_MODE_CURRENT       = 3,  /* 电流模式 */
    ROBSTRIDE_MODE_POSITION_CSP  = 5,  /* 位置模式 CSP（周期同步位置） */
} robstride_run_mode_t;

/* ============================================================
 *  电机状态枚举（反馈帧 bit22~23）
 * ============================================================ */
typedef enum {
    ROBSTRIDE_STATE_RESET  = 0,  /* 复位模式 */
    ROBSTRIDE_STATE_CALI   = 1,  /* 标定模式 */
    ROBSTRIDE_STATE_MOTOR  = 2,  /* 运行模式 */
} robstride_motor_state_t;

/* ============================================================
 *  故障标志位（反馈帧 bit16~21 及故障反馈帧）
 * ============================================================ */
#define ROBSTRIDE_FAULT_UNDERVOLTAGE    (1U << 0)   /* bit16: 欠压故障 */
#define ROBSTRIDE_FAULT_OVERCURRENT     (1U << 1)   /* bit17: 三相电流故障 */
#define ROBSTRIDE_FAULT_OVERTEMP        (1U << 2)   /* bit18: 过温 */
#define ROBSTRIDE_FAULT_ENCODER         (1U << 3)   /* bit19: 磁编码故障 */
#define ROBSTRIDE_FAULT_OVERLOAD        (1U << 4)   /* bit20: 堵转过载故障 */
#define ROBSTRIDE_FAULT_UNCALIBRATED    (1U << 5)   /* bit21: 未标定 */

/* ============================================================
 *  电机反馈数据结构体
 * ============================================================ */
typedef struct {
    float    position;      /* 当前角度 rad，范围 -4π ~ +4π */
    float    velocity;      /* 当前角速度 rad/s */
    float    torque;        /* 当前力矩 Nm */
    float    temperature;   /* 当前温度 °C */
    uint8_t  mode_state;    /* 模式状态，见 robstride_motor_state_t */
    uint8_t  fault_flags;   /* 故障标志位，见 ROBSTRIDE_FAULT_xxx */
} robstride_feedback_t;

/* ============================================================
 *  单个电机实例结构体
 * ============================================================ */
typedef struct {
    uint8_t              can_id;    /* 电机 CAN ID */
    robstride_feedback_t feedback;  /* 最新反馈数据 */
} robstride_motor_t;

/* ============================================================
 *  底层通信函数（直接对应手册通信类型）
 * ============================================================ */

/**
 * @brief  通信类型0：获取设备ID
 * @param  motor_id  目标电机 CAN ID
 */
fsp_err_t robstride_get_device_id(uint8_t motor_id);

/**
 * @brief  通信类型3：电机使能运行
 * @param  motor_id  目标电机 CAN ID
 */
fsp_err_t robstride_enable(uint8_t motor_id);

/**
 * @brief  通信类型4：电机停止运行
 * @param  motor_id  目标电机 CAN ID
 */
fsp_err_t robstride_stop(uint8_t motor_id);

/**
 * @brief  通信类型4：清除电机故障
 * @param  motor_id  目标电机 CAN ID
 */
fsp_err_t robstride_clear_fault(uint8_t motor_id);

/**
 * @brief  通信类型6：设置电机机械零位
 * @param  motor_id  目标电机 CAN ID
 */
fsp_err_t robstride_set_zero(uint8_t motor_id);

/**
 * @brief  通信类型17：读取单个参数
 * @param  motor_id  目标电机 CAN ID
 * @param  index     参数 index，见 ROBSTRIDE_PARAM_xxx
 */
fsp_err_t robstride_read_param(uint8_t motor_id, uint16_t index);

/**
 * @brief  通信类型18：写入 float 类型参数（掉电丢失）
 * @param  motor_id  目标电机 CAN ID
 * @param  index     参数 index，见 ROBSTRIDE_PARAM_xxx
 * @param  value     写入值
 */
fsp_err_t robstride_write_param_float(uint8_t motor_id, uint16_t index, float value);

/**
 * @brief  通信类型18：写入 uint8 类型参数（掉电丢失）
 * @param  motor_id  目标电机 CAN ID
 * @param  index     参数 index，见 ROBSTRIDE_PARAM_xxx
 * @param  value     写入值
 */
fsp_err_t robstride_write_param_uint8(uint8_t motor_id, uint16_t index, uint8_t value);

/**
 * @brief  通信类型22：保存参数到 Flash（掉电不丢失）
 * @param  motor_id  目标电机 CAN ID
 */
fsp_err_t robstride_save_config(uint8_t motor_id);

/**
 * @brief  通信类型1：运控模式电机控制指令（5参数）
 * @param  motor_id      目标电机 CAN ID
 * @param  position      目标角度 rad
 * @param  velocity      目标角速度 rad/s
 * @param  kp            位置增益 Kp (0~500)
 * @param  kd            阻尼增益 Kd (0~5)
 * @param  torque_ff     力矩前馈 Nm (-6~6)
 */
fsp_err_t robstride_motion_control(uint8_t motor_id,
                                   float   position,
                                   float   velocity,
                                   float   kp,
                                   float   kd,
                                   float   torque_ff);

/* ============================================================
 *  高层控制函数
 * ============================================================ */

/**
 * @brief  切换运行模式（通信类型18写入 run_mode）
 * @param  motor_id  目标电机 CAN ID
 * @param  mode      目标模式，见 robstride_run_mode_t
 * @note   切换模式前请先停止电机
 */
fsp_err_t robstride_set_run_mode(uint8_t motor_id, robstride_run_mode_t mode);

/**
 * @brief  CSP 位置模式：设置目标位置
 * @param  motor_id      目标电机 CAN ID
 * @param  position_rad  目标位置 rad
 * @note   使用前需先调用 robstride_set_run_mode(id, ROBSTRIDE_MODE_POSITION_CSP)
 *         并调用 robstride_enable()，以及设置速度限制
 */
fsp_err_t robstride_set_position_csp(uint8_t motor_id, float position_rad);

/**
 * @brief  CSP 位置模式：设置速度限制
 * @param  motor_id   目标电机 CAN ID
 * @param  limit_spd  速度限制 rad/s (0~50)
 */
fsp_err_t robstride_set_csp_speed_limit(uint8_t motor_id, float limit_spd);

/**
 * @brief  PP 位置模式：设置目标位置（含速度和加速度）
 * @param  motor_id      目标电机 CAN ID
 * @param  position_rad  目标位置 rad
 * @param  vel_max       最大速度 rad/s
 * @param  acc_set       加速度 rad/s²
 * @note   使用前需先调用 robstride_set_run_mode(id, ROBSTRIDE_MODE_POSITION_PP)
 *         并调用 robstride_enable()
 */
fsp_err_t robstride_set_position_pp(uint8_t motor_id,
                                    float   position_rad,
                                    float   vel_max,
                                    float   acc_set);

/**
 * @brief  速度模式：设置目标速度
 * @param  motor_id   目标电机 CAN ID
 * @param  speed_rps  目标速度 rad/s
 * @param  limit_cur  电流限制 A（0表示使用默认值）
 * @param  acc_rad    加速度 rad/s²（0表示使用默认值）
 * @note   使用前需先调用 robstride_set_run_mode(id, ROBSTRIDE_MODE_SPEED)
 *         并调用 robstride_enable()
 */
fsp_err_t robstride_set_speed(uint8_t motor_id,
                              float   speed_rps,
                              float   limit_cur,
                              float   acc_rad);

/**
 * @brief  电流模式：设置目标 Iq 电流
 * @param  motor_id  目标电机 CAN ID
 * @param  iq_ref    目标 Iq 电流 A (-11~11)
 * @note   使用前需先调用 robstride_set_run_mode(id, ROBSTRIDE_MODE_CURRENT)
 *         并调用 robstride_enable()
 */
fsp_err_t robstride_set_current(uint8_t motor_id, float iq_ref);

/**
 * @brief  夹爪运控模式：柔顺抓取
 * @param  motor_id       夹爪电机 CAN ID
 * @param  close_position 闭合目标位置 rad（正值为闭合方向）
 * @param  kp             位置刚度 (0~500)，越大夹持力越大
 * @param  kd             阻尼 (0~5)，防止振荡
 * @param  torque_ff      力矩前馈 Nm，可用于主动施加夹持力
 * @note   使用前需先调用 robstride_enable()，夹爪默认处于运控模式
 */
fsp_err_t robstride_gripper_grasp(uint8_t motor_id,
                                  float   close_position,
                                  float   kp,
                                  float   kd,
                                  float   torque_ff);

/**
 * @brief  夹爪运控模式：松开（回到零位）
 * @param  motor_id  夹爪电机 CAN ID
 * @param  kp        位置刚度
 * @param  kd        阻尼
 */
fsp_err_t robstride_gripper_release(uint8_t motor_id, float kp, float kd);

/* ============================================================
 *  反馈解析函数（由 CAN 接收回调调用）
 * ============================================================ */

/**
 * @brief  解析电机反馈帧（通信类型2），更新 g_motors[] 状态
 * @param  rx_frame  接收到的 CAN 帧
 */
void robstride_parse_feedback(can_frame_t rx_frame);

#endif /* ROBSTRIDE_MOTOR_H_ */
