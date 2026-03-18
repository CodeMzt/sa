/**
 * @file drv_microphone.h
 * @brief INMP441 麦克风驱动头文件（接口定义与宏配置）
 * @date 2026-02-11
 * @author Ma Ziteng
 */

#ifndef DRV_MICROPHONE_H_
#define DRV_MICROPHONE_H_

#include "voice_command.h"
#include <stdint.h>

#define MIC_FRAME_SAMPLES  80
#define DECIMATION_FACTOR  1
#define BLOCK_SIZE         (MIC_FRAME_SAMPLES / DECIMATION_FACTOR)

/* 麦克风驱动结构体 */
typedef struct microphone_driver_ctrl {
    char        name[16];
    fsp_err_t   (*init)(void);
    int16_t     *(*get_readbuf)(void);
    void        (*callback)(void);
} microphone_driver_t;

extern microphone_driver_t mic_driver_instance;
extern SemaphoreHandle_t audio_semaphore;

fsp_err_t i2s0_start_rx(void);

#endif /* DRV_MICROPHONE_H_ */
