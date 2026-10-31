#pragma once

// =====================================================================
//  board_config.h -- ข้อเท็จจริงของฮาร์ดแวร์บอร์ด ESP32-2432S028 (CYD)
//
//  ไฟล์นี้รวม "ทุก pin" ไว้ที่เดียว ห้ามแก้ เว้นแต่ฮาร์ดแวร์จริงต่างไป
//  (ค่าที่อยากลองปรับให้แก้ใน app_config.h แทน)
// =====================================================================

#include <Arduino.h>

namespace cyd {

// ---- จอ TFT (HSPI) -- ต้องตรงกับ build_flags ใน platformio.ini ----
constexpr uint8_t TFT_PIN_MISO = 12;
constexpr uint8_t TFT_PIN_MOSI = 13;
constexpr uint8_t TFT_PIN_SCLK = 14;
constexpr uint8_t TFT_PIN_CS = 15;
constexpr uint8_t TFT_PIN_DC = 2;
constexpr uint8_t TFT_PIN_BACKLIGHT = 21;  // !! ห้ามใช้ต่อเซนเซอร์ (จอจะกะพริบตามการสื่อสาร)

// ---- จอสัมผัส XPT2046 (VSPI คนละชุดกับจอ) อ่านแบบ polling ----
constexpr uint8_t TOUCH_PIN_CLK = 25;
constexpr uint8_t TOUCH_PIN_CS = 33;
constexpr uint8_t TOUCH_PIN_MOSI = 32;
constexpr uint8_t TOUCH_PIN_MISO = 39;
constexpr uint8_t TOUCH_PIN_IRQ = 36;  // ยังไม่ใช้
constexpr uint32_t TOUCH_SPI_FREQUENCY = 2500000;

// ---- RGB LED บนบอร์ด (ทดสอบจากบอร์ดจริงแล้ว: แดง=4, เขียว=16, น้ำเงิน=17) ----
// ทั้งสามสีเป็นแบบ active-LOW: ส่ง LOW = ติด, HIGH = ดับ  -> ใช้ PWM ต้อง "กลับค่า duty" (hal_led.cpp ทำให้แล้ว)
constexpr uint8_t LED_PIN_R = 4;
constexpr uint8_t LED_PIN_G = 16;
constexpr uint8_t LED_PIN_B = 17;
constexpr bool LED_ACTIVE_LOW = true;  // ถ้าบอร์ดของคุณสีกลับกัน (ตั้ง 255 แล้วดับ) ให้เปลี่ยนเป็น false

// ---- อื่นๆ บนบอร์ด ----
constexpr uint8_t AUDIO_PIN = 26;        // ลำโพง (ขั้ว SPEAK ผ่านวงจรขยายบนบอร์ด)
constexpr uint8_t LDR_PIN = 34;          // เซนเซอร์แสง input-only (ยิ่งสว่างค่ายิ่งต่ำ)
constexpr uint8_t BOOT_BUTTON_PIN = 0;   // ปุ่ม BOOT (กด = LOW) ใช้เป็นปุ่มทั่วไปได้หลังบูตเสร็จ

// ---- ขา expansion ที่ปลอดภัยสำหรับต่อเซนเซอร์เพิ่ม (ทดสอบแล้ว) ----
constexpr uint8_t I2C_PIN_SDA = 27;
constexpr uint8_t I2C_PIN_SCL = 22;

// ---- microSD (ยังไม่ใช้ใน Lab นี้) ----
constexpr uint8_t SD_PIN_CS = 5;
constexpr uint8_t SD_PIN_MOSI = 23;
constexpr uint8_t SD_PIN_CLK = 18;
constexpr uint8_t SD_PIN_MISO = 19;

// !! GPIO34/35/36/39 เป็น input-only: อ่านค่าได้อย่างเดียว สั่ง output ไม่ได้

// ---- จอ ----
constexpr uint16_t DISPLAY_WIDTH = 320;
constexpr uint16_t DISPLAY_HEIGHT = 240;
constexpr uint8_t DISPLAY_ROTATION = 1;  // แนวนอน (ทิศ/กระจกเงาปรับต่อได้บนจอ แท็บ "จอภาพ")

}  // namespace cyd
