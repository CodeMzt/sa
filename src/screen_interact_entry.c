/**
 * @file  screen_interact_entry.c
 * @brief 屏幕交互任务入口（初始化 LVGL + UI，循环处理 UI 事件与状态更新）
 * @date  2026-02-11
 * @author Ma Ziteng
 */

#include "screen_interact.h"
#include "drv_i2c_touchpad.h"
#include "drv_spi_display.h"
#include "sys_log.h"
#include "test_mode.h"
#include "ui_app.h"
#include "lv_port.h"
#include "lvgl\lvgl.h"
#include "nvm_manager.h"
#include "modules/servo/drv_servo.h"
#include "shared_data.h"

#define RAD2DEG_F 57.2957795130823208768f
#define LOG_READY_WAIT_SLICE_MS   10U
#define LOG_READY_WAIT_MAX_MS   1000U
#define SCREEN_WARMUP_DELAY_MS  1000U

void screen_interact_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED(pvParameters);

#if TEST_MODE_ACTIVE && !TEST_KEEP_SCREEN_INTERACT
    LOG_I("Test mode: screen_interact thread disabled.");
    vTaskDelete(NULL);
    return;
#endif

    uint32_t wait_ms = 0U;
    while ((!g_log_system_ready) && (wait_ms < LOG_READY_WAIT_MAX_MS)) {
        vTaskDelay(pdMS_TO_TICKS(LOG_READY_WAIT_SLICE_MS));
        wait_ms += LOG_READY_WAIT_SLICE_MS;
    }

    vTaskDelay(pdMS_TO_TICKS(SCREEN_WARMUP_DELAY_MS));

    lv_port_init();
    ui_app_init();
    LOG_I("Screen interact started.");
    while (1) {
        servo_link_check();
        ui_update_status();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}


/**
 * @brief 示教保存帧实现
 * @param group_idx 组索引
 * @param frame_idx 帧索引
 * @param duration_ms 持续时间
 * @param action_type 动作类型
 */
void user_on_teach_save_frame(uint8_t group_idx, uint8_t frame_idx, uint16_t duration_ms, uint8_t action_type) {
    motion_config_t *cfg = (motion_config_t *)nvm_get_motion_config();
    if (!cfg || group_idx >= TOTAL_ACTION_GROUPS || frame_idx >= TEACH_FRAMES_PER_GROUP) {
        LOG_W("[TEACH] Invalid frame params: group=%d, frame=%d", group_idx, frame_idx);
        return;
    }

    action_sequence_t *seq = &cfg->groups[group_idx];
    motion_frame_t *frame = &seq->frames[frame_idx];

    /* 保存关节绝对角（单位：deg） */
    frame->angle_m1    = g_motors[0].feedback.position * RAD2DEG_F;
    frame->angle_m2    = g_motors[1].feedback.position * RAD2DEG_F;
    frame->angle_m3    = g_motors[2].feedback.position * RAD2DEG_F;
    frame->angle_m4    = g_motors[3].feedback.position * RAD2DEG_F;
    /* 兼容字段 angle_m5 现用于保存第 5 关节角度。 */
    frame->angle_m5    = g_motors[4].feedback.position * RAD2DEG_F;
    frame->duration_ms = duration_ms;
    frame->action      = (uint8_t)action_type;
    seq->joint_mask    = MOTION_JOINT_MASK_ALL;

    /* 更新帧计数 */
    if (frame_idx + 1 > seq->frame_count) seq->frame_count = (uint16_t)(frame_idx + 1);

    LOG_D("[TEACH] Save frame G%d F%d (deg): M1=%.2f, M2=%.2f, M3=%.2f, M4=%.2f, M5=%.2f, dur=%dms, action=%d",
          group_idx, frame_idx, 
          frame->angle_m1, frame->angle_m2, frame->angle_m3, frame->angle_m4, frame->angle_m5,
          duration_ms, action_type);
}
