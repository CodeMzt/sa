/**
 * @file    drv_i2c_touchpad.c
 * @brief   FT5x06 I2C 触摸驱动实现
 * @date    2026-01-27
 * @author  Ma Ziteng
 */

#include "drv_i2c_touchpad.h"
#include "sys_log.h"
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------- */
/* Private Defines                                                            */
/* -------------------------------------------------------------------------- */

#define TOUCH_I2C_INSTANCE     g_i2c_master2      /**< FSP I2C Instance */
#define TOUCH_RESET_PIN        BSP_IO_PORT_04_PIN_03
#define TOUCH_INT_PIN          BSP_IO_PORT_04_PIN_08

#define REG_FT5X06_TD_STATUS   (0x02)             /**< Touch Status Register */
#define REG_FT5X06_TOUCH1_XH   (0x03)             /**< Start address of Point 1 */
#define BYTES_PER_POINT        (6)                /**< Bytes occupied by one touch point */

/* -------------------------------------------------------------------------- */
/* Private Variables                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief 全局变量，维护发送、接收进度，起信号量作用
 */
static volatile bool g_i2c_tx_cplt = false;
static volatile bool g_i2c_rx_cplt = false;

/* Private function prototypes */
static void ft5x06_init(touch_dev_t *dev);
static bool ft5x06_read(touch_dev_t *dev, touch_monitor_t *data);

/**
 * @brief 注册设备对象
 */
static touch_dev_t g_touch_dev = {
    .name     = "ft5x06",
    .rotation = TOUCH_ROT_NONE, /* Default rotation, modify as needed */
    .init     = ft5x06_init,
    .read     = ft5x06_read
};

/* -------------------------------------------------------------------------- */
/* I2C Helper Functions                                                       */
/* -------------------------------------------------------------------------- */


void i2c_master2_callback(i2c_master_callback_args_t * p_args)
{
    if (I2C_MASTER_EVENT_TX_COMPLETE == p_args->event) g_i2c_tx_cplt = true;
    else if (I2C_MASTER_EVENT_RX_COMPLETE == p_args->event) g_i2c_rx_cplt = true;
    else {
        /* Error handling: force flags to avoid deadlock */
        g_i2c_tx_cplt = true;
        g_i2c_rx_cplt = true;
    }
}

/**
 * @brief  等待发送完成直至超时
 * @retval FSP_SUCCESS      完成
 * @retval FSP_ERR_TIMEOUT  超时
 */
static fsp_err_t wait_tx_complete(void)
{
    uint32_t timeout = 100; /* 100ms timeout */

    while (!g_i2c_tx_cplt && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout--;
    }

    if (timeout == 0) return FSP_ERR_TIMEOUT;

    g_i2c_tx_cplt = false;
    return FSP_SUCCESS;
}

/**
 * @brief  等待接收完成直至超时
 * @retval FSP_SUCCESS      完成
 * @retval FSP_ERR_TIMEOUT  超时
 */
static fsp_err_t wait_rx_complete(void)
{
    uint32_t timeout = 100;

    while (!g_i2c_rx_cplt && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout--;
    }

    if (timeout == 0) return FSP_ERR_TIMEOUT;

    g_i2c_rx_cplt = false;
    return FSP_SUCCESS;
}

/**
 * @brief  I2C指定地址写
 * @param  reg      寄存器地址
 * @param  buf      写缓冲区
 * @param  len      数据长度
 */
static void write_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t tmp_buf[32];

    if (len > 30) return;

    tmp_buf[0] = reg;
    memcpy(&tmp_buf[1], buf, len);

    fsp_err_t err = TOUCH_I2C_INSTANCE.p_api->write(TOUCH_I2C_INSTANCE.p_ctrl, tmp_buf, len + 1, false);
    if (FSP_SUCCESS != err) {
        LOG_E("i2c write failed: %d", err);
        return;
    }
    wait_tx_complete();
}

/**
 * @brief  I2C指定地址读
 * @param  reg      寄存器地址
 * @param  buf      读缓冲区
 * @param  len      数据长度
 */
static void read_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    fsp_err_t err = TOUCH_I2C_INSTANCE.p_api->write(TOUCH_I2C_INSTANCE.p_ctrl, &reg, 1, true); /* Restart condition */
    if (FSP_SUCCESS != err) return;
    wait_tx_complete();

    err = TOUCH_I2C_INSTANCE.p_api->read(TOUCH_I2C_INSTANCE.p_ctrl, buf, len, false);
    if (FSP_SUCCESS != err) return;
    wait_rx_complete();
}

/* -------------------------------------------------------------------------- */
/* Driver Implementation                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief   FT5x06 设备初始化函数
 * @param   dev     设备对象
 */
static void ft5x06_init(touch_dev_t *dev)
{
    (void)dev;

    g_ioport.p_api->pinWrite(g_ioport.p_ctrl, TOUCH_RESET_PIN, BSP_IO_LEVEL_LOW);
    vTaskDelay(pdMS_TO_TICKS(20));

    g_ioport.p_api->pinWrite(g_ioport.p_ctrl, TOUCH_RESET_PIN, BSP_IO_LEVEL_HIGH);
    vTaskDelay(pdMS_TO_TICKS(200));

    g_ioport.p_api->pinCfg(g_ioport.p_ctrl, TOUCH_INT_PIN, IOPORT_CFG_PORT_DIRECTION_INPUT);

    fsp_err_t err = TOUCH_I2C_INSTANCE.p_api->open(TOUCH_I2C_INSTANCE.p_ctrl, TOUCH_I2C_INSTANCE.p_cfg);
    if (FSP_SUCCESS != err && FSP_ERR_ALREADY_OPEN != err) {
        LOG_E("touch i2c open failed");
        return;
    }

    uint8_t id = 0;
    read_reg(0xA3, &id, 1);
    LOG_I("ft5x06 chip id: 0x%02x", id);

    uint8_t mode = 0;
    write_reg(0x00, &mode, 1);
}

/**
 * @brief  应用旋转设置
 * @param  dev      设备对象
 * @param  raw_x    原始x
 * @param  raw_y    原始y
 * @param  out_x    变换后的x
 * @param  out_y    变换后的y
 */
static void apply_rotation(touch_dev_t *dev, uint16_t raw_x, uint16_t raw_y, uint16_t *out_x, uint16_t *out_y)
{
    uint16_t temp;
    switch (dev->rotation)
    {
        case TOUCH_ROT_NONE:
            *out_x = raw_x;
            *out_y = raw_y;
            break;

        case TOUCH_ROT_90:
            /* Portrait to Landscape (CW 90) */
            temp   = raw_x;
            *out_x = raw_y;
            *out_y = LCD_WIDTH_PHYSICAL - temp;
            break;

        case TOUCH_ROT_180:
            *out_x = LCD_WIDTH_PHYSICAL - raw_x;
            *out_y = LCD_HEIGHT_PHYSICAL - raw_y;
            break;

        case TOUCH_ROT_270:
            temp   = raw_x;
            *out_x = LCD_HEIGHT_PHYSICAL - raw_y;
            *out_y = temp;
            break;

        default:
            *out_x = raw_x;
            *out_y = raw_y;
            break;
    }
}

/**
 * @brief  获取触控信息
 * @param  dev      设备对象
 * @param  data     触控信息
 * @retval true     成功
 * @retval false    错误
 */
static bool ft5x06_read(touch_dev_t *dev, touch_monitor_t *data)
{
    uint8_t reg_val = 0;

    read_reg(REG_FT5X06_TD_STATUS, &reg_val, 1);

    uint8_t point_count = reg_val & 0x0F;
    if (point_count > TOUCH_MAX_POINTS) point_count = TOUCH_MAX_POINTS;

    data->count = point_count;

    if (point_count == 0) return true;

    uint8_t buf[TOUCH_MAX_POINTS * BYTES_PER_POINT];
    read_reg(REG_FT5X06_TOUCH1_XH, buf, point_count * BYTES_PER_POINT);

    for (int i = 0; i < point_count; i++) {
        uint8_t *p_buf = &buf[i * BYTES_PER_POINT];

        uint16_t raw_x = (uint16_t)(((p_buf[0] & 0x0F) << 8) | p_buf[1]);
        uint16_t raw_y = (uint16_t)(((p_buf[2] & 0x0F) << 8) | p_buf[3]);

        data->points[i].event = (touch_event_t)((p_buf[0] & 0xC0) >> 6);
        data->points[i].id    = (p_buf[2] & 0xF0) >> 4;

        apply_rotation(dev, raw_x, raw_y, &data->points[i].x, &data->points[i].y);
    }

    return true;
}

/**
 * @brief  获取ft5x06设备对象地址
 * @retval 设备对象指针
 */
touch_dev_t * touch_dev_get(void)
{
    return &g_touch_dev;
}
