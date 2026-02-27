/**
 * @file motion_ctrl.c
 * @brief 运控模式主控制器实现
 * @date 2026-02-27
 * @author Ma Ziteng
 */

#include "motion_ctrl.h"
#include "sys_log.h"
#include "shared_data.h"
#include <string.h>
#include <math.h>

/* 全局控制器实例 */
motion_controller_t g_motion_ctrl = {0};

/* 默认配置 */
static const motion_ctrl_config_t DEFAULT_CONFIG = {
    /* 示教模式参数 */
    .teach = {
        .kp = {0.0f, 0.0f, 0.0f, 0.0f},            /* 零刚度 */
        .kd = {0.5f, 0.5f, 0.5f, 0.5f},            /* 中等阻尼 */
        .enable_joint1 = false,                    /* 默认不启用关节1零力拖动 */
        .joint1_kd = 0.5f                          /* 关节1阻尼 */
    },
    
    /* 回放模式参数 */
    .playback = {
        .kp = {50.0f, 50.0f, 50.0f, 50.0f},        /* 中等刚度 */
        .kd = {1.0f, 1.0f, 1.0f, 1.0f}             /* 中等阻尼 */
    },
    
    /* 安全参数 */
    .max_torque = {6.0f, 6.0f, 6.0f, 6.0f},        /* 最大力矩 */
    .max_velocity = {10.0f, 10.0f, 10.0f, 10.0f}   /* 最大速度 */
};

/**
 * @brief 初始化运控控制器
 */
bool motion_ctrl_init(motion_controller_t *ctrl, const motion_ctrl_config_t *config) {
    if (ctrl == NULL) {
        LOG_E("motion_ctrl_init: ctrl is NULL");
        return false;
    }
    
    /* 初始化控制器状态 */
    memset(ctrl, 0, sizeof(motion_controller_t));
    ctrl->state = MOTION_STATE_IDLE;
    ctrl->is_initialized = false;
    ctrl->emergency_stop = false;
    
    /* 设置配置 */
    if (config != NULL) {
        memcpy(&ctrl->config, config, sizeof(motion_ctrl_config_t));
    } else {
        memcpy(&ctrl->config, &DEFAULT_CONFIG, sizeof(motion_ctrl_config_t));
    }
    
    /* 初始化重力补偿参数（如果未设置） */
    grav_init_default(&ctrl->config.grav_params);
    
    /* 初始化轨迹控制器 */
    traj_reset(&ctrl->trajectory);
    
    /* 初始化输出数组 */
    for (int i = 0; i < 4; i++) {
        ctrl->last_q_target[i] = 0.0f;
        ctrl->last_v_target[i] = 0.0f;
    }
    
    ctrl->is_initialized = true;
    LOG_I("Motion controller initialized");
    
    return true;
}

/**
 * @brief 设置运控模式（从CSP模式切换到运控模式），默认不设置就是运控模式，所以此函数可以不调用
 */
bool motion_ctrl_set_motion_mode(motion_controller_t *ctrl) {
    if (ctrl == NULL || !ctrl->is_initialized) {
        LOG_E("motion_ctrl_set_motion_mode: controller not initialized");
        return false;
    }
    
    LOG_I("Switching joints 1-4 to motion control mode...");
    
    fsp_err_t err;
    const uint8_t joint_ids[4] = {
        ROBSTRIDE_MOTOR_ID_JOINT1,
        ROBSTRIDE_MOTOR_ID_JOINT2,
        ROBSTRIDE_MOTOR_ID_JOINT3,
        ROBSTRIDE_MOTOR_ID_JOINT4
    };
    
    /* 切换到运控模式 */
    for (uint8_t i = 0; i < 4; i++) {
        err = robstride_set_run_mode(joint_ids[i], ROBSTRIDE_MODE_MOTION_CTRL);
        if (err != FSP_SUCCESS) {
            LOG_E("Failed to set joint %d to motion mode: %d", i+1, err);
            return false;
        }
        vTaskDelay(5);  /* 等待模式切换完成 */
    }
    
    LOG_I("All joints switched to motion control mode");
    return true;
}

/**
 * @brief 启动示教拖动模式
 */
bool motion_ctrl_start_teaching(motion_controller_t *ctrl)
{
    if (ctrl == NULL || !ctrl->is_initialized) {
        LOG_E("motion_ctrl_start_teaching: controller not initialized");
        return false;
    }
    
    if (ctrl->state != MOTION_STATE_IDLE) {
        LOG_W("Cannot start teaching while in state %d", ctrl->state);
        return false;
    }
    
    /* 确保关节处于运控模式 */
    // if (!motion_ctrl_set_motion_mode(ctrl)) {
    //     return false;
    // }
    
    /* 重置轨迹控制器 */
    traj_reset(&ctrl->trajectory);
    ctrl->seq_running = false;
    
    ctrl->state = MOTION_STATE_TEACHING;
    LOG_I("Teaching mode started");
    
    return true;
}

/**
 * @brief 启动回放模式
 */
bool motion_ctrl_start_playback(motion_controller_t *ctrl, const action_sequence_t *seq) {
    if (ctrl == NULL || !ctrl->is_initialized || seq == NULL) {
        LOG_E("motion_ctrl_start_playback: invalid parameters");
        return false;
    }
    
    if (ctrl->state != MOTION_STATE_IDLE) {
        LOG_W("Cannot start playback while in state %d", ctrl->state);
        return false;
    }
    
    if (seq->frame_count < 2) {
        LOG_E("Action sequence must have at least 2 frames");
        return false;
    }
    
    /* 确保关节处于运控模式 */
    // if (!motion_ctrl_set_motion_mode(ctrl)) {
    //     return false;
    // }
    
    /* 保存序列并初始化轨迹 */
    memcpy(&ctrl->current_seq, seq, sizeof(action_sequence_t));
    
    if (!traj_init_from_sequence(&ctrl->trajectory, seq)) {
        LOG_E("Failed to initialize trajectory from sequence");
        return false;
    }
    
    ctrl->seq_running = true;
    ctrl->seq_start_time = 0.0f;
    ctrl->state = MOTION_STATE_PLAYBACK;
    LOG_I("Playback mode started with %d frames", seq->frame_count);
    
    return true;
}

/**
 * @brief 停止当前运行模式
 */
void motion_ctrl_stop(motion_controller_t *ctrl) {
    if (ctrl == NULL || !ctrl->is_initialized) {
        return;
    }
    
    /* 停止所有电机 */
    robstride_stop(ROBSTRIDE_MOTOR_ID_JOINT1);
    robstride_stop(ROBSTRIDE_MOTOR_ID_JOINT2);
    robstride_stop(ROBSTRIDE_MOTOR_ID_JOINT3);
    robstride_stop(ROBSTRIDE_MOTOR_ID_JOINT4);
    robstride_stop(ROBSTRIDE_MOTOR_ID_GRIPPER);
    
    /* 重置状态 */
    ctrl->state = MOTION_STATE_IDLE;
    ctrl->seq_running = false;
    traj_reset(&ctrl->trajectory);
    
    LOG_I("Motion control stopped");
}

/**
 * @brief 计算重力补偿力矩
 */
static void compute_gravity_compensation(motion_controller_t *ctrl, float q[4], float tau_ff[4]) {
    /* 读取当前关节角度 */
    q[0] = g_motors[0].feedback.position;
    q[1] = g_motors[1].feedback.position;
    q[2] = g_motors[2].feedback.position;
    q[3] = g_motors[3].feedback.position;
    
    /* 计算重力补偿 */
    grav_compute(&ctrl->config.grav_params, q, tau_ff);
}

/**
 * @brief 示教模式控制循环
 */
static void teach_control_loop(motion_controller_t *ctrl) {
    float q[4] = {0};
    float v_target[4] = {0};  /* 速度目标为0 */
    float tau_ff[4] = {0};
    
    /* 读取当前位置作为目标位置（保持当前位置） */
    q[0] = g_motors[0].feedback.position;
    q[1] = g_motors[1].feedback.position;
    q[2] = g_motors[2].feedback.position;
    q[3] = g_motors[3].feedback.position;
    
    /* 计算重力补偿 */
    compute_gravity_compensation(ctrl, q, tau_ff);
    
    /* 对每个关节发送控制指令 */
    const uint8_t joint_ids[4] = {
        ROBSTRIDE_MOTOR_ID_JOINT1,
        ROBSTRIDE_MOTOR_ID_JOINT2,
        ROBSTRIDE_MOTOR_ID_JOINT3,
        ROBSTRIDE_MOTOR_ID_JOINT4
    };
    
    for (int i = 0; i < 4; i++) {
        float kp = ctrl->config.teach.kp[i];
        float kd = ctrl->config.teach.kd[i];
        
        /* 关节1可选是否启用零力拖动 */
        if (i == 0 && !ctrl->config.teach.enable_joint1) {
            kp = 50.0f;  /* 保持一定刚度锁定位置 */
            kd = ctrl->config.teach.joint1_kd;
        }   
        
        /* 力矩限幅 */
        float torque = tau_ff[i];
        if (torque > ctrl->config.max_torque[i]) {
            torque = ctrl->config.max_torque[i];
        } else if (torque < -ctrl->config.max_torque[i]) {
            torque = -ctrl->config.max_torque[i];
        }
        
        /* 发送运控指令 */
        robstride_motion_control(joint_ids[i], q[i], v_target[i], kp, kd, torque);
    }
}

/**
 * @brief 回放模式控制循环
 */
static void playback_control_loop(motion_controller_t *ctrl, float dt) {
    if (!ctrl->seq_running) return;
    
    ctrl->seq_start_time += dt;
    
    float q_target[4] = {0};
    float v_target[4] = {0};
    bool seg_done = false, seq_done = false;
    
    traj_eval(&ctrl->trajectory, ctrl->seq_start_time, q_target, v_target, &seg_done, &seq_done);
    
    /* 保存目标值 */
    memcpy(ctrl->last_q_target, q_target, 4 * sizeof(float));
    memcpy(ctrl->last_v_target, v_target, 4 * sizeof(float));
    
    /* 计算重力补偿 */
    float tau_ff[4] = {0};
    float q_current[4] = {
        g_motors[0].feedback.position,
        g_motors[1].feedback.position,
        g_motors[2].feedback.position,
        g_motors[3].feedback.position
    };
    
    compute_gravity_compensation(ctrl, q_current, tau_ff);
    
    /* 对每个关节发送控制指令 */
    const uint8_t joint_ids[4] = {
        ROBSTRIDE_MOTOR_ID_JOINT1,
        ROBSTRIDE_MOTOR_ID_JOINT2,
        ROBSTRIDE_MOTOR_ID_JOINT3,
        ROBSTRIDE_MOTOR_ID_JOINT4
    };
    
    for (int i = 0; i < 4; i++) {
        float kp = ctrl->config.playback.kp[i];
        float kd = ctrl->config.playback.kd[i];
        
        /* 速度限幅 */
        float v = v_target[i];
        if (v > ctrl->config.max_velocity[i]) {
            v = ctrl->config.max_velocity[i];
        } else if (v < -ctrl->config.max_velocity[i]) {
            v = -ctrl->config.max_velocity[i];
        }
        
        /* 力矩限幅 */
        float torque = tau_ff[i];
        if (torque > ctrl->config.max_torque[i]) {
            torque = ctrl->config.max_torque[i];
        } else if (torque < -ctrl->config.max_torque[i]) {
            torque = -ctrl->config.max_torque[i];
        }
        
        /* 发送运控指令 */
        robstride_motion_control(joint_ids[i], q_target[i], v, kp, kd, torque);
    }
    
    /* 处理段完成事件（例如夹爪动作） */
    if (seg_done) {
        /* 获取当前段的动作 */
        uint32_t current_seg = ctrl->trajectory.current_segment;
        if (current_seg < MAX_FRAMES_PER_SEQ - 1) {
            uint8_t action = ctrl->trajectory.segments[current_seg][0].action;
            
            /* 处理夹爪动作 */
            if (action == ACTION_GRIP) {
                /* TODO: 调用夹爪抓取函数 */
                LOG_I("Segment %d: GRIP action triggered", current_seg);
            } else if (action == ACTION_RELEASE) {
                /* TODO: 调用夹爪释放函数 */
                LOG_I("Segment %d: RELEASE action triggered", current_seg);
            }
        }
    }
    
    /* 检查序列是否完成 */
    if (seq_done) {
        LOG_I("Playback sequence completed");
        ctrl->seq_running = false;
        ctrl->state = MOTION_STATE_IDLE;
    }
}

/**
 * @brief 运控模式主控制循环
 */
void motion_ctrl_loop(motion_controller_t *ctrl, float dt) {
    if (ctrl == NULL || !ctrl->is_initialized || ctrl->emergency_stop) return;
    
    switch (ctrl->state) {
        case MOTION_STATE_TEACHING:
            teach_control_loop(ctrl);
            break;
            
        case MOTION_STATE_PLAYBACK:
            playback_control_loop(ctrl, dt);
            break;
            
        case MOTION_STATE_IDLE:
        case MOTION_STATE_ERROR:
            /* 空闲或错误状态，不进行控制 */
            break;
            
        default:
            LOG_E("Unknown motion state: %d", ctrl->state);
            ctrl->state = MOTION_STATE_ERROR;
            break;
    }
}

/**
 * @brief 获取当前状态
 */
motion_state_t motion_ctrl_get_state(const motion_controller_t *ctrl) {
    return (ctrl != NULL) ? ctrl->state : MOTION_STATE_ERROR;
}

/**
 * @brief 设置重力补偿参数
 */
void motion_ctrl_set_grav_params(motion_controller_t *ctrl, const grav_param_t *params) {
    if (ctrl != NULL && params != NULL) memcpy(&ctrl->config.grav_params, params, sizeof(grav_param_t));
}

/**
 * @brief 设置示教模式参数
 */
void motion_ctrl_set_teach_params(motion_controller_t *ctrl, const float kp[4], 
                                  const float kd[4], bool enable_joint1) {
    if (ctrl == NULL) {
        return;
    }
    
    if (kp != NULL) {
        memcpy(ctrl->config.teach.kp, kp, 4 * sizeof(float));
    }
    
    if (kd != NULL) {
        memcpy(ctrl->config.teach.kd, kd, 4 * sizeof(float));
    }
    
    ctrl->config.teach.enable_joint1 = enable_joint1;
}

/**
 * @brief 设置回放模式参数
 */
void motion_ctrl_set_playback_params(motion_controller_t *ctrl, const float kp[4], 
                                     const float kd[4]) {
    if (ctrl == NULL) return;
    if (kp != NULL) memcpy(ctrl->config.playback.kp, kp, 4 * sizeof(float));
    if (kd != NULL) memcpy(ctrl->config.playback.kd, kd, 4 * sizeof(float));
}

/**
 * @brief 触发急停
 */
void motion_ctrl_emergency_stop(motion_controller_t *ctrl) {
    if (ctrl == NULL || !ctrl->is_initialized) return;
    ctrl->emergency_stop = true;
    motion_ctrl_stop(ctrl);
    LOG_E("Emergency stop triggered");
}

/**
 * @brief 清除急停状态
 */
void motion_ctrl_clear_emergency_stop(motion_controller_t *ctrl) {
    if (ctrl == NULL || !ctrl->is_initialized) {
        return;
    }
    ctrl->emergency_stop = false;
    LOG_I("Emergency stop cleared");
}