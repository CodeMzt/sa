/**
 * @file    lv_port.c
 * @brief   LVGL 8.3 移植层实现文件
 * @author  Ma Ziteng(参考官方示例)
 * @date    2026-01-27
 */

#include "lv_port.h"
#include "lvgl/lvgl.h"
#include "drv_spi_display.h"
#include "drv_i2c_touchpad.h"

/* -------------------------------------------------------------------------- */
/* Private Defines                                                            */
/* -------------------------------------------------------------------------- */

/* 显存缓冲区大小 (1/10 屏幕面积) */
#define DISP_BUF_SIZE   (LCD_WIDTH * LCD_HEIGHT / 10)

/* -------------------------------------------------------------------------- */
/* Private Variables                                                          */
/* -------------------------------------------------------------------------- */

/* 定义两个静态显存缓冲区 */
static lv_color_t g_disp_buf_1[DISP_BUF_SIZE];
static lv_color_t g_disp_buf_2[DISP_BUF_SIZE];

/* -------------------------------------------------------------------------- */
/* Private Function Prototypes                                                */
/* -------------------------------------------------------------------------- */

/* 回调函数签名 */
static void disp_flush_cb(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
static void touchpad_read_cb(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);



/**
 * @brief   LVGL 核心初始化与驱动注册入口
 */
void lv_port_init(void)
{
    lv_init();

    lcd_spi_init();

    touch_dev_t * tp = touch_dev_get();
    if (tp && tp->init) {
        tp->init(tp);
    }

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, g_disp_buf_1, g_disp_buf_2, DISP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;

    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);

    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read_cb;

    lv_indev_drv_register(&indev_drv);
}

/**
 * @brief   LVGL 显示刷新回调函数
 * @param   disp_drv 显示驱动句柄
 * @param   area     待刷新区域
 * @param   color_p  像素数据指针 (lv_color_t*)
 */
static void disp_flush_cb(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    lcd_spi_set_window(area->x1, area->y1, area->x2, area->y2);

    uint32_t w = (uint32_t)lv_area_get_width(area);
    uint32_t h = (uint32_t)lv_area_get_height(area);
    uint32_t data_len = w * h * 2;

    lcd_spi_flush_data((uint8_t *)color_p, data_len);

    vTaskDelay(10);

    lv_disp_flush_ready(disp_drv);
}

/**
 * @brief   LVGL        输入读取回调函数
 * @param   indev_drv   输入驱动句柄
 * @param   data        数据结构体
 */
static void touchpad_read_cb(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    (void)indev_drv;

    touch_dev_t * tp = touch_dev_get();
    if (tp == NULL || tp->read == NULL) {
        return;
    }

    touch_monitor_t touch_data;
    bool is_valid = tp->read(tp, &touch_data);

    if (is_valid && (touch_data.count > 0)) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touch_data.points[0].x;
        data->point.y = touch_data.points[0].y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}
