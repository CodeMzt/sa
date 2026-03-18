/**
 * @file drv_microphone.c
 * @brief INMP441 麦克风驱动实现（I2S + DTC 接收，音频增益与处理）
 * @date 2026-02-11
 * @author Ma Ziteng
 */

#include "drv_microphone.h"
#include "ei_integration.h"

#define MIC_GAIN            16
#define MIC_RAW_BUFFER_SIZE (MIC_FRAME_SAMPLES * 2)

static int32_t audio_rx_buffer[2][MIC_RAW_BUFFER_SIZE];
static int16_t audio_read_buffer[2][BLOCK_SIZE];

static volatile uint8_t dtc_buffer_index = 0;
static volatile uint8_t read_buffer_index = 1;

static fsp_err_t inmp441_init(void);
static void signal_audio_ready(void);
static int16_t *inmp441_get_readbuf(void);

microphone_driver_t mic_driver_instance = {
    .name        = "inmp441",
    .init        = inmp441_init,
    .get_readbuf = inmp441_get_readbuf,
    .callback    = signal_audio_ready
};

/**
 * @brief 初始化INMP441麦克风模块 - I2S初始化
 * @return FSP_SUCCESS 表示成功
 */
static fsp_err_t inmp441_init(void) {
    fsp_err_t err = FSP_SUCCESS;

    err = g_timer_audio_clk.p_api->open(g_timer_audio_clk.p_ctrl, g_timer_audio_clk.p_cfg);
    if (err != FSP_SUCCESS) return err;

    err = g_timer_audio_clk.p_api->start(g_timer_audio_clk.p_ctrl);
    if (err != FSP_SUCCESS) return err;

    err = g_i2s0.p_api->open(g_i2s0.p_ctrl, g_i2s0.p_cfg);
    if (err != FSP_SUCCESS) return err;

    xSemaphoreGive(g_uart_tx_sem);

    return FSP_SUCCESS;
}

/**
 * @brief 提供可读取音频数据缓存数组指针
 * @return 音频数据缓冲区指针
 */
static int16_t *inmp441_get_readbuf(void) {
    return audio_read_buffer[read_buffer_index];
}

/**
 * @brief 数据处理，包括增益以及带通滤波
 */
static void signal_audio_ready(void) {
    for (uint16_t i = 0; i < BLOCK_SIZE; i++) {
        int32_t audio_data = (audio_rx_buffer[read_buffer_index][2 * i * DECIMATION_FACTOR + 1] >> 16) +
                             (audio_rx_buffer[read_buffer_index][2 * i * DECIMATION_FACTOR] >> 16);
        audio_data = __SSAT(audio_data * MIC_GAIN, 16);
        audio_read_buffer[read_buffer_index][i] = (int16_t)audio_data;
    }
#ifndef TEST
    ei_feed_audio(audio_read_buffer[read_buffer_index], BLOCK_SIZE);
#endif
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(audio_semaphore, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
 * @brief 启动I2S的DTC接收数据
 * @return FSP_SUCCESS 表示成功
 */
fsp_err_t i2s0_start_rx(void) {
    return g_i2s0.p_api->read(g_i2s0.p_ctrl, audio_rx_buffer[dtc_buffer_index], MIC_RAW_BUFFER_SIZE * 4);
}

/**
 * @brief I2S（实则是DTC）中断回调函数
 * @param p_args I2S 事件参数
 */
void i2s0_callback(i2s_callback_args_t *p_args) {   
    if (p_args->event == I2S_EVENT_RX_FULL) {
        read_buffer_index = dtc_buffer_index;
        dtc_buffer_index ^= 1;
        g_i2s0.p_api->read(g_i2s0.p_ctrl, audio_rx_buffer[dtc_buffer_index], MIC_RAW_BUFFER_SIZE * 4);
        mic_driver_instance.callback();
    }
}

