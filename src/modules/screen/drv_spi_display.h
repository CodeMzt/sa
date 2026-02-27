/**
 * @file    drv_spi_display.h
 * @brief   ST7796S SPI LCD 屏幕驱动
 * @author  Ma Ziteng(参考百问网的驱动编写)
 * @date    2026-01-27
 */

#ifndef DRV_SPI_DISPLAY_H_
#define DRV_SPI_DISPLAY_H_

#include "screen_interact.h"

/* -------------------------------------------------------------------------- */
/* Macro Defines                                                              */
/* -------------------------------------------------------------------------- */

/** @brief LCD 物理尺寸 */
#define LCD_WIDTH       (320)
#define LCD_HEIGHT      (480)

/** @brief 基本颜色定义(RGB565) */
#define LCD_COLOR_BLACK (0x0000)
#define LCD_COLOR_WHITE (0xFFFF)
#define LCD_COLOR_RED   (0xF800)
#define LCD_COLOR_GREEN (0x07E0)
#define LCD_COLOR_BLUE  (0x001F)

/* -------------------------------------------------------------------------- */
/* Exported Functions                                                         */
/* -------------------------------------------------------------------------- */

fsp_err_t lcd_spi_init(void);

void lcd_spi_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

fsp_err_t lcd_spi_flush_data(uint8_t *data, uint32_t len);

#endif /* DRV_SPI_DISPLAY_H_ */
