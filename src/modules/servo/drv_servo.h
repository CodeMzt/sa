/**
 * @file drv_servo.h
 * @brief 串口舵机总线驱动接口
 * @author Ma Ziteng
 */

#ifndef DRV_SERVO_H_
#define DRV_SERVO_H_

#include "servo_bus.h"
#include "robstride_motor.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* 串口舵机当前有效接口                                                        */
/*                                                                            */
/* 本头文件对应当前生效的底层运控接口。关节 1~4 通过舵机模式位置插补控制，       */
/* 夹爪通过电机模式定扭矩控制。新增底层功能应优先扩展这里，而不是继续扩展旧版    */
/* CAN/RobStride 运控接口。                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief 初始化串口舵机总线与默认控制模式
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool servo_init(void);

/**
 * @brief 关闭串口舵机总线
 */
void servo_deinit(void);

/**
 * @brief 刷新全部电机反馈镜像
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_refresh_feedback(void);

/**
 * @brief 仅刷新关节 1~4 的位置反馈镜像
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_refresh_joint_feedback(void);

/**
 * @brief 批量下发 4 个关节的位置目标
 * @param q_target 4 个关节的目标角度，单位 rad
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_write_joint_positions(const float q_target[ROBSTRIDE_ACTIVE_JOINT_NUM]);

/**
 * @brief 将全部关节锁舵在当前反馈位置
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_hold_joint_current(void);

/**
 * @brief 将指定关节切回舵机模式并打开锁定扭矩
 * @param motor_id 关节电机 ID
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_set_joint_servo_mode(uint8_t motor_id);

/**
 * @brief 将指定关节锁舵在当前反馈位置
 * @param motor_id 关节电机 ID
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_lock_joint_current(uint8_t motor_id);

/**
 * @brief 解除指定关节的锁舵扭矩，进入自由态
 * @param motor_id 关节电机 ID
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_unlock_joint(uint8_t motor_id);

/**
 * @brief 停止指定电机
 * @param motor_id 电机 ID
 * @param hold_position true 表示停在当前位置保持，false 表示关闭扭矩
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_stop_motor(uint8_t motor_id, bool hold_position);

/**
 * @brief 将当前机械位置设置为逻辑零位
 * @param motor_id 电机 ID
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_set_zero(uint8_t motor_id);
bool servo_supports_zero_calibration(void);

/**
 * @brief 夹爪执行闭合扭矩
 * @param torque_cmd 力矩/PWM 命令值
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_gripper_grasp(int16_t torque_cmd);

/**
 * @brief 夹爪执行保持扭矩
 * @param torque_cmd 力矩/PWM 命令值
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_gripper_hold(int16_t torque_cmd);

/**
 * @brief 夹爪执行张开扭矩
 * @param torque_cmd 力矩/PWM 命令值
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_gripper_release(int16_t torque_cmd);

/**
 * @brief 停止夹爪扭矩输出
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_gripper_stop(void);

/**
 * @brief 获取夹爪当前反馈位置对应的舵机命令值
 * @return 夹爪当前位置命令值
 */
uint16_t servo_gripper_feedback_cmd(void);

/**
 * @brief 将夹爪移动到指定命令位置
 * @param target_cmd 目标命令值，函数内部会自动限幅
 * @param applied_cmd 输出最终生效的目标命令值，可为 NULL
 * @return FSP_SUCCESS 成功
 */
fsp_err_t servo_gripper_move_to_cmd(int32_t target_cmd, uint16_t *applied_cmd);

/**
 * @brief 基于最近一次反馈时间更新总线在线状态
 */
void servo_link_check(void);

/**
 * @brief 查询串口舵机总线是否在线
 * @return true 在线
 * @return false 离线
 */
bool servo_is_connected(void);

/**
 * @brief 在链路异常时输出电机诊断快照
 */
void servo_log_link_diagnostics(void);

#ifdef __cplusplus
}
#endif

#endif /* DRV_SERVO_H_ */
