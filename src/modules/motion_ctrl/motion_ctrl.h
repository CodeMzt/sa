/**
 * @file motion_ctrl.h
 * @brief 机械臂运动控制状态机接口
 */

#ifndef MOTION_CTRL_H_
#define MOTION_CTRL_H_

#include <stdbool.h>
#include <stdint.h>
#include "robstride_motor.h"
#include "trajectory.h"

/* -------------------------------------------------------------------------- */
/* 当前有效运控说明                                                            */
/*                                                                            */
/* 本模块是当前生效的中层运控状态机：                                          */
/* 1. 关节 1~4 使用串口舵机模式位置插补；                                      */
/* 2. 夹爪使用电机模式定扭矩；                                                  */
/* 3. 上层动作队列、录帧/回放入口保持兼容。                                     */
/*                                                                            */
/* 注意：这里虽然仍复用了 `robstride_motor.h` 里的 ID、限位和镜像定义，但旧版     */
/* CAN 运控函数本身已经不是当前有效控制路径。                                  */
/* -------------------------------------------------------------------------- */

#define TEACH_MODE_LOOP_FREQ_HZ      25.0f
#define TEACH_MODE_LOOP_PERIOD_MS    40.0f
#define PLAYBACK_MODE_LOOP_FREQ_HZ   33.3333f
#define PLAYBACK_MODE_LOOP_PERIOD_MS 30.0f
#define IDLE_MODE_LOOP_FREQ_HZ       50.0f
#define IDLE_MODE_LOOP_PERIOD_MS     20.0f

/**
 * @brief 运控主状态
 */
typedef enum {
    MOTION_STATE_IDLE = 0,
    MOTION_STATE_TEACHING,
    MOTION_STATE_PLAYBACK,
    MOTION_STATE_ERROR
} motion_state_t;

/**
 * @brief 夹爪持续控制状态
 */
typedef enum {
    GRIPPER_HOLD_IDLE = 0,
    GRIPPER_HOLD_GRASP = 1,
    GRIPPER_HOLD_RELEASE = 2
} gripper_hold_state_t;

/**
 * @brief 触觉交接监控状态
 */
typedef enum {
    HANDOFF_IDLE = 0,
    HANDOFF_ARMED,
    HANDOFF_RELEASE_ACTIVE,
    HANDOFF_DONE
} handoff_state_t;

/**
 * @brief 运控配置参数
 */
typedef struct {
    struct {
        float kp[ROBSTRIDE_JOINT_NUM];
        float kd[ROBSTRIDE_JOINT_NUM];
    } controller;

    float max_torque[ROBSTRIDE_JOINT_NUM];
    float max_velocity[ROBSTRIDE_JOINT_NUM];
} motion_ctrl_config_t;

/**
 * @brief 运控运行时上下文
 */
typedef struct {
    motion_state_t state;
    motion_ctrl_config_t config;

    traj_controller_t trajectory;
    float seq_start_time;
    float playback_upload_q[ROBSTRIDE_ACTIVE_JOINT_NUM];
    bool playback_upload_q_valid;

    float hold_q_ref[ROBSTRIDE_JOINT_NUM];
    bool hold_q_valid;
    bool idle_lock_active;
    bool teach_locked;
    float teach_lock_q[ROBSTRIDE_JOINT_NUM];
    float teach_prev_q[ROBSTRIDE_JOINT_NUM];
    float teach_settle_s;

    uint8_t teach_jog_motor_id;
    float teach_jog_q_ref;
    bool teach_jog_q_valid;
    uint16_t teach_jog_gripper_cmd_ref;
    bool teach_jog_gripper_cmd_valid;
    bool teach_jog_engaged;
    bool teach_jog_hold_active;
    int8_t teach_jog_hold_direction;
    uint8_t teach_jog_hold_step_level;
    float teach_jog_hold_elapsed_s;

    gripper_hold_state_t gripper_hold_state;
    handoff_state_t handoff_state;
    float gripper_release_timer;
    float gripper_keepalive_timer;
    float gripper_action_pause_s;
    float handoff_release_stop_timer;

    bool is_initialized;
    bool emergency_stop;
    uint8_t torque_limit_cycles[ROBSTRIDE_JOINT_NUM];
} motion_controller_t;

/**
 * @brief 全局运控实例
 */
extern motion_controller_t g_motion_ctrl;

/**
 * @brief 初始化运控状态机
 * @param ctrl 控制器实例
 * @param config 配置参数，传 NULL 使用默认值
 * @return true 成功
 * @return false 失败
 */
bool motion_ctrl_init(motion_controller_t *ctrl, const motion_ctrl_config_t *config);

/**
 * @brief 让关节回到串口舵机模式
 * @param ctrl 控制器实例
 * @return true 成功
 * @return false 失败
 */
bool motion_ctrl_set_motion_mode(motion_controller_t *ctrl);

/**
 * @brief 进入示教模式
 * @param ctrl 控制器实例
 * @return true 成功
 * @return false 失败
 */
bool motion_ctrl_start_teaching(motion_controller_t *ctrl);

/**
 * @brief 启动轨迹回放
 * @param ctrl 控制器实例
 * @param seq 动作序列
 * @return true 成功
 * @return false 失败
 */
bool motion_ctrl_start_playback(motion_controller_t *ctrl, const action_sequence_t *seq);

/**
 * @brief 停止当前动作并切换到姿态保持
 * @param ctrl 控制器实例
 */
void motion_ctrl_stop(motion_controller_t *ctrl);

/**
 * @brief 清除 teach_jog 命令
 * @param ctrl 控制器实例
 * @param stop_last_motor 是否停止最后一个被遥控的电机
 */
void motion_ctrl_clear_teach_jog(motion_controller_t *ctrl, bool stop_last_motor);

/**
 * @brief 进入触觉交接等待态
 * @param ctrl 控制器实例
 */
void motion_ctrl_arm_handoff_wait(motion_controller_t *ctrl);

/**
 * @brief 重置触觉交接等待态
 * @param ctrl 控制器实例
 */
void motion_ctrl_reset_handoff_wait(motion_controller_t *ctrl);

/**
 * @brief 查询交接释放流程是否完成
 * @param ctrl 控制器实例
 * @return true 已完成，可继续后续动作
 * @return false 尚未完成
 */
bool motion_ctrl_is_handoff_done(const motion_controller_t *ctrl);

/**
 * @brief 运行一次状态机控制循环
 * @param ctrl 控制器实例
 * @param dt 控制周期，单位 s
 */
void motion_ctrl_loop(motion_controller_t *ctrl, float dt);

/**
 * @brief 获取当前状态
 * @param ctrl 控制器实例
 * @return 当前状态
 */
motion_state_t motion_ctrl_get_state(const motion_controller_t *ctrl);

/**
 * @brief 更新重力补偿参数
 * @param ctrl 控制器实例
 * @param params 重力补偿参数
 */

/**
 * @brief 更新示教模式控制参数
 * @param ctrl 控制器实例
 * @param kp 刚度参数
 * @param kd 阻尼参数
 * @param enable_joint1 兼容保留参数，目前不再生效
 */
void motion_ctrl_set_teach_params(motion_controller_t *ctrl,
                                  const float kp[ROBSTRIDE_JOINT_NUM],
                                  const float kd[ROBSTRIDE_JOINT_NUM],
                                  bool enable_joint1);

/**
 * @brief 更新回放模式控制参数
 * @param ctrl 控制器实例
 * @param kp 刚度参数
 * @param kd 阻尼参数
 */
void motion_ctrl_set_playback_params(motion_controller_t *ctrl,
                                     const float kp[ROBSTRIDE_JOINT_NUM],
                                     const float kd[ROBSTRIDE_JOINT_NUM]);

/**
 * @brief 触发急停
 * @param ctrl 控制器实例
 */
void motion_ctrl_emergency_stop(motion_controller_t *ctrl);

/**
 * @brief 清除急停状态
 * @param ctrl 控制器实例
 */
void motion_ctrl_clear_emergency_stop(motion_controller_t *ctrl);

/**
 * @brief 力矩保护触发回调
 * @param joint_index 触发保护的关节索引，范围 0~3
 * @param torque_feedback 触发时的实时力矩代理值
 * @param torque_limit 配置的保护阈值
 */
void motion_ctrl_on_torque_protection(uint8_t joint_index, float torque_feedback, float torque_limit);

/**
 * @brief 获取当前状态对应的控制周期
 * @param state 当前状态
 * @return 控制周期，单位 ms
 */
static inline float get_ctrl_period(motion_state_t state) {
    switch (state) {
        case MOTION_STATE_TEACHING:
            return TEACH_MODE_LOOP_PERIOD_MS;
        case MOTION_STATE_PLAYBACK:
            return PLAYBACK_MODE_LOOP_PERIOD_MS;
        case MOTION_STATE_IDLE:
            return IDLE_MODE_LOOP_PERIOD_MS;
        default:
            return PLAYBACK_MODE_LOOP_PERIOD_MS;
    }
}

#endif /* MOTION_CTRL_H_ */
