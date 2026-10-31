#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "ไฟ RGB": ปรับสีด้วยแถบเลื่อน, ปุ่มสีสำเร็จรูป, ทดสอบอัตโนมัติ (ไล่สีพร้อมบอกว่าต้องเห็นสีอะไร), สายรุ้ง
// =====================================================================

namespace {

enum class Mode : uint8_t { Manual, AutoTest, Rainbow };

lv_obj_t *gSl[3], *gSlLabel[3];
lv_obj_t *gPreview, *gExpect, *gBtnTest, *gBtnRainbow;
Mode gMode = Mode::Manual;
uint32_t gStepAt = 0;
int gStep = 0;
uint16_t gHue = 0;

struct Preset {
    const char *name;
    uint8_t r, g, b;
};
constexpr Preset PRESETS[] = {{"แดง", 255, 0, 0},  {"เขียว", 0, 255, 0}, {"น้ำเงิน", 0, 0, 255}, {"เหลือง", 255, 255, 0},
                              {"ฟ้า", 0, 255, 255}, {"ม่วง", 255, 0, 255}, {"ขาว", 255, 255, 255}, {"ดับ", 0, 0, 0}};
constexpr int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

// แสดงค่าบนจอให้ตรงกับ LED (ไม่สั่ง LED ซ้ำ)
void showRgb(uint8_t r, uint8_t g, uint8_t b)
{
    const uint8_t v[3] = {r, g, b};
    const char *names[3] = {"แดง R", "เขียว G", "น้ำเงิน B"};
    for (int i = 0; i < 3; i++) {
        if (lv_slider_get_value(gSl[i]) != v[i]) lv_slider_set_value(gSl[i], v[i], LV_ANIM_OFF);
        lv_label_set_text_fmt(gSlLabel[i], "%s  %3d", names[i], v[i]);
    }
    lv_obj_set_style_bg_color(gPreview, lv_color_make(r, g, b), 0);
}

void setRgb(uint8_t r, uint8_t g, uint8_t b)
{
    hal::ledSetRgb(r, g, b);
    showRgb(r, g, b);
}

void setMode(Mode m)
{
    gMode = m;
    if (m == Mode::AutoTest) lv_obj_add_state(gBtnTest, LV_STATE_CHECKED);
    else lv_obj_clear_state(gBtnTest, LV_STATE_CHECKED);
    if (m == Mode::Rainbow) lv_obj_add_state(gBtnRainbow, LV_STATE_CHECKED);
    else lv_obj_clear_state(gBtnRainbow, LV_STATE_CHECKED);
    if (m != Mode::AutoTest) lv_label_set_text(gExpect, "");
    gStep = 0;
    gStepAt = 0;
}

void onSlider(lv_event_t *)
{
    setMode(Mode::Manual);
    setRgb(lv_slider_get_value(gSl[0]), lv_slider_get_value(gSl[1]), lv_slider_get_value(gSl[2]));
}

void onPreset(lv_event_t *e)
{
    const Preset *p = static_cast<const Preset *>(lv_event_get_user_data(e));
    setMode(Mode::Manual);
    setRgb(p->r, p->g, p->b);
}

void onTest(lv_event_t *)
{
    setMode(gMode == Mode::AutoTest ? Mode::Manual : Mode::AutoTest);
    if (gMode == Mode::Manual) setRgb(0, 0, 0);
}

void onRainbow(lv_event_t *)
{
    setMode(gMode == Mode::Rainbow ? Mode::Manual : Mode::Rainbow);
    if (gMode == Mode::Manual) setRgb(0, 0, 0);
}

void tick(lv_timer_t *)
{
    const uint32_t now = millis();
    if (gMode == Mode::AutoTest) {
        if (gStepAt != 0 && now - gStepAt < app::LED_TEST_STEP_MS) return;
        gStepAt = now;
        const Preset &p = PRESETS[gStep];
        setRgb(p.r, p.g, p.b);
        lv_label_set_text_fmt(gExpect, "LED ต้องเป็น: %s  (%d/%d)", p.name, gStep + 1, PRESET_COUNT);
        Serial.printf("[LED] test %d/%d  R=%d G=%d B=%d\n", gStep + 1, PRESET_COUNT, p.r, p.g, p.b);
        gStep = (gStep + 1) % PRESET_COUNT;
    } else if (gMode == Mode::Rainbow) {
        // วงล้อสี (hue 0-359) -> R,G,B 8 บิต: แต่ละช่วง 60 องศา มีสีหนึ่งเพิ่มขึ้นหรือลดลง
        gHue = (gHue + 2) % 360;
        const uint8_t rise = static_cast<uint8_t>((gHue % 60) * 255 / 60), fall = 255 - rise;
        switch (gHue / 60) {
            case 0: setRgb(255, rise, 0); break;
            case 1: setRgb(fall, 255, 0); break;
            case 2: setRgb(0, 255, rise); break;
            case 3: setRgb(0, fall, 255); break;
            case 4: setRgb(rise, 0, 255); break;
            default: setRgb(255, 0, fall); break;
        }
    }
}

}  // namespace

void ui_led_tab(lv_obj_t *tab)
{
    lv_obj_t *card = ui_card(tab, "สีของ LED บนบอร์ด (ด้านหลังจอ)");
    gPreview = lv_obj_create(card);
    lv_obj_remove_style_all(gPreview);
    lv_obj_set_size(gPreview, LV_PCT(100), 22);
    lv_obj_set_style_bg_opa(gPreview, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(gPreview, 6, 0);
    lv_obj_set_style_border_width(gPreview, 1, 0);
    lv_obj_set_style_border_color(gPreview, lv_palette_main(LV_PALETTE_GREY), 0);

    const lv_palette_t pal[3] = {LV_PALETTE_RED, LV_PALETTE_GREEN, LV_PALETTE_BLUE};
    for (int i = 0; i < 3; i++) {
        gSlLabel[i] = ui_label(card, "");
        gSl[i] = lv_slider_create(card);
        lv_obj_set_width(gSl[i], LV_PCT(95));
        lv_slider_set_range(gSl[i], 0, 255);
        lv_obj_set_style_bg_color(gSl[i], lv_palette_main(pal[i]), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(gSl[i], lv_palette_main(pal[i]), LV_PART_KNOB);
        lv_obj_add_event_cb(gSl[i], onSlider, LV_EVENT_VALUE_CHANGED, nullptr);
        ui_no_scroll(gSl[i]);
    }

    card = ui_card(tab, "สีสำเร็จรูป");
    lv_obj_t *row = ui_row(card);
    for (const Preset &p : PRESETS) ui_btn(row, p.name, onPreset, const_cast<Preset *>(&p));

    card = ui_card(tab, "ทดสอบ");
    row = ui_row(card);
    gBtnTest = ui_btn(row, LV_SYMBOL_PLAY " ทดสอบอัตโนมัติ", onTest);
    gBtnRainbow = ui_btn(row, LV_SYMBOL_LOOP " สายรุ้ง", onRainbow);
    gExpect = ui_label(card, "", nullptr, lv_palette_main(LV_PALETTE_AMBER));
    lv_obj_t *note = ui_label(card,
                              "LED เป็นแบบ active-LOW: ขา LOW = ติด\nโปรแกรมกลับค่า duty ให้แล้ว (255 = สว่างสุด)\nถ้าสีที่เห็นไม่ตรง ตรวจขาใน board_config.h",
                              nullptr, lv_palette_lighten(LV_PALETTE_GREY, 1));
    lv_obj_set_width(note, LV_PCT(100));

    uint8_t r, g, b;
    hal::ledGetRgb(r, g, b);
    showRgb(r, g, b);
    lv_timer_create(tick, 20, nullptr);
}
