#include "app_config.h"
#include "board_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "I/O": ควบคุม LED0-3 (P0-P3) และอ่านสวิตช์ SW0-3 (P4-P7) ผ่าน PCF8574
//    - แตะ LED บนจอ = สลับ LED จริง (มี animation เรืองแสง + เสียง)
//    - กดสวิตช์จริง = ป้ายสวิตช์สว่าง + คลื่นกระจาย + เสียง (โหมดปกติ: สลับ LED ตามหมายเลข)
//    - โหมดอนิเมชัน: วิ่งไป-กลับ / นับเลขฐาน 2 / กะพริบ / ตามสวิตช์
// =====================================================================

namespace {

enum class Mode : uint8_t { Manual, Chase, Counter, Blink, Mirror };

// ตัวแปรอนิเมชัน 0..255 (อาจเกิน 255 เล็กน้อยจาก overshoot) ของ LED / ป้ายสวิตช์ 1 ดวง
struct Glow {
    lv_obj_t *obj = nullptr;
    lv_color_t color;
    int32_t v = 0;
    int shadow = 0;     // ความกว้างเงาเรืองแสงสูงสุด (px)
    lv_obj_t *fil = nullptr;  // ไส้หลอด (lv_line) ถ้ามี: เทา -> ขาวอมเหลืองตามความสว่าง
    uint32_t offBg = 0x232834;      // สีตอนดับ (ป้ายสวิตช์ใช้ค่านี้ หลอดไฟตั้งค่าเอง)
    uint32_t offBorder = 0x3A4150;
};

constexpr int N = 4;
constexpr uint32_t LED_COLORS[N] = {0xFF5252, 0xFFB300, 0x39E58C, 0x38BDF8};
constexpr uint32_t BULB_OFF_BG = 0x2E3542;      // กระจกหลอดตอนยังไม่เปล่งแสง
constexpr uint32_t BULB_OFF_BORDER = 0x7A8494;  // ขอบแก้ว
constexpr uint32_t FIL_OFF = 0x7D8794;
constexpr uint32_t FIL_ON = 0xFFF6D5;

Glow gLed[N], gSw[N];
lv_obj_t *gLedState[N], *gSwState[N];
lv_obj_t *gStatusDot, *gStatusText, *gSpeedLabel;
lv_obj_t *gBtn[5];  // ดัชนี = Mode (Manual ไม่มีปุ่ม)

Mode gMode = Mode::Manual;
uint8_t gMask = 0, gSwPrev = 0;
bool gFirstTick = true;
uint16_t gStepMs = app::IO_STEP_DEFAULT_MS;
uint32_t gStepAt = 0;
int gStep = 0;
int gLastRaw = -1;
bool gLastOnline = false;

void applyGlow(Glow &g)
{
    const int b = LV_CLAMP(0, g.v, 255);
    lv_obj_set_style_bg_color(g.obj, lv_color_mix(g.color, lv_color_hex(g.offBg), b), 0);
    lv_obj_set_style_border_color(g.obj, lv_color_mix(lv_color_white(), lv_color_hex(g.offBorder), b), 0);
    if (g.fil != nullptr) lv_obj_set_style_line_color(g.fil, lv_color_mix(lv_color_hex(FIL_ON), lv_color_hex(FIL_OFF), b), 0);
    const int w = b * g.shadow / 255;
    lv_obj_set_style_shadow_width(g.obj, w, 0);
    lv_obj_set_style_shadow_spread(g.obj, w / 4, 0);
    lv_obj_set_style_shadow_color(g.obj, g.color, 0);
    lv_obj_set_style_shadow_opa(g.obj, static_cast<lv_opa_t>(b * 200 / 255), 0);
}

void glowExec(void *var, int32_t v)
{
    Glow *g = static_cast<Glow *>(var);
    g->v = v;
    applyGlow(*g);
}

void glowTo(Glow &g, bool on, uint32_t ms)
{
    lv_anim_del(&g, glowExec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &g);
    lv_anim_set_exec_cb(&a, glowExec);
    lv_anim_set_values(&a, g.v, on ? 255 : 0);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, on ? lv_anim_path_overshoot : lv_anim_path_ease_out);
    lv_anim_start(&a);
}

// คลื่นวงกลมกระจายออกจากกลางป้ายสวิตช์ แล้วจางหายและลบตัวเอง
void rippleExec(void *var, int32_t s)
{
    lv_obj_t *o = static_cast<lv_obj_t *>(var);
    lv_obj_set_size(o, s, s);
    lv_obj_center(o);
    lv_obj_set_style_border_opa(o, static_cast<lv_opa_t>((70 - s) * 255 / 60), 0);
}

void rippleDone(lv_anim_t *a)
{
    lv_obj_del(static_cast<lv_obj_t *>(a->var));
}

void ripple(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(o, 3, 0);
    lv_obj_set_style_border_color(o, color, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, rippleExec);
    lv_anim_set_ready_cb(&a, rippleDone);
    lv_anim_set_values(&a, 12, 70);
    lv_anim_set_time(&a, 450);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void pulseExec(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(var), static_cast<lv_opa_t>(v), 0);
}

// ---------------------------------------------------------------------
//  ตรรกะ LED
// ---------------------------------------------------------------------
void setLeds(uint8_t mask)
{
    mask &= 0x0F;
    const uint8_t changed = mask ^ gMask;
    gMask = mask;
    hal::ioSetLeds(mask);
    const uint32_t ms = gMode == Mode::Manual ? 280 : (gStepMs < 220 ? gStepMs : 220);
    for (int i = 0; i < N; i++) {
        if (!((changed >> i) & 1)) continue;
        const bool on = (mask >> i) & 1;
        glowTo(gLed[i], on, ms);
        lv_label_set_text(gLedState[i], on ? "ON" : "OFF");
        lv_obj_set_style_text_color(gLedState[i], on ? lv_color_hex(LED_COLORS[i]) : lv_palette_main(LV_PALETTE_GREY), 0);
    }
}

void setMode(Mode m)
{
    gMode = m;
    for (int i = 1; i < 5; i++) {
        if (static_cast<int>(m) == i) lv_obj_add_state(gBtn[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(gBtn[i], LV_STATE_CHECKED);
    }
    gStep = 0;
    gStepAt = 0;
}

void onLed(lv_event_t *e)
{
    const int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    setMode(Mode::Manual);
    setLeds(gMask ^ (1 << i));
    const bool on = (gMask >> i) & 1;
    hal::soundTone(on ? 700 + i * 150 : 380, 70);
}

void onMode(lv_event_t *e)
{
    const Mode m = static_cast<Mode>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    setMode(gMode == m ? Mode::Manual : m);  // กดซ้ำ = ออกจากโหมด
    setLeds(0);
    hal::soundTone(gMode == Mode::Manual ? 330 : 990, 80);
}

void onAll(lv_event_t *e)
{
    const bool on = reinterpret_cast<intptr_t>(lv_event_get_user_data(e)) != 0;
    setMode(Mode::Manual);
    setLeds(on ? 0x0F : 0x00);
    hal::soundPlay(on ? hal::Melody::Success : hal::Melody::Error);
}

void onSpeed(lv_event_t *e)
{
    gStepMs = lv_slider_get_value(lv_event_get_target(e));
    lv_label_set_text_fmt(gSpeedLabel, "ความเร็วอนิเมชัน  %u ms/ก้าว", gStepMs);
}

// ---------------------------------------------------------------------
//  ตัวจับเวลา: อ่านสวิตช์ + สถานะชิป + ขับโหมดอนิเมชัน
// ---------------------------------------------------------------------
void updateStatus()
{
    const bool online = hal::ioOnline();
    const int raw = online ? hal::ioRaw() : -1;
    if (online == gLastOnline && raw == gLastRaw) return;
    gLastOnline = online;
    gLastRaw = raw;
    lv_obj_set_style_bg_color(gStatusDot, online ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED), 0);
    if (online) lv_label_set_text_fmt(gStatusText, "PCF8574 @0x%02X  ออนไลน์  อ่านได้ 0x%02X", cyd::PCF8574_ADDR, raw);
    else lv_label_set_text_fmt(gStatusText, "ไม่พบ PCF8574 @0x%02X  (SDA %d SCL %d)", cyd::PCF8574_ADDR, cyd::I2C_PIN_SDA, cyd::I2C_PIN_SCL);
}

void onSwitchEdge(int i, bool pressed)
{
    glowTo(gSw[i], pressed, 120);
    lv_label_set_text(gSwState[i], pressed ? "กด" : "ปล่อย");
    lv_obj_set_style_text_color(gSwState[i], pressed ? lv_color_white() : lv_palette_main(LV_PALETTE_GREY), 0);
    Serial.printf("[IO] SW%d %s\n", i, pressed ? "pressed" : "released");
    if (!pressed) return;
    ripple(gSw[i].obj, gSw[i].color);
    hal::soundTone(520 + i * 170, 60);
    if (gMode == Mode::Manual) setLeds(gMask ^ (1 << i));
}

void tick(lv_timer_t *)
{
    const uint32_t now = millis();
    hal::ioPoll(now);
    updateStatus();

    const uint8_t sw = hal::ioSwitches();
    if (gFirstTick) {  // สวิตช์ที่ค้างอยู่ตอนเปิดเครื่องไม่นับเป็นการกด
        gFirstTick = false;
        gSwPrev = sw;
    }
    const uint8_t changed = sw ^ gSwPrev;
    gSwPrev = sw;
    for (int i = 0; i < N; i++)
        if ((changed >> i) & 1) onSwitchEdge(i, (sw >> i) & 1);

    if (gMode == Mode::Mirror) {
        if (sw != gMask) setLeds(sw);
    } else if (gMode != Mode::Manual) {
        if (gStepAt != 0 && now - gStepAt < gStepMs) return;
        gStepAt = now;
        static const uint8_t chase[] = {0, 1, 2, 3, 2, 1};  // ไป-กลับ
        switch (gMode) {
            case Mode::Chase: setLeds(1 << chase[gStep % 6]); break;
            case Mode::Counter: setLeds(gStep & 0x0F); break;
            case Mode::Blink: setLeds((gStep & 1) ? 0x00 : 0x0F); break;
            default: break;
        }
        gStep++;
    }
}

// ---------------------------------------------------------------------
//  สร้างหน้าจอ
// ---------------------------------------------------------------------
lv_obj_t *cell(lv_obj_t *row)
{
    lv_obj_t *c = lv_obj_create(row);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 66, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, 3, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

lv_obj_t *evenRow(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

lv_obj_t *modeBtn(lv_obj_t *row, const char *text, Mode m)
{
    lv_obj_t *b = ui_btn(row, text, onMode, reinterpret_cast<void *>(static_cast<intptr_t>(m)));
    gBtn[static_cast<int>(m)] = b;
    return b;
}

}  // namespace

void ui_io_tab(lv_obj_t *tab)
{
    // ---- สถานะชิป ----
    lv_obj_t *card = ui_card(tab, nullptr);
    lv_obj_t *st = ui_row(card);
    gStatusDot = lv_obj_create(st);
    lv_obj_remove_style_all(gStatusDot);
    lv_obj_set_size(gStatusDot, 12, 12);
    lv_obj_set_style_radius(gStatusDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(gStatusDot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(gStatusDot, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_anim_t pa;  // จุดสถานะเต้นเป็นจังหวะ
    lv_anim_init(&pa);
    lv_anim_set_var(&pa, gStatusDot);
    lv_anim_set_exec_cb(&pa, pulseExec);
    lv_anim_set_values(&pa, 255, 70);
    lv_anim_set_time(&pa, 700);
    lv_anim_set_playback_time(&pa, 700);
    lv_anim_set_repeat_count(&pa, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&pa);
    gStatusText = ui_label(st, "กำลังตรวจสอบ...", nullptr, lv_palette_lighten(LV_PALETTE_GREY, 2));

    // ---- LED ----
    card = ui_card(tab, "LED0-3 (P0-P3)  แตะเพื่อสลับ");
    lv_obj_t *row = evenRow(card);
    for (int i = 0; i < N; i++) {
        lv_obj_t *c = cell(row);
        // ---- หลอดไฟ: ตัวรับแตะ (56x68) ประกอบด้วย คอหลอด / กระจก / ไส้หลอด / แสงสะท้อน / เกลียว ----
        lv_obj_t *bulb = lv_obj_create(c);
        lv_obj_remove_style_all(bulb);
        lv_obj_set_size(bulb, 56, 68);
        lv_obj_add_flag(bulb, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(bulb, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(bulb, onLed, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));

        auto part = [bulb](int w, int h, int x, int y, uint32_t color, int radius) {
            lv_obj_t *o = lv_obj_create(bulb);
            lv_obj_remove_style_all(o);
            lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);  // ให้การแตะไปถึง bulb
            lv_obj_set_size(o, w, h);
            lv_obj_set_pos(o, x, y);
            lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
            lv_obj_set_style_radius(o, radius, 0);
            return o;
        };
        part(22, 10, 17, 42, 0x6C7686, 3);                           // คอหลอด (อยู่หลังกระจก)
        lv_obj_t *glass = part(46, 46, 5, 0, BULB_OFF_BG, LV_RADIUS_CIRCLE);  // กระจก
        lv_obj_set_style_border_width(glass, 2, 0);

        static const lv_point_t FIL_PTS[] = {{0, 26}, {0, 14}, {5, 7}, {9, 14}, {13, 7}, {17, 14}, {22, 7}, {22, 14}, {22, 26}};
        lv_obj_t *fil = lv_line_create(bulb);
        lv_obj_clear_flag(fil, LV_OBJ_FLAG_CLICKABLE);
        lv_line_set_points(fil, FIL_PTS, sizeof(FIL_PTS) / sizeof(FIL_PTS[0]));
        lv_obj_set_pos(fil, 17, 16);
        lv_obj_set_style_line_width(fil, 2, 0);
        lv_obj_set_style_line_rounded(fil, true, 0);

        lv_obj_t *shine = part(8, 15, 13, 8, 0xFFFFFF, LV_RADIUS_CIRCLE);  // แสงสะท้อนบนกระจก
        lv_obj_set_style_bg_opa(shine, LV_OPA_40, 0);
        part(28, 7, 14, 51, 0xA3ACB8, 2);  // เกลียวหลอด
        part(28, 7, 14, 58, 0x7B8591, 2);
        part(12, 4, 22, 64, 0x4A5260, 2);  // ปลายขั้ว

        gLed[i].obj = glass;
        gLed[i].fil = fil;
        gLed[i].color = lv_color_hex(LED_COLORS[i]);
        gLed[i].shadow = 26;
        gLed[i].offBg = BULB_OFF_BG;
        gLed[i].offBorder = BULB_OFF_BORDER;
        gLed[i].v = 0;
        applyGlow(gLed[i]);
        char name[8];
        snprintf(name, sizeof(name), "LED%d", i);
        ui_label(c, name);
        gLedState[i] = ui_label(c, "OFF", nullptr, lv_palette_main(LV_PALETTE_GREY));
    }
    row = ui_row(card);
    ui_btn(row, LV_SYMBOL_POWER " ติดทั้งหมด", onAll, reinterpret_cast<void *>(1));
    ui_btn(row, LV_SYMBOL_CLOSE " ดับทั้งหมด", onAll, reinterpret_cast<void *>(0));

    // ---- สวิตช์ ----
    card = ui_card(tab, "SW0-3 (P4-P7)  กดปุ่มจริงบนบอร์ดต่อ");
    row = evenRow(card);
    for (int i = 0; i < N; i++) {
        lv_obj_t *p = lv_obj_create(row);
        lv_obj_remove_style_all(p);
        lv_obj_set_size(p, 64, 52);
        lv_obj_set_style_radius(p, 12, 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(p, 2, 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        gSw[i].obj = p;
        gSw[i].color = lv_color_hex(0x38BDF8);
        gSw[i].shadow = 14;
        gSw[i].v = 0;
        applyGlow(gSw[i]);
        char name[8];
        snprintf(name, sizeof(name), "SW%d", i);
        lv_obj_t *l = ui_label(p, name);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 4);
        gSwState[i] = ui_label(p, "ปล่อย", nullptr, lv_palette_main(LV_PALETTE_GREY));
        lv_obj_align(gSwState[i], LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    // ---- โหมดอนิเมชัน ----
    card = ui_card(tab, "โหมดอนิเมชัน (กดซ้ำ = หยุด)");
    row = ui_row(card);
    modeBtn(row, LV_SYMBOL_RIGHT " วิ่ง", Mode::Chase);
    modeBtn(row, LV_SYMBOL_PLUS " นับเลขฐาน 2", Mode::Counter);
    modeBtn(row, LV_SYMBOL_EYE_OPEN " กะพริบ", Mode::Blink);
    modeBtn(row, LV_SYMBOL_SHUFFLE " ตามสวิตช์", Mode::Mirror);
    gSpeedLabel = ui_label(card, "");
    lv_obj_t *sl = lv_slider_create(card);
    lv_obj_set_width(sl, LV_PCT(95));
    lv_slider_set_range(sl, 60, 600);
    lv_slider_set_value(sl, gStepMs, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl, onSpeed, LV_EVENT_VALUE_CHANGED, nullptr);
    ui_no_scroll(sl);
    lv_label_set_text_fmt(gSpeedLabel, "ความเร็วอนิเมชัน  %u ms/ก้าว", gStepMs);

    lv_obj_t *note = ui_label(card,
                              "LED เป็นแบบ sink: ขา P = LOW จึงติด\nสวิตช์ต่อ pull-up 10k: กด = LOW\nโหมดปกติ: กดสวิตช์ SWn = สลับ LEDn",
                              nullptr, lv_palette_lighten(LV_PALETTE_GREY, 1));
    lv_obj_set_width(note, LV_PCT(100));

    updateStatus();
    lv_timer_create(tick, app::IO_POLL_MS, nullptr);
}
