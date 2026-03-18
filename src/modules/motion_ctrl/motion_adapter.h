/**
 * @file motion_adapter.h
 * @brief 运动控制适配层接口（反馈展开 + 外环PD + 底层速度下发）
 * @date 2026-03-17
 * @author Ma Ziteng
 */

#ifndef MOTION_ADAPTER_H_
#define MOTION_ADAPTER_H_

#include <stdint.h>
#include <stdbool.h>

#define MOTION_ADAPTER_JOINTS (4U)

typedef struct {
    float kp[MOTION_ADAPTER_JOINTS];
    float kd[MOTION_ADAPTER_JOINTS];
    float max_velocity[MOTION_ADAPTER_JOINTS];
    float max_acceleration[MOTION_ADAPTER_JOINTS];
    uint32_t nvm_save_interval_ms;
} motion_adapter_config_t;

typedef struct {
    motion_adapter_config_t cfg;
    int32_t sector[MOTION_ADAPTER_JOINTS];
    float last_raw[MOTION_ADAPTER_JOINTS];
    float abs_pos[MOTION_ADAPTER_JOINTS];
    float prev_v_cmd[MOTION_ADAPTER_JOINTS];
    bool valid[MOTION_ADAPTER_JOINTS]; // 是否已成功捕获过绝对位置
    bool sector_dirty; // 位置环绕后是否需要保存扇区
    uint32_t last_save_tick;
    bool initialized;
} motion_adapter_t;

bool motion_adapter_init(motion_adapter_t *adapter, const motion_adapter_config_t *cfg);
void motion_adapter_set_pd(motion_adapter_t *adapter, const float kp[4], const float kd[4]);
bool motion_adapter_capture_abs(motion_adapter_t *adapter, float q_abs_out[4]);
bool motion_adapter_step(motion_adapter_t *adapter,
                         float dt,
                         const float q_target[4],
                         const float v_target[4],
                         float q_abs_out[4],
                         float v_cmd_out[4],
                         float a_cmd_out[4]);
void motion_adapter_force_persist_sector(motion_adapter_t *adapter);

#endif /* MOTION_ADAPTER_H_ */
