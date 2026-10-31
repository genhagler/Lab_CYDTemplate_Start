#pragma once

// =====================================================================
//  hal.h -- ชั้นฮาร์ดแวร์ (ไม่รู้จัก LVGL): ค่าถาวร, จอ + ไฟหลังจอ, จอสัมผัส, RGB LED, LDR, เสียง
//  UI (ui_*.cpp) เรียกฮาร์ดแวร์ผ่านฟังก์ชันในไฟล์นี้เท่านั้น
// =====================================================================

#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

namespace hal {

// ---- ค่าที่จำถาวร (NVS) + ข้อมูลระบบ ----
Preferences &prefs();
void begin();
const char *resetReasonText();

// ---- จอ (hal_display.cpp) ----
// ค่าแก้ปัญหาจอที่ปรับได้ตอนรัน (เขียนลงชิปจอผ่านคำสั่ง MADCTL 0x36 และ INVON/INVOFF)
struct DisplayCfg {
    bool invert = false;  // กลับสี (ขาว <-> ดำ)
    bool bgr = false;     // ลำดับสีของแผงจอเป็น BGR (แดง <-> น้ำเงิน)
    bool flipH = false;   // กลับซ้าย-ขวา (กระจกเงา)
    bool flipV = false;   // กลับบน-ล่าง
};
TFT_eSPI &tft();
void displayBegin();                 // tft.init + ใช้ค่าที่บันทึกไว้ + ไฟหลังจอ
const DisplayCfg &displayCfg();
DisplayCfg displayDefaults();        // ค่าเริ่มต้นตามชิปจอที่เลือกใน platformio.ini
void displayApply(const DisplayCfg &c, bool save = true);
uint8_t displayMadctl();             // ค่า MADCTL ที่ส่งให้ชิปจอล่าสุด (ไว้แสดง/ดีบัก)
const char *displayDriverName();     // "ILI9341" / "ST7789"
void setBacklight(int percent, bool save = false);
int backlight();

// ---- จอสัมผัส XPT2046 (hal_touch.cpp) ----
struct RawTouch {
    int16_t x = 0, y = 0, z = 0;
};
// calibrate แบบ affine: x = a*u + b*v + c,  y = d*u + e*v + f   (u, v = ค่าดิบ)
// รองรับแกนสลับ / กลับด้าน / เอียงเล็กน้อย ได้ในสมการเดียว
struct TouchCal {
    bool valid = false;
    float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
    float fitRms = 0, fitMax = 0, verifyErr = 0;  // คุณภาพของการ calibrate ครั้งล่าสุด (px)
};
struct TouchState {
    bool down = false;
    int16_t x = 0, y = 0;        // พิกัดจอหลัง calibrate + กรอง
    int16_t rawX = 0, rawY = 0;  // ค่าดิบล่าสุดขณะแตะ
    int16_t z = 0;               // แรงกดล่าสุด
};
void touchBegin();
bool touchReadRaw(RawTouch &out);  // อ่านชิป 1 ครั้ง (true = กำลังแตะ)
void touchPoll(uint32_t now);      // debounce + แปลงพิกัด (เรียกจาก read callback ของ LVGL)
const TouchState &touch();
void touchCapture(bool on);        // true = หน้า calibrate อ่านชิปเอง (LVGL จะไม่เห็นการแตะ)
bool touchCaptured();
const TouchCal &touchCal();
void touchCalSet(const TouchCal &c, bool save = true);
void touchMap(const TouchCal &c, float u, float v, float &x, float &y);
// fit สมการ affine จาก n จุด (least squares): คืน false ถ้าจุดไม่พอ/ซ้ำกัน/ช่วงค่าดิบแคบเกินไป
bool touchCalFit(const float *u, const float *v, const float *x, const float *y, int n, TouchCal &out);
void touchCalMirror(bool h, bool v);  // จอถูกกลับด้าน -> กลับสมการตาม (ไม่ต้อง calibrate ใหม่)

// ---- RGB LED (hal_led.cpp) ค่า 0-255 ต่อสี ----
void ledBegin();
void ledSetRgb(uint8_t r, uint8_t g, uint8_t b);
void ledGetRgb(uint8_t &r, uint8_t &g, uint8_t &b);

// ---- เซนเซอร์แสง ----
int ldrRaw();  // 0-4095 (ยิ่งสว่างยิ่งต่ำ)

// ---- เสียง (hal_sound.cpp) เล่นใน task แยก ไม่ทำให้ UI หน่วง ----
enum class Melody : uint8_t { Startup, Success, Error, Scale, Song, Count };
constexpr const char *VOLUME_NAMES[4] = {"OFF", "LOW", "MID", "HIGH"};
void soundBegin();
void soundClick();
void soundTone(uint16_t hz, uint16_t ms);
void soundSweep(uint16_t fromHz, uint16_t toHz, uint16_t ms);
void soundPlay(Melody m);
void soundStop();
bool soundBusy();
uint16_t soundNowHz();  // ความถี่ที่กำลังเล่น (0 = เงียบ)
uint8_t soundVolume();
void soundSetVolume(uint8_t v, bool save = true);

}  // namespace hal
