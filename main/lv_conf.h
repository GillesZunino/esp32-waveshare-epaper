// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// LVGL v9.x configuration for Waveshare 2.15" 4-color ePaper display
// -----------------------------------------------------------------------------------

/* clang-format off */
#if 1   /* Set to "1" to enable, "0" to ignore */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/* Color depth: 16 = RGB565 */
#define LV_COLOR_DEPTH 16

/*====================
   MEMORY SETTINGS
 *====================*/

/* LVGL internal heap size (bytes) */
#define LV_MEM_SIZE (48 * 1024U)

/*====================
   HAL SETTINGS
 *====================*/

/* Default DPI — e-paper 2.15" at ~170 DPI */
#define LV_DPI_DEF 170

/*====================
   OPERATING SYSTEM
 *====================*/

/* Thread safety is handled manually by the port via a FreeRTOS mutex.
   Set LV_USE_OS to none to keep LVGL single-threaded internally. */
#define LV_USE_OS LV_OS_NONE

/*====================
   TICK
 *====================*/

/* 0 = use lv_tick_inc() (called from our ESP timer). */
#define LV_TICK_CUSTOM 0

/*====================
   FONT USAGE
 *====================*/

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   LOGGING
 *====================*/

#define LV_USE_LOG      1
#define LV_LOG_LEVEL    LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF   1

/*====================
   WIDGETS
 *====================*/

#define LV_USE_LABEL    1
#define LV_USE_BUTTON   1
#define LV_USE_IMAGE    1
#define LV_USE_LINE     1
#define LV_USE_ARC      1
#define LV_USE_BAR      1
#define LV_USE_SLIDER   1

/*====================
   THEME
 *====================*/

#define LV_USE_THEME_DEFAULT 1

/* Disable animated state transitions - e-paper displays cannot render them meaningfully */
#define LV_THEME_DEFAULT_TRANSITION_TIME 0

/*====================
   DEMO
 *====================*/

/* Enable the widget demo for initial testing */
#define LV_USE_DEMO_WIDGETS 1

#endif /* LV_CONF_H */

#endif /* End of "Content enable" */
