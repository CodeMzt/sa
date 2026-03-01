#include "voice_command.h"
#include "drv_microphone.h"
#include "sys_log.h"
#include "log_task.h"
#include "ei_integration.h"
#include "shared_data.h"
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

#define VOTE_WINDOW_SIZE    10
#define VOTE_FORCEPS        36
#define VOTE_HEMOSTAT       36
#define VOTE_SCALPEL        36

static uint8_t g_vote_history[VOTE_WINDOW_SIZE];
static uint8_t g_vote_index = 0;
static uint8_t g_vote_count = 0;

/**
 * @brief 窗口投票处理
 * @param inference_result 推理结果
 * @return 最终投票结果
 */
static int process_vote_window(int inference_result) {
    int final_result = -1;

    if (inference_result >= 0 && inference_result <= 4) {
        if (g_vote_count < VOTE_WINDOW_SIZE) {
            g_vote_count++;
        }

        g_vote_history[g_vote_index] = inference_result;
        g_vote_index = (g_vote_index + 1) % VOTE_WINDOW_SIZE;

        uint8_t counts[5] = {0};
        uint8_t consecutive = 0;
        uint8_t last_class = 0xFF;

        for (uint8_t i = 0; i < g_vote_count; i++) {
            uint8_t idx = (g_vote_index - g_vote_count + i + VOTE_WINDOW_SIZE) % VOTE_WINDOW_SIZE;
            uint8_t curr_class = g_vote_history[idx];

            if (curr_class == last_class) {
                consecutive++;
            } else {
                consecutive = 1;
                last_class = curr_class;
            }
            counts[curr_class] += consecutive;
        }

        LOG_D("Vote: class=%d, counts=[%d,%d,%d,%d,%d], window=%d/%d",
               inference_result,
               counts[0], counts[1], counts[2], counts[3], counts[4],
               g_vote_count, VOTE_WINDOW_SIZE);

        if (g_vote_count >= VOTE_WINDOW_SIZE) {
            if (counts[1] >= VOTE_FORCEPS) {
                final_result = 1;
            } else if (counts[2] >= VOTE_HEMOSTAT) {
                final_result = 2;
            } else if (counts[3] >= VOTE_SCALPEL) {
                final_result = 3;
            }
        }
    }

    return final_result;
}

void voice_command_entry(void *pvParameters) {
    FSP_PARAMETER_NOT_USED(pvParameters);
    vTaskDelay(pdMS_TO_TICKS(1000));
    fsp_err_t err = mic_driver_instance.init();
    if (err != FSP_SUCCESS) {
        LOG_E("Mic Init Failed");
        return;
    }
    ei_model_init();
    LOG_D("Voice Command Thread Start.");
    g_sys_status.is_mic_connected = true;
    R_BSP_IrqDisable(g_i2s0_cfg.rxi_irq);
    while(1) {
        while(g_sys_status.is_voice_command_running) {
            int ret = ei_run_inference();
            if(ret != -1) {
                int final_result = process_vote_window(ret);
                if(final_result >= 1 && final_result <= 3) {
                    LOG_D("Add instrument %d to queue.", get_instrument_name(final_result));
                    add_instrument_to_queue(final_result);
                    update_queue_display_string();
                }
            }
            vTaskDelay(5);
        }
        vTaskDelay(100);
    }
}
