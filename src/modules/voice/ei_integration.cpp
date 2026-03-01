/*
 * ei_integration.cpp
 * 增加了滑动窗口平滑处理逻辑
 */

#include "ei_integration.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "voice_command.h"
#include <stdio.h>
#include <string.h> 
#include "sys_log.h"
#include "edge-impulse-sdk/CMSIS/DSP/Include/arm_math.h"

// 环形缓冲区总大小
#define AUDIO_BUFFER_SIZE  (16000)
// 每次推理的滑动步长 (250ms = 2000 samples @ 8kHz)
#define INFERENCE_STRIDE   (2000)

#define DECISION_WINDOW    (EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE)

// 环形缓冲区
static int16_t audio_buffer[AUDIO_BUFFER_SIZE];
static uint32_t buf_write_index = 0;
static bool buffer_full_enough = false;

// 存储每个类别的累加得分
static float g_score_accumulator[EI_CLASSIFIER_LABEL_COUNT] = {0};
// 记录在当前决策周期内运行了多少次推理
static int g_inference_count = 0;
// 记录当前决策周期已经走过了多少采样点
static uint32_t g_samples_since_decision = 0;


// 回调函数 (保持不变)
static int get_signal_data(size_t offset, size_t length, float *out_ptr) {
    int32_t read_start = (int32_t)buf_write_index - EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE + offset;
    if (read_start < 0) {
        read_start += AUDIO_BUFFER_SIZE;
    }
    size_t dist_to_end = AUDIO_BUFFER_SIZE - read_start;
    if (length <= dist_to_end) {
        arm_q15_to_float(&audio_buffer[read_start], out_ptr, length);
    } else {
        arm_q15_to_float(&audio_buffer[read_start], out_ptr, dist_to_end);
        size_t remaining = length - dist_to_end;
        arm_q15_to_float(&audio_buffer[0], &out_ptr[dist_to_end], remaining);
    }
    return EIDSP_OK;
}

void ei_model_init(void) {
    // 初始化时清空积分器
    memset(g_score_accumulator, 0, sizeof(g_score_accumulator));
    g_inference_count = 0;
    g_samples_since_decision = 0;
}

void ei_feed_audio(int16_t *data, uint32_t samples_count) {
    for (uint32_t i = 0; i < samples_count; i++) {
        audio_buffer[buf_write_index] = data[i];
        buf_write_index++;
        if (buf_write_index >= AUDIO_BUFFER_SIZE) {
            buf_write_index = 0;
            buffer_full_enough = true;
        }
    }
}

int ei_run_inference(void) {

    if (!buffer_full_enough && buf_write_index < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        return -1; // 数据不够
    }

    static uint32_t last_inference_idx = 0;

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    signal.get_data = &get_signal_data;

    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);

    if (res != EI_IMPULSE_OK) {
        return -1;
    }

    // --- 3. 【核心修改】平滑与积分逻辑 ---
    int final_idx = -1;
    float final_avg_score = 0.0f;
    // A. 将本次推理的各个类别得分累加
    for (size_t ix = 1; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if(result.classification[ix].value > final_avg_score){
            final_idx = ix;
            final_avg_score = result.classification[ix].value;
        }
    }
    return final_idx;

    return -1;
}

const char* ei_get_label_name(int class_idx) {
    if (class_idx >= 0 && class_idx < EI_CLASSIFIER_LABEL_COUNT) {
        return ei_classifier_inferencing_categories[class_idx];
    }
    return "Unknown";
}
