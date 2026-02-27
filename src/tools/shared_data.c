/*
 * shared_data.c
 *
 *  Created on: 2026年2月21日
 *      Author: Ma Ziteng
 */

#include "shared_data.h"
#include <string.h>
#include <stdbool.h>

volatile system_status_t g_sys_status = {
    .is_eth_connected = false,
    .is_can_connected = false,
    .is_mic_connected = false,
    .is_wifi_connected = false,
    .is_running = false,
    .is_voice_command_running = false,
    .is_emergency_stop = false,
    .current_target = "",
    .queue_list = "",
    .act_queue = {INSTRUMENT_NONE, INSTRUMENT_NONE, INSTRUMENT_NONE},
    .act_queue_count = 0
};

/**
 * @brief 检查器械是否在队列中
 * @param inst 器械类型
 * @return true 表示在队列中
 */
bool is_instrument_in_queue(instrument_t inst) {
    for (uint8_t i = 0; i < g_sys_status.act_queue_count; i++) {
        if (g_sys_status.act_queue[i] == inst) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 添加器械到队列
 * @param inst 器械类型
 * @return true 表示添加成功
 */
bool add_instrument_to_queue(instrument_t inst) {
    if (inst == INSTRUMENT_NONE) {
        return false;
    }

    if (g_sys_status.act_queue_count >= 3) {
        return false;
    }

    if (is_instrument_in_queue(inst)) {
        return false;
    }

    g_sys_status.act_queue[g_sys_status.act_queue_count] = inst;
    g_sys_status.act_queue_count++;
    return true;
}

/**
 * @brief 从队列中移除器械
 * @param index 队列索引
 */
void remove_instrument_from_queue(uint8_t index) {
    if (index >= g_sys_status.act_queue_count) {
        return;
    }

    for (uint8_t i = index; i < g_sys_status.act_queue_count - 1; i++) {
        g_sys_status.act_queue[i] = g_sys_status.act_queue[i + 1];
    }

    g_sys_status.act_queue_count--;
    g_sys_status.act_queue[g_sys_status.act_queue_count] = INSTRUMENT_NONE;
}

/**
 * @brief 清空器械队列
 */
void clear_act_queue(void) {
    memset(g_sys_status.act_queue, INSTRUMENT_NONE, sizeof(g_sys_status.act_queue));
    g_sys_status.act_queue_count = 0;
}

/**
 * @brief 获取器械名称
 * @param inst 器械类型
 * @return 器械名称字符串
 */
const char* get_instrument_name(instrument_t inst) {
    switch (inst) {
        case INSTRUMENT_SCALPEL: return "SCALPEL";
        case INSTRUMENT_HEMOSTAT: return "HEMOSTAT";
        case INSTRUMENT_FORCEPS: return "FORCEPS";
        default: return "NONE";
    }
}

/**
 * @brief 更新队列显示字符串
 */
void update_queue_display_string(void) {
    if (g_sys_status.act_queue_count == 0) {
        snprintf(g_sys_status.queue_list, sizeof(g_sys_status.queue_list), "Queue: [EMPTY]");
    } else {
        char buf[64] = {0};
        int pos = 0;

        pos += snprintf(buf + pos, sizeof(buf) - pos, "Queue: ");

        for (uint8_t i = 0; i < g_sys_status.act_queue_count; i++) {
            if (i == 0) {
                pos += snprintf(buf + pos, sizeof(buf) - pos, "[%s]", get_instrument_name(g_sys_status.act_queue[i]));
            } else {
                pos += snprintf(buf + pos, sizeof(buf) - pos, " -> %s", get_instrument_name(g_sys_status.act_queue[i]));
            }
        }

        strncpy(g_sys_status.queue_list, buf, sizeof(g_sys_status.queue_list));
    }
}
