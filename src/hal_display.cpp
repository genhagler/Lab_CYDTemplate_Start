#include "app_config.h"
#include "board_config.h"
#include "hal.h"

// =====================================================================
//  จอ TFT + ไฟหลังจอ
//
//  ปัญหาจอที่พบบ่อยบน CYD (สีกลับ, แดงสลับน้ำเงิน, ภาพกลับด้าน) แก้ได้ด้วยคำสั่ง 2 ตัวของชิปจอ
//    INVON / INVOFF (0x21 / 0x20)  = กลับสีทั้งจอ
//    MADCTL (0x36) 1 ไบต์ บอกชิปว่าจะวาดหน่วยความจำภาพลงจอในทิศไหน:
//        bit 7 MY  กลับลำดับแถว         bit 6 MX  กลับลำดับคอลัมน์
//        bit 5 MV  สลับแถว/คอลัมน์ (= หมุน 90 องศา เป็นแนวนอน)
//        bit 3 BGR ลำดับสีของแผงเป็น B-G-R
//  เมื่อ MV = 1 (แนวนอน) MY จะกลับซ้าย-ขวา และ MX จะกลับบน-ล่าง
//  ILI9341 และ ST7789 ใช้ความหมายบิตเดียวกัน ต่างกันแค่ "ค่าตั้งต้น" ของแนวนอน (ดู LANDSCAPE_BASE)
//  ILI9341 ใช้ rotation 1 = MV, rotation 3 = MV|MX|MY (หมุน 180 องศา = กลับทั้งสองแกน) ตรงกับตารางนี้
// =====================================================================

namespace hal {

namespace {

TFT_eSPI gTft;
DisplayCfg gCfg;
uint8_t gMadctl = 0;
int gBacklight = app::BL_DEFAULT_PERCENT;

// ค่าแนวนอน (rotation 1) ของ TFT_eSPI แต่ละชิป -- ดู TFT_Drivers/*_Rotation.h
#if defined(ST7789_DRIVER)
constexpr uint8_t LANDSCAPE_BASE = TFT_MAD_MX | TFT_MAD_MV;
constexpr uint8_t DRIVER_ID = 2;
#else
constexpr uint8_t LANDSCAPE_BASE = TFT_MAD_MV;
constexpr uint8_t DRIVER_ID = 1;
#endif

// เก็บใน NVS เป็น 16 บิต: บิต 0-3 = ค่า 4 ตัว, บิต 8-15 = ชิปจอที่ใช้ตอนบันทึก
// (เปลี่ยน env จาก cyd เป็น cyd_st7789 ค่าที่บันทึกไว้ของอีกชิปจะไม่ถูกนำมาใช้)
uint16_t pack(const DisplayCfg &c)
{
    return static_cast<uint16_t>((DRIVER_ID << 8) | (c.invert ? 1 : 0) | (c.bgr ? 2 : 0) | (c.flipH ? 4 : 0) | (c.flipV ? 8 : 0));
}

bool unpack(uint16_t v, DisplayCfg &c)
{
    if ((v >> 8) != DRIVER_ID) return false;
    c.invert = v & 1;
    c.bgr = v & 2;
    c.flipH = v & 4;
    c.flipV = v & 8;
    return true;
}

void writeToPanel(const DisplayCfg &c)
{
    uint8_t m = LANDSCAPE_BASE;
    if (c.flipH) m ^= TFT_MAD_MY;
    if (c.flipV) m ^= TFT_MAD_MX;
    if (c.bgr) m |= TFT_MAD_BGR;
    gMadctl = m;

    gTft.dmaWait();  // ห้ามส่งคำสั่งระหว่าง DMA กำลังส่งภาพ
    gTft.writecommand(TFT_MADCTL);
    gTft.writedata(m);
    gTft.invertDisplay(c.invert);
}

}  // namespace

TFT_eSPI &tft()
{
    return gTft;
}

DisplayCfg displayDefaults()
{
    DisplayCfg c;
#if defined(TFT_INVERSION_ON)
    c.invert = true;
#else
    c.invert = false;
#endif
    c.bgr = (TFT_MAD_COLOR_ORDER == TFT_MAD_BGR);
    return c;
}

void displayBegin()
{
    gTft.init();
    gTft.setRotation(cyd::DISPLAY_ROTATION);  // ตั้งขนาด 320x240 ในไลบรารี (MADCTL จะเขียนทับด้านล่าง)
    gTft.fillScreen(TFT_BLACK);
    gTft.initDMA();
    gTft.startWrite();  // ค้าง transaction ไว้ตลอด (จอสัมผัสอยู่คนละบัส SPI จึงไม่ชนกัน)

    gCfg = displayDefaults();
    const uint16_t saved = prefs().getUShort("disp", 0);
    const bool loaded = unpack(saved, gCfg);
    writeToPanel(gCfg);
    Serial.printf("[DISP] driver=%s %s invert=%d bgr=%d flipH=%d flipV=%d MADCTL=0x%02X\n", displayDriverName(), loaded ? "saved" : "defaults",
                  gCfg.invert, gCfg.bgr, gCfg.flipH, gCfg.flipV, gMadctl);

    // ไฟหลังจอ: ต้องทำหลัง tft.init() (ไลบรารีเปิดไฟด้วย digitalWrite ไว้ก่อน) แล้วเปลี่ยนเป็น PWM
    gBacklight = prefs().getUChar("bl", app::BL_DEFAULT_PERCENT);
    ledcSetup(app::BL_CHANNEL, app::BL_PWM_FREQ_HZ, app::BL_PWM_BITS);
    ledcAttachPin(cyd::TFT_PIN_BACKLIGHT, app::BL_CHANNEL);
    setBacklight(gBacklight);
}

const DisplayCfg &displayCfg()
{
    return gCfg;
}

void displayApply(const DisplayCfg &c, bool save)
{
    // จอกลับด้าน -> ภาพที่ตำแหน่ง x ไปอยู่ที่ (319 - x) จึงต้องกลับสมการจอสัมผัสตามด้วย นิ้วจะได้ตรงกับภาพ
    const bool mirrorH = c.flipH != gCfg.flipH;
    const bool mirrorV = c.flipV != gCfg.flipV;
    gCfg = c;
    writeToPanel(gCfg);
    if (mirrorH || mirrorV) touchCalMirror(mirrorH, mirrorV);
    if (save) prefs().putUShort("disp", pack(gCfg));
    Serial.printf("[DISP] invert=%d bgr=%d flipH=%d flipV=%d MADCTL=0x%02X%s\n", gCfg.invert, gCfg.bgr, gCfg.flipH, gCfg.flipV, gMadctl,
                  save ? " (saved)" : "");
}

uint8_t displayMadctl()
{
    return gMadctl;
}

const char *displayDriverName()
{
    return DRIVER_ID == 2 ? "ST7789" : "ILI9341";
}

void setBacklight(int percent, bool save)
{
    gBacklight = constrain(percent, app::BL_MIN_PERCENT, 100);
    const uint32_t maxValue = (1UL << app::BL_PWM_BITS) - 1;
    ledcWrite(app::BL_CHANNEL, (static_cast<uint32_t>(gBacklight) * maxValue + 50) / 100);
    if (save) prefs().putUChar("bl", static_cast<uint8_t>(gBacklight));
}

int backlight()
{
    return gBacklight;
}

}  // namespace hal
