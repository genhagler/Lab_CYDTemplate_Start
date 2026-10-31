#include <math.h>

#include "app_config.h"
#include "board_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  หน้า calibrate จอสัมผัสแบบแม่นยำ
//
//  1) เป้า 5 จุด: 4 มุม + กลางจอ  (จุดมากกว่าจำนวนตัวแปร = เฉลี่ยความผิดพลาดของนิ้วได้)
//  2) แต่ละจุด: รอนิ้วกดนิ่ง -> ทิ้งค่าช่วงแรก (CAL_SETTLE_SAMPLES) -> เก็บ CAL_SAMPLES ค่า
//     -> ใช้ "ค่ามัธยฐาน" (ไม่ไวต่อค่าเพี้ยนเหมือนค่าเฉลี่ย) และตรวจว่านิ้วนิ่งพอ (IQR ไม่เกิน CAL_MAX_SPREAD_RAW)
//  3) คำนวณสมการ affine ด้วย least squares แล้วตรวจความคลาดเคลื่อนของทั้ง 5 จุด
//  4) จุดตรวจสอบ (ไม่ได้ใช้คำนวณ) ต้องคลาดเคลื่อนไม่เกิน CAL_MAX_VERIFY_PX จึงบันทึก
//  ระหว่างนี้หน้านี้อ่านชิปจอสัมผัสเอง (hal::touchCapture) LVGL จะไม่เห็นการแตะ
// =====================================================================

namespace {

// WaitLift: ต้องไม่มีนิ้วบนจอก่อนรับเป้าถัดไป (เช่นนิ้วที่ยังกดปุ่ม "Calibrate (กดค้าง)" ค้างอยู่ตอนหน้านี้เปิด
//           ถ้าไม่รอ จะถูกนับเป็นจุดที่ 1 ทันทีที่ตำแหน่งปุ่ม -> calibrate ผิด)
enum class St : uint8_t { WaitLift, WaitPress, Sampling, WaitRelease, Pause };

constexpr int FIT_POINTS = 5;
constexpr int ALL_POINTS = FIT_POINTS + 1;  // + จุดตรวจสอบ

lv_obj_t *gScr = nullptr;
lv_obj_t *gCross, *gTitle, *gInfo, *gBar;
lv_timer_t *gTimer = nullptr;
St gState = St::WaitPress;
int gIndex = 0;
uint8_t gRun = 0;  // นับการแตะ/ไม่แตะติดกัน
uint8_t gSkip = 0;
int gCount = 0;
int16_t gSx[app::CAL_SAMPLES], gSy[app::CAL_SAMPLES];
float gU[ALL_POINTS], gV[ALL_POINTS];
hal::TouchCal gNew;
bool gFirst = false;
uint32_t gIdleSince = 0;
uint32_t gPauseUntil = 0;
void (*gAfterPause)() = nullptr;

void target(int i, int &x, int &y)
{
    const int m = app::CAL_MARGIN;
    const int r = cyd::DISPLAY_WIDTH - 1 - m, b = cyd::DISPLAY_HEIGHT - 1 - m;
    switch (i) {
        case 0: x = m, y = m; break;     // บนซ้าย
        case 1: x = r, y = m; break;     // บนขวา
        case 2: x = r, y = b; break;     // ล่างขวา
        case 3: x = m, y = b; break;     // ล่างซ้าย
        case 4: x = cyd::DISPLAY_WIDTH / 2, y = cyd::DISPLAY_HEIGHT / 2; break;  // กลางจอ
        default: x = 250, y = 165; break;  // จุดตรวจสอบ (ไม่อยู่ในชุดคำนวณ)
    }
}

// ค่ามัธยฐาน + ช่วงควอไทล์ (IQR = Q3 - Q1) บอกว่าค่าแกว่งแค่ไหน
void medianIqr(int16_t *v, int n, int16_t &median, int16_t &iqr)
{
    for (int i = 1; i < n; i++) {  // insertion sort (n เล็ก)
        const int16_t k = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > k) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = k;
    }
    median = v[n / 2];
    iqr = static_cast<int16_t>(v[(3 * n) / 4] - v[n / 4]);
}

void showPoint()
{
    int x, y;
    target(gIndex, x, y);
    ui_crosshair_move(gCross, x, y);
    lv_obj_clear_flag(gCross, LV_OBJ_FLAG_HIDDEN);
    if (gIndex < FIT_POINTS)
        lv_label_set_text_fmt(gInfo, "แตะกลางเป้า แล้วกดค้างไว้\nจุด %d / %d", gIndex + 1, FIT_POINTS);
    else
        lv_label_set_text(gInfo, "จุดตรวจสอบ: แตะกลางเป้าอีกครั้ง\nเพื่อยืนยันความแม่นยำ");
    lv_obj_set_style_text_color(gInfo, lv_color_white(), 0);
    lv_bar_set_value(gBar, 0, LV_ANIM_OFF);
    gState = St::WaitLift;
    gRun = 0;
    gIdleSince = millis();
}

void restart()
{
    gIndex = 0;
    showPoint();
}

void pause(uint32_t ms, void (*then)())
{
    lv_obj_add_flag(gCross, LV_OBJ_FLAG_HIDDEN);
    gState = St::Pause;
    gPauseUntil = millis() + ms;
    gAfterPause = then;
}

void message(const char *text, lv_palette_t color)
{
    lv_label_set_text(gInfo, text);
    lv_obj_set_style_text_color(gInfo, lv_palette_main(color), 0);
}

void close()
{
    lv_timer_del(gTimer);
    gTimer = nullptr;
    lv_obj_del(gScr);
    gScr = nullptr;
    hal::touchCapture(false);
    ui_touch_refresh();
}

void fail(const char *why)
{
    hal::soundPlay(hal::Melody::Error);
    char buf[160];
    snprintf(buf, sizeof(buf), "%s\nเริ่มใหม่จากจุดที่ 1", why);
    message(buf, LV_PALETTE_RED);
    pause(2500, restart);
}

void finishFit()
{
    float x[FIT_POINTS], y[FIT_POINTS];
    for (int i = 0; i < FIT_POINTS; i++) {
        int tx, ty;
        target(i, tx, ty);
        x[i] = tx;
        y[i] = ty;
    }
    if (!hal::touchCalFit(gU, gV, x, y, FIT_POINTS, gNew)) {
        fail("แตะผิดจุด (ค่าดิบไม่เป็นรูปสี่เหลี่ยม)");
        return;
    }
    if (gNew.fitMax > app::CAL_MAX_FIT_ERROR_PX) {
        char buf[96];
        snprintf(buf, sizeof(buf), "คลาดเคลื่อน %.1f px (เกิน %.0f px)", gNew.fitMax, app::CAL_MAX_FIT_ERROR_PX);
        fail(buf);
        return;
    }
    gIndex = FIT_POINTS;  // ไปจุดตรวจสอบ
    showPoint();
}

void finishVerify()
{
    int tx, ty;
    target(FIT_POINTS, tx, ty);
    float px, py;
    hal::touchMap(gNew, gU[FIT_POINTS], gV[FIT_POINTS], px, py);
    gNew.verifyErr = hypotf(px - tx, py - ty);
    Serial.printf("[CAL] verify target (%d,%d) -> (%.1f,%.1f) error %.2f px\n", tx, ty, px, py, gNew.verifyErr);
    if (gNew.verifyErr > app::CAL_MAX_VERIFY_PX) {
        char buf[96];
        snprintf(buf, sizeof(buf), "จุดตรวจสอบคลาดเคลื่อน %.1f px", gNew.verifyErr);
        fail(buf);
        return;
    }
    hal::touchCalSet(gNew, true);
    hal::soundPlay(hal::Melody::Success);
    char buf[160];
    snprintf(buf, sizeof(buf), LV_SYMBOL_OK " Calibrate สำเร็จ\nเฉลี่ย %.1f px  มากสุด %.1f px\nจุดตรวจสอบ %.1f px", gNew.fitRms, gNew.fitMax, gNew.verifyErr);
    message(buf, LV_PALETTE_GREEN);
    pause(2500, close);
}

void pointAccepted(int16_t u, int16_t v)
{
    gU[gIndex] = u;
    gV[gIndex] = v;
    Serial.printf("[CAL] point %d raw = (%d, %d)\n", gIndex + 1, u, v);
    hal::soundTone(1568, 60);
    lv_obj_add_flag(gCross, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(gInfo, "ดีมาก  ยกนิ้วขึ้นได้");
    lv_obj_set_style_text_color(gInfo, lv_palette_main(LV_PALETTE_GREEN), 0);
    gState = St::WaitRelease;
    gRun = 0;
}

void tick(lv_timer_t *)
{
    const uint32_t now = millis();
    if (gState == St::Pause) {
        if (static_cast<int32_t>(now - gPauseUntil) >= 0 && gAfterPause != nullptr) gAfterPause();
        return;
    }

    hal::RawTouch raw;
    const bool touching = hal::touchReadRaw(raw);

    switch (gState) {
        case St::WaitLift:
            gRun = touching ? 0 : gRun + 1;
            if (gRun >= 5) {  // ไม่มีนิ้วบนจอติดกัน ~50 ms
                gState = St::WaitPress;
                gRun = 0;
            }
            break;

        case St::WaitPress:
            if (touching) {
                gIdleSince = now;
                if (++gRun >= 3) {  // แตะติดกัน 3 ครั้ง (~30 ms) = กดจริง
                    gState = St::Sampling;
                    gSkip = app::CAL_SETTLE_SAMPLES;
                    gCount = 0;
                    gRun = 0;
                }
            } else {
                gRun = 0;
                // calibrate ซ้ำแล้วไม่แตะเลย = ยกเลิก (ค่าเดิมยังอยู่ ไม่ได้แก้)
                if (!gFirst && hal::touchCal().valid && now - gIdleSince > app::CAL_CANCEL_MS) {
                    Serial.println("[CAL] cancelled (timeout): keeping the previous calibration");
                    close();
                }
            }
            break;

        case St::Sampling:
            if (!touching) {
                if (++gRun >= 3) {  // ยกนิ้วก่อนเก็บครบ
                    message("ยกนิ้วเร็วไป  กดค้างจนแถบเต็ม", LV_PALETTE_YELLOW);
                    lv_bar_set_value(gBar, 0, LV_ANIM_OFF);
                    gState = St::WaitPress;
                    gRun = 0;
                }
                break;
            }
            gRun = 0;
            if (gSkip > 0) {
                gSkip--;
                break;
            }
            gSx[gCount] = raw.x;
            gSy[gCount] = raw.y;
            gCount++;
            lv_bar_set_value(gBar, gCount, LV_ANIM_OFF);
            if (gCount >= app::CAL_SAMPLES) {
                int16_t mx, my, sx, sy;
                medianIqr(gSx, gCount, mx, sx);
                medianIqr(gSy, gCount, my, sy);
                if (sx > app::CAL_MAX_SPREAD_RAW || sy > app::CAL_MAX_SPREAD_RAW) {
                    Serial.printf("[CAL] point %d too noisy (IQR u=%d v=%d), sampling again\n", gIndex + 1, sx, sy);
                    message("นิ้วยังไม่นิ่ง  กดค้างไว้นิ่งๆ", LV_PALETTE_YELLOW);
                    gCount = 0;
                    gSkip = app::CAL_SETTLE_SAMPLES;
                    lv_bar_set_value(gBar, 0, LV_ANIM_OFF);
                    break;
                }
                pointAccepted(mx, my);
            }
            break;

        case St::WaitRelease:
            gRun = touching ? 0 : gRun + 1;
            if (gRun >= 5) {  // ปล่อยนิ้วแน่นอนแล้ว (~50 ms)
                gIndex++;
                if (gIndex == FIT_POINTS) finishFit();
                else if (gIndex > FIT_POINTS) finishVerify();
                else showPoint();
            }
            break;

        default: break;
    }
}

}  // namespace

bool ui_calibrate_active()
{
    return gScr != nullptr;
}

void ui_calibrate_start(bool firstTime)
{
    if (gScr != nullptr) return;
    gFirst = firstTime;
    hal::touchCapture(true);

    gScr = ui_overlay();
    lv_obj_set_style_bg_color(gScr, lv_color_hex(0x0A0F1A), 0);

    gTitle = ui_label(gScr, "Calibrate จอสัมผัส", nullptr, lv_palette_main(LV_PALETTE_CYAN));
    lv_obj_align(gTitle, LV_ALIGN_TOP_MID, 0, 4);

    gInfo = ui_label(gScr, "", nullptr, lv_color_white());
    lv_obj_set_style_text_align(gInfo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(gInfo, LV_ALIGN_TOP_MID, 0, 46);

    gBar = lv_bar_create(gScr);
    lv_obj_set_size(gBar, 120, 6);
    lv_obj_align(gBar, LV_ALIGN_BOTTOM_MID, 0, -66);
    lv_bar_set_range(gBar, 0, app::CAL_SAMPLES);

    lv_obj_t *hint = ui_label(gScr, firstTime ? "ใช้เล็บหรือปากกาปลายมน จะแม่นกว่านิ้ว" : "ไม่แตะ 20 วินาที = ยกเลิก (ใช้ค่าเดิม)", nullptr,
                              lv_palette_main(LV_PALETTE_GREY));
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -40);

    gCross = ui_crosshair(gScr, lv_palette_main(LV_PALETTE_LIGHT_GREEN));
    Serial.println("[CAL] start: touch the 5 targets, then the verify target");
    restart();
    gTimer = lv_timer_create(tick, 10, nullptr);
}
