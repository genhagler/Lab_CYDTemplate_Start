#include <math.h>

#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "สัมผัส": ดูค่าดิบ/พิกัด/แรงกดสด, ลากนิ้ววาดเส้น, ทดสอบความแม่นยำ 9 จุด, calibrate ใหม่
// =====================================================================

namespace {

lv_obj_t *gRawLabel, *gPixLabel, *gCalLabel;
lv_obj_t *gPad;
constexpr int DOTS = 80;  // จุดที่วาดในกล่อง (วนใช้ซ้ำ ไม่สร้างใหม่เรื่อยๆ จนหน่วยความจำหมด)
lv_obj_t *gDots[DOTS];
int gDotNext = 0;
lv_point_t gLastDot = {-100, -100};

void updateLive(lv_timer_t *)
{
    static char lastRaw[64] = "", lastPix[64] = "";
    char buf[64];
    const hal::TouchState &t = hal::touch();
    snprintf(buf, sizeof(buf), "ดิบ  u=%4d  v=%4d  แรงกด z=%4d", t.rawX, t.rawY, t.z);
    if (strcmp(buf, lastRaw) != 0) {
        strcpy(lastRaw, buf);
        lv_label_set_text(gRawLabel, buf);
    }
    snprintf(buf, sizeof(buf), "จอ  x=%3d  y=%3d   %s", t.x, t.y, t.down ? "กดอยู่" : "ปล่อย");
    if (strcmp(buf, lastPix) != 0) {
        strcpy(lastPix, buf);
        lv_label_set_text(gPixLabel, buf);
        lv_obj_set_style_text_color(gPixLabel, t.down ? lv_palette_main(LV_PALETTE_GREEN) : lv_color_white(), 0);
    }
}

void onPadDraw(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    lv_area_t a;
    lv_obj_get_coords(gPad, &a);
    const int x = p.x - a.x1, y = p.y - a.y1;
    if (abs(x - gLastDot.x) + abs(y - gLastDot.y) < 3) return;  // ขยับน้อยเกินไป ไม่ต้องวางจุดใหม่
    gLastDot = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
    lv_obj_t *d = gDots[gDotNext];
    gDotNext = (gDotNext + 1) % DOTS;
    lv_obj_set_pos(d, x - 2, y - 2);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_HIDDEN);
    (void)e;
}

void onPadReleased(lv_event_t *)
{
    gLastDot = {-100, -100};
}

void onClear(lv_event_t *)
{
    for (lv_obj_t *d : gDots) lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    gDotNext = 0;
}

void onRecal(lv_event_t *)
{
    hal::soundPlay(hal::Melody::Success);
    ui_calibrate_start(false);
}

void onAccuracy(lv_event_t *)
{
    ui_touch_accuracy_start();
}

// ---------------------------------------------------------------------
//  ทดสอบความแม่นยำ 9 จุด: แตะกลางเป้า ดูว่าพิกัดที่ได้ห่างจากเป้ากี่พิกเซล
// ---------------------------------------------------------------------
lv_obj_t *gAcc = nullptr;
lv_obj_t *gAccCross, *gAccInfo;
int gAccIndex = 0;
float gAccErr[9];
constexpr int ACC_X[3] = {40, 160, 280};
constexpr int ACC_Y[3] = {40, 120, 200};

void accTarget(int i, int &x, int &y)
{
    x = ACC_X[i % 3];
    y = ACC_Y[i / 3];
}

lv_color_t gradeColor(float err)
{
    if (err <= 4.0f) return lv_palette_main(LV_PALETTE_GREEN);
    if (err <= 8.0f) return lv_palette_main(LV_PALETTE_YELLOW);
    return lv_palette_main(LV_PALETTE_RED);
}

void accMarker(int x, int y, lv_color_t color)
{
    lv_obj_t *m = lv_obj_create(gAcc);
    lv_obj_remove_style_all(m);
    lv_obj_set_size(m, 7, 7);
    lv_obj_set_pos(m, x - 3, y - 3);
    lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(m, color, 0);
    lv_obj_clear_flag(m, LV_OBJ_FLAG_CLICKABLE);
}

void accShowTarget()
{
    int tx, ty;
    accTarget(gAccIndex, tx, ty);
    ui_crosshair_move(gAccCross, tx, ty);
    lv_label_set_text_fmt(gAccInfo, "แตะกลางเป้า  จุด %d/9", gAccIndex + 1);
}

void onAccReleased(lv_event_t *)
{
    if (gAccIndex >= 9) {  // จบแล้ว แตะเพื่อปิด
        lv_obj_del(gAcc);
        gAcc = nullptr;
        return;
    }
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    int tx, ty;
    accTarget(gAccIndex, tx, ty);
    const float err = hypotf(static_cast<float>(p.x - tx), static_cast<float>(p.y - ty));
    gAccErr[gAccIndex] = err;
    Serial.printf("[ACC] target %d (%d,%d) touched (%d,%d) error %.1f px\n", gAccIndex + 1, tx, ty, p.x, p.y, err);
    accMarker(p.x, p.y, gradeColor(err));
    hal::soundTone(err <= 8.0f ? 1568 : 392, 60);
    gAccIndex++;

    if (gAccIndex < 9) {
        accShowTarget();
        return;
    }
    // สรุปผล
    float sum = 0, worst = 0;
    for (float e : gAccErr) {
        sum += e;
        worst = fmaxf(worst, e);
    }
    const float avg = sum / 9.0f;
    const char *grade = worst <= 4.0f ? "ดีมาก" : worst <= 8.0f ? "ใช้งานได้" : "ควร calibrate ใหม่";
    lv_obj_add_flag(gAccCross, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 9; i++) {  // เป้าเล็กสีเทา เทียบกับจุดที่แตะ
        int x, y;
        accTarget(i, x, y);
        accMarker(x, y, lv_palette_main(LV_PALETTE_GREY));
    }
    char buf[120];
    snprintf(buf, sizeof(buf), "เฉลี่ย %.1f px   มากสุด %.1f px\nผล: %s\nแตะเพื่อปิด", avg, worst, grade);
    lv_label_set_text(gAccInfo, buf);
    lv_obj_set_style_text_color(gAccInfo, gradeColor(worst), 0);
    Serial.printf("[ACC] average %.1f px  worst %.1f px  -> %s\n", avg, worst, worst <= 4.0f ? "excellent" : worst <= 8.0f ? "ok" : "recalibrate");
    hal::soundPlay(worst <= 8.0f ? hal::Melody::Success : hal::Melody::Error);
}

}  // namespace

void ui_touch_accuracy_start()
{
    if (gAcc != nullptr) return;
    gAcc = ui_overlay();
    lv_obj_set_style_bg_color(gAcc, lv_color_hex(0x101820), 0);
    lv_obj_add_event_cb(gAcc, onAccReleased, LV_EVENT_RELEASED, nullptr);
    gAccInfo = ui_label(gAcc, "", nullptr, lv_color_white());
    lv_obj_set_style_text_align(gAccInfo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(gAccInfo, LV_ALIGN_CENTER, 0, -30);
    gAccCross = ui_crosshair(gAcc, lv_palette_main(LV_PALETTE_CYAN));
    gAccIndex = 0;
    accShowTarget();
}

void ui_touch_refresh()
{
    const hal::TouchCal &c = hal::touchCal();
    if (!c.valid) {
        lv_label_set_text(gCalLabel, "ยังไม่ได้ calibrate");
        return;
    }
    char buf[200];
    snprintf(buf, sizeof(buf), "x = %.4f*u %+.4f*v %+.1f\ny = %.4f*u %+.4f*v %+.1f\nfit: เฉลี่ย %.1f px  มากสุด %.1f px\nจุดตรวจสอบ: %.1f px", c.a,
             c.b, c.c, c.d, c.e, c.f, c.fitRms, c.fitMax, c.verifyErr);
    lv_label_set_text(gCalLabel, buf);
}

void ui_touch_tab(lv_obj_t *tab)
{
    lv_obj_t *card = ui_card(tab, "ค่าจากจอสัมผัส (สด)");
    gRawLabel = ui_label(card, "");
    gPixLabel = ui_label(card, "");

    card = ui_card(tab, "ลากนิ้ววาดในกล่อง");
    gPad = lv_obj_create(card);
    lv_obj_set_size(gPad, LV_PCT(100), 110);
    lv_obj_set_style_pad_all(gPad, 0, 0);
    lv_obj_set_style_bg_color(gPad, lv_color_black(), 0);
    lv_obj_clear_flag(gPad, LV_OBJ_FLAG_SCROLLABLE);
    ui_no_scroll(gPad);
    lv_obj_add_event_cb(gPad, onPadDraw, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(gPad, onPadReleased, LV_EVENT_RELEASED, nullptr);
    for (lv_obj_t *&d : gDots) {
        d = lv_obj_create(gPad);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 5, 5);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_palette_main(LV_PALETTE_LIGHT_GREEN), 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    }
    ui_btn(card, LV_SYMBOL_TRASH " ล้าง", onClear);

    card = ui_card(tab, "ความแม่นยำ / Calibrate");
    lv_obj_t *row = ui_row(card);
    ui_btn(row, LV_SYMBOL_GPS " ทดสอบ 9 จุด", onAccuracy);
    lv_obj_t *recal = ui_btn(row, LV_SYMBOL_SETTINGS " Calibrate (กดค้าง)", onRecal, nullptr, LV_EVENT_LONG_PRESSED);
    lv_obj_set_style_bg_color(recal, lv_palette_main(LV_PALETTE_DEEP_ORANGE), 0);
    gCalLabel = ui_label(card, "", nullptr, lv_palette_lighten(LV_PALETTE_GREY, 2));
    ui_touch_refresh();

    lv_timer_create(updateLive, 50, nullptr);
    updateLive(nullptr);
}
