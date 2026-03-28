/**
 * @file    shared_data.h
 * @brief   全局共享数据定义与操作接口
 * @date    2026-01-24
 * @author  Ma Ziteng
 */

#ifndef SHARED_DATA_H_
#define SHARED_DATA_H_

#include <stddef.h>
#include "hal_data.h"
#include "motor_state.h"

/* 器械枚举 */
typedef enum {
    INSTRUMENT_NONE = 0,
    INSTRUMENT_FORCEPS = 1,
    INSTRUMENT_HEMOSTAT = 2,
    INSTRUMENT_SCALPEL = 3
} instrument_t;

typedef struct {
    bool is_eth_connected;
    bool is_can_connected;
    bool is_motion_link_connected;
    bool is_mic_connected;
    bool is_wifi_connected;
    bool is_debug_mode_active;

    bool is_running;
    bool is_voice_command_running;
    bool is_emergency_stop;

    char current_target[32];
    char queue_list[64];

    instrument_t act_queue[3];
    uint8_t act_queue_count;
} system_status_t;

typedef struct {
    bool active;
    uint8_t motor_id;
    int8_t direction;
    uint8_t step_level;
    uint32_t last_update_tick;
} teach_jog_hold_cmd_t;

extern volatile system_status_t g_sys_status;
extern volatile bool g_log_system_ready;

/* 全局电机数组（由 motor_state.c 定义，此处导出） */
extern motor_t g_motors[MOTOR_NUM];

/* 发送命令队列 */
extern QueueHandle_t can_tx_queue;

/* 器械队列操作函数 */
bool is_instrument_in_queue(instrument_t inst);
bool add_instrument(instrument_t inst);
void remove_instrument(uint8_t index);
void clear_act_queue(void);
uint8_t queue_count_get(void);
bool queue_head_peek(instrument_t *inst, uint8_t *queue_count);
void queue_text_get(char *buffer, size_t buffer_size);
const char* get_instrument_name(instrument_t inst);
void update_queue_display(void);

void teach_jog_hold_read(teach_jog_hold_cmd_t *cmd);
void teach_jog_hold_set(uint8_t motor_id, int8_t direction, uint8_t step_level, uint32_t last_update_tick);
void teach_jog_hold_clear(void);
bool teach_jog_hold_active(void);

void motion_link_set(bool connected);
bool motion_link_is_up(void);

#endif /* SHARED_DATA_H_ */
