// =====================================================================
//  Lab_Template_Start  --  ESP32 CYD Dev Level 2 (LVGL 8.3)
//  บอร์ด ESP32-2432S028 (CYD) 320x240 + จอสัมผัส XPT2046
//
//  เตรียมบอร์ดให้พร้อมก่อนเริ่ม Lab อื่น:
//    จอภาพ   สี / ทิศ / ข้อความไทย ถูกต้อง (แก้ invert, BGR, กลับด้าน ได้บนจอ จำค่าถาวร)
//    สัมผัส  calibrate 5 จุด + จุดตรวจสอบ, ทดสอบความแม่นยำ 9 จุด
//    ไฟ RGB  แถบเลื่อน / ทดสอบอัตโนมัติ / สายรุ้ง
//    เสียง   ความดัง / เสียงทดสอบ / เปียโน / เลือกความถี่
//
//  ไฟล์ในโปรเจกต์: ดู src/ui.h และ src/hal.h
//    main.cpp      เริ่มระบบ + ผูก LVGL กับจอ (DMA) และจอสัมผัส + คำสั่ง Serial (ไฟล์นี้)
// =====================================================================

#include <Arduino.h>
#include <lvgl.h>

#include "app_config.h"
#include "board_config.h"
#include "hal.h"
#include "ui.h"

// loop() มี LVGL + printf ภาษาไทย  stack 8 KB เดิมเสี่ยงล้น (ล้น = รีสตาร์ทเอง) จึงขยายเป็น 16 KB
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

constexpr int BUF_LINES = 40;  // บัฟเฟอร์วาด 40 บรรทัด x 2 ชุด (ประมาณ 51 KB)

lv_disp_draw_buf_t drawBuf;
lv_color_t buf1[cyd::DISPLAY_WIDTH * BUF_LINES];
lv_color_t buf2[cyd::DISPLAY_WIDTH * BUF_LINES];
lv_disp_drv_t dispDrv;
lv_indev_drv_t indevDrv;
bool gPrintRaw = false;

void displayFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP)
{
    TFT_eSPI &tft = hal::tft();
    const uint32_t w = area->x2 - area->x1 + 1;
    const uint32_t h = area->y2 - area->y1 + 1;
    // DMA: ส่งภาพออกจอเบื้องหลัง ระหว่างนั้น LVGL วาดบัฟเฟอร์อีกชุดได้เลย
    // byte ถูกสลับมาแล้วจาก LV_COLOR_16_SWAP จึงไม่ต้องสลับซ้ำ
    tft.dmaWait();  // รอชุดก่อนหน้าส่งเสร็จ
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixelsDMA(reinterpret_cast<uint16_t *>(&colorP->full), w * h);
    lv_disp_flush_ready(disp);
}

void touchRead(lv_indev_drv_t *, lv_indev_data_t *data)
{
    static bool prevDown = false;
    static int16_t lastX = 0, lastY = 0;

    if (hal::touchCaptured()) {  // หน้า calibrate อ่านชิปเองอยู่
        data->state = LV_INDEV_STATE_REL;
        data->point.x = lastX;
        data->point.y = lastY;
        prevDown = false;
        return;
    }

    hal::touchPoll(millis());
    const hal::TouchState &t = hal::touch();
    const bool down = t.down && hal::touchCal().valid;
    if (down) {
        lastX = t.x;
        lastY = t.y;
        if (!prevDown) hal::soundClick();
    }
    if (gPrintRaw && down != prevDown) Serial.printf("[TOUCH] %s x=%3d y=%3d raw=(%4d,%4d) z=%4d\n", down ? "down" : "up  ", t.x, t.y, t.rawX, t.rawY, t.z);
    data->state = down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = lastX;
    data->point.y = lastY;
    prevDown = down;
}

void lvglBegin()
{
    lv_init();

    lv_disp_draw_buf_init(&drawBuf, buf1, buf2, cyd::DISPLAY_WIDTH * BUF_LINES);
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = cyd::DISPLAY_WIDTH;
    dispDrv.ver_res = cyd::DISPLAY_HEIGHT;
    dispDrv.flush_cb = displayFlush;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    lv_indev_drv_init(&indevDrv);
    indevDrv.type = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = touchRead;
    indevDrv.scroll_limit = 8;  // px ที่ต้องลากก่อนเริ่มเลื่อนหน้า
    lv_indev_drv_register(&indevDrv);
}

void resetDisplaySettings()
{
    hal::displayApply(hal::displayDefaults());
    hal::setBacklight(app::BL_DEFAULT_PERCENT, true);
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    ui_display_refresh();
    hal::soundPlay(hal::Melody::Success);
    Serial.println("[DISP] reset to defaults");
}

void printStatus()
{
    const hal::TouchCal &c = hal::touchCal();
    const hal::DisplayCfg &d = hal::displayCfg();
    Serial.printf("[STATUS] up=%lus heap=%uKB (min %uKB) loopStackFree=%uB | disp %s inv=%d bgr=%d fH=%d fV=%d bl=%d%% | touch %s | vol=%s ldr=%d\n",
                  millis() / 1000, static_cast<unsigned>(ESP.getFreeHeap() / 1024), static_cast<unsigned>(ESP.getMinFreeHeap() / 1024),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)), hal::displayDriverName(), d.invert, d.bgr, d.flipH, d.flipV,
                  hal::backlight(), c.valid ? "calibrated" : "NOT calibrated", hal::VOLUME_NAMES[hal::soundVolume()], hal::ldrRaw());
}

void printHelp()
{
    Serial.println(F("---- Serial commands ----\n"
                     " h  help            i  status\n"
                     " c  calibrate touch k  show calibration\n"
                     " d  display config  r  reset display config (also: hold BOOT 1 s)\n"
                     " t  toggle raw touch print\n"
                     " l  LED test R,G,B,W   s  sound test"));
}

void serialCommands()
{
    while (Serial.available()) {
        const char ch = static_cast<char>(Serial.read());
        switch (ch) {
            case 'h': printHelp(); break;
            case 'i': printStatus(); break;
            case 'c': ui_calibrate_start(false); break;
            case 'k': {
                const hal::TouchCal &c = hal::touchCal();
                Serial.printf("[CAL] %s  x = %.5f*u %+.5f*v %+.1f   y = %.5f*u %+.5f*v %+.1f  fit rms %.2f max %.2f verify %.2f px\n",
                              c.valid ? "valid" : "INVALID", c.a, c.b, c.c, c.d, c.e, c.f, c.fitRms, c.fitMax, c.verifyErr);
                const hal::TouchState &t = hal::touch();
                Serial.printf("[TOUCH] last raw=(%d,%d) z=%d -> x=%d y=%d\n", t.rawX, t.rawY, t.z, t.x, t.y);
                break;
            }
            case 'd': {
                const hal::DisplayCfg &d = hal::displayCfg();
                Serial.printf("[DISP] driver=%s invert=%d bgr=%d flipH=%d flipV=%d MADCTL=0x%02X backlight=%d%%\n", hal::displayDriverName(), d.invert,
                              d.bgr, d.flipH, d.flipV, hal::displayMadctl(), hal::backlight());
                break;
            }
            case 'r': resetDisplaySettings(); break;
            case 't':
                gPrintRaw = !gPrintRaw;
                Serial.printf("[TOUCH] raw print %s\n", gPrintRaw ? "ON" : "OFF");
                break;
            case 'l': {  // ทดสอบ LED แบบบล็อกสั้นๆ (ใช้ดีบักเท่านั้น)
                const uint8_t seq[5][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}};
                const char *names[5] = {"RED", "GREEN", "BLUE", "WHITE", "OFF"};
                for (int i = 0; i < 5; i++) {
                    hal::ledSetRgb(seq[i][0], seq[i][1], seq[i][2]);
                    Serial.printf("[LED] %s\n", names[i]);
                    delay(500);
                }
                break;
            }
            case 's': hal::soundPlay(hal::Melody::Scale); Serial.println("[SOUND] scale C5-C6"); break;
            default: break;
        }
    }
}

// กดปุ่ม BOOT ค้าง 1 วินาที = รีเซ็ตค่าจอ (ทางออกเมื่อตั้งค่าจอผิดจนมองไม่เห็น)
void bootButton(uint32_t now)
{
    static uint32_t downSince = 0;
    static bool done = false;
    if (digitalRead(cyd::BOOT_BUTTON_PIN) == LOW) {
        if (downSince == 0) downSince = now;
        if (!done && now - downSince >= 1000) {
            done = true;
            resetDisplaySettings();
        }
    } else {
        downSince = 0;
        done = false;
    }
}

}  // namespace

void setup()
{
    Serial.begin(app::SERIAL_BAUD);
    delay(200);
    Serial.printf("\n=== %s %s | LVGL %d.%d.%d ===\n", app::APP_NAME, app::FW_VERSION, LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    hal::begin();  // ค่าที่จำถาวร (NVS)
    Serial.printf("[BOOT] reset reason: %s\n", hal::resetReasonText());
    hal::ledBegin();
    hal::soundBegin();
    hal::displayBegin();  // จอ + ค่าแก้จอที่บันทึกไว้ + ไฟหลังจอ
    hal::touchBegin();    // โหลดค่า calibrate (ถ้ายังไม่มี หน้า calibrate จะขึ้นเองหลัง splash)

    lvglBegin();
    ui_init();
    hal::soundPlay(hal::Melody::Startup);
    printHelp();
}

void loop()
{
    const uint32_t wait = lv_timer_handler();  // LVGL บอกเองว่าอีกกี่ ms ถึงมีงานถัดไป
    delay(constrain(wait, 1, 5));

    const uint32_t now = millis();
    serialCommands();
    bootButton(now);

    static uint32_t lastStatus = 0;
    if (app::STATUS_PRINT_MS != 0 && now - lastStatus >= app::STATUS_PRINT_MS) {
        lastStatus = now;
        printStatus();
    }
}
