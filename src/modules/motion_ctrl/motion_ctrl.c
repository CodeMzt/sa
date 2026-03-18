/**
 * @file motion_ctrl.c
 * @brief 运控模式主控制器实现
 * @date 2026-02-27
 * @author Ma Ziteng
 */

#include "motion_ctrl.h"
#include "sys_log.h"
#include "shared_data.h"
#include "nvm_manager.h"
#include <string.h>
#include <math.h>

#define DEG2RAD_F 0.01745329251994329577f
#define GRIPPER_ACTION_KP      8.0f
#define GRIPPER_ACTION_KD      0.8f

/* 全局控制器实例 */
motion_controller_t g_motion_ctrl = {0};

static fsp_err_t trigger_gripper_action(uint8_t action) {
    if (action == ACTION_GRIP) {
        return robstride_gripper_grasp(ROBSTRIDE_MOTOR_ID_GRIPPER,
                                       ROBSTRIDE_GRIPPER_CLOSED_POS_RAD,
                                       GRIPPER_ACTION_KP,
                                       GRIPPER_ACTION_KD,
                                       0.0f);
    }

    if (action == ACTION_RELEASE) {
        return robstride_gripper_release(ROBSTRIDE_MOTOR_ID_GRIPPER,
                                         GRIPPER_ACTION_KP,
                                         GRIPPER_ACTION_KD);
    }

    return FSP_ERR_ASSERTION;
}

static bool capture_idle_hold_targets(motion_controller_t *ctrl) {
    if (ctrl == NULL) return false;

    float q_abs[4] = {0};
    if (!motion_adapter_capture_abs(&ctrl->adapter, q_abs)) {
        ctrl->idle_hold_valid = false;
        LOG_E("Capture idle abs failed");
        return false;
    }

    ctrl->idle_q_hold[0] = q_abs[0];
    ctrl->idle_q_hold[1] = q_abs[1];
    ctrl->idle_q_hold[2] = q_abs[2];
    ctrl->idle_q_hold[3] = q_abs[3];
    ctrl->idle_hold_valid = true;

    memcpy(ctrl->last_q_target, ctrl->idle_q_hold, 4 * sizeof(float));
    memset(ctrl->last_v_target, 0, 4 * sizeof(float));

    return true;
}

static void motion_ctrl_set_common_pd(motion_controller_t *ctrl, const float kp[4], const float kd[4]) {
    if (ctrl == NULL) return;
    if (kp != NULL) memcpy(ctrl->config.controller.kp, kp, 4 * sizeof(float));
    if (kd != NULL) memcpy(ctrl->config.controller.kd, kd, 4 * sizeof(float));
}

/* 默认配置 */
static const motion_ctrl_config_t DEFAULT_CONFIG = {
    /* 三模式共用控制器参数 */
    .controller = {
        .kp = {8.0f, 8.0f, 8.0f, 8.0f},
        .kd = {0.8f, 0.8f, 0.8f, 0.8f}
    },
    
    /* 安全参数 */
    .max_torque = {6.0f, 6.0f, 6.0f, 6.0f},        /* 最大力矩 */
    .max_velocity = {0.2f, 0.2f, 0.2f, 0.2f}   /* 最大速度 */
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
    ctrl->idle_hold_valid = false;
    
    /* 设置配置 */
    if (config != NULL) memcpy(&ctrl->config, config, sizeof(motion_ctrl_config_t));
    else {
        memcpy(&ctrl->config, &DEFAULT_CONFIG, sizeof(motion_ctrl_config_t));
        /* 仅默认配置时填充默认重力参数 */
        grav_init_default(&ctrl->config.grav_params);
    }
    
    /* 初始化轨迹控制器 */
    traj_reset(&ctrl->trajectory);
    
    /* 初始化输出数组 */
    for (int i = 0; i < 4; i++) {
        ctrl->last_q_target[i] = 0.0f;
        ctrl->last_v_target[i] = 0.0f;
        ctrl->idle_q_hold[i] = 0.0f;
    }

    motion_adapter_config_t adapter_cfg = {0};
    adapter_cfg.nvm_save_interval_ms = 2000U;
    for (int i = 0; i < 4; i++) {
        adapter_cfg.kp[i] = ctrl->config.controller.kp[i];
        adapter_cfg.kd[i] = ctrl->config.controller.kd[i];
        adapter_cfg.max_velocity[i] = ctrl->config.max_velocity[i];
        adapter_cfg.max_acceleration[i] = fmaxf(8.0f * ctrl->config.max_velocity[i], 8.0f);
    }

    if (!motion_adapter_init(&ctrl->adapter, &adapter_cfg)) {
        LOG_E("Failed to initialize motion adapter");
        return false;
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
    
    LOG_I("Switching joints 1-4 to speed control mode...");
    
    fsp_err_t err;
    const uint8_t joint_ids[4] = {
        ROBSTRIDE_MOTOR_ID_JOINT1,
        ROBSTRIDE_MOTOR_ID_JOINT2,
        ROBSTRIDE_MOTOR_ID_JOINT3,
        ROBSTRIDE_MOTOR_ID_JOINT4
    };
    
    /* 切换到速度模式 */
    for (uint8_t i = 0; i < 4; i++) {
        robstride_stop(joint_ids[i]);
        err = robstride_set_run_mode(joint_ids[i], ROBSTRIDE_MODE_SPEED);
        if (err != FSP_SUCCESS) {
            LOG_E("Failed to set joint %d to speed mode: %d", i+1, err);
            return false;
        }
        vTaskDelay(5);  /* 等待模式切换完成 */
        err = robstride_enable(joint_ids[i]);
        if (err != FSP_SUCCESS) {
            LOG_E("Failed to set joint %d to speed mode: %d", i+1, err);
            return false;
        }
    }
    
    LOG_I("All joints switched to speed control mode");
    return true;
}

/**
 * @brief 启动示教拖动模式
 */
bool motion_ctrl_start_teaching(motion_controller_t *ctrl) {
    if (ctrl == NULL || !ctrl->is_initialized) {
        LOG_E("motion_ctrl_start_teaching: controller not initialized");
        return false;
    }
    if (ctrl->state != MOTION_STATE_IDLE) {
        LOG_W("Cannot start teaching while in state %d", ctrl->state);
        return false;
    }
    
    /* 确保关节处于运控模式 */
    if (!motion_ctrl_set_motion_mode(ctrl)) {
        LOG_E("Failed to set motion mode before teaching");
        return false;
    }
    
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
    
    if (seq->frame_count < 2 || seq->frame_count > MAX_FRAMES_PER_SEQ) {
        LOG_E("Action sequence frame_count invalid: %u", (unsigned int)seq->frame_count);
        return false;
    }
    
    /* 确保关节处于运控模式 */
    if (!motion_ctrl_set_motion_mode(ctrl)) {
        LOG_E("Failed to set motion mode before playback");
        return false;
    }
    
    /* 保存原始序列（存储单位：度） */
    memcpy(&ctrl->current_seq, seq, sizeof(action_sequence_t));

    /* 回放控制统一使用弧度，并补入当前姿态作为起点帧 */
    action_sequence_t seq_ctrl = {0};
    if (seq->frame_count < MAX_FRAMES_PER_SEQ) {
        float q_abs_start[4] = {0};
        if (!motion_adapter_capture_abs(&ctrl->adapter, q_abs_start)) {
            LOG_E("Playback start rejected: capture abs failed");
            return false;
        }

        seq_ctrl.frame_count = seq->frame_count + 1U;
        seq_ctrl.frames[0].angle_m1 = q_abs_start[0];
        seq_ctrl.frames[0].angle_m2 = q_abs_start[1];
        seq_ctrl.frames[0].angle_m3 = q_abs_start[2];
        seq_ctrl.frames[0].angle_m4 = q_abs_start[3];
        seq_ctrl.frames[0].duration_ms = 0U;
        seq_ctrl.frames[0].action = MOVE_ONLY;

        for (uint32_t i = 0; i < seq->frame_count; i++) {
            float angle_m1 = seq->frames[i].angle_m1 * DEG2RAD_F;
            float angle_m2 = seq->frames[i].angle_m2 * DEG2RAD_F;
            float angle_m3 = seq->frames[i].angle_m3 * DEG2RAD_F;
            float angle_m4 = seq->frames[i].angle_m4 * DEG2RAD_F;

            seq_ctrl.frames[i + 1U].angle_m1 = angle_m1;
            seq_ctrl.frames[i + 1U].angle_m2 = angle_m2;
            seq_ctrl.frames[i + 1U].angle_m3 = angle_m3;
            seq_ctrl.frames[i + 1U].angle_m4 = angle_m4;
            seq_ctrl.frames[i + 1U].duration_ms = seq->frames[i].duration_ms;
            seq_ctrl.frames[i + 1U].action = seq->frames[i].action;
        }
    } else {
        seq_ctrl = *seq;
        for (uint32_t i = 0; i < seq_ctrl.frame_count; i++) {
            seq_ctrl.frames[i].angle_m1 *= DEG2RAD_F;
            seq_ctrl.frames[i].angle_m2 *= DEG2RAD_F;
            seq_ctrl.frames[i].angle_m3 *= DEG2RAD_F;
            seq_ctrl.frames[i].angle_m4 *= DEG2RAD_F;
        }
        LOG_W("Sequence at max frame count, playback starts from first keyframe (no current-pose prepend)");
    }
    
    if (!traj_init_from_sequence(&ctrl->trajectory, &seq_ctrl)) {
        LOG_E("Failed to initialize trajectory from sequence");
        return false;
    }
    
    ctrl->seq_running = true;
    ctrl->seq_start_time = 0.0f;
    ctrl->state = MOTION_STATE_PLAYBACK;
    LOG_I("Playback mode started with %d frames, total_duration=%.3fs",
        seq->frame_count,
        ctrl->trajectory.total_duration);
    
    return true;
}

/**
 * @brief 停止当前运行模式
 */
void motion_ctrl_stop(motion_controller_t *ctrl) {
    if (ctrl == NULL || !ctrl->is_initialized) return;

    /* 软停：仅切换到IDLE，不下发电机STOP，保持电机使能与运控模式 */
    if (!capture_idle_hold_targets(ctrl)) {
        ctrl->state = MOTION_STATE_ERROR;
        ctrl->seq_running = false;
        traj_reset(&ctrl->trajectory);
        LOG_E("Motion control stop rejected: capture abs failed");
        return;
    }

    ctrl->state = MOTION_STATE_IDLE;
    ctrl->seq_running = false;
    traj_reset(&ctrl->trajectory);
    motion_adapter_force_persist_sector(&ctrl->adapter);

    LOG_I("Motion control switched to IDLE (motors remain enabled)");
}

/**
 * @brief 示教模式控制循环
 * @param ctrl 控制器实例指针
 * @param dt 时间步长（秒）
 */
static void teach_control_loop(motion_controller_t *ctrl, float dt) {
    float q_target[4] = {0};
    float v_target[4] = {0};  /* 速度目标为0 */

    if (!motion_adapter_capture_abs(&ctrl->adapter, q_target)) {
        LOG_E("Teach capture abs failed");
        return;
    }

    motion_adapter_set_pd(&ctrl->adapter, ctrl->config.controller.kp, ctrl->config.controller.kd);
    if (!motion_adapter_step(&ctrl->adapter, dt, q_target, v_target, NULL, NULL, NULL)) {
        LOG_E("Teach adapter step failed");
    }
}

/**
 * @brief 回放模式控制循环
 */
static void playback_control_loop(motion_controller_t *ctrl, float dt) {
    if (!ctrl->seq_running) return;
    static uint32_t s_track_log_div = 0U;

    float prev_time = ctrl->seq_start_time;
    ctrl->seq_start_time += dt;
    
    float q_target[4] = {0};
    float v_target[4] = {0};
    bool seg_done = false, seq_done = false;
    
    traj_eval(&ctrl->trajectory, ctrl->seq_start_time, q_target, v_target, &seg_done, &seq_done);

    const sys_config_t *sys_cfg = nvm_get_sys_config();
    for (int i = 0; i < 4; i++) {
        float q_min = ((float)sys_cfg->angle_min[i] * 0.01f) * DEG2RAD_F;
        float q_max = ((float)sys_cfg->angle_max[i] * 0.01f) * DEG2RAD_F;
        if (q_min > q_max) {
            float tmp = q_min;
            q_min = q_max;
            q_max = tmp;
        }
        if (q_target[i] < q_min) q_target[i] = q_min;
        else if (q_target[i] > q_max) q_target[i] = q_max;
    }
    
    /* 保存目标值 */
    memcpy(ctrl->last_q_target, q_target, 4 * sizeof(float));
    memcpy(ctrl->last_v_target, v_target, 4 * sizeof(float));
    
    float q_current[4] = {0};
    float v_cmd[4] = {0};
    float a_cmd[4] = {0};

    motion_adapter_set_pd(&ctrl->adapter, ctrl->config.controller.kp, ctrl->config.controller.kd);
    if (!motion_adapter_step(&ctrl->adapter, dt, q_target, v_target, q_current, v_cmd, a_cmd)) {
        LOG_E("Playback adapter step failed");
    }

    if ((s_track_log_div++ % 20U) == 0U) {
        LOG_D("Track J1 c=%.3f t=%.3f e=%.3f | J2 c=%.3f t=%.3f e=%.3f | J3 c=%.3f t=%.3f e=%.3f | J4 c=%.3f t=%.3f e=%.3f",
              q_current[0], q_target[0], q_target[0] - q_current[0],
              q_current[1], q_target[1], q_target[1] - q_current[1],
              q_current[2], q_target[2], q_target[2] - q_current[2],
              q_current[3], q_target[3], q_target[3] - q_current[3]);
    }
    
    /* 处理段边界事件（例如夹爪动作）
     * 使用时间跨越检测，避免1ms阈值在10ms控制周期下漏触发。
     */
    if (ctrl->trajectory.total_segments > 0U) {
        float boundary_time = 0.0f;
        for (uint16_t seg = 0; seg < ctrl->trajectory.total_segments; seg++) {
            boundary_time += ctrl->trajectory.segments[seg][0].duration;
            if ((prev_time < boundary_time) && (ctrl->seq_start_time >= boundary_time)) {
                uint8_t action = ctrl->trajectory.segments[seg][0].action;
                if (action == ACTION_GRIP) {
                    fsp_err_t err = trigger_gripper_action(action);
                    if (err == FSP_SUCCESS) {
                        LOG_I("Segment %lu: GRIP action triggered", (unsigned long)seg);
                    } else {
                        LOG_E("Segment %lu: GRIP action failed, err=%d", (unsigned long)seg, err);
                    }
                } else if (action == ACTION_RELEASE) {
                    fsp_err_t err = trigger_gripper_action(action);
                    if (err == FSP_SUCCESS) {
                        LOG_I("Segment %lu: RELEASE action triggered", (unsigned long)seg);
                    } else {
                        LOG_E("Segment %lu: RELEASE action failed, err=%d", (unsigned long)seg, err);
                    }
                }
            }
        }
    }
    
    /* 检查序列是否完成 */
    if (seq_done) {
        LOG_I("Playback sequence completed");
        ctrl->seq_running = false;
        if (!capture_idle_hold_targets(ctrl)) {
            ctrl->state = MOTION_STATE_ERROR;
            LOG_E("Playback completion rejected: capture abs failed");
            return;
        }
        motion_adapter_force_persist_sector(&ctrl->adapter);
        ctrl->state = MOTION_STATE_IDLE;
    }
}

/**
 * @brief IDLE保持模式控制循环（锁当前位置）
 */
static void idle_control_loop(motion_controller_t *ctrl, float dt) {
    if (!ctrl->idle_hold_valid) {
        if (!capture_idle_hold_targets(ctrl)) {
            ctrl->state = MOTION_STATE_ERROR;
            LOG_E("Idle control rejected: capture abs failed");
            return;
        }
    }

    float v_target[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    motion_adapter_set_pd(&ctrl->adapter, ctrl->config.controller.kp, ctrl->config.controller.kd);
    if (!motion_adapter_step(&ctrl->adapter, dt, ctrl->idle_q_hold, v_target, NULL, NULL, NULL)) {
        LOG_E("Idle adapter step failed");
    }
}

/**
 * @brief 运控模式主控制循环
 * @param ctrl 控制器实例指针
 * @param dt 时间步长（秒）
 */
void motion_ctrl_loop(motion_controller_t *ctrl, float dt) {
    if (ctrl == NULL || !ctrl->is_initialized || ctrl->emergency_stop) return;
    if (dt <= 0.0f) return;
    
    switch (ctrl->state) {
        case MOTION_STATE_TEACHING:
            teach_control_loop(ctrl, dt);
            break;
            
        case MOTION_STATE_PLAYBACK:
            playback_control_loop(ctrl, dt);
            break;
            
        case MOTION_STATE_IDLE:
            idle_control_loop(ctrl, dt);
            break;

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
    motion_ctrl_set_common_pd(ctrl, kp, kd);
    (void)enable_joint1;
}

/**
 * @brief 设置回放模式参数
 */
void motion_ctrl_set_playback_params(motion_controller_t *ctrl, const float kp[4], 
                                     const float kd[4]) {
    motion_ctrl_set_common_pd(ctrl, kp, kd);
}

/**
 * @brief 触发急停
 */
void motion_ctrl_emergency_stop(motion_controller_t *ctrl) {
    if (ctrl == NULL || !ctrl->is_initialized) return;
    ctrl->emergency_stop = true;

    /* 急停：硬停所有电机 */
    robstride_stop(ROBSTRIDE_MOTOR_ID_JOINT1);
    robstride_stop(ROBSTRIDE_MOTOR_ID_JOINT2);
    robstride_stop(ROBSTRIDE_MOTOR_ID_JOINT3);
    robstride_stop(ROBSTRIDE_MOTOR_ID_JOINT4);
    robstride_stop(ROBSTRIDE_MOTOR_ID_GRIPPER);

    motion_ctrl_stop(ctrl);
    LOG_E("Emergency stop triggered");
}

/**
 * @brief 清除急停状态
 */
void motion_ctrl_clear_emergency_stop(motion_controller_t *ctrl) {
    if (ctrl == NULL || !ctrl->is_initialized) return;
    ctrl->emergency_stop = false;
    LOG_I("Emergency stop cleared");
}
