/**
 * @file motion_ctrl.c
 * @brief 机械臂运动控制状态机实现
 */

/* -------------------------------------------------------------------------- */
/* 当前有效运控说明                                                            */
/*                                                                            */
/* 本文件是当前实际生效的中层运控实现：关节 1~4 使用串口舵机位置插补控制，        */
/* 夹爪使用电机模式定扭矩控制。                                                 */
/*                                                                            */
/* 兼容说明：仍保留部分旧配置字段和旧上层接口名，以减少上层改动；但旧版 CAN 运控  */
/* 函数已经不是当前有效控制路径。                                               */
/* -------------------------------------------------------------------------- */

#include "motion_ctrl.h"
#include "drv_servo.h"
#include "drv_touch.h"
#include "nvm_manager.h"
#include "shared_data.h"
#include "sys_log.h"
#include <string.h>

#define DEG2RAD_F                    0.01745329251994329577f
#define RAD2DEG_F                    57.2957795130823208768f
#define TEACH_JOG_STEP_LEVEL_COUNT   (3U)
#define TEACH_JOG_HOLD_REPEAT_S      (0.04f)

#define TEACH_UNLOCK_RAD             (1.0f * DEG2RAD_F)
#define TEACH_STILL_RAD              (0.5f * DEG2RAD_F)
#define TEACH_SETTLE_S               (0.20f) // 达到静止状态后需要持续多长时间才认为真正进入示教状态，避免因为短暂的静止误触发示教；需要根据实际控制周期和系统稳定性进行调整，过小可能导致误触发示教，过大则可能导致示教响应迟钝
#define TORQUE_PROTECTION_CYCLES     (3U) // 连续多少个控制周期力矩超限后触发保护
#define JOINT_LIMIT_DEFAULT_MIN_DEG  (-180.0f) // 默认的软件限幅范围，单位度；实际应用中会从 NVM 配置读取并覆盖该默认值，确保不会因为 NVM 配置错误而完全失去运动能力；如果 NVM 配置的 min/max 反了，也会自动修正为合理范围
#define JOINT_LIMIT_DEFAULT_MAX_DEG  (180.0f)

#define GRIPPER_RELEASE_TIMEOUT_S    (2.0f) // 夹爪释放动作的最大持续时间，超过该时间后即使没有力矩反馈也强制停止释放动作，避免长时间施加过大力矩导致损伤；同时配合触觉交接监控逻辑，在检测到交接完成后也会提前停止释放动作
#define HANDOFF_PRE_RELEASE_STOP_S   (0.05f) // 在触觉交接监控逻辑中，从检测到交接到强制停止释放动作之间的时间，确保在交接完成后夹爪能够快速停止施加过大力矩，避免损伤；同时配合夹爪控制逻辑，在该时间内持续施加释放命令以确保能够顺利完成交接

#define GRIPPER_HOLD_KEEPALIVE_S     (0.25f) // 夹爪保持状态下的心跳信号周期，用于维持夹爪的保持状态
#define GRIPPER_NO_TOUCH_PAUSE_S     (1.0f) // 在没有触觉反馈的情况下持续施加夹爪动作命令的最大时间，超过该时间后即使没有力矩反馈也强制停止夹爪动作，避免长时间施加过大力矩导致损伤；同时配合触觉交接监控逻辑，在检测到触觉不可用后也会提前停止夹爪动作
#define TOUCH_DISABLE_ERR_COUNT      (3U)

#define TOUCH_FILTER_ALPHA           (0.25f) // 触觉传感器数据滤波系数，较小的值可以获得更平滑的力反馈，但响应会更慢；需要根据实际传感器的噪声特性和控制周期进行调整
#define TOUCH_BIAS_TRACK_ALPHA       (0.05f) // 触觉零偏在线跟踪的滤波系数，较小的值可以更平滑地跟踪零偏变化，但响应会更慢；需要根据实际传感器的稳定性和环境变化情况进行调整
#define TOUCH_BIAS_INIT_SAMPLES      (50U) // 触觉零偏初始化采样数量，在系统启动或重置交接等待态时进行采样平均以获得初始零偏值；需要根据实际传感器的稳定性和噪声特性进行调整，过少可能导致零偏估计不准确，过多则会增加系统准备时间
#define HANDOFF_REF_INIT_SAMPLES     (15U) // 交接参考值初始化采样数量，在交接等待态的 ARMED 状态开始时进行采样平均以获得初始交接参考值；需要根据实际传感器的稳定性和噪声特性进行调整，过少可能导致参考值不准确，过多则会增加交接响应时间
#define HANDOFF_REF_TRACK_ALPHA      (0.10f) // 交接参考值在线跟踪的滤波系数，较小的值可以更平滑地跟踪参考值变化，但响应会更慢；需要根据实际传感器的稳定性和环境变化情况进行调整
#define HANDOFF_REF_FT_MIN_SQ        (225.0f) // 交接参考切向力的最小模长平方，只有当参考切向力的模长平方超过该值时才认为参考有效；需要根据实际应用中夹持物的重量和摩擦情况进行调整，过小可能导致误触发交接，过大则可能导致无法检测到交接
#define HANDOFF_CUR_FT_VALID_SQ      (144.0f) // 当前切向力的最小模长平方，只有当当前切向力的模长平方超过该值时才认为当前切向力有效；需要根据实际应用中夹持物的重量和摩擦情况进行调整，过小可能导致误触发交接，过大则可能导致无法检测到交接
#define HANDOFF_DIR_CHANGE_COS_MAX   (0.50f)
#define HANDOFF_FT_RELEASE_RATIO     (0.20f) // 交接释放判定的切向力模长与参考切向力模长的最大比值，只有当当前切向力模长小于参考切向力模长乘以该值时才认为满足释放条件；需要根据实际应用中夹持物的重量和摩擦情况进行调整，过小可能导致无法检测到交接，过大则可能导致误触发交接
#define HANDOFF_FT_RELEASE_RATIO_SQ  (HANDOFF_FT_RELEASE_RATIO * HANDOFF_FT_RELEASE_RATIO)
#define HANDOFF_CANDIDATE_CYCLES     (2U) // 交接候选状态持续的控制周期数，只有当满足交接条件持续达到该周期数时才真正触发交接状态的切换；需要根据实际控制周期和传感器噪声特性进行调整，过小可能导致误触发交接，过大则可能导致交接响应迟钝
#define HANDOFF_DONE_CYCLES          (4U) // 交接完成状态持续的控制周期数，用于确保交接动作完全执行完毕；需要根据实际控制周期和传感器噪声特性进行调整
#define HANDOFF_TOUCH_ERR_LOG_PERIOD (500U) // 触觉传感器错误日志记录周期

motion_controller_t g_motion_ctrl = {0};

typedef struct {
    float raw_fx;
    float raw_fy;
    float raw_fz;
    float filt_fx;
    float filt_fy;
    float filt_fz;
    float bias_fx; // 零偏值，正负表示传感器输出的正负方向力
    float bias_fy;
    float bias_fz;
    float ref_fn; // 交接参考法向力，正值表示当前夹持物被挤压在手爪内
    float ref_tx; // 交接参考切向力，正值表示当前夹持物相对于手爪向某个特定方向摩擦
    float ref_ty;
    uint16_t bias_sample_count;
    uint16_t ref_sample_count;
    uint16_t comm_error_count;
    uint8_t candidate_cycles;
    uint8_t settle_cycles;
    bool filter_valid;
    bool bias_valid;
    bool ref_ready;
    bool dir_change_latched;
} touch_handoff_runtime_t;

static touch_handoff_runtime_t s_touch_handoff = {0};
static bool s_touch_monitor_enabled = false;

static void set_gripper_hold_state(motion_controller_t *ctrl, gripper_hold_state_t state);
static bool unlock_all_joints(void);
static void refresh_joint_feedback_best_effort(const char *reason);
static void log_gripper_cmd_error(const char *op, fsp_err_t err);

/* 当前运行时控制关节 1~5。 */
static const uint8_t k_joint_ids[ROBSTRIDE_ACTIVE_JOINT_NUM] = {
    ROBSTRIDE_MOTOR_ID_JOINT1,
    ROBSTRIDE_MOTOR_ID_JOINT2,
    ROBSTRIDE_MOTOR_ID_JOINT3,
    ROBSTRIDE_MOTOR_ID_JOINT4,
    ROBSTRIDE_MOTOR_ID_JOINT5
};

static const motion_ctrl_config_t DEFAULT_CONFIG = {
    .controller = {
        .kp = {8.0f, 8.0f, 8.0f, 8.0f, 8.0f},
        .kd = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f},
    },
    .max_torque = {
        (float) MOTION_DEFAULT_CURRENT_LIMIT_MA,
        (float) MOTION_DEFAULT_CURRENT_LIMIT_MA,
        (float) MOTION_DEFAULT_CURRENT_LIMIT_MA,
        (float) MOTION_DEFAULT_CURRENT_LIMIT_MA,
        (float) MOTION_DEFAULT_CURRENT_LIMIT_MA
    },
    .max_velocity = {0.1f, 0.1f, 0.1f, 0.1f, 0.1f},
};

static const float k_teach_jog_joint_step_rad[TEACH_JOG_STEP_LEVEL_COUNT] = {
    0.4f * DEG2RAD_F,
    1.0f * DEG2RAD_F,
    2.0f * DEG2RAD_F,
};

static const uint16_t k_teach_jog_gripper_step_cmd[TEACH_JOG_STEP_LEVEL_COUNT] = {
    40U,
    100U,
    200U,
};

/**
 * @brief 将浮点数限制在指定范围内
 */
static inline float clampf_range(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

/**
 * @brief 在不引入 libm 的情况下计算浮点数绝对值
 */
static inline float absf_local(float value) {return (value >= 0.0f) ? value : -value;}

static uint16_t get_gripper_current_limit_ma(void) {
    const sys_config_t *sys_cfg = nvm_get_sys_config();

    if ((sys_cfg != NULL) && (sys_cfg->current_limit[4] > 0U)) {
        return sys_cfg->current_limit[4];
    }

    return MOTION_DEFAULT_CURRENT_LIMIT_MA;
}

/**
 * @brief 计算二维向量模长平方，避免引入 sqrtf
 */
static inline float vec2_norm_sq(float x, float y) {
    return (x * x) + (y * y);
}

/**
 * @brief 清空触觉交接运行态
 * @param keep_bias true 保留零偏标定；false 一并清空
 */
static void reset_touch_handoff_runtime(bool keep_bias) {
    float bias_fx = s_touch_handoff.bias_fx;
    float bias_fy = s_touch_handoff.bias_fy;
    float bias_fz = s_touch_handoff.bias_fz;
    uint16_t bias_sample_count = s_touch_handoff.bias_sample_count;
    bool bias_valid = s_touch_handoff.bias_valid;

    memset(&s_touch_handoff, 0, sizeof(s_touch_handoff));

    if (keep_bias) {
        s_touch_handoff.bias_fx = bias_fx;
        s_touch_handoff.bias_fy = bias_fy;
        s_touch_handoff.bias_fz = bias_fz;
        s_touch_handoff.bias_sample_count = bias_sample_count;
        s_touch_handoff.bias_valid = bias_valid;
    }
}

static bool touch_monitor_enabled(void) {
    return s_touch_monitor_enabled;
}

static void disable_touch_monitor(void) {
    if (!s_touch_monitor_enabled) {
        return;
    }

    s_touch_monitor_enabled = false;
    reset_touch_handoff_runtime(false);
}

/**
 * @brief 刷新一次触觉原始值并更新低通滤波结果
 */
static bool touch_refresh_filtered_sample(void) {
    if (!touch_monitor_enabled()) {
        return false;
    }

    if (get_touch_data_process() != FSP_SUCCESS) {
        s_touch_handoff.comm_error_count++;
        if (s_touch_handoff.comm_error_count >= TOUCH_DISABLE_ERR_COUNT) {
            disable_touch_monitor();
        }
        return false;
    }

    s_touch_handoff.comm_error_count = 0U;
    s_touch_handoff.raw_fx = (float) g_touch_data_s.fx;
    s_touch_handoff.raw_fy = (float) g_touch_data_s.fy;
    s_touch_handoff.raw_fz = (float) g_touch_data_s.fz;

    if (!s_touch_handoff.filter_valid) {
        s_touch_handoff.filt_fx = s_touch_handoff.raw_fx;
        s_touch_handoff.filt_fy = s_touch_handoff.raw_fy;
        s_touch_handoff.filt_fz = s_touch_handoff.raw_fz;
        s_touch_handoff.filter_valid = true;
    } else {
        s_touch_handoff.filt_fx += TOUCH_FILTER_ALPHA * (s_touch_handoff.raw_fx - s_touch_handoff.filt_fx);
        s_touch_handoff.filt_fy += TOUCH_FILTER_ALPHA * (s_touch_handoff.raw_fy - s_touch_handoff.filt_fy);
        s_touch_handoff.filt_fz += TOUCH_FILTER_ALPHA * (s_touch_handoff.raw_fz - s_touch_handoff.filt_fz);
    }

    return true;
}

/**
 * @brief 在空载时慢速更新触觉零偏
 */
static void touch_update_bias(void) {
    if (!touch_refresh_filtered_sample()) return;

    if (!s_touch_handoff.bias_valid) {
        s_touch_handoff.bias_fx = s_touch_handoff.filt_fx;
        s_touch_handoff.bias_fy = s_touch_handoff.filt_fy;
        s_touch_handoff.bias_fz = s_touch_handoff.filt_fz;
        s_touch_handoff.bias_sample_count = 1U;
        s_touch_handoff.bias_valid = true;
        return;
    }

    if (s_touch_handoff.bias_sample_count < TOUCH_BIAS_INIT_SAMPLES) {
        float sample_count = (float) (s_touch_handoff.bias_sample_count + 1U);
        s_touch_handoff.bias_fx += (s_touch_handoff.filt_fx - s_touch_handoff.bias_fx) / sample_count;
        s_touch_handoff.bias_fy += (s_touch_handoff.filt_fy - s_touch_handoff.bias_fy) / sample_count;
        s_touch_handoff.bias_fz += (s_touch_handoff.filt_fz - s_touch_handoff.bias_fz) / sample_count;
        s_touch_handoff.bias_sample_count++;
        return;
    }

    s_touch_handoff.bias_fx += TOUCH_BIAS_TRACK_ALPHA * (s_touch_handoff.filt_fx - s_touch_handoff.bias_fx);
    s_touch_handoff.bias_fy += TOUCH_BIAS_TRACK_ALPHA * (s_touch_handoff.filt_fy - s_touch_handoff.bias_fy);
    s_touch_handoff.bias_fz += TOUCH_BIAS_TRACK_ALPHA * (s_touch_handoff.filt_fz - s_touch_handoff.bias_fz);
}

/**
 * @brief 基于滤波值和零偏计算当前接触力
 */
static void touch_get_centered_force(float *fx, float *fy, float *fz) {
    float bias_fx = s_touch_handoff.bias_valid ? s_touch_handoff.bias_fx : 0.0f;
    float bias_fy = s_touch_handoff.bias_valid ? s_touch_handoff.bias_fy : 0.0f;
    float bias_fz = s_touch_handoff.bias_valid ? s_touch_handoff.bias_fz : 0.0f;

    if (fx != NULL) *fx = s_touch_handoff.filt_fx - bias_fx;
    if (fy != NULL) *fy = s_touch_handoff.filt_fy - bias_fy;
    if (fz != NULL) *fz = s_touch_handoff.filt_fz - bias_fz;
}

/**
 * @brief 重置交接等待态，不清零零偏标定
 */
static void reset_handoff_wait_internal(motion_controller_t *ctrl) {
    if (ctrl == NULL) return;

    ctrl->handoff_state = HANDOFF_IDLE;
    ctrl->handoff_release_stop_timer = 0.0f;
    reset_touch_handoff_runtime(true);
}

/**
 * @brief 轮询触觉交接监控逻辑
 */
static void update_touch_handoff(motion_controller_t *ctrl) {
    float tx = 0.0f;
    float ty = 0.0f;
    float tz = 0.0f;
    float fn = 0.0f;
    float ft_sq = 0.0f;

    if (ctrl == NULL) return;

    if (!touch_monitor_enabled()) {
        if (ctrl->handoff_state != HANDOFF_IDLE) {
            ctrl->handoff_state = HANDOFF_DONE;
            LOG_W("Touch unavailable, bypass handoff wait");
        }
        return;
    }

    if (ctrl->handoff_state == HANDOFF_IDLE) {
        if (ctrl->gripper_hold_state == GRIPPER_HOLD_IDLE) {
            touch_update_bias();
        }
        return;
    }

    if (ctrl->handoff_state == HANDOFF_DONE) return;


    if (!touch_refresh_filtered_sample()) {
        s_touch_handoff.candidate_cycles = 0U;
        s_touch_handoff.settle_cycles = 0U;
        return;
    }

    touch_get_centered_force(&tx, &ty, &tz);
    fn = absf_local(tz);
    ft_sq = vec2_norm_sq(tx, ty);

    if (ctrl->handoff_state == HANDOFF_ARMED) {
        if (ctrl->gripper_hold_state != GRIPPER_HOLD_GRASP) {
            ctrl->handoff_state = HANDOFF_DONE;
            LOG_W("Handoff wait armed without gripper grasp; fallback continue");
            return;
        }

        if (s_touch_handoff.ref_sample_count < HANDOFF_REF_INIT_SAMPLES) {
            float sample_count = (float) (s_touch_handoff.ref_sample_count + 1U);
            s_touch_handoff.ref_fn += (fn - s_touch_handoff.ref_fn) / sample_count;
            s_touch_handoff.ref_tx += (tx - s_touch_handoff.ref_tx) / sample_count;
            s_touch_handoff.ref_ty += (ty - s_touch_handoff.ref_ty) / sample_count;
            s_touch_handoff.ref_sample_count++;
        } else {
            s_touch_handoff.ref_fn += HANDOFF_REF_TRACK_ALPHA * (fn - s_touch_handoff.ref_fn);
            s_touch_handoff.ref_tx += HANDOFF_REF_TRACK_ALPHA * (tx - s_touch_handoff.ref_tx);
            s_touch_handoff.ref_ty += HANDOFF_REF_TRACK_ALPHA * (ty - s_touch_handoff.ref_ty);
        }

        s_touch_handoff.ref_ready =
            (vec2_norm_sq(s_touch_handoff.ref_tx, s_touch_handoff.ref_ty) >= HANDOFF_REF_FT_MIN_SQ);

        if (!s_touch_handoff.ref_ready) {
            s_touch_handoff.candidate_cycles = 0U;
            s_touch_handoff.dir_change_latched = false;
            return;
        }

        float ref_ft_sq = vec2_norm_sq(s_touch_handoff.ref_tx, s_touch_handoff.ref_ty);
        float dot = (tx * s_touch_handoff.ref_tx) + (ty * s_touch_handoff.ref_ty);
        bool ft_valid = (ft_sq >= HANDOFF_CUR_FT_VALID_SQ) && (ref_ft_sq >= HANDOFF_REF_FT_MIN_SQ);
        bool dir_change = false;
        bool ft_ratio_small = (ft_sq <= (ref_ft_sq * HANDOFF_FT_RELEASE_RATIO_SQ));

        if (ft_valid) {
            if (dot <= 0.0f) {
                dir_change = true;
            } else {
                float dir_lhs = dot * dot;
                float dir_rhs = (HANDOFF_DIR_CHANGE_COS_MAX * HANDOFF_DIR_CHANGE_COS_MAX) * ref_ft_sq * ft_sq;
                dir_change = (dir_lhs <= dir_rhs);
            }
        }

        if (dir_change) s_touch_handoff.dir_change_latched = true;
        

        if (s_touch_handoff.dir_change_latched && ft_ratio_small) {
            if (s_touch_handoff.candidate_cycles < UINT8_MAX) {
                s_touch_handoff.candidate_cycles++;
            }
        } else {
            s_touch_handoff.candidate_cycles = 0U;
        }

        if (s_touch_handoff.candidate_cycles >= HANDOFF_CANDIDATE_CYCLES) {
            ctrl->handoff_state = HANDOFF_RELEASE_ACTIVE;
            ctrl->handoff_release_stop_timer = 0.0f;
            s_touch_handoff.settle_cycles = 0U;
            set_gripper_hold_state(ctrl, GRIPPER_HOLD_RELEASE);
            {
                fsp_err_t err = servo_gripper_release((int16_t) get_gripper_current_limit_ma());
                log_gripper_cmd_error("RELEASE", err);
            }
            LOG_I("Touch handoff detected, auto release armed");
        }

        return;
    }

    if (ctrl->handoff_state == HANDOFF_RELEASE_ACTIVE) {
        float ref_ft_sq = vec2_norm_sq(s_touch_handoff.ref_tx, s_touch_handoff.ref_ty);
        bool touch_quiet = (ref_ft_sq > 0.0f) && (ft_sq <= (ref_ft_sq * HANDOFF_FT_RELEASE_RATIO_SQ));
        if (touch_quiet) {
            if (s_touch_handoff.settle_cycles < UINT8_MAX) {
                s_touch_handoff.settle_cycles++;
            }
        } else {
            s_touch_handoff.settle_cycles = 0U;
        }

        if (s_touch_handoff.settle_cycles >= HANDOFF_DONE_CYCLES) {
            (void) servo_gripper_stop();
            set_gripper_hold_state(ctrl, GRIPPER_HOLD_IDLE);
            ctrl->handoff_state = HANDOFF_DONE;
            LOG_I("Touch handoff release completed");
        }
    }
}

/**
 * @brief 根据 NVM 配置读取单个关节的软件限幅
 */
static void get_joint_limit_rad(uint8_t joint_index, float *q_min, float *q_max) {
    float min_deg = JOINT_LIMIT_DEFAULT_MIN_DEG;
    float max_deg = JOINT_LIMIT_DEFAULT_MAX_DEG;
    const sys_config_t *sys_cfg = nvm_get_sys_config();

    if ((sys_cfg != NULL) && (joint_index < ROBSTRIDE_JOINT_NUM)) {
        min_deg = (float) sys_cfg->angle_min[joint_index] * 0.01f;
        max_deg = (float) sys_cfg->angle_max[joint_index] * 0.01f;
    }

    if (min_deg > max_deg) {
        float temp = min_deg;
        min_deg = max_deg;
        max_deg = temp;
    }

    if (q_min != NULL) {
        *q_min = min_deg * DEG2RAD_F;
    }
    if (q_max != NULL) {
        *q_max = max_deg * DEG2RAD_F;
    }
}

/**
 * @brief 对关节命令应用新的 NVM 软件限幅
 */
static float clamp_joint_position_to_nvm(uint8_t motor_id, float position_cmd) {
    if (!is_joint_motor_id(motor_id)) {
        return position_cmd;
    }

    uint8_t joint_index = (uint8_t) (motor_id - ROBSTRIDE_MOTOR_ID_JOINT1);
    float q_min = 0.0f;
    float q_max = 0.0f;
    get_joint_limit_rad(joint_index, &q_min, &q_max);
    return clampf_range(position_cmd, q_min, q_max);
}

/**
 * @brief 清除力矩保护去抖计数
 */
static void reset_torque_protection_state(motion_controller_t *ctrl) {
    if (ctrl == NULL) return;

    memset(ctrl->torque_limit_cycles, 0, sizeof(ctrl->torque_limit_cycles));
}

/**
 * @brief 读取指定电机的最新缓存位置反馈
 */
static const robstride_feedback_t *get_motor_feedback(uint8_t motor_id) {
    static const robstride_feedback_t k_empty_feedback = {0};
    uint8_t motor_index = get_motor_index(motor_id);
    return (motor_index < ROBSTRIDE_MOTOR_NUM) ? &g_motors[motor_index].feedback : &k_empty_feedback;
}

static void clear_playback_upload_state(motion_controller_t *ctrl) {
    if (ctrl == NULL) return;

    memset(ctrl->playback_upload_q, 0, sizeof(ctrl->playback_upload_q));
    ctrl->playback_upload_q_valid = false;
}

static void update_playback_upload_state(motion_controller_t *ctrl,
                                         const float q_target[ROBSTRIDE_ACTIVE_JOINT_NUM]) {
    if ((ctrl == NULL) || (q_target == NULL)) return;

    memcpy(ctrl->playback_upload_q, q_target, sizeof(ctrl->playback_upload_q));
    ctrl->playback_upload_q_valid = true;
}

/**
 * @brief 读取指定电机的最新缓存速度反馈
 */
static void read_joint_pose(float q[ROBSTRIDE_ACTIVE_JOINT_NUM]) {
    if (q == NULL) return;

    for (uint8_t i = 0U; i < ROBSTRIDE_ACTIVE_JOINT_NUM; ++i) {
        q[i] = clamp_joint_position_to_nvm(k_joint_ids[i], get_motor_feedback(k_joint_ids[i])->position);
    }
}

/**
 * @brief 读取指定电机的力矩反馈代理值
 */

/**
 * @brief 清除临时的 teach_jog 参考状态
 */
static void reset_teach_jog_ref(motion_controller_t *ctrl) {
    if (ctrl == NULL) return;

    ctrl->teach_jog_motor_id = 0U;
    ctrl->teach_jog_q_ref = 0.0f;
    ctrl->teach_jog_q_valid = false;
    ctrl->teach_jog_gripper_cmd_ref = 0U;
    ctrl->teach_jog_gripper_cmd_valid = false;
    ctrl->teach_jog_hold_active = false;
    ctrl->teach_jog_hold_direction = 0;
    ctrl->teach_jog_hold_step_level = 0U;
    ctrl->teach_jog_hold_elapsed_s = 0.0f;
}

static bool teach_jog_level_valid(uint8_t step_level) {
    return step_level < TEACH_JOG_STEP_LEVEL_COUNT;
}

static void refresh_joint_feedback_best_effort(const char *reason) {
    if (servo_refresh_joint_feedback() == FSP_SUCCESS) {
        return;
    }

    if (reason != NULL) {
        LOG_W("%s joint feedback refresh failed, using cached pose", reason);
    }
}

/**
 * @brief 采集当前关节姿态作为保持参考
 */
static void capture_hold_refs(motion_controller_t *ctrl) {
    if (ctrl == NULL) return;

    read_joint_pose(ctrl->hold_q_ref);
    ctrl->hold_q_valid = true;
}

/**
 * @brief 在保持参考尚未有效时进行懒初始化
 */
static void ensure_hold_refs(motion_controller_t *ctrl) {
    if ((ctrl != NULL) && !ctrl->hold_q_valid) {
        capture_hold_refs(ctrl);
    }
}

static void capture_teach_prev_q(motion_controller_t *ctrl) {
    if (ctrl == NULL) return;

    read_joint_pose(ctrl->teach_prev_q);
}

static void reset_teach_passive_state(motion_controller_t *ctrl) {
    if (ctrl == NULL) return;

    ctrl->teach_locked = false;
    ctrl->teach_settle_s = 0.0f;
    memset(ctrl->teach_lock_q, 0, sizeof(ctrl->teach_lock_q));
    memset(ctrl->teach_prev_q, 0, sizeof(ctrl->teach_prev_q));
}

static bool enter_teach_lock(motion_controller_t *ctrl) {
    if (ctrl == NULL) return false;

    capture_hold_refs(ctrl);
    if (servo_hold_joint_current() != FSP_SUCCESS) {
        LOG_E("Failed to lock teaching joints at current pose");
        return false;
    }

    memcpy(ctrl->teach_lock_q, ctrl->hold_q_ref, sizeof(ctrl->teach_lock_q));
    capture_teach_prev_q(ctrl);
    ctrl->teach_settle_s = 0.0f;
    ctrl->teach_locked = true;
    ctrl->idle_lock_active = false;
    return true;
}

static bool enter_teach_free(motion_controller_t *ctrl) {
    if (ctrl == NULL) return false;

    if (!unlock_all_joints()) {
        return false;
    }

    capture_hold_refs(ctrl);
    capture_teach_prev_q(ctrl);
    ctrl->teach_settle_s = 0.0f;
    ctrl->teach_locked = false;
    ctrl->idle_lock_active = false;
    return true;
}

/**
 * @brief 应用关节位置限幅，并在触边时取消继续外推的速度
 */
static void position_limit(uint8_t motor_id, float *position_cmd, float *velocity_cmd) {
    if ((position_cmd == NULL) || (velocity_cmd == NULL)) return;

    float q_cmd = clamp_joint_position_to_nvm(motor_id, *position_cmd);

    if (is_joint_motor_id(motor_id)) {
        uint8_t joint_index = (uint8_t) (motor_id - ROBSTRIDE_MOTOR_ID_JOINT1);
        float q_min = 0.0f;
        float q_max = 0.0f;
        get_joint_limit_rad(joint_index, &q_min, &q_max);
        bool hit_upper_limit = (q_cmd >= q_max) && (*velocity_cmd > 0.0f);
        bool hit_lower_limit = (q_cmd <= q_min) && (*velocity_cmd < 0.0f);
        if (hit_upper_limit || hit_lower_limit) {
            *velocity_cmd = 0.0f;
        }
    }

    *position_cmd = q_cmd;
}

/**
 * @brief 更新上层兼容保留的 kp/kd 参数
 */
static void motion_ctrl_set_common_pd(motion_controller_t *ctrl,
                                      const float kp[ROBSTRIDE_JOINT_NUM],
                                      const float kd[ROBSTRIDE_JOINT_NUM]) {
    if (ctrl == NULL) return;

    if (kp != NULL) memcpy(ctrl->config.controller.kp, kp, ROBSTRIDE_JOINT_NUM * sizeof(float));
    if (kd != NULL) memcpy(ctrl->config.controller.kd, kd, ROBSTRIDE_JOINT_NUM * sizeof(float));
}

/**
 * @brief 通过串口舵机后端下发四个关节的位置目标
 */
static bool send_joint_position_targets(const float q_target[ROBSTRIDE_ACTIVE_JOINT_NUM]) {
    float q_cmd[ROBSTRIDE_ACTIVE_JOINT_NUM];

    if (q_target == NULL) return false;

    for (uint8_t i = 0U; i < ROBSTRIDE_ACTIVE_JOINT_NUM; ++i) {
        q_cmd[i] = clamp_joint_position_to_nvm(k_joint_ids[i], q_target[i]);
    }

    return (servo_write_joint_positions(q_cmd) == FSP_SUCCESS);
}

/**
 * @brief 统一解除关节 1~4 的锁舵扭矩
 */
static bool unlock_all_joints(void) {
    for (uint8_t i = 0U; i < ROBSTRIDE_ACTIVE_JOINT_NUM; ++i) {
        if (servo_unlock_joint(k_joint_ids[i]) != FSP_SUCCESS) {
            LOG_E("Failed to unlock joint %u", (unsigned int) (i + 1U));
            return false;
        }
    }

    return true;
}

/**
 * @brief 统一将关节 1~4 锁舵在当前位置
 */
static bool lock_all_joints_current(motion_controller_t *ctrl) {
    if (ctrl == NULL) return false;

    capture_hold_refs(ctrl);
    if (servo_hold_joint_current() != FSP_SUCCESS) {
        LOG_E("Failed to lock all joints at current pose");
        return false;
    }

    ctrl->idle_lock_active = true;
    return true;
}

/**
 * @brief 若当前处于 IDLE 锁舵态，则先解除锁舵
 */
static bool release_idle_lock_if_needed(motion_controller_t *ctrl) {
    if ((ctrl == NULL) || !ctrl->idle_lock_active) {
        return true;
    }

    if (!unlock_all_joints()) {
        return false;
    }

    ctrl->idle_lock_active = false;
    return true;
}

/**
 * @brief 维持夹爪在抓取/释放动作之间的定扭矩状态机,并根据位置/速度反馈判断是否进入抓取保持状态
 */
static void set_gripper_hold_state(motion_controller_t *ctrl, gripper_hold_state_t state) {
    if (ctrl == NULL) return;

    ctrl->gripper_hold_state = state;
    ctrl->gripper_release_timer = 0.0f;
    ctrl->gripper_keepalive_timer = (state == GRIPPER_HOLD_IDLE) ? 0.0f : GRIPPER_HOLD_KEEPALIVE_S;
    ctrl->handoff_release_stop_timer = 0.0f;
}

static void log_gripper_cmd_error(const char *op, fsp_err_t err) {
    if ((op != NULL) && (err != FSP_SUCCESS)) {
        LOG_E("Gripper %s command failed: %d", op, err);
    }
}

static void arm_gripper_action_pause_if_needed(motion_controller_t *ctrl, uint8_t action) {
    if ((ctrl == NULL) || touch_monitor_enabled()) return;
    if ((action != ACTION_GRIP) && (action != ACTION_RELEASE)) return;
    ctrl->gripper_action_pause_s = GRIPPER_NO_TOUCH_PAUSE_S;
}

/**
 * @brief 更新夹爪控制状态机，维持抓取/释放动作的定扭矩输出，
 *        并在释放动作时根据触觉交接状态判断是否提前停止以避免过度施力
 */
static void update_gripper_control(motion_controller_t *ctrl, float dt) {
    if (ctrl == NULL) return;
    (void) dt;

    switch (ctrl->gripper_hold_state) {
        case GRIPPER_HOLD_GRASP:
            break;

        case GRIPPER_HOLD_RELEASE:
            if ((ctrl->handoff_state == HANDOFF_RELEASE_ACTIVE) &&
                (ctrl->handoff_release_stop_timer < HANDOFF_PRE_RELEASE_STOP_S)) {
                ctrl->handoff_release_stop_timer += dt;
                (void) servo_gripper_stop();
                break;
            }

            ctrl->gripper_release_timer += dt;
            if (ctrl->gripper_release_timer >= GRIPPER_RELEASE_TIMEOUT_S) {
                fsp_err_t err = servo_gripper_stop();
                log_gripper_cmd_error("STOP", err);
                set_gripper_hold_state(ctrl, GRIPPER_HOLD_IDLE);
            }
            break;

        case GRIPPER_HOLD_IDLE:
        default:
            break;
    }
}

/**
 * @brief 将轨迹动作或帧动作转换为夹爪定扭矩状态
 */
static fsp_err_t trigger_gripper_action(motion_controller_t *ctrl, uint8_t action) {
    if (ctrl == NULL) return FSP_ERR_INVALID_POINTER;

    if (action == ACTION_GRIP) {
        fsp_err_t err = FSP_SUCCESS;
        uint16_t current_limit_ma = get_gripper_current_limit_ma();
        reset_handoff_wait_internal(ctrl);
        set_gripper_hold_state(ctrl, GRIPPER_HOLD_GRASP);
        err = servo_gripper_grasp((int16_t) current_limit_ma);
        log_gripper_cmd_error("GRIP", err);
        if (err == FSP_SUCCESS) {
            ctrl->gripper_keepalive_timer = 0.0f;
            arm_gripper_action_pause_if_needed(ctrl, ACTION_GRIP);
        }
        LOG_I("Gripper GRIP torque hold activated");
        return err;
    }

    if (action == ACTION_RELEASE) {
        fsp_err_t err = FSP_SUCCESS;
        uint16_t current_limit_ma = get_gripper_current_limit_ma();
        reset_handoff_wait_internal(ctrl);
        set_gripper_hold_state(ctrl, GRIPPER_HOLD_RELEASE);
        err = servo_gripper_release((int16_t) current_limit_ma);
        log_gripper_cmd_error("RELEASE", err);
        if (err == FSP_SUCCESS) {
            ctrl->gripper_keepalive_timer = 0.0f;
            arm_gripper_action_pause_if_needed(ctrl, ACTION_RELEASE);
        }
        LOG_I("Gripper RELEASE torque hold activated");
        return err;
    }

    return FSP_ERR_INVALID_ARGUMENT;
}

/**
 * @brief 实现关节低刚度拖动近似控制
 */
static bool teach_should_unlock(const motion_controller_t *ctrl) {
    if ((ctrl == NULL) || !ctrl->teach_locked) {
        return false;
    }

    for (uint8_t i = 0U; i < ROBSTRIDE_ACTIVE_JOINT_NUM; ++i) {
        float q_fb = clamp_joint_position_to_nvm(k_joint_ids[i], get_motor_feedback(k_joint_ids[i])->position);
        if (absf_local(q_fb - ctrl->teach_lock_q[i]) > TEACH_UNLOCK_RAD) {
            return true;
        }
    }

    return false;
}

/**
 * @brief 将 teach_jog 指令叠加到当前示教保持参考上
 */
static bool teach_all_joints_still(motion_controller_t *ctrl) {
    bool all_still = true;

    if (ctrl == NULL) {
        return false;
    }

    for (uint8_t i = 0U; i < ROBSTRIDE_ACTIVE_JOINT_NUM; ++i) {
        float q_fb = clamp_joint_position_to_nvm(k_joint_ids[i], get_motor_feedback(k_joint_ids[i])->position);
        if (absf_local(q_fb - ctrl->teach_prev_q[i]) > TEACH_STILL_RAD) {
            all_still = false;
        }
        ctrl->teach_prev_q[i] = q_fb;
    }

    return all_still;
}

static bool execute_teach_jog_hold_step(motion_controller_t *ctrl, const teach_jog_hold_cmd_t *step) {
    bool motor_changed = false;

    if ((ctrl == NULL) || (step == NULL)) {
        return false;
    }

    if (!is_motor_id_valid(step->motor_id) ||
        ((step->direction != -1) && (step->direction != 1)) ||
        !teach_jog_level_valid(step->step_level)) {
        LOG_W("Discarded invalid teach_jog hold: motor=%u dir=%d level=%u",
              (unsigned int) step->motor_id,
              (int) step->direction,
              (unsigned int) step->step_level);
        return false;
    }

    motor_changed = (!ctrl->teach_jog_engaged || (ctrl->teach_jog_motor_id != step->motor_id));

    if (step->motor_id == ROBSTRIDE_MOTOR_ID_GRIPPER) {
        uint16_t applied_cmd = 0U;
        int32_t target_cmd = 0;

        if (ctrl->teach_jog_engaged && is_joint_motor_id(ctrl->teach_jog_motor_id)) {
            (void) servo_stop_motor(ctrl->teach_jog_motor_id, false);
            ctrl->teach_jog_q_valid = false;
        }

        set_gripper_hold_state(ctrl, GRIPPER_HOLD_IDLE);
        reset_handoff_wait_internal(ctrl);
        (void) servo_gripper_stop();

        if (motor_changed || !ctrl->teach_jog_gripper_cmd_valid) {
            ctrl->teach_jog_gripper_cmd_ref = servo_gripper_feedback_cmd();
            ctrl->teach_jog_gripper_cmd_valid = true;
        }

        target_cmd = (int32_t) ctrl->teach_jog_gripper_cmd_ref +
                     ((int32_t) step->direction * (int32_t) k_teach_jog_gripper_step_cmd[step->step_level]);

        if (servo_gripper_move_to_cmd(target_cmd, &applied_cmd) != FSP_SUCCESS) {
            LOG_E("Teach jog hold failed to move gripper");
            return true;
        }

        ctrl->teach_jog_gripper_cmd_ref = applied_cmd;
        ctrl->teach_jog_motor_id = step->motor_id;
        ctrl->teach_jog_engaged = true;
        ctrl->teach_settle_s = 0.0f;
        return true;
    }

    if (ctrl->teach_locked && !enter_teach_free(ctrl)) {
        motion_ctrl_clear_teach_jog(ctrl, true);
        return false;
    }

    if (motor_changed && ctrl->teach_jog_engaged &&
        is_joint_motor_id(ctrl->teach_jog_motor_id) &&
        (ctrl->teach_jog_motor_id != step->motor_id)) {
        (void) servo_stop_motor(ctrl->teach_jog_motor_id, false);
        ctrl->teach_jog_q_valid = false;
    } else if (motor_changed && ctrl->teach_jog_engaged &&
               (ctrl->teach_jog_motor_id == ROBSTRIDE_MOTOR_ID_GRIPPER)) {
        set_gripper_hold_state(ctrl, GRIPPER_HOLD_IDLE);
        reset_handoff_wait_internal(ctrl);
        (void) servo_gripper_stop();
        ctrl->teach_jog_gripper_cmd_valid = false;
    }

    if (motor_changed) {
        if (servo_set_joint_servo_mode(step->motor_id) != FSP_SUCCESS) {
            LOG_E("Teach jog hold failed to arm joint %u", (unsigned int) step->motor_id);
            motion_ctrl_clear_teach_jog(ctrl, true);
            return false;
        }
        ctrl->teach_jog_q_valid = false;
    }

    ctrl->teach_jog_motor_id = step->motor_id;
    ctrl->teach_jog_engaged = true;

    if (!ctrl->teach_jog_q_valid) {
        ctrl->teach_jog_q_ref = clamp_joint_position_to_nvm(step->motor_id,
                                                            get_motor_feedback(step->motor_id)->position);
        ctrl->teach_jog_q_valid = true;
    }

    {
        float q_target[ROBSTRIDE_ACTIVE_JOINT_NUM] = {0};
        uint8_t idx = (uint8_t) (step->motor_id - ROBSTRIDE_MOTOR_ID_JOINT1);
        float step_delta = k_teach_jog_joint_step_rad[step->step_level] * (float) step->direction;
        float q_next = ctrl->teach_jog_q_ref + step_delta;

        position_limit(step->motor_id, &q_next, &step_delta);
        ctrl->teach_jog_q_ref = q_next;

        read_joint_pose(q_target);
        q_target[idx] = q_next;

        if (!send_joint_position_targets(q_target)) {
            LOG_E("Teach jog hold command failed");
            return true;
        }

        ctrl->hold_q_ref[idx] = q_next;
        capture_teach_prev_q(ctrl);
        ctrl->teach_settle_s = 0.0f;
    }

    return true;
}

static bool run_teach_jog_hold(motion_controller_t *ctrl, float dt) {
    teach_jog_hold_cmd_t hold = {0};
    bool hold_changed = false;

    if (ctrl == NULL) {
        return false;
    }

    teach_jog_hold_read(&hold);
    if (!hold.active) {
        ctrl->teach_jog_hold_active = false;
        ctrl->teach_jog_hold_direction = 0;
        ctrl->teach_jog_hold_step_level = 0U;
        ctrl->teach_jog_hold_elapsed_s = 0.0f;
        return false;
    }

    hold_changed = (!ctrl->teach_jog_hold_active) ||
                   (ctrl->teach_jog_motor_id != hold.motor_id) ||
                   (ctrl->teach_jog_hold_direction != hold.direction) ||
                   (ctrl->teach_jog_hold_step_level != hold.step_level);

    ctrl->teach_jog_hold_active = true;
    ctrl->teach_jog_hold_direction = hold.direction;
    ctrl->teach_jog_hold_step_level = hold.step_level;

    if (hold_changed) {
        ctrl->teach_jog_hold_elapsed_s = TEACH_JOG_HOLD_REPEAT_S;
    } else {
        ctrl->teach_jog_hold_elapsed_s += dt;
    }

    if (ctrl->teach_jog_hold_elapsed_s < TEACH_JOG_HOLD_REPEAT_S) {
        return true;
    }

    ctrl->teach_jog_hold_elapsed_s -= TEACH_JOG_HOLD_REPEAT_S;
    return execute_teach_jog_hold_step(ctrl, &hold);
}

/**
 * @brief TEACHING 状态的周期控制循环
 */
static void teach_control_loop(motion_controller_t *ctrl, float dt) {
    bool hold_active = false;

    if (ctrl == NULL) return;
    if (dt <= 0.0f) dt = 0.01f;

    ensure_hold_refs(ctrl);

    hold_active = run_teach_jog_hold(ctrl, dt);

    if (!hold_active) {
        if (ctrl->teach_jog_engaged) {
            ctrl->teach_settle_s += dt;
            if ((ctrl->teach_settle_s >= TEACH_SETTLE_S) &&
                !ctrl->teach_locked &&
                enter_teach_lock(ctrl)) {
                motion_ctrl_clear_teach_jog(ctrl, false);
                LOG_I("Teach jog timeout relock triggered");
            }
            update_touch_handoff(ctrl);
            update_gripper_control(ctrl, dt);
            return;
        }

        if (ctrl->teach_locked) {
            if (teach_should_unlock(ctrl) && enter_teach_free(ctrl)) {
                LOG_I("Teach passive unlock triggered");
            }
        } else {
            if (teach_all_joints_still(ctrl)) {
                ctrl->teach_settle_s += dt;
                if ((ctrl->teach_settle_s >= TEACH_SETTLE_S) && enter_teach_lock(ctrl)) {
                    LOG_I("Teach passive relock triggered");
                }
            } else {
                ctrl->teach_settle_s = 0.0f;
            }
        }
    } else {
        ctrl->teach_settle_s = 0.0f;
    }

    update_touch_handoff(ctrl);
    update_gripper_control(ctrl, dt);
}

/**
 * @brief PLAYBACK 状态的周期控制循环
 */
static void playback_control_loop(motion_controller_t *ctrl, float dt) {
    float prev_time;
    float q_target[TRAJ_MAX_JOINTS] = {0};
    float v_target[TRAJ_MAX_JOINTS] = {0};
    bool seg_done = false;
    bool seq_done = false;
    bool action_pause_armed = false;
    static uint32_t s_track_log_div = 0U;

    if ((ctrl == NULL) || (ctrl->state != MOTION_STATE_PLAYBACK)) return;

    if (ctrl->gripper_action_pause_s > 0.0f) {
        ctrl->gripper_action_pause_s -= dt;
        if (ctrl->gripper_action_pause_s < 0.0f) {
            ctrl->gripper_action_pause_s = 0.0f;
        }
        update_touch_handoff(ctrl);
        update_gripper_control(ctrl, dt);
        return;
    }

    prev_time = ctrl->seq_start_time;
    ctrl->seq_start_time += dt;

    traj_eval(&ctrl->trajectory, ctrl->seq_start_time, q_target, v_target, &seg_done, &seq_done);
    (void) v_target;
    (void) seg_done;

    for (uint8_t i = 0U; i < ROBSTRIDE_ACTIVE_JOINT_NUM; ++i) {
        q_target[i] = clamp_joint_position_to_nvm(k_joint_ids[i], q_target[i]);
    }

    update_playback_upload_state(ctrl, q_target);

    if (!send_joint_position_targets(q_target)) {
        LOG_E("Playback joint command failed");
    }

    if ((s_track_log_div++ % 20U) == 0U) {
        LOG_D("Playback target deg: J1=%.2f J2=%.2f J3=%.2f J4=%.2f J5=%.2f",
              q_target[0] * RAD2DEG_F,
              q_target[1] * RAD2DEG_F,
              q_target[2] * RAD2DEG_F,
              q_target[3] * RAD2DEG_F,
              q_target[4] * RAD2DEG_F);
    }

    if (ctrl->trajectory.total_segments > 0U) {
        float boundary_time = 0.0f;
        for (uint16_t seg = 0U; seg < ctrl->trajectory.total_segments; ++seg) {
            boundary_time += ctrl->trajectory.segments[seg][0].duration;
            if ((prev_time < boundary_time) && (ctrl->seq_start_time >= boundary_time)) {
                uint8_t action = ctrl->trajectory.segments[seg][0].action;
                if ((action == ACTION_GRIP) || (action == ACTION_RELEASE)) {
                    (void) trigger_gripper_action(ctrl, action);
                    action_pause_armed = (ctrl->gripper_action_pause_s > 0.0f);
                }
            }
        }
    }

    update_touch_handoff(ctrl);
    update_gripper_control(ctrl, dt);

    if (action_pause_armed || (ctrl->gripper_action_pause_s > 0.0f)) {
        return;
    }

    if (seq_done) {
        capture_hold_refs(ctrl);
        clear_playback_upload_state(ctrl);
        ctrl->state = MOTION_STATE_IDLE;
        traj_reset(&ctrl->trajectory);
        LOG_I("Playback sequence completed");
    }
}

/**
 * @brief IDLE 状态的姿态保持循环
 */
static void idle_control_loop(motion_controller_t *ctrl, float dt) {
    ensure_hold_refs(ctrl);
    if (!ctrl->idle_lock_active && !lock_all_joints_current(ctrl)) {
        LOG_E("Idle lock command failed");
    }
    update_touch_handoff(ctrl);
    update_gripper_control(ctrl, dt);
}

/**
 * @brief 检查关节力矩反馈是否触发碰撞保护
 */
static bool check_joint_torque_protection(motion_controller_t *ctrl) {
    if (ctrl == NULL) {
        return false;
    }

    for (uint8_t i = 0U; i < ROBSTRIDE_ACTIVE_JOINT_NUM; ++i) {
        float limit = ctrl->config.max_torque[i];
        float torque = get_motor_feedback(k_joint_ids[i])->torque;
        float torque_fb = absf_local(torque);

        if ((limit > 0.0f) && (torque_fb >= limit)) {
            if (ctrl->torque_limit_cycles[i] < UINT8_MAX) {
                ctrl->torque_limit_cycles[i]++;
            }
        } else {
            ctrl->torque_limit_cycles[i] = 0U;
        }

        if (ctrl->torque_limit_cycles[i] >= TORQUE_PROTECTION_CYCLES) {
            motion_ctrl_on_torque_protection(i, torque, limit);
            motion_ctrl_stop(ctrl);
            LOG_E("Torque protection latched on joint %u, fb=%.2f, limit=%.2f",
                  (unsigned int) (i + 1U),
                  torque,
                  limit);
            return true;
        }
    }

    return false;
}

/**
 * @brief 初始化运控状态机
 * @param ctrl 要初始化的控制器实例
 * @param config 可选配置，传 NULL 使用默认参数
 * @retval true 初始化成功
 * @retval false 初始化失败
 */
bool motion_ctrl_init(motion_controller_t *ctrl, const motion_ctrl_config_t *config) {
    if (ctrl == NULL) {
        LOG_E("motion_ctrl_init: ctrl is NULL");
        return false;
    }

    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->state = MOTION_STATE_IDLE;
    set_gripper_hold_state(ctrl, GRIPPER_HOLD_IDLE);
    reset_touch_handoff_runtime(false);
    s_touch_monitor_enabled = touch_drv_is_ready();

    if (config != NULL) {
        memcpy(&ctrl->config, config, sizeof(ctrl->config));
    } else {
        memcpy(&ctrl->config, &DEFAULT_CONFIG, sizeof(ctrl->config));
    }

    traj_reset(&ctrl->trajectory);
    capture_hold_refs(ctrl);
    clear_playback_upload_state(ctrl);
    ctrl->idle_lock_active = false;
    reset_teach_passive_state(ctrl);
    reset_teach_jog_ref(ctrl);
    reset_handoff_wait_internal(ctrl);
    reset_torque_protection_state(ctrl);
    ctrl->is_initialized = true;

    LOG_I("Motion controller initialized");
    return true;
}

/**
 * @brief 将关节 1~4 切回串口舵机闭环模式
 */
bool motion_ctrl_set_motion_mode(motion_controller_t *ctrl) {
    if ((ctrl == NULL) || !ctrl->is_initialized) {
        LOG_E("motion_ctrl_set_motion_mode: controller not initialized");
        return false;
    }

    for (uint8_t i = 0U; i < ROBSTRIDE_ACTIVE_JOINT_NUM; ++i) {
        if (servo_set_joint_servo_mode(k_joint_ids[i]) != FSP_SUCCESS) {
            LOG_E("Failed to set joint %u servo mode", (unsigned int) (i + 1U));
            return false;
        }
    }

    ctrl->idle_lock_active = false;
    ctrl->teach_locked = false;
    return true;
}

/**
 * @brief 进入低刚度示教状态
 */
bool motion_ctrl_start_teaching(motion_controller_t *ctrl) {
    if ((ctrl == NULL) || !ctrl->is_initialized) {
        LOG_E("motion_ctrl_start_teaching: controller not initialized");
        return false;
    }

    if ((ctrl->state != MOTION_STATE_IDLE) && (ctrl->state != MOTION_STATE_TEACHING)) {
        LOG_W("Cannot start teaching while in state %d", ctrl->state);
        return false;
    }

    refresh_joint_feedback_best_effort("Teaching entry");
    teach_jog_hold_clear();
    reset_teach_jog_ref(ctrl);
    reset_teach_passive_state(ctrl);
    reset_torque_protection_state(ctrl);
    traj_reset(&ctrl->trajectory);
    clear_playback_upload_state(ctrl);
    ctrl->seq_start_time = 0.0f;
    ctrl->gripper_action_pause_s = 0.0f;
    reset_handoff_wait_internal(ctrl);
    ctrl->state = MOTION_STATE_TEACHING;
    ctrl->idle_lock_active = false;
    if (!enter_teach_lock(ctrl)) {
        ctrl->state = MOTION_STATE_IDLE;
        return false;
    }
    LOG_I("Teaching mode started");
    return true;
}

/**
 * @brief 构建样条轨迹并进入 PLAYBACK 状态
 */
bool motion_ctrl_start_playback(motion_controller_t *ctrl, const action_sequence_t *seq) {
    action_sequence_t seq_ctrl = {0};

    if ((ctrl == NULL) || !ctrl->is_initialized || (seq == NULL)) {
        LOG_E("motion_ctrl_start_playback: invalid parameters");
        return false;
    }

    if (!nvm_is_action_sequence_valid(seq, false)) {
        LOG_E("motion_ctrl_start_playback: invalid action sequence");
        return false;
    }

    if (ctrl->state != MOTION_STATE_IDLE) {
        LOG_W("Cannot start playback while in state %d", ctrl->state);
        return false;
    }

    if ((seq->frame_count < 2U) || (seq->frame_count > MAX_FRAMES_PER_SEQ)) {
        LOG_E("Action sequence frame_count invalid: %u", (unsigned int) seq->frame_count);
        return false;
    }

    clear_playback_upload_state(ctrl);
    if (!release_idle_lock_if_needed(ctrl)) {
        return false;
    }

    if (!motion_ctrl_set_motion_mode(ctrl)) {
        return false;
    }

    if (seq->frame_count < MAX_FRAMES_PER_SEQ) {
        seq_ctrl.frame_count = seq->frame_count + 1U;
        seq_ctrl.frames[0].angle_m1 = g_motors[0].feedback.position;
        seq_ctrl.frames[0].angle_m2 = g_motors[1].feedback.position;
        seq_ctrl.frames[0].angle_m3 = g_motors[2].feedback.position;
        seq_ctrl.frames[0].angle_m4 = g_motors[3].feedback.position;
        /* 兼容字段 angle_m5 现用于第 5 关节，并在回放前统一转换为弧度。 */
        seq_ctrl.frames[0].angle_m5 = g_motors[4].feedback.position;
        seq_ctrl.frames[0].duration_ms = 0U;
        seq_ctrl.frames[0].action = MOVE_ONLY;

        for (uint32_t i = 0U; i < seq->frame_count; ++i) {
            seq_ctrl.frames[i + 1U].angle_m1 = seq->frames[i].angle_m1 * DEG2RAD_F;
            seq_ctrl.frames[i + 1U].angle_m2 = seq->frames[i].angle_m2 * DEG2RAD_F;
            seq_ctrl.frames[i + 1U].angle_m3 = seq->frames[i].angle_m3 * DEG2RAD_F;
            seq_ctrl.frames[i + 1U].angle_m4 = seq->frames[i].angle_m4 * DEG2RAD_F;
            seq_ctrl.frames[i + 1U].angle_m5 = seq->frames[i].angle_m5 * DEG2RAD_F;
            seq_ctrl.frames[i + 1U].duration_ms = seq->frames[i].duration_ms;
            seq_ctrl.frames[i + 1U].action = seq->frames[i].action;
        }
    } else {
        seq_ctrl = *seq;
        for (uint32_t i = 0U; i < seq_ctrl.frame_count; ++i) {
            seq_ctrl.frames[i].angle_m1 *= DEG2RAD_F;
            seq_ctrl.frames[i].angle_m2 *= DEG2RAD_F;
            seq_ctrl.frames[i].angle_m3 *= DEG2RAD_F;
            seq_ctrl.frames[i].angle_m4 *= DEG2RAD_F;
            seq_ctrl.frames[i].angle_m5 *= DEG2RAD_F;
        }
    }

    if (!traj_init_from_sequence(&ctrl->trajectory, &seq_ctrl)) {
        LOG_E("Failed to initialize trajectory from sequence");
        return false;
    }

    ctrl->seq_start_time = 0.0f;
    ctrl->gripper_action_pause_s = 0.0f;
    reset_handoff_wait_internal(ctrl);
    reset_torque_protection_state(ctrl);
    ctrl->state = MOTION_STATE_PLAYBACK;
    LOG_I("Playback mode started with %lu frames", (unsigned long) seq->frame_count);
    return true;
}

/**
 * @brief 停止当前模式并回退到 IDLE 姿态保持
 */
void motion_ctrl_stop(motion_controller_t *ctrl) {
    if ((ctrl == NULL) || !ctrl->is_initialized) return;

    teach_jog_hold_clear();
    if (ctrl->state != MOTION_STATE_PLAYBACK) {
        refresh_joint_feedback_best_effort("Motion stop");
    }
    capture_hold_refs(ctrl);
    set_gripper_hold_state(ctrl, GRIPPER_HOLD_IDLE);
    reset_handoff_wait_internal(ctrl);
    reset_teach_passive_state(ctrl);
    (void) servo_gripper_stop();
    clear_playback_upload_state(ctrl);
    ctrl->state = MOTION_STATE_IDLE;
    ctrl->seq_start_time = 0.0f;
    ctrl->gripper_action_pause_s = 0.0f;
    traj_reset(&ctrl->trajectory);
    reset_teach_jog_ref(ctrl);
    ctrl->teach_jog_engaged = false;
    reset_torque_protection_state(ctrl);
    if (!lock_all_joints_current(ctrl)) {
        ctrl->idle_lock_active = false;
    }
    LOG_I("Motion control stopped and switched to IDLE hold");
}

/**
 * @brief 清除 teach_jog 命令，并按需停止最后一个受控电机
 */
void motion_ctrl_clear_teach_jog(motion_controller_t *ctrl, bool stop_last_motor) {
    uint8_t motor_id = 0U;
    teach_jog_hold_cmd_t hold = {0};
    teach_jog_hold_read(&hold);

    if ((ctrl != NULL) && is_motor_id_valid(ctrl->teach_jog_motor_id)) {
        motor_id = ctrl->teach_jog_motor_id;
    } else if (hold.active && is_motor_id_valid(hold.motor_id)) {
        motor_id = hold.motor_id;
    }

    teach_jog_hold_clear();

    if (stop_last_motor && is_motor_id_valid(motor_id)) {
        if (motor_id == ROBSTRIDE_MOTOR_ID_GRIPPER) {
            set_gripper_hold_state(ctrl, GRIPPER_HOLD_IDLE);
            reset_handoff_wait_internal(ctrl);
            (void) servo_gripper_stop();
        } else if ((ctrl != NULL) && (ctrl->state == MOTION_STATE_TEACHING)) {
            (void) servo_stop_motor(motor_id, false);
        } else {
            (void) servo_stop_motor(motor_id, true);
        }
    }

    if (ctrl != NULL) {
        capture_hold_refs(ctrl);
        capture_teach_prev_q(ctrl);
        ctrl->teach_settle_s = 0.0f;
        reset_teach_jog_ref(ctrl);
        ctrl->teach_jog_engaged = false;
    }
}

void motion_ctrl_arm_handoff_wait(motion_controller_t *ctrl) {
    if ((ctrl == NULL) || !ctrl->is_initialized) {
        return;
    }

    ctrl->handoff_release_stop_timer = 0.0f;
    reset_touch_handoff_runtime(true);

    if (!touch_monitor_enabled()) {
        ctrl->handoff_state = HANDOFF_DONE;
        return;
    }

    if (ctrl->gripper_hold_state != GRIPPER_HOLD_GRASP) {
        ctrl->handoff_state = HANDOFF_DONE;
        LOG_W("Handoff wait requested without active grasp; bypassing wait");
        return;
    }

    ctrl->handoff_state = HANDOFF_ARMED;
    LOG_I("Touch handoff wait armed");
}

void motion_ctrl_reset_handoff_wait(motion_controller_t *ctrl) {
    if ((ctrl == NULL) || !ctrl->is_initialized) {
        return;
    }

    reset_handoff_wait_internal(ctrl);
}

bool motion_ctrl_is_handoff_done(const motion_controller_t *ctrl) {
    return (ctrl != NULL) && (ctrl->handoff_state == HANDOFF_DONE);
}

/**
 * @brief 按当前运控状态分发一个控制周期
 */
void motion_ctrl_loop(motion_controller_t *ctrl, float dt) {
    if ((ctrl == NULL) || !ctrl->is_initialized || ctrl->emergency_stop) return;
    if (dt <= 0.0f) return;

    if ((ctrl->state == MOTION_STATE_TEACHING) && check_joint_torque_protection(ctrl)) {
        return;
    }

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
        default:
            clear_playback_upload_state(ctrl);
            ctrl->state = MOTION_STATE_ERROR;
            break;
    }
}

/**
 * @brief 获取当前运控状态
 */
motion_state_t motion_ctrl_get_state(const motion_controller_t *ctrl) {
    return (ctrl != NULL) ? ctrl->state : MOTION_STATE_ERROR;
}

/**
 * @brief 更新兼容保留的重力补偿参数
 */

/* -------------------------------------------------------------------------- */
/* 兼容参数接口                                                                 */
/* 保留旧的 kp/kd 参数设置接口，保证上层调用不需要改名；新串口方案下这些参数主要   */
/* 作为配置兼容字段保存，不再直接映射到底层舵机协议。                           */
/* -------------------------------------------------------------------------- */
/* 兼容参数接口                                                                 */
/*                                                                            */
/* 保留旧的 kp/kd 参数设置接口，以保证上层调用不需要改名；在新串口方案下，这些   */
/* 参数主要作为配置兼容字段保存，不再直接映射到底层舵机协议。                   */
/* -------------------------------------------------------------------------- */
/**
 * @brief 保存示教模式兼容参数
 */
void motion_ctrl_set_teach_params(motion_controller_t *ctrl,
                                  const float kp[ROBSTRIDE_JOINT_NUM],
                                  const float kd[ROBSTRIDE_JOINT_NUM],
                                  bool enable_joint1) {
    motion_ctrl_set_common_pd(ctrl, kp, kd);
    (void) enable_joint1;
}

/**
 * @brief 保存回放模式兼容参数
 */
void motion_ctrl_set_playback_params(motion_controller_t *ctrl,
                                     const float kp[ROBSTRIDE_JOINT_NUM],
                                     const float kd[ROBSTRIDE_JOINT_NUM]) {
    motion_ctrl_set_common_pd(ctrl, kp, kd);
}

/**
 * @brief 触发急停，并将机械臂冻结在当前姿态
 */
void motion_ctrl_emergency_stop(motion_controller_t *ctrl) {
    if ((ctrl == NULL) || !ctrl->is_initialized) return;

    motion_ctrl_stop(ctrl);
    ctrl->emergency_stop = true;
    (void) lock_all_joints_current(ctrl);
    (void) servo_gripper_stop();
    LOG_E("Emergency stop triggered");
}

/**
 * @brief 清除控制器侧的急停锁存
 */
void motion_ctrl_clear_emergency_stop(motion_controller_t *ctrl) {
    if ((ctrl == NULL) || !ctrl->is_initialized) return;

    ctrl->emergency_stop = false;
    capture_hold_refs(ctrl);
    reset_torque_protection_state(ctrl);
    LOG_I("Emergency stop cleared");
}

/**
 * @brief 力矩保护预留回调，当前默认空实现
 */
void motion_ctrl_on_torque_protection(uint8_t joint_index, float torque_feedback, float torque_limit) {
    (void) joint_index;
    (void) torque_feedback;
    (void) torque_limit;
}
