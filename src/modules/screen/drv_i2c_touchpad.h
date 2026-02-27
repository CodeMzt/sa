/**
 * @file    drv_i2c_touchpad.h
 * @brief   FT5x06 I2C 触摸驱动
 * @author  Ma Ziteng(参考百问网的驱动编写)
 * @date    2026-01-27
 */

#ifndef DRV_I2C_TOUCHPAD_H_
#define DRV_I2C_TOUCHPAD_H_

#include "screen_interact.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------- */
/* Macro Defines                                                              */
/* -------------------------------------------------------------------------- */

/** @brief FT5x06 从设备地址(7-bit) */
#define FT5X06_I2C_ADDR         (0x38)

/** @brief 最多触控点数 */
#define TOUCH_MAX_POINTS        (5)

/** @brief 物理屏幕数据 */
#define LCD_WIDTH_PHYSICAL      (320)
#define LCD_HEIGHT_PHYSICAL     (480)

/* -------------------------------------------------------------------------- */
/* Typedef Enums                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief 旋转配置
 */
typedef enum {
    TOUCH_ROT_NONE = 0, /**< No rotation */
    TOUCH_ROT_90,       /**< Rotate 90 degrees clockwise */
    TOUCH_ROT_180,      /**< Rotate 180 degrees */
    TOUCH_ROT_270       /**< Rotate 270 degrees clockwise */
} touch_rotation_t;

/**
 * @brief 触摸事件
 */
typedef enum {
    TOUCH_EVENT_DOWN    = 0, /**< Finger put down */
    TOUCH_EVENT_LIFT    = 1, /**< Finger lifted */
    TOUCH_EVENT_CONTACT = 2, /**< Finger moving/contact */
    TOUCH_EVENT_NONE    = 3  /**< No event */
} touch_event_t;

/* -------------------------------------------------------------------------- */
/* Typedef Structs                                                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief 触点信息对象
 */
typedef struct {
    uint8_t       id;      /**<  ID (0-15) */
    uint16_t      x;       /**< X 坐标 */
    uint16_t      y;       /**< Y 坐标 */
    touch_event_t event;   /**< 事件类型 */
} touch_point_t;

/**
 * @brief 触摸对象
 * @note  包含了多触点的触点数以及各个触点对象
 */
typedef struct {
    uint8_t       count;                     /**< 触点数 */
    touch_point_t points[TOUCH_MAX_POINTS];  /**< 触点对象数组 */
} touch_monitor_t;

/* Forward declaration */
struct touch_dev_s;

/**
 * @brief 触控设备对象
 */
typedef struct touch_dev_s {
    char            *name;      /**< 设备名 */
    uint16_t        width;      /**< 宽度 */
    uint16_t        height;     /**< S宽度 */
    touch_rotation_t rotation;  /**< 旋转设置 */

    /**
     * @brief   初始化函数
     * @param   触控设备对象指针
     */
    void (*init)(struct touch_dev_s *dev);

    /**
     * @brief   获取触点函数
     * @param   触控设备对象指针
     * @param   存储触点信息地址
     * @retval  false   失败（i2c错误等）
     */
    bool (*read)(struct touch_dev_s *dev, touch_monitor_t *data);
} touch_dev_t;

/* -------------------------------------------------------------------------- */
/* Exported Functions                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief  获取ft5x06设备对象地址
 * @retval 设备对象指针
 */
touch_dev_t * touch_dev_get(void);

#endif /* DRV_I2C_TOUCHPAD_H_ */
