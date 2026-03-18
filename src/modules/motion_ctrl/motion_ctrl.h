/**
 * @file motion_ctrl.h
 * @brief 运控模式主控制器
 * @date 2026-02-27
 * @author Ma Ziteng
 * 
 * 运控主状态机下的机械臂示教/空闲/回放控制
 * 三种模式共用同一套控制器参数，仅目标生成行为不同：
 * 1. 示教模式：由 teach_jog 命令驱动单电机遥控
 * 2. 空闲模式：不发送周期控制指令，仅维持空闲状态
 * 3. 回放模式：按轨迹目标执行动作
 */

#ifndef MOTION_CTRL_H_
#define MOTION_CTRL_H_

#include <stdint.h>
#include <stdbool.h>
#include "robstride_motor.h"
#include "gravity_comp.h"
#include "trajectory.h"

/* 控制器配置参数 */
#define TEACH_MODE_LOOP_FREQ_HZ      100.0f   /* 示教模式控制频率 100Hz */
#define TEACH_MODE_LOOP_PERIOD_MS    10.0f   /* 示教模式控制周期 10ms */
#define PLAYBACK_MODE_LOOP_FREQ_HZ   100.0f  /* 回放模式控制频率 100Hz */
#define PLAYBACK_MODE_LOOP_PERIOD_MS 10.0f    /* 回放模式控制周期 10ms */
#define IDLE_MODE_LOOP_FREQ_HZ       100.0f   /* IDLE轮询频率 100Hz（无控制下发） */
#define IDLE_MODE_LOOP_PERIOD_MS     10.0f    /* IDLE轮询周期 10ms */

/* 运行状态枚举 */
typedef enum {
    MOTION_STATE_IDLE = 0,      /* 空闲状态 */
    MOTION_STATE_TEACHING,      /* 示教拖动模式 */
    MOTION_STATE_PLAYBACK,      /* 回放模式 */
    MOTION_STATE_ERROR          /* 错误状态 */
} motion_state_t;

/* 控制器配置 */
typedef struct {
    /* 三种模式共用控制器参数 */
    struct {
        float kp[4];            /* 位置刚度 */
        float kd[4];            /* 阻尼系数 */
    } controller;
    
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

    /* teach_jog 运行上下文（复用 TEACHING 状态） */
    uint8_t teach_jog_motor_id; /* 当前遥控电机ID，0表示无活动遥控 */
    float teach_jog_q_ref;      /* 遥控位置参考（关节侧rad） */
    bool teach_jog_q_valid;     /* 遥控位置参考是否已初始化 */
    
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
 * @note 必须在使用前调用，控制输出与反馈读取均由本模块直接处理
 */
bool motion_ctrl_init(motion_controller_t *ctrl, const motion_ctrl_config_t *config);

/**
 * @brief 设置控制模式（从其他模式切换到运控模式）
 * @param ctrl 控制器实例指针
 * @return true 成功，false 失败
 * @note 将关节1~4切换到运控模式并重新使能
 */
bool motion_ctrl_set_motion_mode(motion_controller_t *ctrl);

/**
 * @brief 启动示教遥控模式
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
 * @note 硬停：下发电机STOP后切换到空闲状态
 */
void motion_ctrl_stop(motion_controller_t *ctrl);

/**
 * @brief 清除当前遥控命令并可选停止最后一个遥控电机
 * @param ctrl 控制器实例指针
 * @param stop_last_motor true: 下发 stop；false: 仅清状态
 */
void motion_ctrl_clear_teach_jog(motion_controller_t *ctrl, bool stop_last_motor);

/**
 * @brief 运控模式主控制循环（应按当前模式控制周期调用，默认100Hz）
 * @param ctrl 控制器实例指针
 * @param dt 时间步长（秒）
 * @note TEACH/PLAYBACK下发控制，IDLE仅维护状态不下发
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
 * @param kp 位置刚度数组（长度4）
 * @param kd 阻尼系数数组（长度4）
 * @param enable_joint1 兼容保留参数，不再生效
 * @note 本接口会更新三种模式共用的控制器参数
 */
void motion_ctrl_set_teach_params(motion_controller_t *ctrl, const float kp[4], 
                                  const float kd[4], bool enable_joint1);

/**
 * @brief 设置回放模式参数
 * @param ctrl 控制器实例指针
 * @param kp 位置刚度数组（长度4）
 * @param kd 阻尼系数数组（长度4）
 * @note 本接口会更新三种模式共用的控制器参数
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


/**
 * @brief 获取当前模式的控制周期（ms）
 * @param state 当前运行状态
 * @return 控制周期（ms）
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
            return PLAYBACK_MODE_LOOP_PERIOD_MS;  /* 默认使用回放模式频率 */
    }
}

#endif /* MOTION_CTRL_H_ */