#include <Wire.h>

#include "app_config.h"
#include "board_config.h"
#include "hal.h"

// =====================================================================
//  hal_io.cpp -- PCF8574 (I2C 0x20)
//    P0-P3 : LED แบบ sink  (เขียน 0 = ติด)
//    P4-P7 : สวิตช์ + pull-up 10k (กด = 0)  ต้องเขียน 1 ไว้เสมอเพื่อให้ขาเป็นอินพุต (quasi-bidirectional)
// =====================================================================

namespace hal {

namespace {

uint8_t gLeds = 0;      // bit i = LED i ติด
uint8_t gSwitches = 0;  // bit i = สวิตช์ i ถูกกด
uint8_t gRaw = 0xFF;
bool gOnline = false;
uint32_t gNextAt = 0;

bool writeLeds()
{
    const uint8_t out = 0xF0 | (~gLeds & 0x0F);
    Wire.beginTransmission(cyd::PCF8574_ADDR);
    Wire.write(out);
    return Wire.endTransmission() == 0;
}

void setOnline(bool on)
{
    if (on == gOnline) return;
    gOnline = on;
    if (!on) gSwitches = 0;
    Serial.printf("[IO] PCF8574 0x%02X %s\n", cyd::PCF8574_ADDR, on ? "ONLINE" : "OFFLINE");
}

}  // namespace

void ioBegin()
{
    Wire.begin(cyd::I2C_PIN_SDA, cyd::I2C_PIN_SCL, app::IO_I2C_FREQ_HZ);
    Wire.setTimeOut(20);  // ms กัน UI ค้างเมื่อบัสมีปัญหา
    setOnline(writeLeds());
}

void ioPoll(uint32_t now)
{
    if (!gOnline && now < gNextAt) return;
    if (!gOnline) {  // ลองต่อใหม่: เขียนสถานะ LED กลับไปด้วย
        if (!writeLeds()) {
            gNextAt = now + app::IO_RETRY_MS;
            return;
        }
        setOnline(true);
    }
    if (Wire.requestFrom(cyd::PCF8574_ADDR, static_cast<uint8_t>(1)) != 1) {
        setOnline(false);
        gNextAt = now + app::IO_RETRY_MS;
        return;
    }
    gRaw = Wire.read();
    gSwitches = (~(gRaw >> 4)) & 0x0F;
}

bool ioOnline() { return gOnline; }

void ioSetLeds(uint8_t mask)
{
    gLeds = mask & 0x0F;
    if (gOnline && !writeLeds()) {
        setOnline(false);
        gNextAt = millis() + app::IO_RETRY_MS;
    }
}

uint8_t ioLeds() { return gLeds; }
uint8_t ioSwitches() { return gSwitches; }
uint8_t ioRaw() { return gRaw; }

}  // namespace hal
