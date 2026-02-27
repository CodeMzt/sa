/**
 * @file motion_ctrl.h
 * @brief 运控模式主控制器
 * @date 2026-02-27
 * @author Ma Ziteng
 * 
 * 运控模式下的机械臂示教/回放控制
 * 支持两种运行状态：
 * 1. 示教拖动模式（零力拖动）：抵消重力+阻尼，手推能动，松手能停
 * 2. 回放模式（轨迹跟踪）：自然三次样条轨迹跟踪
 */

#ifndef MOTION_CTRL_H_
#define MOTION_CTRL_H_

#include <stdint.h>
#include <stdbool.h>
#include "robstride_motor.h"
#include "gravity_comp.h"
#include "trajectory.h"

/* 控制器配置参数 */
#define MOTION_CTRL_LOOP_FREQ_HZ     200.0f  /* 控制频率 200Hz */
#define MOTION_CTRL_LOOP_PERIOD_MS   5        /* 控制周期 5ms */

/* 运行状态枚举 */
typedef enum {
    MOTION_STATE_IDLE = 0,      /* 空闲状态 */
    MOTION_STATE_TEACHING,      /* 示教拖动模式 */
    MOTION_STATE_PLAYBACK,      /* 回放模式 */
    MOTION_STATE_ERROR          /* 错误状态 */
} motion_state_t;

/* 控制器模式配置 */
typedef struct {
    /* 示教模式参数 */
    struct {
        float kp[4];            /* 位置刚度（通常为0） */
        float kd[4];            /* 阻尼系数（0.2~1.0） */
        bool enable_joint1;     /* 是否启用关节1的零力拖动 */
        float joint1_kd;        /* 关节1的阻尼系数 */
    } teach;
    
    /* 回放模式参数 */
    struct {
        float kp[4];            /* 位置刚度（可配，不同关节可不同） */
        float kd[4];            /* 阻尼系数 */
    } playback;
    
    /* 重力补偿参数 */
    grav_param_t grav_params;   /* 重力补偿参数 */
    
    /* 安全参数 */
    float max_torque[4];        /* 最大力矩限制（Nm） */
    float max_velocity[4];      /* 最大速度限制（rad/s） */
} motion_ctrl_config_t;

/* 控制器状态 */
typedef struct {
    motion_state_t state;       /* 当前运行状态 */
    motion_ctrl_config_t config;     /* 当前配置 */
    
    /* 轨迹跟踪相关 */
    traj_controller_t trajectory;  /* 轨迹控制器实例 */
    action_sequence_t current_seq; /* 当前动作序列 */
    bool seq_running;           /* 序列是否正在运行 */
    float seq_start_time;       /* 序列开始时间（秒） */
    
    /* 控制输出 */
    float last_q_target[4];     /* 上次目标位置 */
    float last_v_target[4];     /* 上次目标速度 */
    
    /* 状态标志 */
    bool is_initialized;        /* 是否已初始化 */
    bool emergency_stop;        /* 急停标志 */
} motion_controller_t;

/* 全局控制器实例 */
extern motion_controller_t g_motion_ctrl;

/**
 * @brief 初始化运控控制器
 * @param ctrl 控制器实例指针
 * @param config 配置参数（可为NULL使用默认配置）
 * @return true 成功，false 失败
 * @note 必须在使用前调用，初始化关节模式为运控模式
 */
bool motion_ctrl_init(motion_controller_t *ctrl, const motion_ctrl_config_t *config);

/**
 * @brief 设置运控模式（从CSP模式切换到运控模式）
 * @param ctrl 控制器实例指针
 * @return true 成功，false 失败
 * @note 将关节1~4从CSP模式切换到运控模式
 */
bool motion_ctrl_set_motion_mode(motion_controller_t *ctrl);

/**
 * @brief 启动示教拖动模式
 * @param ctrl 控制器实例指针
 * @return true 成功，false 失败
 */
bool motion_ctrl_start_teaching(motion_controller_t *ctrl);

/**
 * @brief 启动回放模式
 * @param ctrl 控制器实例指针
 * @param seq 要回放的动作序列
 * @return true 成功，false 失败
 */
bool motion_ctrl_start_playback(motion_controller_t *ctrl, const action_sequence_t *seq);

/**
 * @brief 停止当前运行模式
 * @param ctrl 控制器实例指针
 * @note 停止所有关节的控制，切换到空闲状态
 */
void motion_ctrl_stop(motion_controller_t *ctrl);

/**
 * @brief 运控模式主控制循环（应在1kHz任务中调用）
 * @param ctrl 控制器实例指针
 * @param dt 时间步长（秒）
 * @note 根据当前状态计算控制输出并下发到电机
 */
void motion_ctrl_loop(motion_controller_t *ctrl, float dt);

/**
 * @brief 获取当前状态
 * @param ctrl 控制器实例指针
 * @return 当前运行状态
 */
motion_state_t motion_ctrl_get_state(const motion_controller_t *ctrl);

/**
 * @brief 设置重力补偿参数
 * @param ctrl 控制器实例指针
 * @param params 重力补偿参数
 */
void motion_ctrl_set_grav_params(motion_controller_t *ctrl, const grav_param_t *params);

/**
 * @brief 设置示教模式参数
 * @param ctrl 控制器实例指针
 * @param kp 位置刚度数组（长度4，通常为0）
 * @param kd 阻尼系数数组（长度4）
 * @param enable_joint1 是否启用关节1的零力拖动
 */
void motion_ctrl_set_teach_params(motion_controller_t *ctrl, const float kp[4], 
                                  const float kd[4], bool enable_joint1);

/**
 * @brief 设置回放模式参数
 * @param ctrl 控制器实例指针
 * @param kp 位置刚度数组（长度4）
 * @param kd 阻尼系数数组（长度4）
 */
void motion_ctrl_set_playback_params(motion_controller_t *ctrl, const float kp[4], 
                                     const float kd[4]);

/**
 * @brief 触发急停
 * @param ctrl 控制器实例指针
 */
void motion_ctrl_emergency_stop(motion_controller_t *ctrl);

/**
 * @brief 清除急停状态
 * @param ctrl 控制器实例指针
 */
void motion_ctrl_clear_emergency_stop(motion_controller_t *ctrl);

#endif /* MOTION_CTRL_H_ */