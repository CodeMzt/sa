#include "screen_interact.h"

#include "drv_i2c_touchpad.h"
#include "drv_spi_display.h"
#include "sys_log.h"
#include "test_mode.h"
#include "ui_app.h"
#include "lv_port.h"
#include "lvgl\lvgl.h"
#include "nvm_manager.h"
#include "robstride_motor.h"
#include "drv_canfd.h"
#include <math.h>

#define RAD2DEG_F 57.2957795130823208768f

void screen_interact_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED(pvParameters);

#if TEST_MODE_ACTIVE && !TEST_KEEP_SCREEN_INTERACT
    LOG_I("Test mode: screen_interact thread disabled.");
    vTaskDelete(NULL);
    return;
#endif

    vTaskDelay(pdMS_TO_TICKS(3200));

    lv_port_init();
    ui_app_init();
    LOG_I("Screen interact started.");
    while (1) {
        canfd_link_check();
        ui_update_status();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}


/**
 * @brief 示教保存帧实现（覆盖 weak）
 * 将当前电机位置和参数保存到 motion_config 的 RAM 镜像
 */
void user_on_teach_save_frame(uint8_t group_idx, uint8_t frame_idx, uint16_t duration_ms, uint8_t action_type){
    motion_config_t * cfg = (motion_config_t *)nvm_get_motion_config();
    if (!cfg || group_idx >= TOTAL_ACTION_GROUPS || frame_idx >= TEACH_FRAMES_PER_GROUP) {
        LOG_W("[TEACH] Invalid frame params: group=%d, frame=%d", group_idx, frame_idx);
        return;
    }

    action_sequence_t * seq = &cfg->groups[group_idx];
    motion_frame_t * frame = &seq->frames[frame_idx];

    /* 保存电机位置 */
    frame->angle_m1    = g_motors[0].feedback.position * RAD2DEG_F;
    frame->angle_m2    = g_motors[1].feedback.position * RAD2DEG_F;
    frame->angle_m3    = g_motors[2].feedback.position * RAD2DEG_F;
    frame->angle_m4    = g_motors[3].feedback.position * RAD2DEG_F;
    frame->duration_ms = duration_ms;
    frame->action      = (uint8_t)action_type;

    /* 更新帧计数（确保覆盖到当前帧） */
    if (frame_idx + 1 > seq->frame_count) {
        seq->frame_count = frame_idx + 1;
    }

    LOG_D("[TEACH] Save frame G%d F%d (deg): M1=%.2f, M2=%.2f, M3=%.2f, M4=%.2f, dur=%dms, action=%d",
          group_idx, frame_idx, 
          frame->angle_m1, frame->angle_m2, frame->angle_m3, frame->angle_m4,
          duration_ms, action_type);
}
