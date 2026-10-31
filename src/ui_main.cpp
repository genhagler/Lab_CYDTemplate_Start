#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  ui_main.cpp -- ธีม + tabview 5 แท็บ + ฟังก์ชันช่วยสร้าง widget
// =====================================================================

namespace {

lv_obj_t *gTabview = nullptr;

void splashDone(lv_timer_t *t)
{
    lv_obj_t *splash = static_cast<lv_obj_t *>(t->user_data);
    lv_obj_del(splash);
    lv_timer_del(t);
    if (!hal::touchCal().valid) ui_calibrate_start(true);  // ยังไม่เคย calibrate -> เข้าหน้า calibrate เอง
}

void showSplash()
{
    lv_obj_t *s = ui_overlay();
    lv_obj_set_style_bg_color(s, lv_color_hex(0x0B1A2E), 0);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(s, "ESP32 CYD Dev Level 2", &font_th_24, lv_palette_main(LV_PALETTE_CYAN));
    ui_label(s, app::APP_NAME);
    ui_label(s, "ตรวจจอ | จอสัมผัส | ไฟ RGB | เสียง", nullptr, lv_palette_lighten(LV_PALETTE_GREY, 2));
    char buf[48];
    snprintf(buf, sizeof(buf), "%s  |  จอ %s", app::FW_VERSION, hal::displayDriverName());
    ui_label(s, buf, nullptr, lv_palette_main(LV_PALETTE_GREY));
    lv_timer_create(splashDone, 1500, s);
}

}  // namespace

void ui_init()
{
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_CYAN), true, &font_th_16);
    lv_disp_set_theme(disp, th);

    gTabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, UI_TAB_H);
    // ปิดการปัดซ้าย-ขวาเพื่อเปลี่ยนแท็บ: ลากแถบเลื่อน/วาดเส้นจะได้ไม่เปลี่ยนแท็บโดยไม่ตั้งใจ (กดที่ชื่อแท็บแทน)
    lv_obj_clear_flag(lv_tabview_get_content(gTabview), LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tabBtns = lv_tabview_get_tab_btns(gTabview);
    lv_obj_set_style_text_font(tabBtns, &font_th_16, 0);
    lv_obj_set_style_pad_all(tabBtns, 0, 0);

    struct TabDef {
        const char *name;
        void (*build)(lv_obj_t *);
    };
    const TabDef tabs[] = {{"จอภาพ", ui_display_tab}, {"สัมผัส", ui_touch_tab}, {"ไฟRGB", ui_led_tab}, {"เสียง", ui_sound_tab}, {"ระบบ", ui_system_tab}};
    for (const TabDef &t : tabs) {
        lv_obj_t *page = lv_tabview_add_tab(gTabview, t.name);
        lv_obj_set_style_pad_all(page, 6, 0);
        lv_obj_set_style_pad_row(page, 6, 0);
        lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_ACTIVE);
        t.build(page);
    }

    showSplash();
}

// ---------------------------------------------------------------------
//  ฟังก์ชันช่วย
// ---------------------------------------------------------------------
lv_obj_t *ui_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    if (title != nullptr) ui_label(card, title, nullptr, lv_palette_main(LV_PALETTE_CYAN));
    return card;
}

lv_obj_t *ui_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_pad_row(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    if (font != nullptr) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

lv_obj_t *ui_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user, lv_event_code_t code)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_set_style_pad_ver(b, 6, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    if (cb != nullptr) lv_obj_add_event_cb(b, cb, code, user);
    return b;
}

lv_obj_t *ui_switch_row(lv_obj_t *parent, const char *text, bool on, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    ui_label(row, text);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 46, 24);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return sw;
}

lv_obj_t *ui_overlay()
{
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, UI_W, UI_H);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_black(), 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *ui_crosshair(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 41, 41);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *h = lv_obj_create(c);  // เส้นนอน
    lv_obj_remove_style_all(h);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(h, color, 0);
    lv_obj_set_size(h, 41, 1);
    lv_obj_set_pos(h, 0, 20);

    lv_obj_t *v = lv_obj_create(c);  // เส้นตั้ง
    lv_obj_remove_style_all(v);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(v, color, 0);
    lv_obj_set_size(v, 1, 41);
    lv_obj_set_pos(v, 20, 0);

    lv_obj_t *ring = lv_obj_create(c);  // วงกลมรอบเป้า
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 21, 21);
    lv_obj_set_pos(ring, 10, 10);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, color, 0);
    for (lv_obj_t *part : {h, v, ring}) lv_obj_clear_flag(part, LV_OBJ_FLAG_CLICKABLE);  // แตะทะลุไปถึงพื้นหลัง
    return c;
}

void ui_crosshair_move(lv_obj_t *cross, int x, int y)
{
    lv_obj_set_pos(cross, x - 20, y - 20);
}

void ui_no_scroll(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
}
