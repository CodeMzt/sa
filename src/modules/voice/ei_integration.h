/**
 * @file ei_integration.h
 * @brief Edge Impulse 模型推理集成头文件（接口定义）
 * @date 2026-02-11
 * @author Ma Ziteng
 */

#ifndef EI_INTEGRATION_H_
#define EI_INTEGRATION_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化模型
 */
void ei_model_init(void);

/**
 * @brief 将新的音频数据填入环形缓冲区
 * @param data 音频数据指针
 * @param samples_count 采样点数
 */
void ei_feed_audio(int16_t *data, uint32_t samples_count);

/**
 * @brief 运行推理
 * @return 推理结果索引
 */
int ei_run_inference(void);

/**
 * @brief 获取类别名称
 * @param class_idx 类别索引
 * @return 类别名称字符串
 */
const char* ei_get_label_name(int class_idx);

#ifdef __cplusplus
}
#endif

#endif /* EI_INTEGRATION_H_ */
