/**
 * @file    lv_port.h
 * @brief   LVGL 8.3 移植层头文件（初始化接口）
 * @date    2026-01-27
 * @author  Ma Ziteng
 */

#ifndef LV_PORT_H_
#define LV_PORT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* 函数声明                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief   初始化 LVGL 及其底层硬件驱动
 * @details 包含以下步骤：
 * 1. 初始化 LVGL 核心库 (lv_init)
 * 2. 初始化 LCD 和 触摸屏硬件
 * 3. 注册显示驱动 (Display Driver)
 * 4. 注册输入设备驱动 (Input Device Driver)
 * @note    必须在 RTOS 任务中调用（因为硬件初始化包含延时）
 */
void lv_port_init(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV_PORT_ENTRY_H_ */
