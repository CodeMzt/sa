/**
 * @file    shared_data.c
 * @brief   Shared runtime state helpers
 * @date    2026-02-21
 */

#include "shared_data.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

volatile system_status_t g_sys_status = {
    .is_eth_connected = false,
    .is_can_connected = false,
    .is_motion_link_connected = false,
    .is_mic_connected = false,
    .is_wifi_connected = false,
    .is_debug_mode_active = false,
    .is_running = false,
    .is_voice_command_running = false,
    .is_emergency_stop = false,
    .current_target = "",
    .queue_list = "Queue: [EMPTY]",
    .act_queue = {INSTRUMENT_NONE, INSTRUMENT_NONE, INSTRUMENT_NONE},
    .act_queue_count = 0U
};

volatile bool g_log_system_ready = false;

static volatile teach_jog_hold_cmd_t g_teach_jog_hold_cmd = {
    .active = false,
    .motor_id = 0U,
    .direction = 0,
    .step_level = 0U,
    .last_update_tick = 0U,
};

static void update_queue_display_locked(void) {
    if (g_sys_status.act_queue_count == 0U) {
        (void) snprintf((char *) g_sys_status.queue_list,
                        sizeof(g_sys_status.queue_list),
                        "Queue: [EMPTY]");
        return;
    }

    char buf[64] = {0};
    int pos = snprintf(buf, sizeof(buf), "Queue: ");

    for (uint8_t i = 0U; (i < g_sys_status.act_queue_count) && (pos >= 0); ++i) {
        size_t remaining = (pos < (int) sizeof(buf)) ? (sizeof(buf) - (size_t) pos) : 0U;
        if (remaining == 0U) {
            break;
        }

        if (i == 0U) {
            pos += snprintf(buf + pos,
                            remaining,
                            "[%s]",
                            get_instrument_name(g_sys_status.act_queue[i]));
        } else {
            pos += snprintf(buf + pos,
                            remaining,
                            " -> %s",
                            get_instrument_name(g_sys_status.act_queue[i]));
        }
    }

    strncpy((char *) g_sys_status.queue_list, buf, sizeof(g_sys_status.queue_list));
    g_sys_status.queue_list[sizeof(g_sys_status.queue_list) - 1U] = '\0';
}

bool is_instrument_in_queue(instrument_t inst) {
    bool found = false;

    taskENTER_CRITICAL();
    for (uint8_t i = 0U; i < g_sys_status.act_queue_count; ++i) {
        if (g_sys_status.act_queue[i] == inst) {
            found = true;
            break;
        }
    }
    taskEXIT_CRITICAL();

    return found;
}

bool add_instrument(instrument_t inst) {
    bool added = false;

    if (inst == INSTRUMENT_NONE) {
        return false;
    }

    taskENTER_CRITICAL();
    if (g_sys_status.act_queue_count < 3U) {
        bool duplicate = false;
        for (uint8_t i = 0U; i < g_sys_status.act_queue_count; ++i) {
            if (g_sys_status.act_queue[i] == inst) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate) {
            g_sys_status.act_queue[g_sys_status.act_queue_count] = inst;
            g_sys_status.act_queue_count++;
            update_queue_display_locked();
            added = true;
        }
    }
    taskEXIT_CRITICAL();

    return added;
}

void remove_instrument(uint8_t index) {
    taskENTER_CRITICAL();
    if (index < g_sys_status.act_queue_count) {
        for (uint8_t i = index; i < (g_sys_status.act_queue_count - 1U); ++i) {
            g_sys_status.act_queue[i] = g_sys_status.act_queue[i + 1U];
        }

        g_sys_status.act_queue_count--;
        g_sys_status.act_queue[g_sys_status.act_queue_count] = INSTRUMENT_NONE;
        update_queue_display_locked();
    }
    taskEXIT_CRITICAL();
}

void clear_act_queue(void) {
    taskENTER_CRITICAL();
    memset((void *) g_sys_status.act_queue, INSTRUMENT_NONE, sizeof(g_sys_status.act_queue));
    g_sys_status.act_queue_count = 0U;
    update_queue_display_locked();
    taskEXIT_CRITICAL();
}

uint8_t queue_count_get(void) {
    uint8_t count;

    taskENTER_CRITICAL();
    count = g_sys_status.act_queue_count;
    taskEXIT_CRITICAL();

    return count;
}

bool queue_head_peek(instrument_t *inst, uint8_t *queue_count) {
    bool has_head = false;

    taskENTER_CRITICAL();
    if (queue_count != NULL) {
        *queue_count = g_sys_status.act_queue_count;
    }
    if ((inst != NULL) && (g_sys_status.act_queue_count > 0U)) {
        *inst = g_sys_status.act_queue[0];
        has_head = true;
    }
    taskEXIT_CRITICAL();

    return has_head;
}

void queue_text_get(char *buffer, size_t buffer_size) {
    if ((buffer == NULL) || (buffer_size == 0U)) {
        return;
    }

    taskENTER_CRITICAL();
    strncpy(buffer, (const char *) g_sys_status.queue_list, buffer_size);
    taskEXIT_CRITICAL();

    buffer[buffer_size - 1U] = '\0';
}

const char *get_instrument_name(instrument_t inst) {
    switch (inst) {
        case INSTRUMENT_SCALPEL:
            return "SCALPEL";
        case INSTRUMENT_HEMOSTAT:
            return "HEMOSTAT";
        case INSTRUMENT_FORCEPS:
            return "FORCEPS";
        default:
            return "NONE";
    }
}

void update_queue_display(void) {
    taskENTER_CRITICAL();
    update_queue_display_locked();
    taskEXIT_CRITICAL();
}

void teach_jog_hold_read(teach_jog_hold_cmd_t *cmd) {
    if (cmd == NULL) {
        return;
    }

    taskENTER_CRITICAL();
    *cmd = g_teach_jog_hold_cmd;
    taskEXIT_CRITICAL();
}

void teach_jog_hold_set(uint8_t motor_id, int8_t direction, uint8_t step_level, uint32_t last_update_tick) {
    taskENTER_CRITICAL();
    g_teach_jog_hold_cmd.active = true;
    g_teach_jog_hold_cmd.motor_id = motor_id;
    g_teach_jog_hold_cmd.direction = direction;
    g_teach_jog_hold_cmd.step_level = step_level;
    g_teach_jog_hold_cmd.last_update_tick = last_update_tick;
    taskEXIT_CRITICAL();
}

void teach_jog_hold_clear(void) {
    taskENTER_CRITICAL();
    g_teach_jog_hold_cmd.active = false;
    g_teach_jog_hold_cmd.motor_id = 0U;
    g_teach_jog_hold_cmd.direction = 0;
    g_teach_jog_hold_cmd.step_level = 0U;
    g_teach_jog_hold_cmd.last_update_tick = 0U;
    taskEXIT_CRITICAL();
}

bool teach_jog_hold_active(void) {
    bool active = false;

    taskENTER_CRITICAL();
    active = g_teach_jog_hold_cmd.active;
    taskEXIT_CRITICAL();

    return active;
}

void motion_link_set(bool connected) {
    taskENTER_CRITICAL();
    g_sys_status.is_motion_link_connected = connected;
    taskEXIT_CRITICAL();
}

bool motion_link_is_up(void) {
    bool connected;

    taskENTER_CRITICAL();
    connected = g_sys_status.is_motion_link_connected;
    taskEXIT_CRITICAL();

    return connected;
}
