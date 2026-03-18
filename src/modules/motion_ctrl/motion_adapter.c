/**
 * @file motion_adapter.c
 * @brief 运动控制适配层实现
 * @date 2026-03-17
 * @author Ma Ziteng
 */

#include "motion_adapter.h"
#include "robstride_motor.h"
#include "shared_data.h"
#include "nvm_manager.h"
#include "sys_log.h"
#include "hal_data.h"
#include <math.h>
#include <string.h>

#define PI_F                        3.14159265358979323846f
#define SECTOR_LENGTH_RAD           (8.0f * PI_F)
#define WRAP_THRESHOLD_RAD          (4.0f * PI_F)
#define DEFAULT_NVM_SAVE_MS         (2000U)
#define DEFAULT_MAX_ACC_RAD_S2      (12.0f)


static const uint8_t k_joint_ids[MOTION_ADAPTER_JOINTS] = {
    ROBSTRIDE_MOTOR_ID_JOINT1,
    ROBSTRIDE_MOTOR_ID_JOINT2,
    ROBSTRIDE_MOTOR_ID_JOINT3,
    ROBSTRIDE_MOTOR_ID_JOINT4
};

static fsp_err_t persist_sector_sync(const int32_t sector[4]) {
    sys_config_t new_cfg = *nvm_get_sys_config();
    for (uint8_t i = 0; i < MOTION_ADAPTER_JOINTS; i++) {
        new_cfg.zero_offset[i] = (float)sector[i];
    }
    return nvm_save_sys_config(&new_cfg);
}

/**
 * @brief 获取对应减速比
 * @param joint_idx 关节索引（0~3）
 * @return 减速比
 */
static float get_joint_gear_ratio(uint8_t joint_idx) {
    if (joint_idx >= MOTION_ADAPTER_JOINTS) return 1.0f;
    float ratio = g_motor_gear_ratio[joint_idx];
    return (ratio > 0.0f) ? ratio : 1.0f;
}

/**
 * @brief 限幅函数
 * @param x 待限幅的值
 * @param x_min 最小值
 * @param x_max 最大值
 * @return 限幅后的值
 */
static float clampf(float x, float x_min, float x_max) {
    if (x < x_min) return x_min;
    if (x > x_max) return x_max;
    return x;
}

/**
 * @brief 更新绝对位置（展开环绕）
 * @param adapter 适配器实例指针
 */
static void update_unwrapped_positions(motion_adapter_t *adapter) {
    for (uint8_t i = 0; i < MOTION_ADAPTER_JOINTS; i++) {
        float gear_ratio = get_joint_gear_ratio(i);
        float raw_motor = g_motors[i].feedback.position * gear_ratio;

        if (!adapter->valid[i]) {
            adapter->last_raw[i] = raw_motor;
            adapter->abs_pos[i] = ((float)adapter->sector[i] * SECTOR_LENGTH_RAD + raw_motor) / gear_ratio;
            adapter->valid[i] = true;
            continue;
        }

        float delta = raw_motor - adapter->last_raw[i];
        if (delta > WRAP_THRESHOLD_RAD) {
            adapter->sector[i]--;
            adapter->sector_dirty = true;
        } else if (delta < -WRAP_THRESHOLD_RAD) {
            adapter->sector[i]++;
            adapter->sector_dirty = true;
        }

        adapter->last_raw[i] = raw_motor;
        adapter->abs_pos[i] = ((float)adapter->sector[i] * SECTOR_LENGTH_RAD + raw_motor) / gear_ratio;
    }
}

/**
 * @brief 可能的扇区持久化（写入NVM），根据时间间隔或强制保存
 * @param adapter 适配器实例指针
 * @param force_save 是否强制保存（忽略时间间隔）
 */
static void maybe_persist_sector(motion_adapter_t *adapter, bool force_save) {
    if (!adapter->initialized) return;
    if (!adapter->sector_dirty && !force_save) return;

    uint32_t now_tick = (uint32_t)xTaskGetTickCount();
    uint32_t interval_ticks = pdMS_TO_TICKS(adapter->cfg.nvm_save_interval_ms);

    if (!force_save && interval_ticks > 0U) {
        if ((now_tick - adapter->last_save_tick) < interval_ticks) return;
    }

    fsp_err_t err = persist_sector_sync(adapter->sector);
    if (err == FSP_SUCCESS) {
        adapter->sector_dirty = false;
        adapter->last_save_tick = now_tick;
    } else {
        LOG_E("Sector NVM save failed: %d", err);
    }
}

/**
 * @brief 初始化运动控制适配层
 * @param adapter 适配器实例指针
 * @param cfg 配置参数（可为NULL使用默认配置）
 * @return true 成功，false 失败
 */
bool motion_adapter_init(motion_adapter_t *adapter, const motion_adapter_config_t *cfg) {
    if (adapter == NULL) return false;

    memset(adapter, 0, sizeof(motion_adapter_t));

    if (cfg != NULL) {
        memcpy(&adapter->cfg, cfg, sizeof(motion_adapter_config_t));
    } else {
        for (uint8_t i = 0; i < MOTION_ADAPTER_JOINTS; i++) {
            adapter->cfg.kp[i] = 1.0f;
            adapter->cfg.kd[i] = 0.1f;
            adapter->cfg.max_velocity[i] = 1.0f;
            adapter->cfg.max_acceleration[i] = DEFAULT_MAX_ACC_RAD_S2;
        }
        adapter->cfg.nvm_save_interval_ms = DEFAULT_NVM_SAVE_MS;
    }

    if (adapter->cfg.nvm_save_interval_ms == 0U) {
        adapter->cfg.nvm_save_interval_ms = DEFAULT_NVM_SAVE_MS;
    }

    const sys_config_t *sys_cfg = nvm_get_sys_config();
    for (uint8_t i = 0; i < MOTION_ADAPTER_JOINTS; i++) {
        adapter->sector[i] = (int32_t)lroundf(sys_cfg->zero_offset[i]);
        if (adapter->cfg.max_velocity[i] <= 0.0f) {
            adapter->cfg.max_velocity[i] = 1.0f;
        }
        if (adapter->cfg.max_acceleration[i] <= 0.0f) {
            adapter->cfg.max_acceleration[i] = DEFAULT_MAX_ACC_RAD_S2;
        }
    }

    adapter->initialized = true;
    adapter->last_save_tick = (uint32_t)xTaskGetTickCount();

    update_unwrapped_positions(adapter);
    return true;
}

/**
 * @brief 设置PD参数
 * @param adapter 适配器实例指针
 * @param kp PD比例增益数组（长度4），为NULL表示不修改
 * @param kd PD微分增益数组（长度4），为NULL表示不修改
 */
void motion_adapter_set_pd(motion_adapter_t *adapter, const float kp[4], const float kd[4]) {
    if (adapter == NULL || !adapter->initialized) return;

    if (kp != NULL) {
        memcpy(adapter->cfg.kp, kp, MOTION_ADAPTER_JOINTS * sizeof(float));
    }
    if (kd != NULL) {
        memcpy(adapter->cfg.kd, kd, MOTION_ADAPTER_JOINTS * sizeof(float));
    }
}

/**
 * @brief 捕获当前绝对位置（展开环绕）
 * @param adapter 适配器实例指针
 * @param q_abs_out 绝对位置输出数组（长度4）
 * @return true 成功，false 失败
 */
bool motion_adapter_capture_abs(motion_adapter_t *adapter, float q_abs_out[4]) {
    if (adapter == NULL || !adapter->initialized) return false;

    update_unwrapped_positions(adapter);
    maybe_persist_sector(adapter, false);

    if (q_abs_out != NULL) {
        memcpy(q_abs_out, adapter->abs_pos, MOTION_ADAPTER_JOINTS * sizeof(float));
    }

    return true;
}

/**
 * @brief 执行一步运动控制
 * @param adapter 适配器实例指针
 * @param dt 时间步长（单位：秒）
 * @param q_target 目标位置数组（长度4）
 * @param v_target 目标速度数组（长度4）
 * @param q_abs_out 绝对位置输出数组（长度4）
 * @return true 成功，false 失败
 */
bool motion_adapter_step(motion_adapter_t *adapter,
                         float dt,
                         const float q_target[4],
                         const float v_target[4],
                         float q_abs_out[4],
                         float v_cmd_out[4],
                         float a_cmd_out[4]) {
    if (adapter == NULL || !adapter->initialized) return false;
    if (q_target == NULL || v_target == NULL || dt <= 0.0f) return false;

    update_unwrapped_positions(adapter);
    maybe_persist_sector(adapter, false);

    bool all_ok = true;

    for (uint8_t i = 0; i < MOTION_ADAPTER_JOINTS; i++) {
        float q_abs = adapter->abs_pos[i];
        float q_ref = q_target[i];

        float e_q = q_ref - q_abs;
        float e_v = v_target[i] - g_motors[i].feedback.velocity;

        float v_cmd = v_target[i] + adapter->cfg.kp[i] * e_q + adapter->cfg.kd[i] * e_v;
        float v_lim = adapter->cfg.max_velocity[i];
        v_cmd = clampf(v_cmd, -v_lim, v_lim);

        float a_cmd = (v_cmd - adapter->prev_v_cmd[i]) / dt;
        float a_lim = adapter->cfg.max_acceleration[i];
        a_cmd = clampf(a_cmd, -a_lim, a_lim);

        v_cmd = adapter->prev_v_cmd[i] + a_cmd * dt;
        v_cmd = clampf(v_cmd, -v_lim, v_lim);
        adapter->prev_v_cmd[i] = v_cmd;

        float a_feed = fabsf(a_cmd);
        if (a_feed < 0.01f) a_feed = 0.0f;

        fsp_err_t err = robstride_set_speed(k_joint_ids[i], v_cmd, 0.0f, a_feed);
        if (err != FSP_SUCCESS) {
            all_ok = false;
            LOG_E("Adapter speed cmd failed: joint=%u err=%d", (unsigned int)(i + 1U), err);
        }

        if (q_abs_out != NULL) q_abs_out[i] = q_abs;
        if (v_cmd_out != NULL) v_cmd_out[i] = v_cmd;
        if (a_cmd_out != NULL) a_cmd_out[i] = a_cmd;
    }

    return all_ok;
}

/**
 * @brief 强制持久化当前扇区（写入NVM）
 * @param adapter 适配器实例指针
 */
void motion_adapter_force_persist_sector(motion_adapter_t *adapter) {
    if (adapter == NULL || !adapter->initialized) return;

    update_unwrapped_positions(adapter);
    maybe_persist_sector(adapter, true);
}
