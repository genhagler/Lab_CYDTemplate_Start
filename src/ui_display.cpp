#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "จอภาพ": ตรวจว่าจอแสดงสี ทิศ และข้อความถูกต้อง แล้วแก้ได้ทันทีบนจอ (จำค่าถาวร)
// =====================================================================

namespace {

lv_obj_t *gSwInvert, *gSwBgr, *gSwFlipH, *gSwFlipV;
lv_obj_t *gBlSlider, *gBlLabel, *gMadLabel;

void refreshSettings()
{
    const hal::DisplayCfg &c = hal::displayCfg();
    lv_obj_t *sws[4] = {gSwInvert, gSwBgr, gSwFlipH, gSwFlipV};
    const bool vals[4] = {c.invert, c.bgr, c.flipH, c.flipV};
    for (int i = 0; i < 4; i++) {
        if (vals[i]) lv_obj_add_state(sws[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(sws[i], LV_STATE_CHECKED);
    }
    lv_label_set_text_fmt(gMadLabel, "ชิป %s   MADCTL = 0x%02X", hal::displayDriverName(), hal::displayMadctl());
    lv_slider_set_value(gBlSlider, hal::backlight(), LV_ANIM_OFF);
    lv_label_set_text_fmt(gBlLabel, "ความสว่างจอ %d%%", hal::backlight());
}

// ใช้ค่าใหม่กับชิปจอ แล้วสั่ง LVGL วาดใหม่ทั้งจอ (ภาพในชิปจอเป็นของทิศเดิม)
void apply(const hal::DisplayCfg &c)
{
    hal::displayApply(c);
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    refreshSettings();
}

bool checked(lv_event_t *e)
{
    return lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
}

void onInvert(lv_event_t *e)
{
    hal::DisplayCfg c = hal::displayCfg();
    c.invert = checked(e);
    apply(c);
}
void onBgr(lv_event_t *e)
{
    hal::DisplayCfg c = hal::displayCfg();
    c.bgr = checked(e);
    apply(c);
}
void onFlipH(lv_event_t *e)
{
    hal::DisplayCfg c = hal::displayCfg();
    c.flipH = checked(e);
    apply(c);
}
void onFlipV(lv_event_t *e)
{
    hal::DisplayCfg c = hal::displayCfg();
    c.flipV = checked(e);
    apply(c);
}
void onDefaults(lv_event_t *)
{
    apply(hal::displayDefaults());
}

void onBacklight(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    const int v = lv_slider_get_value(gBlSlider);
    hal::setBacklight(v, code == LV_EVENT_RELEASED);  // บันทึกเมื่อปล่อยนิ้ว (ไม่เขียน flash ทุกครั้งที่ลาก)
    lv_label_set_text_fmt(gBlLabel, "ความสว่างจอ %d%%", v);
}

void onPattern(lv_event_t *)
{
    ui_display_pattern_start();
}

lv_obj_t *swatch(lv_obj_t *row, lv_color_t color, const char *name)
{
    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 31, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *box = lv_obj_create(col);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 29, 29);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, color, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_palette_main(LV_PALETTE_GREY), 0);
    ui_label(col, name);
    return col;
}

lv_obj_t *gradientBar(lv_obj_t *parent, lv_color_t to)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), 14);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_color(bar, to, 0);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);  // ให้การแตะทะลุไปถึงพื้นหลัง
    return bar;
}

// ---------------------------------------------------------------------
//  ทดสอบเต็มจอ: แตะเพื่อไปหน้าถัดไป
// ---------------------------------------------------------------------
lv_obj_t *gPattern = nullptr;
int gStep = 0;
constexpr int PATTERN_STEPS = 7;

lv_obj_t *patternCaption(const char *text, lv_color_t fg, lv_color_t bg)
{
    lv_obj_t *l = ui_label(gPattern, text, nullptr, fg);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(l, bg, 0);
    lv_obj_set_style_pad_hor(l, 8, 0);
    lv_obj_set_style_pad_ver(l, 2, 0);
    lv_obj_set_style_radius(l, 4, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    return l;
}

void buildPatternStep()
{
    lv_obj_clean(gPattern);
    lv_obj_set_style_border_width(gPattern, 0, 0);
    char buf[160];

    struct Solid {
        lv_color_t color;
        const char *name;
    };
    const Solid solids[5] = {{lv_color_make(255, 0, 0), "สีแดง (R)"},
                             {lv_color_make(0, 255, 0), "สีเขียว (G)"},
                             {lv_color_make(0, 0, 255), "สีน้ำเงิน (B)"},
                             {lv_color_white(), "สีขาว (ต้องไม่เป็นดำ)"},
                             {lv_color_black(), "สีดำ (ต้องไม่เป็นขาว)"}};

    if (gStep < 5) {
        lv_obj_set_style_bg_color(gPattern, solids[gStep].color, 0);
        snprintf(buf, sizeof(buf), "%s\nแตะเพื่อไปต่อ  %d/%d", solids[gStep].name, gStep + 1, PATTERN_STEPS);
        patternCaption(buf, lv_color_white(), lv_color_hex(0x202020));
        return;
    }

    lv_obj_set_style_bg_color(gPattern, lv_color_black(), 0);
    if (gStep == 5) {  // ไล่ระดับสี: ต้องเห็นจากดำไปสว่างอย่างต่อเนื่อง (RGB565 จะเห็นเป็นขั้นเล็กๆ ได้ ปกติ)
        lv_obj_t *col = lv_obj_create(gPattern);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, UI_W, UI_H);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
        const lv_color_t tos[4] = {lv_color_make(255, 0, 0), lv_color_make(0, 255, 0), lv_color_make(0, 0, 255), lv_color_white()};
        for (lv_color_t to : tos) {
            lv_obj_t *b = gradientBar(col, to);
            lv_obj_set_size(b, UI_W, UI_H / 4);
        }
        snprintf(buf, sizeof(buf), "ไล่ระดับ แดง/เขียว/น้ำเงิน/เทา\nซ้ายมืด -> ขวาสว่าง  %d/%d", gStep + 1, PATTERN_STEPS);
        patternCaption(buf, lv_color_white(), lv_color_hex(0x202020));
        return;
    }

    // gStep == 6: ตาราง + ขอบจอ + ป้ายมุม  ตรวจทิศ / กระจกเงา / ภาพถูกตัด
    lv_obj_set_style_border_width(gPattern, 1, 0);  // ขอบแดง 1 px ต้องเห็นครบทั้ง 4 ด้าน
    lv_obj_set_style_border_color(gPattern, lv_color_make(255, 0, 0), 0);
    for (int x = 40; x < UI_W; x += 40) {
        lv_obj_t *l = lv_obj_create(gPattern);
        lv_obj_remove_style_all(l);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(l, lv_color_hex(0x404040), 0);
        lv_obj_set_size(l, 1, UI_H);
        lv_obj_set_pos(l, x, 0);
        lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    }
    for (int y = 40; y < UI_H; y += 40) {
        lv_obj_t *l = lv_obj_create(gPattern);
        lv_obj_remove_style_all(l);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(l, lv_color_hex(0x404040), 0);
        lv_obj_set_size(l, UI_W, 1);
        lv_obj_set_pos(l, 0, y);
        lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    }
    struct Corner {
        lv_align_t align;
        const char *text;
    };
    const Corner corners[4] = {{LV_ALIGN_TOP_LEFT, "บนซ้าย"}, {LV_ALIGN_TOP_RIGHT, "บนขวา"}, {LV_ALIGN_BOTTOM_LEFT, "ล่างซ้าย"}, {LV_ALIGN_BOTTOM_RIGHT, "ล่างขวา"}};
    for (const Corner &c : corners) {
        lv_obj_t *l = ui_label(gPattern, c.text, nullptr, lv_palette_main(LV_PALETTE_YELLOW));
        lv_obj_align(l, c.align, (c.align == LV_ALIGN_TOP_LEFT || c.align == LV_ALIGN_BOTTOM_LEFT) ? 4 : -4,
                     (c.align == LV_ALIGN_TOP_LEFT || c.align == LV_ALIGN_TOP_RIGHT) ? 2 : -2);
    }
    lv_obj_t *up = ui_label(gPattern, LV_SYMBOL_UP " ด้านบน", nullptr, lv_palette_main(LV_PALETTE_GREEN));
    lv_obj_align(up, LV_ALIGN_TOP_MID, 0, 2);
    snprintf(buf, sizeof(buf), "ข้อความต้องอ่านได้ปกติ (ไม่กลับด้าน)\nป้ายต้องอยู่ตรงมุมจริง  ขอบแดงครบ 4 ด้าน\nแตะเพื่อปิด  %d/%d", gStep + 1,
             PATTERN_STEPS);
    patternCaption(buf, lv_color_white(), lv_color_hex(0x203040));
}

void onPatternTap(lv_event_t *)
{
    if (++gStep >= PATTERN_STEPS) {
        lv_obj_del(gPattern);
        gPattern = nullptr;
        return;
    }
    buildPatternStep();
}

}  // namespace

void ui_display_refresh()
{
    refreshSettings();
}

void ui_display_pattern_start()
{
    if (gPattern != nullptr) return;
    gPattern = ui_overlay();
    lv_obj_add_event_cb(gPattern, onPatternTap, LV_EVENT_CLICKED, nullptr);
    gStep = 0;
    buildPatternStep();
}

void ui_display_tab(lv_obj_t *tab)
{
    // ---- สีพื้นฐาน ----
    lv_obj_t *card = ui_card(tab, "1) สีพื้นฐาน: ช่องสีต้องตรงกับชื่อ");
    lv_obj_t *row = ui_row(card);
    lv_obj_set_style_pad_column(row, 3, 0);
    swatch(row, lv_color_make(255, 0, 0), "R");
    swatch(row, lv_color_make(0, 255, 0), "G");
    swatch(row, lv_color_make(0, 0, 255), "B");
    swatch(row, lv_color_make(255, 255, 0), "Y");
    swatch(row, lv_color_make(0, 255, 255), "C");
    swatch(row, lv_color_make(255, 0, 255), "M");
    swatch(row, lv_color_white(), "W");
    swatch(row, lv_color_black(), "K");
    ui_label(card, "R แดง G เขียว B น้ำเงิน Y เหลือง\nC ฟ้า M ม่วงแดง W ขาว K ดำ", nullptr, lv_palette_lighten(LV_PALETTE_GREY, 1));
    gradientBar(card, lv_color_make(255, 0, 0));
    gradientBar(card, lv_color_make(0, 255, 0));
    gradientBar(card, lv_color_make(0, 0, 255));
    gradientBar(card, lv_color_white());

    // ---- ตั้งค่าจอ ----
    card = ui_card(tab, "2) แก้ปัญหาจอ (จำค่าอัตโนมัติ)");
    gSwInvert = ui_switch_row(card, "กลับสี (ขาว<->ดำ)", false, onInvert);
    gSwBgr = ui_switch_row(card, "สลับ แดง<->น้ำเงิน (BGR)", false, onBgr);
    gSwFlipH = ui_switch_row(card, "กลับซ้าย-ขวา", false, onFlipH);
    gSwFlipV = ui_switch_row(card, "กลับบน-ล่าง", false, onFlipV);
    gMadLabel = ui_label(card, "", nullptr, lv_palette_main(LV_PALETTE_GREY));
    row = ui_row(card);
    ui_btn(row, LV_SYMBOL_EYE_OPEN " ทดสอบเต็มจอ", onPattern);
    ui_btn(row, LV_SYMBOL_REFRESH " ค่าเริ่มต้น", onDefaults);

    gBlLabel = ui_label(card, "");
    gBlSlider = lv_slider_create(card);
    lv_obj_set_width(gBlSlider, LV_PCT(95));
    lv_slider_set_range(gBlSlider, app::BL_MIN_PERCENT, 100);
    lv_slider_set_value(gBlSlider, hal::backlight(), LV_ANIM_OFF);
    lv_obj_add_event_cb(gBlSlider, onBacklight, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(gBlSlider, onBacklight, LV_EVENT_RELEASED, nullptr);
    lv_label_set_text_fmt(gBlLabel, "ความสว่างจอ %d%%", hal::backlight());
    ui_no_scroll(gBlSlider);
    refreshSettings();

    // ---- ข้อความ ----
    card = ui_card(tab, "3) ข้อความและฟอนต์");
    ui_label(card, "สวัสดี CYD 2.8\"", &font_th_24, lv_palette_main(LV_PALETTE_AMBER));
    ui_label(card, "ไทย: ที่นี่ ผู้ใหญ่ น้ำใจ กุ้ง ฤดู ๑๒๓");
    ui_label(card, "English: Quick brown fox 0123456789");
    ui_label(card, "หน่วย: 25.5°C  75%  12.6V  " LV_SYMBOL_OK " " LV_SYMBOL_WIFI " " LV_SYMBOL_BELL " " LV_SYMBOL_VOLUME_MAX);

    const lv_text_align_t aligns[3] = {LV_TEXT_ALIGN_LEFT, LV_TEXT_ALIGN_CENTER, LV_TEXT_ALIGN_RIGHT};
    const char *alignText[3] = {"ชิดซ้าย", "กึ่งกลาง", "ชิดขวา"};
    lv_obj_t *box = lv_obj_create(card);
    lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *l = ui_label(box, alignText[i]);
        lv_obj_set_width(l, LV_PCT(100));
        lv_obj_set_style_text_align(l, aligns[i], 0);
    }
    lv_obj_t *wrap = ui_label(card, "ข้อความยาว จะขึ้นบรรทัดใหม่ อัตโนมัติ ตรงช่องว่าง เมื่อกำหนดความกว้างของ label ไว้ (ภาษาไทยไม่มีช่องว่างระหว่างคำ จึงควรเว้นวรรคเอง)",
                             nullptr, lv_palette_lighten(LV_PALETTE_GREY, 2));
    lv_obj_set_width(wrap, LV_PCT(100));
    lv_label_set_long_mode(wrap, LV_LABEL_LONG_WRAP);
}
