#ifndef LV_CONF_H
#define LV_CONF_H

// =====================================================================
//  lv_conf.h -- ค่าตั้งของ LVGL 8.3 (ค่าที่ไม่ได้ระบุใช้ค่าเริ่มต้นของ LVGL)
// =====================================================================

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1  // LVGL สลับ byte ให้ตอนวาด จึงส่งเข้า DMA ได้ตรงๆ ใน flush (main.cpp)

// ใช้ heap ปกติของ ESP32 แทน pool คงที่ (pool เล็กจะหมดแล้วค้างเงียบๆ)
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc

// LVGL นับเวลาจาก millis() เอง ไม่ต้องเรียก lv_tick_inc()
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DISP_DEF_REFR_PERIOD 16  // ms ต่อเฟรม
#define LV_INDEV_DEF_READ_PERIOD 10 // อ่านจอสัมผัสทุก 10 ms
#define LV_DPI_DEF 130

#define LV_DRAW_COMPLEX 1   // ต้องเปิด: มุมโค้ง / gradient / arc
#define LV_SHADOW_CACHE_SIZE 0
#define LV_USE_USER_DATA 1

#define LV_TXT_ENC LV_TXT_ENC_UTF8  // ข้อความภาษาไทยในโค้ดเป็น UTF-8

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

// ---- ฟอนต์ ----
// font_th_16 / font_th_24 = Chakra Petch (ASCII + ไทย + °) + สัญลักษณ์ LV_SYMBOL_* (สร้างด้วย tools/gen_lvgl_fonts.ps1)
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(font_th_16) LV_FONT_DECLARE(font_th_24)
#define LV_FONT_DEFAULT &font_th_16

// ---- widget ที่ใช้ ----
#define LV_USE_LABEL 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_SWITCH 1
#define LV_USE_SLIDER 1
#define LV_USE_BAR 1
#define LV_USE_ARC 1
#define LV_USE_LINE 1
#define LV_USE_TABVIEW 1
#define LV_USE_FLEX 1

// ---- ธีม: ใช้ธีม default ของ LVGL (โหมดมืด) ตั้งค่าใน ui_main.cpp ----
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 60

#endif
