/**
 * @file motor_state.h
 * @brief 电机公共镜像与 ID 定义
 *
 * 本模块为 servo_bus 运行链路提供“电机镜像（位置/速度/力矩代理量）”与基础 ID/校验工具。
 * 该镜像用于跨任务共享（UI/网络上报/示教保存/运控等）。
 */

#ifndef MOTOR_STATE_H_
#define MOTOR_STATE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 主机侧电机 ID 定义（历史兼容：关节 1~5 + 夹爪 6） */
#define MOTOR_ID_JOINT1  (1U)
#define MOTOR_ID_JOINT2  (2U)
#define MOTOR_ID_JOINT3  (3U)
#define MOTOR_ID_JOINT4  (4U)
#define MOTOR_ID_JOINT5  (5U)
#define MOTOR_ID_GRIPPER (6U)

/** 关节/电机数量定义 */
#define MOTOR_JOINT_NUM (5U)
#define MOTOR_NUM       (6U)
#define MOTOR_ACTIVE_JOINT_NUM MOTOR_JOINT_NUM
#define MOTOR_ACTIVE_MOTOR_NUM MOTOR_NUM

/** 关节位置限幅（rad），用于在缺乏更完整约束时做最后保护 */
#define MOTOR_JOINT_POSITION_LIMIT_RAD (0.6f)

/**
 * @brief 电机反馈镜像
 *
 * @note 当前位置/速度为上层语义量（关节：关节侧；夹爪：电机侧或机构侧，取决于底层实现）。
 *       torque 字段为“力矩代理量”，在不同链路下可能对应 Nm 或 mA。
 */
typedef struct {
    float position;
    float velocity;
    float torque;
    float temperature;
    uint8_t mode_state;
    uint8_t fault_flags;
} motor_feedback_t;

/**
 * @brief 单电机镜像实例
 */
typedef struct {
    uint8_t id;
    motor_feedback_t feedback;
} motor_t;

/**
 * @brief 全局电机镜像数组
 */
extern motor_t g_motors[MOTOR_NUM];

/**
 * @brief 获取电机在 g_motors 中的索引
 * @param motor_id 电机 ID
 * @return 有效索引 [0, MOTOR_NUM-1]；无效返回 MOTOR_NUM
 */
uint8_t motor_get_index(uint8_t motor_id);

/**
 * @brief 检查电机 ID 是否有效
 * @param motor_id 电机 ID
 * @return true 有效
 */
bool motor_id_is_valid(uint8_t motor_id);

/**
 * @brief 检查电机 ID 是否为关节 1~5
 * @param motor_id 电机 ID
 * @return true 为关节电机
 */
bool motor_id_is_joint(uint8_t motor_id);

/**
 * @brief 对关节侧位置命令做边界限制
 * @param motor_id 目标电机 ID
 * @param position_cmd 关节侧位置命令（rad）
 * @return 限幅后的关节侧位置命令（rad）
 */
float motor_clamp_position_cmd(uint8_t motor_id, float position_cmd);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_STATE_H_ */
