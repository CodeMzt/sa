/**
 * @file  voice_command_entry.c
 * @brief 语音指令任务入口（初始化麦克风 + EI 模型，循环处理语音识别与投票逻辑）
 * @date  2026-02-11
 * @author Ma Ziteng
 */

#include "voice_command.h"
#include "drv_microphone.h"
#include "sys_log.h"
#include "log_task.h"
#include "ei_integration.h"
#include "shared_data.h"
#include "test_mode.h"
#include "ui_app.h"
#include <stdio.h>
#include <string.h>

extern SemaphoreHandle_t audio_semaphore;

#define SAMPLE_RATE     8000

typedef struct __attribute__((packed)) {
    uint8_t  header[2];
    int16_t  payload[BLOCK_SIZE];
} audio_packet_t;

static audio_packet_t g_packets;
static volatile uint16_t g_fill_pos = 0;

#define VOTE_WINDOW_SIZE    12
#define VOTE_FORCEPS        35
#define VOTE_HEMOSTAT       15
#define VOTE_SCALPEL        25
#define LOG_READY_WAIT_SLICE_MS   10U
#define LOG_READY_WAIT_MAX_MS   1000U

static uint8_t g_vote_history[VOTE_WINDOW_SIZE];
static uint8_t g_vote_index = 0;
static uint8_t g_vote_count = 0;

/* Test-only path: enabled only when VOICE_TEST_MODE is defined. */
#if defined(VOICE_TEST_MODE)
#define VOICE_TEST_ENABLED (1)
#else
#define VOICE_TEST_ENABLED (0)
#endif

/**
 * @brief 窗口投票处理
 * @param inference_result 推理结果
 * @return 最终投票结果
 */
static int process_vote_window(int inference_result) {
    int final_result = -1;

    if (inference_result >= 0 && inference_result <= 4) {
        if (g_vote_count < VOTE_WINDOW_SIZE) g_vote_count++;

        g_vote_history[g_vote_index] = (uint8_t)inference_result;
        g_vote_index = (uint8_t)((g_vote_index + 1) % VOTE_WINDOW_SIZE);

        uint8_t counts[5] = {0};
        uint8_t consecutive = 0;
        uint8_t last_class = 0xFF;

        for (uint8_t i = 0; i < g_vote_count; i++) {
            uint8_t idx = (uint8_t)((g_vote_index - g_vote_count + i + VOTE_WINDOW_SIZE) % VOTE_WINDOW_SIZE);
            uint8_t curr_class = g_vote_history[idx];

            if (curr_class == last_class) consecutive++;
            else {
                consecutive = 1;
                last_class = curr_class;
            }
            counts[curr_class] = (uint8_t)(counts[curr_class] + consecutive);
        }

        LOG_D("Vote: class=%d, counts=[%d,%d,%d,%d,%d], window=%d/%d",
               inference_result,
               counts[0], counts[1], counts[2], counts[3], counts[4],
               g_vote_count, VOTE_WINDOW_SIZE);

        if (g_vote_count >= VOTE_WINDOW_SIZE) {
            if (counts[1] >= VOTE_FORCEPS) final_result = 1;
            else if (counts[2] >= VOTE_HEMOSTAT) final_result = 2;
            else if (counts[3] >= VOTE_SCALPEL) final_result = 3;
        }
    }

    return final_result;
}

void voice_command_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED(pvParameters);

#if TEST_MODE_ACTIVE && !TEST_KEEP_VOICE_COMMAND
    LOG_I("Test mode: voice_command thread disabled.");
    vTaskDelete(NULL);
    return;
#endif

    uint32_t wait_ms = 0U;
    while ((!g_log_system_ready) && (wait_ms < LOG_READY_WAIT_MAX_MS)) {
        vTaskDelay(pdMS_TO_TICKS(LOG_READY_WAIT_SLICE_MS));
        wait_ms += LOG_READY_WAIT_SLICE_MS;
    }

    fsp_err_t err = mic_driver_instance.init();
    if (err != FSP_SUCCESS) {
        LOG_E("Mic Init Failed");
        return;
    }
    ei_model_init();
    LOG_D("Voice Command Thread Start.");
    g_sys_status.is_mic_connected = true;
    R_BSP_IrqDisable(g_i2s0_cfg.rxi_irq);

#if VOICE_TEST_ENABLED
    g_sys_status.is_voice_command_running = true;
    R_BSP_IrqEnable(g_i2s0_cfg.rxi_irq);
    i2s0_start_rx();
    LOG_I("VOICE_TEST_MODE enabled: auto start voice test.");
#endif

    while (1) {
        while (g_sys_status.is_voice_command_running) {
            int ret = ei_run_inference();
            if (ret != -1) {
                int final_result = process_vote_window(ret);

                if (final_result >= 1 && final_result <= 3) {
#if VOICE_TEST_ENABLED
                      LOG_I("VT_EVT,id=%d,label=%s",
                          final_result,
                          ei_get_label_name(final_result));
#else
                    LOG_D("Add instrument %d to queue.", final_result);
                    add_instrument((uint8_t)final_result);
                    update_queue_display();
#endif
                }
            }
            vTaskDelay(20);
        }
        vTaskDelay(100);
    }
}
