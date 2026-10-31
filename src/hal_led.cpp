#include "app_config.h"
#include "board_config.h"
#include "hal.h"

// =====================================================================
//  RGB LED บนบอร์ด: PWM (LEDC) 8 บิต สามช่อง
//  LED เป็น active-LOW (ขา LOW = ติด) จึงต้องกลับค่า: duty ที่เขียน = 255 - ความสว่างที่ต้องการ
// =====================================================================

namespace hal {

namespace {
uint8_t gR = 0, gG = 0, gB = 0;

uint32_t duty(uint8_t v)
{
    return cyd::LED_ACTIVE_LOW ? 255 - v : v;
}
}  // namespace

void ledBegin()
{
    const uint8_t pins[3] = {cyd::LED_PIN_R, cyd::LED_PIN_G, cyd::LED_PIN_B};
    const uint8_t chs[3] = {app::LED_CH_R, app::LED_CH_G, app::LED_CH_B};
    for (int i = 0; i < 3; i++) {
        ledcSetup(chs[i], app::LED_PWM_FREQ_HZ, 8);
        ledcAttachPin(pins[i], chs[i]);
    }
    ledSetRgb(0, 0, 0);
}

void ledSetRgb(uint8_t r, uint8_t g, uint8_t b)
{
    gR = r;
    gG = g;
    gB = b;
    ledcWrite(app::LED_CH_R, duty(r));
    ledcWrite(app::LED_CH_G, duty(g));
    ledcWrite(app::LED_CH_B, duty(b));
}

void ledGetRgb(uint8_t &r, uint8_t &g, uint8_t &b)
{
    r = gR;
    g = gG;
    b = gB;
}

}  // namespace hal
