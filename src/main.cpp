/*
 * Charlie v2 - desktop eyes companion
 * Board: Waveshare ESP32-S3-Touch-AMOLED-1.75 (466x466 CO5300 QSPI AMOLED)
 *
 * Features
 *   - Emo / Vector / Cozmo style rounded-rectangle eyes (shape never distorts;
 *     moods are done with lids, like the real robots)
 *   - Mood system: joy, energy and annoyance drift over time and react to you
 *   - Reactions: tap, petting (swipe back and forth), shaking, pick-up / put-down
 *   - Swipe DOWN from the top edge for the menu: eye presets + mute button
 *   - Cute synthesized sounds (no audio files needed)
 *   - Preset and mute setting are remembered after power-off
 *
 * Libraries (all from Waveshare's example package / Library Manager)
 *   GFX_Library_for_Arduino (Waveshare copy), SensorLib (touch driver)
 *   ESP32 Arduino core 3.x (provides ESP_I2S, Preferences, Wire)
 *
 * Arduino IDE settings
 *   Board: ESP32S3 Dev Module | USB CDC On Boot: Enabled | PSRAM: OPI PSRAM
 *   Flash: 16MB
 *
 * Serial test commands (115200):
 *   n=neutral h=happy s=sleepy a=angry d=sad u=surprised l=love z=sleep
 *   m=toggle menu  t=play next test sound
 */

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <ESP_I2S.h>
#include <Arduino_GFX_Library.h>
#if __has_include(<TouchDrv.hpp>)
  #include <TouchDrv.hpp>
#else
  #include <TouchDrvCST92xx.h>
#endif

// =====================================================================
//  USER SETTINGS
// =====================================================================
#define ENABLE_OTA      0     // 1 = enable Wi-Fi OTA (fill in credentials below)
#define DEBUG_TOUCH     0     // 1 = print touch coords + draw a dot
#define DEBUG_IMU       0     // 1 = print accelerometer values twice a second

// If touch lands in the wrong place, flip these (DEBUG_TOUCH helps).
#define TOUCH_SWAP_XY   0
#define TOUCH_MIRROR_X  0
#define TOUCH_MIRROR_Y  0

// If eyes look the wrong way when you tilt him, flip these.
#define IMU_SWAP_XY     0
#define IMU_INV_X       0
#define IMU_INV_Y       0

#define MASTER_VOLUME   0.55f // 0.0 - 1.0 overall loudness of Charlie's sounds
#define PA_ACTIVE_LEVEL HIGH  // speaker amplifier enable level (GPIO46)

#if ENABLE_OTA
  #include <WiFi.h>
  #include <ArduinoOTA.h>
  const char *WIFI_SSID = "YOUR_WIFI";
  const char *WIFI_PASS = "YOUR_PASSWORD";
  bool otaStarted = false;
#endif

// =====================================================================
//  PINS (from Waveshare hardware reference for this board)
// =====================================================================
#define LCD_CS    12
#define LCD_SCLK  38
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_RST   39

#define PIN_SDA   15
#define PIN_SCL   14
#define TP_RST    40
#define TP_INT    11

#define I2S_MCLK_PIN 42
#define I2S_BCLK_PIN 9
#define I2S_WS_PIN   45
#define I2S_DOUT_PIN 8
#define I2S_DIN_PIN  10
#define PA_CTRL_PIN  46

#define IMU_ADDR  0x6B

#define SCREEN_W 466
#define SCREEN_H 466
#define CENTER_X (SCREEN_W / 2)
#define CENTER_Y (SCREEN_H / 2)

// =====================================================================
//  DISPLAY
// =====================================================================
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(
    bus, LCD_RST, 0, SCREEN_W, SCREEN_H, 6, 0, 0, 0);   // same values that worked for you
Arduino_Canvas *gfx = new Arduino_Canvas(SCREEN_W, SCREEN_H, panel);

const uint16_t BG_COLOR  = 0x0000;   // black
const uint16_t EYE_COLOR = 0x07FF;   // cyan - the ONE eye color, never changes
const uint16_t GREY      = 0x2104;
const uint16_t RED       = 0xF800;

// =====================================================================
//  HELPERS
// =====================================================================
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

Preferences prefs;

// =====================================================================
//  EYE PRESETS  (size and shape only - color stays the same)
// =====================================================================
struct Preset { const char *name; int w, h, r, spacing; };
const Preset PRESETS[4] = {
  {"Charlie", 120, 150, 40, 190},
  {"Emo",     140, 115, 50, 200},
  {"Vector",  125, 135, 30, 190},
  {"Cozmo",   108, 155, 24, 180},
};
uint8_t presetIdx = 0;
// Current (smoothly morphing) eye geometry
float gW = 120, gH = 150, gR = 40, gSp = 190;

// =====================================================================
//  SOUND ENGINE (runs on its own core so animation never stutters)
// =====================================================================
enum Snd : uint8_t {
  SND_NONE, SND_BOOT, SND_TAP, SND_HAPPY, SND_PURR, SND_SURPRISE, SND_ANGRY,
  SND_SAD, SND_DIZZY, SND_YAWN, SND_RELIEF, SND_TICK, SND_UNMUTE, SND_WAKE,
  SND_HEY, SND_COUNT
};

struct Note { int16_t f0, f1; uint16_t ms; uint8_t vib, am, vol; };
// f0 = start Hz, f1 = end Hz (0,0 = rest), vib = wobble depth Hz, am = tremolo Hz, vol = 0-100

static const Note N_BOOT[]     = {{600,900,90,0,0,70},{0,0,30,0,0,0},{800,1200,90,0,0,70},{0,0,30,0,0,0},{1000,1700,170,12,0,70}};
static const Note N_TAP[]      = {{700,1150,75,0,0,70}};
static const Note N_HAPPY[]    = {{800,1000,90,0,0,70},{1000,1600,150,10,0,70}};
static const Note N_PURR[]     = {{260,330,170,0,26,75},{330,270,170,0,26,75}};
static const Note N_SURPRISE[] = {{500,1800,230,0,0,75},{1800,1500,90,22,0,60}};
static const Note N_ANGRY[]    = {{170,130,210,0,34,85},{0,0,60,0,0,0},{160,115,270,0,34,85}};
static const Note N_SAD[]      = {{900,500,320,14,0,60},{0,0,40,0,0,0},{700,340,420,18,0,55}};
static const Note N_DIZZY[]    = {{600,900,120,40,0,60},{900,600,120,40,0,60},{600,900,120,40,0,60},{900,450,220,40,0,60}};
static const Note N_YAWN[]     = {{700,350,520,12,0,50},{0,0,60,0,0,0},{500,250,520,10,0,45}};
static const Note N_RELIEF[]   = {{900,600,180,0,0,60},{600,850,170,0,0,60}};
static const Note N_TICK[]     = {{1500,1500,25,0,0,55}};
static const Note N_UNMUTE[]   = {{900,1300,70,0,0,65},{1300,1800,90,0,0,65}};
static const Note N_WAKE[]     = {{500,900,100,0,0,65},{900,700,110,0,0,65}};
static const Note N_HEY[]      = {{520,360,110,0,0,80},{0,0,40,0,0,0},{520,340,150,0,0,80}};

struct SoundDef { const Note *notes; uint8_t n; };
#define SD(a) {a, (uint8_t)(sizeof(a) / sizeof(a[0]))}
static const SoundDef SOUNDS[SND_COUNT] = {
  {nullptr, 0}, SD(N_BOOT), SD(N_TAP), SD(N_HAPPY), SD(N_PURR), SD(N_SURPRISE), SD(N_ANGRY),
  SD(N_SAD), SD(N_DIZZY), SD(N_YAWN), SD(N_RELIEF), SD(N_TICK), SD(N_UNMUTE), SD(N_WAKE), SD(N_HEY)
};

I2SClass i2s;
QueueHandle_t soundQ = nullptr;
volatile bool muted = false;
bool audioOK = false;
const int SAMPLE_RATE = 16000;

static void playNotes(const SoundDef &sd) {
  static int16_t buf[256 * 2];
  float phase = 0, vibPh = 0, amPh = 0;
  const float TWO_PI_F = 6.2831853f;
  for (uint8_t ni = 0; ni < sd.n; ni++) {
    const Note &nt = sd.notes[ni];
    int total = (SAMPLE_RATE * nt.ms) / 1000;
    int done = 0;
    while (done < total) {
      int chunk = min(256, total - done);
      for (int k = 0; k < chunk; k++) {
        int idx = done + k;
        int16_t v = 0;
        if (nt.f0 > 0) {
          float t = (float)idx / (float)total;
          float f = nt.f0 + (nt.f1 - nt.f0) * t;
          if (nt.vib) { f += nt.vib * sinf(vibPh); vibPh += TWO_PI_F * 7.0f / SAMPLE_RATE; }
          phase += TWO_PI_F * f / SAMPLE_RATE;
          if (phase > TWO_PI_F) phase -= TWO_PI_F;
          float att = min(1.0f, idx / (0.008f * SAMPLE_RATE));
          float rel = min(1.0f, (total - idx) / (0.03f * SAMPLE_RATE));
          float env = min(att, rel);
          float s = sinf(phase) + 0.25f * sinf(2.0f * phase);
          if (nt.am) { s *= 0.55f + 0.45f * sinf(amPh); amPh += TWO_PI_F * nt.am / SAMPLE_RATE; }
          v = (int16_t)(s * 0.75f * env * (nt.vol / 100.0f) * MASTER_VOLUME * 32767.0f);
        }
        buf[2 * k] = v;
        buf[2 * k + 1] = v;
      }
      i2s.write((uint8_t *)buf, chunk * 4);
      done += chunk;
    }
  }
  // Fill the DMA ring with silence so the last bit of sound never loops/buzzes
  memset(buf, 0, sizeof(buf));
  for (int i = 0; i < 14; i++) i2s.write((uint8_t *)buf, 256 * 4);
}

static void audioTask(void *) {
  uint8_t id;
  for (;;) {
    if (xQueueReceive(soundQ, &id, portMAX_DELAY) == pdTRUE) {
      if (!muted && id > 0 && id < SND_COUNT) playNotes(SOUNDS[id]);
    }
  }
}

void sound(uint8_t id) {
  if (!audioOK || muted || !soundQ) return;
  xQueueSend(soundQ, &id, 0);
}

// --- ES8311 codec (register sequence follows Espressif's es8311 driver,
//     16-bit, slave mode, MCLK = 256 x 16 kHz = 4.096 MHz) ---
static uint8_t esAddr = 0x18;
static bool esWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(esAddr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
static uint8_t esRead(uint8_t reg) {
  Wire.beginTransmission(esAddr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((int)esAddr, 1);
  return Wire.available() ? Wire.read() : 0;
}
static bool i2cPing(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static bool initCodec() {
  if (i2cPing(0x18)) esAddr = 0x18;
  else if (i2cPing(0x19)) esAddr = 0x19;
  else { Serial.println("ES8311 not found on I2C (0x18/0x19)"); return false; }

  esWrite(0x44, 0x08); esWrite(0x44, 0x08);
  esWrite(0x01, 0x30); esWrite(0x02, 0x00); esWrite(0x03, 0x10); esWrite(0x16, 0x24);
  esWrite(0x04, 0x10); esWrite(0x05, 0x00); esWrite(0x0B, 0x00); esWrite(0x0C, 0x00);
  esWrite(0x10, 0x1F); esWrite(0x11, 0x7F);
  esWrite(0x00, 0x80);                         // power on
  esWrite(0x00, esRead(0x00) & 0xBF);          // slave mode
  esWrite(0x01, 0x3F);                         // clocks on, MCLK from pin
  esWrite(0x06, esRead(0x06) & ~0x20);         // SCLK not inverted
  // coefficients for MCLK 4.096 MHz / 16 kHz (Espressif table row)
  esWrite(0x02, 0x00);
  esWrite(0x03, 0x10);
  esWrite(0x04, 0x20);
  esWrite(0x05, 0x00);
  esWrite(0x06, (esRead(0x06) & 0xE0) | 0x03);
  esWrite(0x07, esRead(0x07) & 0xC0);
  esWrite(0x08, 0xFF);
  // I2S format, 16-bit
  esWrite(0x09, (esRead(0x09) & 0xE0) | 0x0C);
  esWrite(0x0A, (esRead(0x0A) & 0xE0) | 0x0C);
  esWrite(0x0D, 0x01); esWrite(0x0E, 0x02); esWrite(0x12, 0x00); esWrite(0x13, 0x10);
  esWrite(0x1C, 0x6A); esWrite(0x37, 0x08);
  esWrite(0x31, esRead(0x31) & 0x9F);          // un-mute DAC
  esWrite(0x32, 0xBF);                         // DAC volume ~75%
  return true;
}

static void initAudio() {
  pinMode(PA_CTRL_PIN, OUTPUT);
  digitalWrite(PA_CTRL_PIN, PA_ACTIVE_LEVEL);

  if (!initCodec()) return;

  i2s.setPins(I2S_BCLK_PIN, I2S_WS_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCLK_PIN);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("I2S begin failed - sound disabled");
    return;
  }
  soundQ = xQueueCreate(4, sizeof(uint8_t));
  xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 2, nullptr, 0);
  audioOK = true;
}

// =====================================================================
//  IMU (QMI8658, accelerometer only, raw I2C)
// =====================================================================
bool imuOK = false;
float ax = 0, ay = 0, az = 1;       // in g
float restX = 0, restY = 0, restZ = 1;

static bool imuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static void initIMU() {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(0x00);
  Wire.endTransmission(false);
  Wire.requestFrom((int)IMU_ADDR, 1);
  uint8_t id = Wire.available() ? Wire.read() : 0;
  if (id != 0x05) {
    Serial.printf("QMI8658 WHO_AM_I = 0x%02X (expected 0x05) - motion reactions may not work\n", id);
  }
  imuWrite(0x02, 0x40);   // CTRL1: auto-increment addressing
  imuWrite(0x03, 0x16);   // CTRL2: accel +-4g, ~112 Hz
  imuWrite(0x08, 0x01);   // CTRL7: enable accelerometer
  delay(30);
  imuOK = (id == 0x05);
}

static bool readIMU() {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(0x35);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)IMU_ADDR, 6) != 6) return false;
  int16_t rx = Wire.read(); rx |= (int16_t)Wire.read() << 8;
  int16_t ry = Wire.read(); ry |= (int16_t)Wire.read() << 8;
  int16_t rz = Wire.read(); rz |= (int16_t)Wire.read() << 8;
  ax = rx / 8192.0f; ay = ry / 8192.0f; az = rz / 8192.0f;
  return true;
}

// =====================================================================
//  TOUCH
// =====================================================================
TouchDrvCST92xx touch;
bool touchOK = false;
volatile bool touchFlag = false;
void IRAM_ATTR onTouchInt() { touchFlag = true; }

struct AxisTrack { int dir = 0; int travel = 0; int rev = 0; uint32_t lastTurn = 0; };
struct TouchState {
  bool down = false;
  int16_t x = 0, y = 0, sx = 0, sy = 0, lx = 0, ly = 0;
  uint32_t t0 = 0, lastSeen = 0;
  float path = 0;
  bool swipeHandled = false;
  AxisTrack axX, axY;
} ts;

// =====================================================================
//  MOOD SYSTEM
// =====================================================================
enum Mood : uint8_t { M_NEUTRAL, M_HAPPY, M_SLEEPY, M_ANGRY, M_SAD, M_SURPRISED, M_LOVE, M_DIZZY, M_ASLEEP };

struct Expr { float tired, angry, happy, scale, open; };

static Expr exprFor(Mood m) {
  switch (m) {
    case M_HAPPY:     return {0.00f, 0.00f, 0.55f, 1.00f, 1.00f};
    case M_LOVE:      return {0.00f, 0.00f, 0.95f, 1.00f, 1.00f};
    case M_SLEEPY:    return {0.55f, 0.00f, 0.00f, 0.95f, 0.85f};
    case M_ANGRY:     return {0.00f, 0.75f, 0.00f, 0.95f, 1.00f};
    case M_SAD:       return {0.65f, 0.00f, 0.00f, 0.95f, 1.00f};
    case M_SURPRISED: return {0.00f, 0.00f, 0.00f, 1.15f, 1.00f};
    case M_DIZZY:     return {0.20f, 0.00f, 0.00f, 1.00f, 1.00f};
    case M_ASLEEP:    return {0.00f, 0.00f, 0.00f, 0.95f, 0.04f};
    default:          return {0.00f, 0.00f, 0.00f, 1.00f, 1.00f};
  }
}

Expr cur = exprFor(M_NEUTRAL);
float joy = 60, energy = 85, annoy = 0;     // 0..100
uint32_t lastInteract = 0;
bool asleep = false;

Mood baseMood = M_NEUTRAL;
Mood reactMood = M_NEUTRAL;
uint32_t reactEnd = 0;
bool reacting = false;
uint32_t reactStart = 0;

bool menuOpen = false;
float menuP = 0;              // 0 = closed, 1 = fully open

// motion state
bool held = false;
uint32_t liftMs = 0, stillMs = 0, lastShake = 0;
uint32_t peakTimes[10];
uint8_t peakCount = 0;
float prevDyn = 0;
uint32_t lastTap = 0; uint8_t tapBurst = 0;
uint32_t lastPurr = 0;
bool petting = false;
uint32_t lastPetMs = 0;

// animation state
float blinkF = 1, blinkTarget = 1;
uint8_t blinkPhase = 0;
uint32_t nextBlink = 2000, blinkT = 0;
float lookX = 0, lookY = 0, tLookX = 0, tLookY = 0;
uint32_t nextLook = 0;

void applyBrightness() { panel->Display_Brightness(asleep ? 40 : 200); }

void react(Mood m, uint32_t durMs) {
  reactMood = m;
  reactStart = millis();
  reactEnd = reactStart + durMs;
  reacting = true;
}

void noteInteraction() { lastInteract = millis(); }

void wakeUp(bool startled) {
  if (!asleep) return;
  asleep = false;
  energy = max(energy, 40.0f);
  applyBrightness();
  sound(SND_WAKE);
  if (startled) react(M_SURPRISED, 700);
}

void goToSleep() {
  asleep = true;
  applyBrightness();
  sound(SND_YAWN);
}

void onTap() {
  uint32_t now = millis();
  noteInteraction();
  if (asleep) { wakeUp(true); return; }
  if (now - lastTap < 650) tapBurst++; else tapBurst = 1;
  lastTap = now;
  joy = clampf(joy + 5, 0, 100);
  if (tapBurst >= 5) {                        // poked too much - he gets annoyed
    annoy = clampf(annoy + 18, 0, 100);
    react(M_ANGRY, 1400);
    sound(SND_HEY);
    tapBurst = 0;
  } else {
    annoy = clampf(annoy - 3, 0, 100);
    react(M_HAPPY, 750);
    sound(random(2) ? SND_TAP : SND_HAPPY);
    blinkPhase = 0; nextBlink = now;          // little blink on touch
  }
}

void onPickup() {
  noteInteraction();
  wakeUp(false);
  energy = clampf(energy + 5, 0, 100);
  react(M_SURPRISED, 1100);
  sound(SND_SURPRISE);
}

void onPutDown() {
  noteInteraction();
  joy = clampf(joy + 4, 0, 100);
  react(M_HAPPY, 1500);
  sound(SND_RELIEF);
}

void onShake(bool strong) {
  noteInteraction();
  wakeUp(false);
  if (strong) {
    annoy = clampf(annoy + 35, 0, 100);
    joy = clampf(joy - 8, 0, 100);
    react(M_ANGRY, 2500);
    sound(SND_ANGRY);
  } else {
    annoy = clampf(annoy + 10, 0, 100);
    joy = clampf(joy - 3, 0, 100);
    react(M_DIZZY, 1800);
    sound(SND_DIZZY);
  }
}

void updateMood(float dt, uint32_t now) {
  float idleSec = (now - lastInteract) / 1000.0f;
  joy += (55.0f - joy) * (1.0f - expf(-dt / 120.0f));          // drift to calm
  if (idleSec > 120) joy -= 0.15f * dt;                         // lonely if ignored
  if (asleep) energy += 1.5f * dt; else energy -= 0.08f * dt;   // tires out, recharges asleep
  annoy -= 4.0f * dt;
  joy = clampf(joy, 0, 100); energy = clampf(energy, 0, 100); annoy = clampf(annoy, 0, 100);

  if (!asleep && !menuOpen && energy < 15 && idleSec > 30) goToSleep();
  if (asleep && energy >= 90) { asleep = false; applyBrightness(); sound(SND_WAKE); }

  Mood old = baseMood;
  if (asleep) baseMood = M_ASLEEP;
  else if (annoy > 55) baseMood = M_ANGRY;
  else if (energy < 28) baseMood = M_SLEEPY;
  else if (joy < 28) baseMood = M_SAD;
  else if (joy > 72) baseMood = M_HAPPY;
  else baseMood = M_NEUTRAL;

  // little mood noises when he changes on his own
  if (baseMood != old && !reacting) {
    if (baseMood == M_SLEEPY) sound(SND_YAWN);
    else if (baseMood == M_SAD) sound(SND_SAD);
    else if (baseMood == M_HAPPY) sound(SND_HAPPY);
  }
  // occasional chirp when happy, whimper when lonely
  if (!asleep && !menuOpen && random(1000) < 3) {
    if (baseMood == M_HAPPY) sound(SND_HAPPY);
    else if (baseMood == M_SAD) sound(SND_SAD);
  }
}

// =====================================================================
//  MENU
// =====================================================================
static const int BTN_W = 150, BTN_H = 84;
static inline int btnX(int i) { return (i % 2 == 0) ? 73 : 243; }
static inline int btnY(int i) { return (i < 2) ? 95 : 193; }
static const int MUTE_CX = 233, MUTE_CY = 350, MUTE_R = 40;

void openMenu()  { if (!menuOpen) { menuOpen = true;  sound(SND_TICK); } }
void closeMenu() { if (menuOpen)  { menuOpen = false; sound(SND_TICK); } }

void selectPreset(uint8_t i) {
  presetIdx = i;
  prefs.putUChar("preset", presetIdx);
  sound(SND_HAPPY);
  react(M_HAPPY, 600);
}

void setMuted(bool m) {
  muted = m;
  prefs.putBool("mute", muted);
  if (!m) sound(SND_UNMUTE);
}

void menuTap(int x, int y) {
  for (int i = 0; i < 4; i++) {
    if (x >= btnX(i) && x <= btnX(i) + BTN_W && y >= btnY(i) && y <= btnY(i) + BTN_H) {
      selectPreset(i);
      return;
    }
  }
  int dx = x - MUTE_CX, dy = y - MUTE_CY;
  if (dx * dx + dy * dy <= (MUTE_R + 12) * (MUTE_R + 12)) { setMuted(!muted); return; }
  if (y < 60) closeMenu();
}

static void drawCentered(const char *s, int cx, int y, uint8_t size, uint16_t fg, uint16_t bg) {
  gfx->setTextSize(size);
  gfx->setTextColor(fg, bg);
  int wpx = strlen(s) * 6 * size;
  gfx->setCursor(cx - wpx / 2, y);
  gfx->print(s);
}

void drawMenu(float p) {
  int bottom = (int)(p * SCREEN_H);
  if (bottom <= 0) return;
  int dy = bottom - SCREEN_H;
  gfx->fillRect(0, 0, SCREEN_W, bottom, BG_COLOR);

  drawCentered("Eye presets", CENTER_X, 50 + dy, 2, EYE_COLOR, BG_COLOR);

  for (int i = 0; i < 4; i++) {
    int bx = btnX(i), by = btnY(i) + dy;
    bool sel = (i == presetIdx);
    uint16_t fill = sel ? EYE_COLOR : BG_COLOR;
    uint16_t ink  = sel ? BG_COLOR : EYE_COLOR;
    gfx->fillRoundRect(bx, by, BTN_W, BTN_H, 16, fill);
    gfx->drawRoundRect(bx, by, BTN_W, BTN_H, 16, EYE_COLOR);
    // mini preview of that preset's eyes
    int mw = PRESETS[i].w / 6, mh = PRESETS[i].h / 6, mr = max(3, PRESETS[i].r / 6);
    int mcx = bx + BTN_W / 2, mcy = by + 28;
    gfx->fillRoundRect(mcx - 18 - mw / 2, mcy - mh / 2, mw, mh, mr, ink);
    gfx->fillRoundRect(mcx + 18 - mw / 2, mcy - mh / 2, mw, mh, mr, ink);
    drawCentered(PRESETS[i].name, bx + BTN_W / 2, by + BTN_H - 24, 2, ink, fill);
  }

  // mute button
  int cy = MUTE_CY + dy;
  uint16_t bgc = muted ? GREY : EYE_COLOR;
  uint16_t ic  = muted ? EYE_COLOR : BG_COLOR;
  gfx->fillCircle(MUTE_CX, cy, MUTE_R, bgc);
  gfx->fillRect(MUTE_CX - 24, cy - 9, 14, 18, ic);
  gfx->fillTriangle(MUTE_CX - 10, cy - 9, MUTE_CX + 8, cy - 24, MUTE_CX + 8, cy + 24, ic);
  gfx->fillTriangle(MUTE_CX - 10, cy - 9, MUTE_CX - 10, cy + 9, MUTE_CX + 8, cy + 24, ic);
  if (muted) {
    for (int o = -1; o <= 1; o++) {
      gfx->drawLine(MUTE_CX + 14 + o, cy - 12, MUTE_CX + 34 + o, cy + 12, RED);
      gfx->drawLine(MUTE_CX + 14 + o, cy + 12, MUTE_CX + 34 + o, cy - 12, RED);
    }
  } else {
    for (int w = 0; w < 3; w++) {
      int hx = MUTE_CX + 15 + w * 8, hl = 6 + w * 6;
      for (int o = 0; o < 2; o++) gfx->drawLine(hx + o, cy - hl, hx + o, cy + hl, ic);
    }
  }
  drawCentered(muted ? "Sound off" : "Sound on", CENTER_X, 398 + dy, 2, EYE_COLOR, BG_COLOR);
  drawCentered("swipe up to close", CENTER_X, 428 + dy, 1, GREY + 0x4208, BG_COLOR);

  if (bottom < SCREEN_H) gfx->fillRect(0, bottom - 3, SCREEN_W, 3, EYE_COLOR);
}

// =====================================================================
//  TOUCH HANDLING
// =====================================================================
static void trackAxis(AxisTrack &a, int delta, uint32_t now) {
  if (abs(delta) < 3) return;
  int dir = delta > 0 ? 1 : -1;
  if (a.dir != 0 && dir != a.dir && a.travel >= 30) {
    a.rev++;
    a.lastTurn = now;
    a.travel = 0;
  }
  if (dir != a.dir) a.travel = 0;
  a.dir = dir;
  a.travel += abs(delta);
}

void touchDown(int x, int y, uint32_t now) {
  ts.down = true; ts.x = ts.sx = ts.lx = x; ts.y = ts.sy = ts.ly = y;
  ts.t0 = now; ts.path = 0; ts.swipeHandled = false;
  ts.axX = AxisTrack(); ts.axY = AxisTrack();
  noteInteraction();
  if (asleep && !menuOpen) { wakeUp(true); ts.swipeHandled = true; }
}

void touchMove(int x, int y, uint32_t now) {
  int dx = x - ts.lx, dy = y - ts.ly;
  ts.path += sqrtf((float)(dx * dx + dy * dy));
  trackAxis(ts.axX, dx, now);
  trackAxis(ts.axY, dy, now);
  if (abs(dx) >= 3) ts.lx = x;
  if (abs(dy) >= 3) ts.ly = y;
  ts.x = x; ts.y = y;

  if (!menuOpen && !ts.swipeHandled && ts.sy < 110 && (y - ts.sy) > 90) {
    openMenu(); ts.swipeHandled = true;
  } else if (menuOpen && !ts.swipeHandled && (ts.sy - y) > 90) {
    closeMenu(); ts.swipeHandled = true;
  }
}

void touchUp(uint32_t now) {
  ts.down = false;
  if (ts.swipeHandled) return;
  bool shortTap = (now - ts.t0) < 450 && ts.path < 25;
  if (menuOpen) {
    if (shortTap) menuTap(ts.x, ts.y);
  } else if (shortTap && !petting) {
    onTap();
  }
}

void pollTouch(uint32_t now) {
  if (!touchOK) return;
  static uint32_t lastPoll = 0;
  if (!(touchFlag || (ts.down && now - lastPoll >= 16))) {
    if (ts.down && now - ts.lastSeen > 90) touchUp(now);
    return;
  }
  touchFlag = false;
  lastPoll = now;

  int16_t px[5], py[5];
  uint8_t n = touch.getPoint(px, py, touch.getSupportTouchPoint());
  if (n > 0) {
    ts.lastSeen = now;
    int x = constrain(px[0], 0, SCREEN_W - 1), y = constrain(py[0], 0, SCREEN_H - 1);
#if DEBUG_TOUCH
    Serial.printf("touch x=%d y=%d\n", x, y);
#endif
    if (!ts.down) touchDown(x, y, now); else touchMove(x, y, now);
  } else if (ts.down && now - ts.lastSeen > 90) {
    touchUp(now);
  }
}

// =====================================================================
//  MOTION HANDLING (shake / pick-up / put-down)
// =====================================================================
void pollMotion(uint32_t now) {
  static uint32_t lastRead = 0;
  if (!imuOK || now - lastRead < 20) return;
  uint32_t dtMs = now - lastRead;
  lastRead = now;
  if (!readIMU()) return;

  float mag = sqrtf(ax * ax + ay * ay + az * az);
  float dyn = fabsf(mag - 1.0f);
#if DEBUG_IMU
  static uint32_t lastPrint = 0;
  if (now - lastPrint > 500) { lastPrint = now; Serial.printf("acc %.2f %.2f %.2f dyn %.2f held %d\n", ax, ay, az, dyn, held); }
#endif

  // --- shake: count sharp peaks inside a short window ---
  if (dyn > 0.7f && prevDyn <= 0.7f) {
    if (peakCount < 10) peakTimes[peakCount++] = now;
  }
  prevDyn = dyn;
  uint8_t keep = 0;
  for (uint8_t i = 0; i < peakCount; i++) if (now - peakTimes[i] < 900) peakTimes[keep++] = peakTimes[i];
  peakCount = keep;
  if (peakCount >= 4 && now - lastShake > 1500) {
    bool strong = (peakCount >= 7) || (now - lastShake < 6000);
    lastShake = now;
    peakCount = 0;
    onShake(strong);
    liftMs = 0;
    held = true;                 // you're clearly holding him
    stillMs = 0;
    return;
  }

  // --- rest orientation + tilt ---
  float dot = ax * restX + ay * restY + az * restZ;
  float rmag = sqrtf(restX * restX + restY * restY + restZ * restZ);
  float cosA = clampf(dot / (max(mag, 0.01f) * max(rmag, 0.01f)), -1, 1);
  float tiltDeg = acosf(cosA) * 57.2958f;

  if (dyn < 0.05f) stillMs += dtMs; else stillMs = 0;
  if (!held && stillMs > 800) {          // slowly learn how he's resting
    restX = lerpf(restX, ax, 0.1f); restY = lerpf(restY, ay, 0.1f); restZ = lerpf(restZ, az, 0.1f);
  }

  if (!held) {
    if ((dyn > 0.09f && dyn < 0.7f) || tiltDeg > 20.0f) liftMs += dtMs; else liftMs = 0;
    if (liftMs > 250) { held = true; liftMs = 0; onPickup(); }
  } else {
    // settled for a while = put down (even if resting in a new orientation)
    if ((stillMs > 1200 && tiltDeg < 8.0f) || stillMs > 2500) {
      held = false;
      restX = ax; restY = ay; restZ = az;
      onPutDown();
    }
  }
}

// =====================================================================
//  ANIMATION + RENDERING
// =====================================================================
void updateAnim(uint32_t now, float &offX, float &offY) {
  // choose which mood is on show
  if (reacting && now >= reactEnd) reacting = false;
  Mood shown = reacting ? reactMood : baseMood;
  Expr t = exprFor(shown);
  float e = 0.22f;
  cur.tired = lerpf(cur.tired, t.tired, e);
  cur.angry = lerpf(cur.angry, t.angry, e);
  cur.happy = lerpf(cur.happy, t.happy, e);
  cur.scale = lerpf(cur.scale, t.scale, e);
  cur.open  = lerpf(cur.open,  t.open,  e);

  // morph toward the selected preset shape
  const Preset &p = PRESETS[presetIdx];
  gW = lerpf(gW, p.w, 0.18f); gH = lerpf(gH, p.h, 0.18f);
  gR = lerpf(gR, p.r, 0.18f); gSp = lerpf(gSp, p.spacing, 0.18f);

  // blinking
  if (!asleep) {
    if (blinkPhase == 0 && now >= nextBlink) { blinkTarget = 0.0f; blinkPhase = 1; blinkT = now + 90; }
    else if (blinkPhase == 1 && now >= blinkT) { blinkTarget = 1.0f; blinkPhase = 2; blinkT = now + 110; }
    else if (blinkPhase == 2 && now >= blinkT) {
      blinkPhase = 0;
      nextBlink = now + (random(5) == 0 ? 220 : random(2500, 6000));  // sometimes a double blink
    }
  } else { blinkTarget = 1.0f; blinkPhase = 0; }
  blinkF = lerpf(blinkF, blinkTarget, 0.6f);

  // where to look
  float wobX = 0, wobY = 0;
  if (ts.down && !menuOpen && petting) {
    tLookX = clampf((ts.x - CENTER_X) * 0.12f, -24, 24);
    tLookY = clampf((ts.y - CENTER_Y) * 0.10f, -18, 18);
  } else if (held && imuOK) {
    float gx = IMU_SWAP_XY ? ay : ax, gy = IMU_SWAP_XY ? ax : ay;
    if (IMU_INV_X) gx = -gx;
    if (IMU_INV_Y) gy = -gy;
    tLookX = clampf(gx * 30.0f, -24, 24);
    tLookY = clampf(gy * 24.0f, -18, 18);
  } else if (shown == M_NEUTRAL || shown == M_HAPPY || shown == M_SLEEPY || shown == M_SAD) {
    if (now >= nextLook) {
      tLookX = random(-24, 25); tLookY = random(-14, 15);
      if (random(4) == 0) { tLookX = 0; tLookY = 0; }
      nextLook = now + random(900, 3200);
    }
    if (shown == M_SAD) tLookY = max(tLookY, 8.0f);
  } else {
    tLookX = 0; tLookY = 0;
  }
  lookX = lerpf(lookX, tLookX, 0.22f);
  lookY = lerpf(lookY, tLookY, 0.22f);

  // reaction motion
  float rt = reacting ? (now - reactStart) : 0;
  if (reacting && reactMood == M_DIZZY) { wobX = 18 * cosf(rt * 0.012f); wobY = 14 * sinf(rt * 0.012f); }
  else if (reacting && reactMood == M_ANGRY) { wobX = 3 * sinf(rt * 0.09f); }
  else if (reacting && (reactMood == M_HAPPY || reactMood == M_LOVE) && rt < 700) { wobY = -6 * fabsf(sinf(rt * 0.011f)); }
  if (asleep) wobY += 2 * sinf(now / 900.0f);

  offX = lookX + wobX;
  offY = lookY + wobY;
}

void drawEyes(float offX, float offY) {
  float sc = cur.scale;
  int w = (int)(gW * sc), fullH = (int)(gH * sc);
  int h = (int)(fullH * cur.open * blinkF);
  if (h < 6) h = 6;
  int r = min((int)(gR * sc), min(h / 2, w / 2));

  for (int s = 0; s < 2; s++) {
    bool left = (s == 0);
    int cx = CENTER_X + (left ? -1 : 1) * (int)(gSp / 2) + (int)offX;
    int cy = CENTER_Y + (int)offY;
    int x0 = cx - w / 2, y0 = cy - h / 2, top = y0 - 1;

    gfx->fillRoundRect(x0, y0, w, h, r, EYE_COLOR);          // the eye itself - shape never changes

    if (cur.tired > 0.02f) {                                  // droopy lids: outer corners down
      int d = (int)(h * 0.55f * cur.tired);
      if (left) gfx->fillTriangle(x0 - 1, top, x0 + w + 1, top, x0 - 1, top + d, BG_COLOR);
      else      gfx->fillTriangle(x0 - 1, top, x0 + w + 1, top, x0 + w + 1, top + d, BG_COLOR);
    }
    if (cur.angry > 0.02f) {                                  // angry lids: inner corners down
      int d = (int)(h * 0.55f * cur.angry);
      if (left) gfx->fillTriangle(x0 - 1, top, x0 + w + 1, top, x0 + w + 1, top + d, BG_COLOR);
      else      gfx->fillTriangle(x0 - 1, top, x0 + w + 1, top, x0 - 1, top + d, BG_COLOR);
    }
    if (cur.happy > 0.02f) {                                  // happy cheeks push up from below
      int off = (int)(fullH * 0.5f * cur.happy);
      gfx->fillRoundRect(x0 - 1, y0 + h - off + 1, w + 2, fullH, r, BG_COLOR);
    }
  }
}

void renderFrame(uint32_t now) {
  float offX, offY;
  updateAnim(now, offX, offY);

  menuP = lerpf(menuP, menuOpen ? 1.0f : 0.0f, 0.35f);
  if (fabsf(menuP - (menuOpen ? 1.0f : 0.0f)) < 0.01f) menuP = menuOpen ? 1.0f : 0.0f;

  gfx->fillScreen(BG_COLOR);
  if (menuP < 0.999f) drawEyes(offX, offY);
  if (menuP > 0.0f) drawMenu(menuP);
#if DEBUG_TOUCH
  if (ts.down) gfx->fillCircle(ts.x, ts.y, 8, RED);
#endif
  gfx->flush();
}

// =====================================================================
//  PETTING DETECTION (runs every loop)
// =====================================================================
void updatePetting(float dt, uint32_t now) {
  bool recent = (now - ts.axX.lastTurn < 1300 && ts.axX.rev >= 2) ||
                (now - ts.axY.lastTurn < 1300 && ts.axY.rev >= 2);
  petting = ts.down && !menuOpen && !ts.swipeHandled && recent;
  if (petting) {
    noteInteraction();
    joy = clampf(joy + 6.0f * dt, 0, 100);
    annoy = clampf(annoy - 8.0f * dt, 0, 100);
    energy = clampf(energy + 1.0f * dt, 0, 100);
    if (!reacting || reactMood != M_LOVE) react(M_LOVE, 700);
    else reactEnd = now + 700;                 // keep him in love while you pet
    if (now - lastPurr > 850) { sound(SND_PURR); lastPurr = now; }
  }
}

// =====================================================================
//  SERIAL TEST COMMANDS
// =====================================================================
void handleSerial() {
  static uint8_t testSnd = 1;
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'n': baseMood = M_NEUTRAL; react(M_NEUTRAL, 3000); break;
      case 'h': react(M_HAPPY, 3000); break;
      case 's': react(M_SLEEPY, 3000); break;
      case 'a': react(M_ANGRY, 3000); break;
      case 'd': react(M_SAD, 3000); break;
      case 'u': react(M_SURPRISED, 3000); break;
      case 'l': react(M_LOVE, 3000); break;
      case 'z': asleep ? wakeUp(true) : goToSleep(); break;
      case 'm': menuOpen ? closeMenu() : openMenu(); break;
      case 't':
        Serial.printf("test sound %d\n", testSnd);
        sound(testSnd);
        testSnd = (testSnd + 1 >= SND_COUNT) ? 1 : testSnd + 1;
        break;
    }
  }
}

// =====================================================================
//  SETUP / LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Charlie v2 booting...");

  prefs.begin("charlie", false);
  presetIdx = constrain(prefs.getUChar("preset", 0), 0, 3);
  muted = prefs.getBool("mute", false);
  gW = PRESETS[presetIdx].w; gH = PRESETS[presetIdx].h;
  gR = PRESETS[presetIdx].r; gSp = PRESETS[presetIdx].spacing;

  if (!gfx->begin()) {
    Serial.println("Display/canvas init FAILED. Check PSRAM = OPI PSRAM in board settings.");
    while (true) delay(1000);
  }
  gfx->fillScreen(BG_COLOR);
  gfx->flush();
  applyBrightness();

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);

  // touch
  touch.setPins(TP_RST, TP_INT);
  touchOK = touch.begin(Wire, CST92XX_SLAVE_ADDRESS, PIN_SDA, PIN_SCL);
  if (touchOK) {
    touch.setMaxCoordinates(SCREEN_W, SCREEN_H);
#if TOUCH_SWAP_XY
    touch.setSwapXY(true);
#endif
    touch.setMirrorXY(TOUCH_MIRROR_X, TOUCH_MIRROR_Y);
    pinMode(TP_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TP_INT), onTouchInt, FALLING);
  } else {
    Serial.println("Touch init failed - touch reactions disabled");
  }

  initIMU();
  initAudio();

  randomSeed(esp_random());
  uint32_t now = millis();
  lastInteract = now;
  nextBlink = now + 1500;
  nextLook = now + 800;
  sound(SND_BOOT);

#if ENABLE_OTA
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);   // non-blocking
#endif
}

void loop() {
#if ENABLE_OTA
  if (!otaStarted && WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.setHostname("charlie");
    ArduinoOTA.begin();
    otaStarted = true;
    Serial.print("OTA ready at "); Serial.println(WiFi.localIP());
  }
  if (otaStarted) ArduinoOTA.handle();
#endif

  static uint32_t lastFrame = 0, lastMood = 0;
  uint32_t now = millis();

  handleSerial();
  pollTouch(now);
  pollMotion(now);

  if (now - lastMood >= 100) {
    float dt = (now - lastMood) / 1000.0f;
    lastMood = now;
    updatePetting(dt, now);
    updateMood(dt, now);
  }

  if (now - lastFrame >= 33) {
    lastFrame = now;
    renderFrame(now);
  }
}
