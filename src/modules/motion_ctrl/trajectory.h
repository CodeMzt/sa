/**
 * @file trajectory.h
 * @brief 自然三次样条轨迹生成器
 * @date 2026-02-27
 * @author Ma Ziteng
 * 
 * 自然三次样条（Natural Cubic Spline）轨迹生成
 * 支持多关键点插值，生成C2连续的平滑轨迹
 * 输出位置q(t)和速度v(t)的解析表达式
 */

#ifndef TRAJECTORY_H_
#define TRAJECTORY_H_

#include <stdint.h>
#include <stdbool.h>
#include "nvm_types.h"

/* 最大关节数 */
#define TRAJ_MAX_JOINTS 4

/* 轨迹状态 */
typedef enum {
    TRAJ_IDLE = 0,     /* 空闲状态 */
    TRAJ_RUNNING,      /* 运行中 */
    TRAJ_COMPLETED,    /* 已完成 */
    TRAJ_ERROR         /* 错误状态 */
} traj_state_t;

/* 轨迹段信息 */
typedef struct {
    float a[4];        /* 三次多项式系数: a + b*t + c*t^2 + d*t^3 */
    float duration;    /* 段持续时间（秒） */
    uint8_t action;    /* 到达该段末尾时触发的动作 */
} traj_segment_t;

/* 轨迹控制器实例 */
typedef struct {
    traj_state_t state;               /* 当前状态 */
    uint32_t frame_count;             /* 总关键帧数 */
    uint32_t total_segments;          /* 总段数（帧数-1） */
    float total_duration;             /* 总持续时间（秒） */
    float elapsed_time;               /* 已运行时间（秒） */
    uint32_t current_segment;         /* 当前段索引 */
    float segment_elapsed;            /* 当前段已运行时间（秒） */
    
    /* 各关节的样条系数 */
    traj_segment_t segments[MAX_FRAMES_PER_SEQ - 1][TRAJ_MAX_JOINTS];
    
    /* 原始关键帧（用于调试） */
    motion_frame_t frames[MAX_FRAMES_PER_SEQ];
} traj_controller_t;

/**
 * @brief 从动作序列初始化轨迹
 * @param traj 轨迹控制器实例指针
 * @param seq 动作序列指针
 * @return true 初始化成功，false 失败
 * @note 每组示教3帧（加上当前位置帧，总共4帧），样条能处理N=3的情况
 */
bool traj_init_from_sequence(traj_controller_t *traj, const action_sequence_t *seq);

/**
 * @brief 轨迹求值
 * @param traj 轨迹控制器实例指针
 * @param t 归一化时间（秒），通常使用elapsed_time
 * @param q_out 输出位置数组（rad），长度4
 * @param v_out 输出速度数组（rad/s），长度4
 * @param seg_done 当前段是否完成（输出）
 * @param seq_done 整个序列是否完成（输出）
 * @note 如果t超过总时间，则输出最后一个关键帧的值
 */
void traj_eval(traj_controller_t *traj, float t, float q_out[4], float v_out[4], 
               bool *seg_done, bool *seq_done);

/**
 * @brief 重置轨迹控制器
 * @param traj 轨迹控制器实例指针
 */
void traj_reset(traj_controller_t *traj);

/**
 * @brief 步进轨迹控制器
 * @param traj 轨迹控制器实例指针
 * @param dt 时间步长（秒）
 * @param q_out 输出位置数组（rad），长度4
 * @param v_out 输出速度数组（rad/s），长度4
 * @return true 轨迹仍在运行，false 轨迹已完成
 * @note 自动更新内部时间并求值
 */
bool traj_step(traj_controller_t *traj, float dt, float q_out[4], float v_out[4]);

/**
 * @brief 获取轨迹状态
 * @param traj 轨迹控制器实例指针
 * @return 当前轨迹状态
 */
traj_state_t traj_get_state(const traj_controller_t *traj);

#endif /* TRAJECTORY_H_ */