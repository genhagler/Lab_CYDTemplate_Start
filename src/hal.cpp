#include "hal.h"

#include <esp_system.h>

#include "app_config.h"
#include "board_config.h"

namespace hal {

namespace {
Preferences gPrefs;
esp_reset_reason_t gReset = ESP_RST_UNKNOWN;
}  // namespace

Preferences &prefs()
{
    return gPrefs;
}

void begin()
{
    gReset = esp_reset_reason();
    gPrefs.begin(app::NVS_NAMESPACE, false);
    pinMode(cyd::BOOT_BUTTON_PIN, INPUT_PULLUP);
}

const char *resetReasonText()
{
    switch (gReset) {
        case ESP_RST_POWERON: return "POWER ON";
        case ESP_RST_EXT: return "EXT PIN";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "PANIC (crash)";
        case ESP_RST_INT_WDT: return "INT WATCHDOG";
        case ESP_RST_TASK_WDT: return "TASK WATCHDOG";
        case ESP_RST_WDT: return "WATCHDOG";
        case ESP_RST_BROWNOUT: return "BROWNOUT (power dip)";
        default: return "UNKNOWN";
    }
}

int ldrRaw()
{
    long sum = 0;
    for (int i = 0; i < 8; i++) sum += analogRead(cyd::LDR_PIN);
    return static_cast<int>(sum / 8);
}

}  // namespace hal
