/**
 * @file drv_touch.h
 * @brief 触觉模块驱动对外接口
 * @author Ma Ziteng
 */

#ifndef DRV_TOUCH_H_
#define DRV_TOUCH_H_

#include "servo_bus.h"

/**
 * @brief 触觉模块三轴数据结构体
 */
typedef struct
{
    int16_t fx;    /**< X 方向触觉数据 */
    int16_t fy;    /**< Y 方向触觉数据 */
    int16_t fz;    /**< Z 方向触觉数据 */
} g_touch_data_t;

/**
 * @brief 最近一次成功读取到的触觉数据
 */
extern g_touch_data_t g_touch_data_s;

/**
 * @brief 初始化触觉模块驱动
 * @retval FSP_SUCCESS 初始化成功
 * @retval 其他        初始化失败
 */
fsp_err_t touch_drv_init(void);
bool touch_drv_is_ready(void);

/**
 * @brief 读取一次触觉模块数据并刷新全局缓存
 * @retval FSP_SUCCESS      读取成功
 * @retval FSP_ERR_TIMEOUT  通信超时
 * @retval FSP_ERR_ABORTED  通信中止
 * @retval 其他             底层驱动错误
 */
fsp_err_t get_touch_data_process(void);

#endif /* DRV_TOUCH_H_ */
