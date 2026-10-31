#include <esp_chip_info.h>

#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "ระบบ": ข้อมูลบอร์ด/หน่วยความจำ/เซนเซอร์แสง และคำสั่ง Serial
// =====================================================================

namespace {

lv_obj_t *gLive;

void tick(lv_timer_t *)
{
    const uint32_t s = millis() / 1000;
    lv_label_set_text_fmt(gLive, "เปิดเครื่องมา %lu:%02lu:%02lu\nheap ว่าง %u KB (ต่ำสุด %u KB)\nแสง LDR ดิบ %d (ยิ่งสว่างยิ่งต่ำ)\nไฟหลังจอ %d%%   เสียง %s",
                          static_cast<unsigned long>(s / 3600), static_cast<unsigned long>((s / 60) % 60), static_cast<unsigned long>(s % 60),
                          static_cast<unsigned>(ESP.getFreeHeap() / 1024), static_cast<unsigned>(ESP.getMinFreeHeap() / 1024), hal::ldrRaw(),
                          hal::backlight(), hal::VOLUME_NAMES[hal::soundVolume()]);
}

}  // namespace

void ui_system_tab(lv_obj_t *tab)
{
    lv_obj_t *card = ui_card(tab, "บอร์ด");
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s %s\nชิป %s rev %d  %d คอร์  %lu MHz\nflash %u MB  โปรแกรม %u KB\nจอ %s 320x240  LVGL %d.%d.%d\nเริ่มทำงานเพราะ: %s",
             app::APP_NAME, app::FW_VERSION, ESP.getChipModel(), chip.revision, chip.cores, static_cast<unsigned long>(ESP.getCpuFreqMHz()),
             static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)), static_cast<unsigned>(ESP.getSketchSize() / 1024), hal::displayDriverName(),
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, hal::resetReasonText());
    ui_label(card, buf);

    card = ui_card(tab, "สถานะ");
    gLive = ui_label(card, "");

    card = ui_card(tab, "คำสั่ง Serial (115200)");
    ui_label(card,
             "h ช่วยเหลือ   i สถานะ\nc calibrate จอสัมผัส   k ค่า calibrate\nd ค่าจอ   r รีเซ็ตค่าจอ\nt พิมพ์ค่าดิบจอสัมผัส (เปิด/ปิด)\nl ทดสอบ LED   s ทดสอบเสียง",
             nullptr, lv_palette_lighten(LV_PALETTE_GREY, 2));
    ui_label(card, "จอเพี้ยนจนใช้ไม่ได้: กดปุ่ม BOOT\nค้าง 1 วินาที = รีเซ็ตค่าจอ", nullptr, lv_palette_main(LV_PALETTE_AMBER));

    lv_timer_create(tick, 1000, nullptr);
    tick(nullptr);
}
