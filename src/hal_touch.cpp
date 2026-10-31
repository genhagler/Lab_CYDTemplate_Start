#include <SPI.h>
#include <math.h>

#include "app_config.h"
#include "board_config.h"
#include "hal.h"

// =====================================================================
//  จอสัมผัส XPT2046 ผ่าน SPI ชุด VSPI (อ่านเอง ไม่ใช้ไลบรารี)
//
//  แรงกด  z = Z1 + 4095 - Z2   (z >= TOUCH_Z_MIN = มีการแตะ)
//  ค่าดิบ  u, v = 0..4095 ต่อแกน (วัด 3 ครั้ง เลือก "สองค่าที่ใกล้กันที่สุด" มาเฉลี่ย ตัดค่าเพี้ยน)
//  แปลงเป็นพิกเซลด้วย affine:  x = a*u + b*v + c,  y = d*u + e*v + f
//    - Lab 9 (Level 1) ใช้ x = a + b*u แยกแกน ต้องตรวจเองว่าแกนสลับไหม
//    - affine มี 6 ค่า: b, d ไม่เป็น 0 เมื่อแกนสลับหรือแผ่นสัมผัสติดเอียง จึงครอบคลุมทุกกรณีในสมการเดียว
//    - หา 6 ค่าจาก 5 จุดด้วย least squares (จุดมากกว่าตัวแปร = เฉลี่ยความผิดพลาดของการแตะแต่ละจุด)
// =====================================================================

namespace hal {

namespace {

SPIClass touchSpi(VSPI);
TouchCal gCal;
TouchState gTouch;
bool gCapture = false;
uint8_t gPressCount = 0;
uint8_t gReleaseCount = 0;
uint32_t gLastPoll = 0;
bool gSmoothReady = false;
float gSmoothX = 0, gSmoothY = 0;

int16_t bestTwoAverage(int16_t a, int16_t b, int16_t c)
{
    const int16_t dab = abs(a - b);
    const int16_t dac = abs(a - c);
    const int16_t dbc = abs(b - c);
    if (dab <= dac && dab <= dbc) return (a + b) >> 1;
    if (dac <= dab && dac <= dbc) return (a + c) >> 1;
    return (b + c) >> 1;
}

// ค่า calibrate เก็บใน NVS เป็นก้อนเดียว (blob)
struct CalBlob {
    uint32_t magic;
    float a, b, c, d, e, f, fitRms, fitMax, verifyErr;
};
constexpr uint32_t CAL_MAGIC = 0x43414C35;  // "CAL5"

void calLoad()
{
    CalBlob blob{};
    // isKey ก่อน: getBytes ของ key ที่ยังไม่มีจะพิมพ์ error ของ NVS ออก Serial (ไม่อันตราย แต่ทำให้ตกใจ)
    if (prefs().isKey("cal") && prefs().getBytes("cal", &blob, sizeof(blob)) == sizeof(blob) && blob.magic == CAL_MAGIC) {
        gCal.valid = true;
        gCal.a = blob.a;
        gCal.b = blob.b;
        gCal.c = blob.c;
        gCal.d = blob.d;
        gCal.e = blob.e;
        gCal.f = blob.f;
        gCal.fitRms = blob.fitRms;
        gCal.fitMax = blob.fitMax;
        gCal.verifyErr = blob.verifyErr;
        Serial.printf("[CAL] loaded  x = %.5f*u %+.5f*v %+.1f   y = %.5f*u %+.5f*v %+.1f\n", gCal.a, gCal.b, gCal.c, gCal.d, gCal.e, gCal.f);
    } else {
        Serial.println("[CAL] no saved calibration: the calibration screen will start");
    }
}

void calSave()
{
    if (!gCal.valid) {
        prefs().remove("cal");
        return;
    }
    const CalBlob blob{CAL_MAGIC, gCal.a, gCal.b, gCal.c, gCal.d, gCal.e, gCal.f, gCal.fitRms, gCal.fitMax, gCal.verifyErr};
    prefs().putBytes("cal", &blob, sizeof(blob));
    Serial.println("[CAL] saved");
}

}  // namespace

void touchBegin()
{
    pinMode(cyd::TOUCH_PIN_CS, OUTPUT);
    digitalWrite(cyd::TOUCH_PIN_CS, HIGH);
    touchSpi.begin(cyd::TOUCH_PIN_CLK, cyd::TOUCH_PIN_MISO, cyd::TOUCH_PIN_MOSI, cyd::TOUCH_PIN_CS);
    calLoad();
}

bool touchReadRaw(RawTouch &out)
{
    int16_t data[6] = {0, 0, 0, 0, 0, 0};

    touchSpi.beginTransaction(SPISettings(cyd::TOUCH_SPI_FREQUENCY, MSBFIRST, SPI_MODE0));
    digitalWrite(cyd::TOUCH_PIN_CS, LOW);
    touchSpi.transfer(0xB1);  // เริ่มวัด Z1
    const int16_t z1 = static_cast<int16_t>(touchSpi.transfer16(0xC1) >> 3);  // ได้ Z1, เริ่มวัด Z2
    int z = z1 + 4095;
    const int16_t z2 = static_cast<int16_t>(touchSpi.transfer16(0x91) >> 3);  // ได้ Z2, เริ่มวัดแกนแรก
    z -= z2;
    if (z >= app::TOUCH_Z_MIN) {
        touchSpi.transfer16(0x91);  // ค่าแรกหลังเปลี่ยนแกนมักเพี้ยน ทิ้งไป
        data[0] = static_cast<int16_t>(touchSpi.transfer16(0xD1) >> 3);
        data[1] = static_cast<int16_t>(touchSpi.transfer16(0x91) >> 3);
        data[2] = static_cast<int16_t>(touchSpi.transfer16(0xD1) >> 3);
        data[3] = static_cast<int16_t>(touchSpi.transfer16(0x91) >> 3);
    }
    data[4] = static_cast<int16_t>(touchSpi.transfer16(0xD0) >> 3);  // วัดครั้งสุดท้าย + สั่งพัก
    data[5] = static_cast<int16_t>(touchSpi.transfer16(0) >> 3);
    digitalWrite(cyd::TOUCH_PIN_CS, HIGH);
    touchSpi.endTransaction();

    out.z = static_cast<int16_t>(z < 0 ? 0 : z);
    if (z < app::TOUCH_Z_MIN) return false;
    out.x = bestTwoAverage(data[0], data[2], data[4]);
    out.y = bestTwoAverage(data[1], data[3], data[5]);
    return true;
}

void touchMap(const TouchCal &c, float u, float v, float &x, float &y)
{
    x = c.a * u + c.b * v + c.c;
    y = c.d * u + c.e * v + c.f;
}

void touchPoll(uint32_t now)
{
    if (gCapture || now - gLastPoll < app::TOUCH_POLL_MS) return;
    gLastPoll = now;

    RawTouch raw;
    const bool touching = touchReadRaw(raw);
    gTouch.z = raw.z;
    if (touching) {
        gTouch.rawX = raw.x;
        gTouch.rawY = raw.y;
        if (gPressCount < 255) gPressCount++;
        gReleaseCount = 0;
    } else {
        if (gReleaseCount < 255) gReleaseCount++;
        gPressCount = 0;
    }

    if (!gTouch.down && gPressCount >= app::TOUCH_PRESS_SAMPLES) {
        gTouch.down = true;
        gSmoothReady = false;
    }

    if (gTouch.down && touching && gCal.valid) {
        float sx, sy;
        touchMap(gCal, raw.x, raw.y, sx, sy);
        if (!gSmoothReady) {
            gSmoothX = sx;
            gSmoothY = sy;
            gSmoothReady = true;
        } else {  // EMA: ค่าใหม่ = ค่าเดิม + alpha * (ค่าที่วัด - ค่าเดิม)
            gSmoothX += app::TOUCH_SMOOTH_ALPHA * (sx - gSmoothX);
            gSmoothY += app::TOUCH_SMOOTH_ALPHA * (sy - gSmoothY);
        }
        gTouch.x = static_cast<int16_t>(constrain(lroundf(gSmoothX), 0, cyd::DISPLAY_WIDTH - 1));
        gTouch.y = static_cast<int16_t>(constrain(lroundf(gSmoothY), 0, cyd::DISPLAY_HEIGHT - 1));
    }

    if (gTouch.down && gReleaseCount >= app::TOUCH_RELEASE_SAMPLES) gTouch.down = false;
}

const TouchState &touch()
{
    return gTouch;
}

void touchCapture(bool on)
{
    gCapture = on;
    if (!on) {  // คืนให้ LVGL: เริ่มนับใหม่ ไม่ให้ค้างสถานะ "กดอยู่"
        gTouch.down = false;
        gPressCount = 0;
        gReleaseCount = 0;
        gSmoothReady = false;
        gLastPoll = millis();
    }
}

bool touchCaptured()
{
    return gCapture;
}

const TouchCal &touchCal()
{
    return gCal;
}

void touchCalSet(const TouchCal &c, bool save)
{
    gCal = c;
    if (save) calSave();
}

bool touchCalFit(const float *u, const float *v, const float *x, const float *y, int n, TouchCal &out)
{
    if (n < 3) return false;

    // ลบค่าเฉลี่ยออกก่อน (centering) ตัวเลขจะเล็กลง สมการคำนวณแม่นขึ้น และแยกหา c, f ได้ง่าย
    double mu = 0, mv = 0, mx = 0, my = 0;
    float uMin = u[0], uMax = u[0], vMin = v[0], vMax = v[0];
    for (int i = 0; i < n; i++) {
        mu += u[i];
        mv += v[i];
        mx += x[i];
        my += y[i];
        uMin = fminf(uMin, u[i]);
        uMax = fmaxf(uMax, u[i]);
        vMin = fminf(vMin, v[i]);
        vMax = fmaxf(vMax, v[i]);
    }
    mu /= n;
    mv /= n;
    mx /= n;
    my /= n;
    if (uMax - uMin < app::CAL_MIN_SPAN || vMax - vMin < app::CAL_MIN_SPAN) {
        Serial.printf("[CAL] raw span too small (u=%.0f v=%.0f): touched the wrong spot?\n", uMax - uMin, vMax - vMin);
        return false;
    }

    // สมการปกติ (normal equations) ของ least squares:
    //   [Suu Suv] [a]   [Sxu]        [Suu Suv] [d]   [Syu]
    //   [Suv Svv] [b] = [Sxv]   และ  [Suv Svv] [e] = [Syv]
    double Suu = 0, Suv = 0, Svv = 0, Sxu = 0, Sxv = 0, Syu = 0, Syv = 0;
    for (int i = 0; i < n; i++) {
        const double du = u[i] - mu, dv = v[i] - mv, dx = x[i] - mx, dy = y[i] - my;
        Suu += du * du;
        Suv += du * dv;
        Svv += dv * dv;
        Sxu += dx * du;
        Sxv += dx * dv;
        Syu += dy * du;
        Syv += dy * dv;
    }
    const double det = Suu * Svv - Suv * Suv;
    if (fabs(det) < 1e-6 * Suu * Svv) {  // จุดเรียงเป็นเส้นตรงเดียวกัน แก้สมการไม่ได้
        Serial.println("[CAL] points are collinear");
        return false;
    }
    TouchCal c;
    c.a = static_cast<float>((Sxu * Svv - Sxv * Suv) / det);
    c.b = static_cast<float>((Sxv * Suu - Sxu * Suv) / det);
    c.d = static_cast<float>((Syu * Svv - Syv * Suv) / det);
    c.e = static_cast<float>((Syv * Suu - Syu * Suv) / det);
    c.c = static_cast<float>(mx - c.a * mu - c.b * mv);
    c.f = static_cast<float>(my - c.d * mu - c.e * mv);

    // ความคลาดเคลื่อนของแต่ละจุดหลัง fit (ระยะทางเป็นพิกเซล)
    double sum2 = 0;
    float worst = 0;
    for (int i = 0; i < n; i++) {
        float px, py;
        touchMap(c, u[i], v[i], px, py);
        const float err = hypotf(px - x[i], py - y[i]);
        sum2 += err * err;
        worst = fmaxf(worst, err);
    }
    c.fitRms = static_cast<float>(sqrt(sum2 / n));
    c.fitMax = worst;
    c.valid = true;
    out = c;
    Serial.printf("[CAL] fit  x = %.5f*u %+.5f*v %+.1f   y = %.5f*u %+.5f*v %+.1f   rms %.2f px  max %.2f px\n", c.a, c.b, c.c, c.d, c.e,
                  c.f, c.fitRms, c.fitMax);
    return true;
}

void touchCalMirror(bool h, bool v)
{
    if (!gCal.valid) return;
    if (h) {  // x' = (W-1) - x
        gCal.a = -gCal.a;
        gCal.b = -gCal.b;
        gCal.c = (cyd::DISPLAY_WIDTH - 1) - gCal.c;
    }
    if (v) {  // y' = (H-1) - y
        gCal.d = -gCal.d;
        gCal.e = -gCal.e;
        gCal.f = (cyd::DISPLAY_HEIGHT - 1) - gCal.f;
    }
    calSave();
    Serial.printf("[CAL] mirrored to follow display (h=%d v=%d)\n", h, v);
}

}  // namespace hal
