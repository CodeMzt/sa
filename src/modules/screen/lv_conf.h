/**
 * @file lv_conf.h
 * Configuration file for v8.3.x (RA6M5 + ST7796S)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
/* Color depth: 16 (RGB565) */
#define LV_COLOR_DEPTH 16

/* Swap the 2 bytes of RGB565 color. Useful if red and blue are swapped. */
/* 我们在驱动层如果不做交换，这里可能需要设为 1 */
#define LV_COLOR_16_SWAP 1

/*====================
   MEMORY SETTINGS
 *====================*/
/* 1: use custom malloc/free, 0: use the built-in `lv_mem_alloc` */
#define LV_MEM_CUSTOM 0

/* Size of the memory available for `lv_mem_alloc` in bytes (64KB) */
#define LV_MEM_SIZE (32 * 1024U)

/*====================
   HAL SETTINGS
 *====================*/
/* Default display refresh period. LVG will redraw changed areas with this period time */
#define LV_DISP_DEF_REFR_PERIOD 50      /*[ms]*/

/* Input device read period in milliseconds */
#define LV_INDEV_DEF_READ_PERIOD 50     /*[ms]*/

/* Use a custom tick source that tells the elapsed time in milliseconds. */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE "FreeRTOS.h"
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR (xTaskGetTickCount() * portTICK_PERIOD_MS)
#endif

/*====================
   FEATURE CONFIGURATION
 *====================*/

/*-------------
 * Drawing
 *-----------*/
/* Enable complex draw engine: rounded corners, shadows, anti-aliasing */
#define LV_DRAW_COMPLEX 0

/*-------------
 * Logging
 *-----------*/
/* Enable the log module */
#define LV_USE_LOG 1
#if LV_USE_LOG
    /* How important log should be added:
     * LV_LOG_LEVEL_TRACE       A lot of logs to give detailed information
     * LV_LOG_LEVEL_INFO        Log important events
     * LV_LOG_LEVEL_WARN        Log if something unwanted happened but didn't cause a problem
     * LV_LOG_LEVEL_ERROR       Only critical issue, when the system may fail
     * LV_LOG_LEVEL_USER        Only logs added by the user
     * LV_LOG_LEVEL_NONE        Do not log anything */
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

    /* 1: Print the log with 'printf'; 0: User need to register a callback */
    #define LV_LOG_PRINTF 0
#endif

/*-------------
 * Others
 *-----------*/
/* 1: Show CPU usage and FPS count */
#define LV_USE_PERF_MONITOR 0

/* 1: Show the used memory and the memory fragmentation */
#define LV_USE_MEM_MONITOR 0

/*====================
   FONT USAGE
 *====================*/
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1 /* Default */
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/* Always set a default font */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   THEME USAGE
 *====================*/
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 0
    #define LV_THEME_DEFAULT_GROW 1
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif

#define LV_USE_THEME_BASIC 1

/*====================
   TEXT SETTINGS
 *====================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_)]}"

/*====================
   WIDGETS
 *====================*/
#define LV_USE_ARC          0
#define LV_USE_BAR          1
#define LV_USE_BTN          1
#define LV_USE_BTNMATRIX    1
#define LV_USE_CANVAS       1
#define LV_USE_CHECKBOX     1
#define LV_USE_DROPDOWN     1
#define LV_USE_IMG          1
#define LV_USE_LABEL        1
#define LV_USE_LINE         1
#define LV_USE_ROLLER       1
#define LV_USE_SLIDER       1
#define LV_USE_SWITCH       1
#define LV_USE_TEXTAREA     1
#define LV_USE_TABLE        1

/*====================
   EXTRA WIDGETS
 *====================*/
#define LV_USE_ANIMIMG      1
#define LV_USE_CALENDAR     1
#define LV_USE_CHART        1
#define LV_USE_COLORWHEEL   1
#define LV_USE_IMGBTN       1
#define LV_USE_KEYBOARD     1
#define LV_USE_LED          1
#define LV_USE_LIST         1
#define LV_USE_MENU         1
#define LV_USE_METER        0
#define LV_USE_MSGBOX       1
#define LV_USE_SPAN         1
#define LV_USE_SPINBOX      1
#define LV_USE_SPINNER      0
#define LV_USE_TABVIEW      1
#define LV_USE_TILEVIEW     1
#define LV_USE_WIN          1

/*====================
   LAYOUTS
 *====================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*====================
   3RD PARTY LIBRARIES
 *====================*/
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0

#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_SJPG 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_RLOTTIE 0
#define LV_USE_FFMPEG 0

/*====================
   OTHERS
 *====================*/
#define LV_USE_SNAPSHOT 0
#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT 0
#define LV_USE_MSG 0
#define LV_USE_IME_PINYIN 0

#endif /*LV_CONF_H*/
