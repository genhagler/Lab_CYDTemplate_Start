#include "app_config.h"
#include "board_config.h"
#include "hal.h"

// =====================================================================
//  เสียง: ลำโพง GPIO26 ขับด้วย PWM (LEDC) คลื่นสี่เหลี่ยม
//    ความถี่ของ PWM = ระดับเสียง (Hz)   duty = ความดัง (50% ดังสุด, ยิ่งน้อยยิ่งเบา)
//  คำสั่งเข้าคิว แล้ว soundTask (core 0) เล่นตามจังหวะด้วย vTaskDelay จึงไม่หน่วง UI
//  เสียงที่สำคัญกว่าหรือเท่ากันตัดเสียงที่กำลังเล่น ส่วนที่สำคัญน้อยกว่า (เสียงติ๊ก) ระหว่างเล่นจะถูกทิ้ง
// =====================================================================

namespace hal {

namespace {

struct Note {
    uint16_t hz;  // 0 = เงียบ
    uint16_t ms;
};

// ความถี่ตัวโน้ต (Hz): C5 523, D5 587, E5 659, F5 698, G5 784, A5 880, B5 988, C6 1047, E6 1319, G6 1568, C7 2093
constexpr Note M_STARTUP[] = {{1047, 70}, {0, 20}, {1319, 70}, {0, 20}, {1568, 70}, {0, 20}, {2093, 160}};
constexpr Note M_SUCCESS[] = {{1568, 70}, {0, 30}, {2093, 140}};
constexpr Note M_ERROR[] = {{392, 180}, {0, 40}, {262, 320}};
constexpr Note M_SCALE[] = {{523, 200}, {587, 200}, {659, 200}, {698, 200}, {784, 200}, {880, 200}, {988, 200}, {1047, 400}};
// "ช้างๆๆ" (เพลงพื้นบ้าน) E E E . G E D . E E E . G E D
constexpr Note M_SONG[] = {{659, 250}, {0, 30}, {659, 250}, {0, 30}, {659, 420}, {0, 80}, {784, 250}, {659, 250}, {587, 500}, {0, 120},
                           {659, 250}, {0, 30}, {659, 250}, {0, 30}, {659, 420}, {0, 80}, {784, 250}, {659, 250}, {587, 600}};

struct MelodyDef {
    const Note *notes;
    uint8_t count;
};
#define MEL(n) {n, sizeof(n) / sizeof(Note)}
constexpr MelodyDef MELODIES[] = {MEL(M_STARTUP), MEL(M_SUCCESS), MEL(M_ERROR), MEL(M_SCALE), MEL(M_SONG)};
#undef MEL
static_assert(sizeof(MELODIES) / sizeof(MELODIES[0]) == static_cast<size_t>(Melody::Count), "MELODIES must match enum Melody");

enum Kind : uint8_t { K_STOP = 0, K_CLICK = 1, K_TONE = 2, K_MELODY = 3, K_SWEEP = 4 };
struct Cmd {
    uint8_t kind;
    uint8_t id;
    uint16_t hz;
    uint16_t hz2;
    uint16_t ms;
};

uint8_t prioOf(uint8_t kind)
{
    switch (kind) {
        case K_STOP: return 6;
        case K_CLICK: return 1;
        default: return 3;
    }
}

QueueHandle_t gQueue = nullptr;
volatile uint8_t gVolume = app::VOLUME_DEFAULT;
volatile bool gPlaying = false;
volatile uint16_t gNowHz = 0;

void tone(uint16_t hz)
{
    gNowHz = hz;
    if (hz == 0 || gVolume == 0) {
        ledcWrite(app::AUDIO_CHANNEL, 0);
        return;
    }
    ledcChangeFrequency(app::AUDIO_CHANNEL, hz, app::AUDIO_BITS);
    ledcWrite(app::AUDIO_CHANNEL, app::VOLUME_DUTY[gVolume]);
}

// รอ ms มิลลิวินาที แต่ถ้ามีคำสั่งที่สำคัญพอเข้ามา ให้หยุดรอทันที (คืน true + เก็บคำสั่งไว้ใน next)
bool waitOrInterrupt(uint32_t ms, uint8_t curKind, Cmd &next)
{
    const TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
    for (TickType_t t = xTaskGetTickCount(); t < end; t = xTaskGetTickCount()) {
        Cmd in;
        if (xQueueReceive(gQueue, &in, end - t) == pdTRUE && prioOf(in.kind) >= prioOf(curKind)) {
            next = in;
            return true;
        }
    }
    return false;
}

void soundTask(void *)
{
    Cmd next{};
    bool have = false;
    for (;;) {
        if (!have) xQueueReceive(gQueue, &next, portMAX_DELAY);
        have = false;
        const Cmd cur = next;
        gPlaying = cur.kind != K_STOP;

        if (cur.kind == K_SWEEP) {  // กวาดความถี่ทีละขั้น 10 ms
            const int steps = max(1, cur.ms / 10);
            for (int i = 0; i <= steps && !have; i++) {
                tone(static_cast<uint16_t>(cur.hz + (static_cast<int32_t>(cur.hz2) - cur.hz) * i / steps));
                have = waitOrInterrupt(10, cur.kind, next);
            }
        } else {
            Note single{cur.hz, cur.ms};
            const Note *notes = nullptr;
            uint8_t count = 0;
            if (cur.kind == K_CLICK) {
                single = {2400, 8};
                notes = &single;
                count = 1;
            } else if (cur.kind == K_TONE) {
                notes = &single;
                count = 1;
            } else if (cur.kind == K_MELODY && cur.id < static_cast<uint8_t>(Melody::Count)) {
                notes = MELODIES[cur.id].notes;
                count = MELODIES[cur.id].count;
            }
            for (uint8_t i = 0; i < count && !have; i++) {
                tone(notes[i].hz);
                have = waitOrInterrupt(notes[i].ms, cur.kind, next);
            }
        }
        tone(0);
        if (!have) gPlaying = false;
    }
}

void send(const Cmd &c)
{
    if (gQueue == nullptr || (gVolume == 0 && c.kind != K_STOP)) return;
    xQueueSend(gQueue, &c, 0);  // คิวเต็ม = ทิ้ง (ไม่รอ)
}

}  // namespace

void soundBegin()
{
    gVolume = min<uint8_t>(prefs().getUChar("vol", app::VOLUME_DEFAULT), 3);
    ledcSetup(app::AUDIO_CHANNEL, 2000, app::AUDIO_BITS);
    ledcAttachPin(cyd::AUDIO_PIN, app::AUDIO_CHANNEL);
    ledcWrite(app::AUDIO_CHANNEL, 0);
    gQueue = xQueueCreate(6, sizeof(Cmd));
    // core 0: ไม่แย่งเวลาวาดจอของ loop() ที่ core 1
    xTaskCreatePinnedToCore(soundTask, "sound", 4096, nullptr, 2, nullptr, 0);
}

void soundClick()
{
    if (app::CLICK_SOUND) send({K_CLICK, 0, 0, 0, 0});
}

void soundTone(uint16_t hz, uint16_t ms)
{
    send({K_TONE, 0, hz, 0, ms});
}

void soundSweep(uint16_t fromHz, uint16_t toHz, uint16_t ms)
{
    send({K_SWEEP, 0, fromHz, toHz, ms});
}

void soundPlay(Melody m)
{
    send({K_MELODY, static_cast<uint8_t>(m), 0, 0, 0});
}

void soundStop()
{
    send({K_STOP, 0, 0, 0, 0});
}

bool soundBusy()
{
    return gPlaying;
}

uint16_t soundNowHz()
{
    return gNowHz;
}

uint8_t soundVolume()
{
    return gVolume;
}

void soundSetVolume(uint8_t v, bool save)
{
    gVolume = v % 4;
    if (save) prefs().putUChar("vol", gVolume);
    if (gVolume == 0) soundStop();
}

}  // namespace hal
