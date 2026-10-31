#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "เสียง": ความดัง, เสียงทดสอบ, เปียโน 8 คีย์, เลือกความถี่เอง
// =====================================================================

namespace {

lv_obj_t *gStatus, *gFreqSlider, *gFreqLabel;

const char *VOL_MAP[] = {"ปิด", "เบา", "กลาง", "ดัง", ""};
const char *PIANO_MAP[] = {"C", "D", "E", "F", "G", "A", "B", "C'", ""};
constexpr uint16_t PIANO_HZ[8] = {523, 587, 659, 698, 784, 880, 988, 1047};  // C5 - C6

void onVolume(lv_event_t *e)
{
    lv_obj_t *m = lv_event_get_target(e);
    const uint16_t id = lv_btnmatrix_get_selected_btn(m);
    if (id == LV_BTNMATRIX_BTN_NONE) return;
    hal::soundSetVolume(static_cast<uint8_t>(id));
    hal::soundPlay(hal::Melody::Success);
}

void onBeep(lv_event_t *)
{
    hal::soundTone(1000, 300);
}
void onScale(lv_event_t *)
{
    hal::soundPlay(hal::Melody::Scale);
}
void onSong(lv_event_t *)
{
    hal::soundPlay(hal::Melody::Song);
}
void onSweep(lv_event_t *)
{
    hal::soundSweep(200, 4000, 2500);
}
void onStop(lv_event_t *)
{
    hal::soundStop();
}

void onPiano(lv_event_t *e)
{
    const uint16_t id = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
    if (id < 8) hal::soundTone(PIANO_HZ[id], 300);
}

void onFreq(lv_event_t *)
{
    const int hz = lv_slider_get_value(gFreqSlider);
    lv_label_set_text_fmt(gFreqLabel, "ความถี่ %d Hz", hz);
    hal::soundTone(static_cast<uint16_t>(hz), 150);  // เล่นสั้นๆ ระหว่างลาก ได้ยินว่าความถี่เปลี่ยน
}

void onFreqPlay(lv_event_t *)
{
    hal::soundTone(static_cast<uint16_t>(lv_slider_get_value(gFreqSlider)), 1000);
}

void tick(lv_timer_t *)
{
    static uint16_t last = 0xFFFF;
    const uint16_t hz = hal::soundBusy() ? hal::soundNowHz() : 0;
    if (hz == last) return;
    last = hz;
    if (hal::soundVolume() == 0) lv_label_set_text(gStatus, LV_SYMBOL_MUTE " ปิดเสียงอยู่");
    else if (hz == 0) lv_label_set_text(gStatus, LV_SYMBOL_VOLUME_MID " พร้อม");
    else lv_label_set_text_fmt(gStatus, LV_SYMBOL_VOLUME_MAX " กำลังเล่น %u Hz", hz);
}

lv_obj_t *matrix(lv_obj_t *parent, const char **map, int height)
{
    lv_obj_t *m = lv_btnmatrix_create(parent);
    lv_btnmatrix_set_map(m, map);
    lv_obj_set_size(m, LV_PCT(100), height);
    lv_obj_set_style_pad_all(m, 3, 0);
    lv_obj_set_style_pad_gap(m, 3, 0);
    ui_no_scroll(m);
    return m;
}

}  // namespace

void ui_sound_tab(lv_obj_t *tab)
{
    lv_obj_t *card = ui_card(tab, "ความดัง (duty ของ PWM)");
    lv_obj_t *vol = matrix(card, VOL_MAP, 40);
    lv_btnmatrix_set_btn_ctrl_all(vol, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(vol, true);
    lv_btnmatrix_set_btn_ctrl(vol, hal::soundVolume(), LV_BTNMATRIX_CTRL_CHECKED);
    lv_obj_add_event_cb(vol, onVolume, LV_EVENT_VALUE_CHANGED, nullptr);
    gStatus = ui_label(card, "", nullptr, lv_palette_main(LV_PALETTE_AMBER));

    card = ui_card(tab, "เสียงทดสอบ");
    lv_obj_t *row = ui_row(card);
    ui_btn(row, "บี๊ป 1 kHz", onBeep);
    ui_btn(row, "ไล่โน้ต", onScale);
    ui_btn(row, "เพลง", onSong);
    ui_btn(row, "กวาดความถี่", onSweep);
    lv_obj_t *stop = ui_btn(row, LV_SYMBOL_STOP " หยุด", onStop);
    lv_obj_set_style_bg_color(stop, lv_palette_main(LV_PALETTE_RED), 0);

    card = ui_card(tab, "เปียโน (C5 - C6)");
    lv_obj_t *piano = matrix(card, PIANO_MAP, 50);
    lv_obj_add_event_cb(piano, onPiano, LV_EVENT_PRESSED, nullptr);

    card = ui_card(tab, "เลือกความถี่เอง");
    gFreqLabel = ui_label(card, "ความถี่ 1000 Hz");
    gFreqSlider = lv_slider_create(card);
    lv_obj_set_width(gFreqSlider, LV_PCT(95));
    lv_slider_set_range(gFreqSlider, 100, 5000);
    lv_slider_set_value(gFreqSlider, 1000, LV_ANIM_OFF);
    lv_obj_add_event_cb(gFreqSlider, onFreq, LV_EVENT_VALUE_CHANGED, nullptr);
    ui_no_scroll(gFreqSlider);
    ui_btn(card, LV_SYMBOL_PLAY " เล่น 1 วินาที", onFreqPlay);
    lv_obj_t *note = ui_label(card, "หูคนได้ยินชัดช่วง 500-4000 Hz  ลำโพงเล็กบนบอร์ดเบาลงมากเมื่อต่ำกว่า 300 Hz", nullptr,
                              lv_palette_lighten(LV_PALETTE_GREY, 1));
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);

    lv_timer_create(tick, 50, nullptr);
    tick(nullptr);
}
