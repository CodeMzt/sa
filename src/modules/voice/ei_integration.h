/*
 * ei_integration.h
 *
 *  Created on: 2026年2月5日
 *      Author: Ma Ziteng
 */

#ifndef EI_INTEGRATION_H_
#define EI_INTEGRATION_H_

#include <stdint.h>
#include <stdbool.h>



#ifdef __cplusplus
extern "C" {
#endif

// 初始化模型
void ei_model_init(void);

// 将新的音频数据填入环形缓冲区 (在 I2S/定时器中断里调用)
void ei_feed_audio(int16_t *data, uint32_t samples_count);

// 运行推理
int ei_run_inference(void);

// 获取类别名称 (用于打印)
const char* ei_get_label_name(int class_idx);



#ifdef __cplusplus
}
#endif

#endif /* EI_INTEGRATION_H_ */
