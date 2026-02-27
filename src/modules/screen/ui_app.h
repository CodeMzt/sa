/**
 * @file    ui_app.h
 * @brief   UI 应用层头文件
 */

#ifndef UI_APP_H_
#define UI_APP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "shared_data.h"



void ui_app_init(void);
void ui_update_status(void);

/* 队列操作API */
bool ui_add_instrument_to_queue(instrument_t inst);

/* 用户回调 */
void user_on_start(void);
void user_on_stop(void);
void user_on_voice_start(void);
void user_on_voice_stop(void);
void user_on_emergency_stop(void);
void user_on_estop_reset(void); /* 新增：急停复位回调 */
void user_on_instrument_selected(const char* instrument_name);

/* 示教模式回调 */
void user_on_teach_save_frame(uint8_t group_idx, uint8_t frame_idx, uint16_t duration_ms, uint8_t action_type);

#ifdef __cplusplus
}
#endif

#endif /* UI_APP_H_ */
