/**
 * @file    drv_spi_display.c
 * @brief   ST7796S SPI LCD 屏幕驱动 (RTOS 信号量优化版)
 * @author  Ma Ziteng(参考百问网的驱动编写)
 * @date    2026-01-27
 */

#include "drv_spi_display.h"
#include <stdio.h>

/* FreeRTOS Includes */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* -------------------------------------------------------------------------- */
/* Private Defines                                                            */
/* -------------------------------------------------------------------------- */

/* 引脚定义 */
#define PIN_LCD_DC              BSP_IO_PORT_01_PIN_04   /**< 数据/命令 选择引脚 */
#define PIN_LCD_RESET           BSP_IO_PORT_01_PIN_05   /**< 复位引脚 */
#define PIN_LCD_BACKLIGHT       BSP_IO_PORT_06_PIN_08   /**< 背光控制引脚 */

#define LCD_SPI_INSTANCE        g_spi1                  /**< spi句柄 */

/* ST7796S 命令 */
#define CMD_CASET               (0x2A)                  /**< 列地址设置 */
#define CMD_RASET               (0x2B)                  /**< 行地址设置 */
#define CMD_RAMWR               (0x2C)                  /**< 写显存 */

/* -------------------------------------------------------------------------- */
/* Private Variables                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief SPI 完成信号量
 * @note  用于替代 volatile flag，实现阻塞等待，降低 CPU 占用
 */
static SemaphoreHandle_t g_spi_sem = NULL;

/* -------------------------------------------------------------------------- */
/* Private Function Prototypes                                                */
/* -------------------------------------------------------------------------- */

static void lcd_hw_reset(void);
static void lcd_backlight_on(void);
static void write_cmd(uint8_t cmd);
static void write_data(uint8_t data);
static void write_buffer(uint8_t *data, uint32_t len);
static fsp_err_t wait_transfer_complete(void);

/* -------------------------------------------------------------------------- */
/* Callback Function                                                          */
/* -------------------------------------------------------------------------- */

void spi1_callback(spi_callback_args_t *p_args)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if ((SPI_EVENT_TRANSFER_COMPLETE == p_args->event) ||
        (SPI_EVENT_TRANSFER_ABORTED == p_args->event) ||
        (SPI_EVENT_ERR_MODE_FAULT == p_args->event))
    {
        if (g_spi_sem != NULL) {
            xSemaphoreGiveFromISR(g_spi_sem, &xHigherPriorityTaskWoken);
        }
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* -------------------------------------------------------------------------- */
/* Exported Functions                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief   SPI 外设初始化以及芯片初始化设置
 * @note    需要在线程中调用（RTOS）
 * @retval  FSP_SUCCESS         成功
 * @retval  FSP_ERR_ASSERTION   错误
 * @retval  FSP_ERR_OUT_OF_MEMORY 信号量创建失败
 */
fsp_err_t lcd_spi_init(void)
{
    fsp_err_t err;

    if (g_spi_sem == NULL) {
        g_spi_sem = xSemaphoreCreateBinary();
        if (g_spi_sem == NULL) {
            return FSP_ERR_OUT_OF_MEMORY;
        }
    }

    err = LCD_SPI_INSTANCE.p_api->open(&g_spi1_ctrl, &g_spi1_cfg);
    if (FSP_SUCCESS != err && FSP_ERR_ALREADY_OPEN != err)
    {
        return err;
    }

    lcd_hw_reset();
    lcd_backlight_on();

    write_cmd(0xE0);
    write_data(0xF0); write_data(0x3E); write_data(0x30); write_data(0x06);
    write_data(0x0A); write_data(0x03); write_data(0x4D); write_data(0x56);
    write_data(0x3A); write_data(0x06); write_data(0x0F); write_data(0x04);
    write_data(0x18); write_data(0x13); write_data(0x00);

    write_cmd(0xE1);
    write_data(0x0F); write_data(0x37); write_data(0x31); write_data(0x0B);
    write_data(0x0D); write_data(0x06); write_data(0x4D); write_data(0x34);
    write_data(0x38); write_data(0x06); write_data(0x11); write_data(0x01);
    write_data(0x18); write_data(0x13); write_data(0x00);

    write_cmd(0xC0); write_data(0x18); write_data(0x17);
    write_cmd(0xC1); write_data(0x41);
    write_cmd(0xC5); write_data(0x00);
    write_cmd(0x1A); write_data(0x80);

    write_cmd(0x36); write_data(0x48);
    write_cmd(0x3A); write_data(0x55);
    write_cmd(0xB0); write_data(0x00);
    write_cmd(0xB1); write_data(0xA0);
    write_cmd(0xB4); write_data(0x02);
    write_cmd(0xB6); write_data(0x02); write_data(0x02);
    write_cmd(0xE9); write_data(0x00);
    write_cmd(0xF7);
    write_data(0xA9); write_data(0x51); write_data(0x2C); write_data(0x82);

    write_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));

    write_cmd(0x21);
    write_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_spi_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

    uint8_t line_buf[320 * 2] = {0};
    for (int y = 0; y < LCD_HEIGHT; y++) {
        lcd_spi_flush_data(line_buf, sizeof(line_buf));
    }

    return FSP_SUCCESS;
}

/**
 * @brief   设置有效窗口
 */
void lcd_spi_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t data[4];

    write_cmd(CMD_CASET);
    data[0] = (uint8_t)(x1 >> 8) & 0xFF;
    data[1] = (uint8_t)(x1 & 0xFF);
    data[2] = (uint8_t)(x2 >> 8) & 0xFF;
    data[3] = (uint8_t)(x2 & 0xFF);
    write_buffer(data, 4);

    write_cmd(CMD_RASET);
    data[0] = (uint8_t)(y1 >> 8) & 0xFF;
    data[1] = (uint8_t)(y1 & 0xFF);
    data[2] = (uint8_t)(y2 >> 8) & 0xFF;
    data[3] = (uint8_t)(y2 & 0xFF);
    write_buffer(data, 4);

    write_cmd(CMD_RAMWR);
}

/**
 * @brief   刷新像素信息
 * @param   data    像素数据缓存
 * @param   len     像素数据长度
 * @retval  FSP_SUCCESS         成功
 * @retval  FSP_ERR_TIMEOUT     SPI超时
 */
fsp_err_t lcd_spi_flush_data(uint8_t *data, uint32_t len)
{
    g_ioport.p_api->pinWrite(g_ioport.p_ctrl, PIN_LCD_DC, BSP_IO_LEVEL_HIGH);

    fsp_err_t err = LCD_SPI_INSTANCE.p_api->write(LCD_SPI_INSTANCE.p_ctrl, data, len, SPI_BIT_WIDTH_8_BITS);
    if (FSP_SUCCESS != err) return err;

    return wait_transfer_complete();
}

/* -------------------------------------------------------------------------- */
/* Private Helper Functions                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief   等待SPI发送完毕 (使用信号量阻塞)
 * @retval  FSP_SUCCESS      发送完成
 * @retval  FSP_ERR_TIMEOUT  超时
 */
static fsp_err_t wait_transfer_complete(void)
{
    if (xSemaphoreTake(g_spi_sem, pdMS_TO_TICKS(200)) == pdTRUE)
    {
        return FSP_SUCCESS;
    }

    return FSP_ERR_TIMEOUT;
}

/**
 * @brief   lcd屏幕硬件复位
 */
static void lcd_hw_reset(void)
{
    g_ioport.p_api->pinWrite(g_ioport.p_ctrl, PIN_LCD_RESET, BSP_IO_LEVEL_LOW);
    vTaskDelay(pdMS_TO_TICKS(120));
    g_ioport.p_api->pinWrite(g_ioport.p_ctrl, PIN_LCD_RESET, BSP_IO_LEVEL_HIGH);
    vTaskDelay(pdMS_TO_TICKS(120));
}

/**
 * @brief   开启LCD背光
 */
static void lcd_backlight_on(void)
{
    g_ioport.p_api->pinWrite(g_ioport.p_ctrl, PIN_LCD_BACKLIGHT, BSP_IO_LEVEL_HIGH);
}

/**
 * @brief   写命令
 * @param   cmd     命令字节
 */
static void write_cmd(uint8_t cmd)
{
    g_ioport.p_api->pinWrite(g_ioport.p_ctrl, PIN_LCD_DC, BSP_IO_LEVEL_LOW);

    LCD_SPI_INSTANCE.p_api->write(LCD_SPI_INSTANCE.p_ctrl, &cmd, 1, SPI_BIT_WIDTH_8_BITS);
    wait_transfer_complete();
}

/**
 * @brief   写数据
 * @param   data    数据字节
 */
static void write_data(uint8_t data)
{
    g_ioport.p_api->pinWrite(g_ioport.p_ctrl, PIN_LCD_DC, BSP_IO_LEVEL_HIGH);

    LCD_SPI_INSTANCE.p_api->write(LCD_SPI_INSTANCE.p_ctrl, &data, 1, SPI_BIT_WIDTH_8_BITS);
    wait_transfer_complete();
}

/**
 * @brief   写一组数据
 * @param   data    数据缓存指针
 * @param   len     数据长度
 */
static void write_buffer(uint8_t *data, uint32_t len)
{
    g_ioport.p_api->pinWrite(g_ioport.p_ctrl, PIN_LCD_DC, BSP_IO_LEVEL_HIGH);

    LCD_SPI_INSTANCE.p_api->write(LCD_SPI_INSTANCE.p_ctrl, data, len, SPI_BIT_WIDTH_8_BITS);
    wait_transfer_complete();
}
