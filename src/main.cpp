/*
 * Charlie - Alive Desktop Eyes
 * Board: Waveshare ESP32-S3-Touch-AMOLED-1.75 (466x466 CO5300)
 *
 * Full-screen rendering (no band, no green bars, eyes never cut off).
 * Soft cyan rounded-rect eyes with gentle shading.
 * Lots of idle looking, breathing, double-blinks, reactions.
 * Brightness control + soft alarm via serial.
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

#define MASTER_VOLUME 0.48f
#define PA_ACTIVE_LEVEL HIGH

#define LCD_CS 12
#define LCD_SCLK 38
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_RST 39
#define PIN_SDA 15
#define PIN_SCL 14
#define TP_RST 40
#define TP_INT 11
#define I2S_MCLK_PIN 42
#define I2S_BCLK_PIN 9
#define I2S_WS_PIN 45
#define I2S_DOUT_PIN 8
#define I2S_DIN_PIN 10
#define PA_CTRL_PIN 46
#define IMU_ADDR 0x6B

#define SCREEN_W 466
#define SCREEN_H 466
#define CENTER_X (SCREEN_W / 2)
#define CENTER_Y (SCREEN_H / 2)

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(bus, LCD_RST, 0, SCREEN_W, SCREEN_H, 6, 0, 0, 0);
Arduino_Canvas *gfx = new Arduino_Canvas(SCREEN_W, SCREEN_H, panel);

const uint16_t BG = 0x0000;
const uint16_t EYE = 0x05DF;          // soft cyan
const uint16_t HIGHLIGHT = 0x6FFF;    // gentle lighter cyan, not white

uint8_t brightness = 160;
bool asleep = false;

static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

Preferences prefs;

const int BASE_W = 118;
const int BASE_H = 138;
const int BASE_R = 42;
const int BASE_SP = 178;

// Sounds
enum Snd : uint8_t { SND_NONE, SND_BOOT, SND_TAP, SND_HAPPY, SND_PURR, SND_SURPRISE, SND_ANGRY, SND_YAWN, SND_ALARM, SND_COUNT };
struct Note { int16_t f0, f1; uint16_t ms; uint8_t vol; };
static const Note N_BOOT[] = {{480,880,80,55},{880,1250,100,55}};
static const Note N_TAP[] = {{680,1050,55,55}};
static const Note N_HAPPY[] = {{760,1150,75,55},{1150,1450,95,55}};
static const Note N_PURR[] = {{240,290,160,50},{290,240,160,50}};
static const Note N_SURPRISE[] = {{420,1400,200,60}};
static const Note N_ANGRY[] = {{150,120,200,65}};
static const Note N_YAWN[] = {{520,260,420,35}};
static const Note N_ALARM[] = {{600,800,180,45},{800,1000,200,45},{1000,700,220,40}};

struct SoundDef { const Note *notes; uint8_t n; };
#define SD(a) {a, (uint8_t)(sizeof(a)/sizeof(a[0]))}
static const SoundDef SOUNDS[SND_COUNT] = {
  {nullptr,0}, SD(N_BOOT), SD(N_TAP), SD(N_HAPPY), SD(N_PURR),
  SD(N_SURPRISE), SD(N_ANGRY), SD(N_YAWN), SD(N_ALARM)
};

I2SClass i2s;
QueueHandle_t soundQ = nullptr;
volatile bool muted = false;
bool audioOK = false;
const int SAMPLE_RATE = 16000;

static void playNotes(const SoundDef &sd) {
  static int16_t buf[256*2];
  float phase = 0;
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
          float t = (float)idx / total;
          float f = nt.f0 + (nt.f1 - nt.f0) * t;
          phase += TWO_PI_F * f / SAMPLE_RATE;
          if (phase > TWO_PI_F) phase -= TWO_PI_F;
          float env = min(1.0f, idx / (0.012f * SAMPLE_RATE)) * min(1.0f, (total - idx) / (0.04f * SAMPLE_RATE));
          v = (int16_t)(sinf(phase) * 0.6f * env * (nt.vol / 100.0f) * MASTER_VOLUME * 32767);
        }
        buf[2 * k] = v; buf[2 * k + 1] = v;
      }
      i2s.write((uint8_t *)buf, chunk * 4);
      done += chunk;
    }
  }
  memset(buf, 0, sizeof(buf));
  for (int i = 0; i < 5; i++) i2s.write((uint8_t *)buf, 256 * 4);
}

static void audioTask(void *) {
  uint8_t id;
  for (;;) if (xQueueReceive(soundQ, &id, portMAX_DELAY) == pdTRUE)
    if (!muted && id > 0 && id < SND_COUNT) playNotes(SOUNDS[id]);
}

void sound(uint8_t id) {
  if (!audioOK || muted || !soundQ) return;
  xQueueSend(soundQ, &id, 0);
}

static uint8_t esAddr = 0x18;
static bool esWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(esAddr); Wire.write(reg); Wire.write(val); return Wire.endTransmission() == 0;
}
static bool initCodec() {
  Wire.beginTransmission(esAddr);
  if (Wire.endTransmission() != 0) esAddr = 0x19;
  esWrite(0x00, 0x80); esWrite(0x01, 0x3F); esWrite(0x32, 0xBF);
  return true;
}
static void initAudio() {
  pinMode(PA_CTRL_PIN, OUTPUT); digitalWrite(PA_CTRL_PIN, PA_ACTIVE_LEVEL);
  if (!initCodec()) return;
  i2s.setPins(I2S_BCLK_PIN, I2S_WS_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCLK_PIN);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) return;
  soundQ = xQueueCreate(4, sizeof(uint8_t));
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 2, nullptr, 0);
  audioOK = true;
}

bool imuOK = false; float ax = 0, ay = 0, az = 1; float restX = 0, restY = 0, restZ = 1;
static bool imuWrite(uint8_t r, uint8_t v) {
  Wire.beginTransmission(IMU_ADDR); Wire.write(r); Wire.write(v); return Wire.endTransmission() == 0;
}
static void initIMU() {
  Wire.beginTransmission(IMU_ADDR); Wire.write(0); Wire.endTransmission(false);
  Wire.requestFrom((int)IMU_ADDR, 1); uint8_t id = Wire.available() ? Wire.read() : 0;
  imuWrite(0x02, 0x40); imuWrite(0x03, 0x16); imuWrite(0x08, 0x01); delay(30);
  imuOK = (id == 0x05);
}
static bool readIMU() {
  Wire.beginTransmission(IMU_ADDR); Wire.write(0x35);
  if (Wire.endTransmission(false) || Wire.requestFrom((int)IMU_ADDR, 6) != 6) return false;
  int16_t rx = Wire.read() | (Wire.read() << 8);
  int16_t ry = Wire.read() | (Wire.read() << 8);
  int16_t rz = Wire.read() | (Wire.read() << 8);
  ax = rx / 8192.0f; ay = ry / 8192.0f; az = rz / 8192.0f;
  return true;
}

TouchDrvCST92xx touch;
bool touchOK = false; volatile bool touchFlag = false;
void IRAM_ATTR onTouchInt() { touchFlag = true; }
struct TouchState { bool down = false; int16_t x = 0, y = 0, sx = 0, sy = 0; uint32_t t0 = 0, lastSeen = 0; float path = 0; } ts;

enum Mood : uint8_t { M_NEUTRAL, M_HAPPY, M_SLEEPY, M_ANGRY, M_SAD, M_SURPRISED, M_LOVE, M_DIZZY, M_ASLEEP };
struct Expr { float tired, angry, happy, scale, open; };
Expr exprFor(Mood m) {
  switch (m) {
    case M_HAPPY:     return {0.00f, 0.00f, 0.45f, 1.00f, 1.00f};
    case M_LOVE:      return {0.00f, 0.00f, 0.80f, 1.02f, 1.00f};
    case M_SLEEPY:    return {0.40f, 0.00f, 0.00f, 0.96f, 0.78f};
    case M_ANGRY:     return {0.00f, 0.60f, 0.00f, 0.96f, 1.00f};
    case M_SAD:       return {0.50f, 0.00f, 0.00f, 0.96f, 1.00f};
    case M_SURPRISED: return {0.00f, 0.00f, 0.00f, 1.10f, 1.00f};
    case M_DIZZY:     return {0.12f, 0.00f, 0.00f, 1.00f, 1.00f};
    case M_ASLEEP:    return {0.00f, 0.00f, 0.00f, 0.96f, 0.05f};
    default:          return {0.00f, 0.00f, 0.00f, 1.00f, 1.00f};
  }
}
Expr cur = exprFor(M_NEUTRAL);
float joy = 62, energy = 78, annoy = 0;
uint32_t lastInteract = 0;
Mood baseMood = M_NEUTRAL, reactMood = M_NEUTRAL;
uint32_t reactEnd = 0, reactStart = 0; bool reacting = false;
bool held = false; uint32_t liftMs = 0, stillMs = 0, lastShake = 0;
uint32_t peakTimes[6]; uint8_t peakCount = 0; float prevDyn = 0;
uint32_t lastTap = 0; uint8_t tapBurst = 0; uint32_t lastPurr = 0; bool petting = false;
float blinkF = 1, blinkTarget = 1; uint8_t blinkPhase = 0; uint32_t nextBlink = 2000, blinkT = 0;
uint8_t blinkCount = 0;
float lookX = 0, lookY = 0, tLookX = 0, tLookY = 0; uint32_t nextLook = 0;
float breath = 0;

// Alarm
uint32_t alarmAt = 0;   // millis when to fire, 0 = none
bool alarmRinging = false;
uint32_t lastAlarmSound = 0;

void applyBrightness() {
  uint8_t b = asleep ? max(30, brightness / 4) : brightness;
  panel->setBrightness(b);
}

void react(Mood m, uint32_t ms) { reactMood = m; reactStart = millis(); reactEnd = reactStart + ms; reacting = true; }
void noteInteraction() { lastInteract = millis(); }
void wakeUp(bool startled) {
  if (!asleep) return;
  asleep = false; energy = max(energy, 50.f); applyBrightness();
  sound(SND_YAWN);
  if (startled) react(M_SURPRISED, 600);
}
void goToSleep() { asleep = true; applyBrightness(); sound(SND_YAWN); }

void onTap() {
  uint32_t now = millis(); noteInteraction();
  if (alarmRinging) { alarmRinging = false; alarmAt = 0; sound(SND_TAP); return; }
  if (asleep) { wakeUp(true); return; }
  if (now - lastTap < 500) tapBurst++; else tapBurst = 1; lastTap = now;
  joy = clampf(joy + 5, 0, 100);
  if (tapBurst >= 5) { annoy = clampf(annoy + 16, 0, 100); react(M_ANGRY, 1200); sound(SND_ANGRY); tapBurst = 0; }
  else { annoy = clampf(annoy - 2, 0, 100); react(M_HAPPY, 700); sound(random(2) ? SND_TAP : SND_HAPPY); blinkPhase = 0; nextBlink = now; }
}
void onPickup() { noteInteraction(); wakeUp(false); energy = clampf(energy + 4, 0, 100); react(M_SURPRISED, 1000); sound(SND_SURPRISE); }
void onPutDown() { noteInteraction(); joy = clampf(joy + 4, 0, 100); react(M_HAPPY, 1100); sound(SND_HAPPY); }
void onShake(bool strong) {
  noteInteraction(); wakeUp(false);
  if (strong) { annoy = clampf(annoy + 25, 0, 100); joy = clampf(joy - 3, 0, 100); react(M_ANGRY, 2000); sound(SND_ANGRY); }
  else { annoy = clampf(annoy + 5, 0, 100); react(M_DIZZY, 1600); sound(SND_SURPRISE); }
}

void updateMood(float dt, uint32_t now) {
  float idle = (now - lastInteract) / 1000.f;
  joy += (60 - joy) * (1 - expf(-dt / 100.f));
  if (idle > 80) joy -= 0.08f * dt;
  if (asleep) energy += 1.0f * dt; else energy -= 0.05f * dt;
  annoy -= 3.0f * dt;
  joy = clampf(joy, 0, 100); energy = clampf(energy, 0, 100); annoy = clampf(annoy, 0, 100);
  if (!asleep && energy < 22 && idle > 20) goToSleep();
  if (asleep && energy > 82) { asleep = false; applyBrightness(); sound(SND_YAWN); }
  Mood old = baseMood;
  if (asleep) baseMood = M_ASLEEP;
  else if (annoy > 45) baseMood = M_ANGRY;
  else if (energy < 35) baseMood = M_SLEEPY;
  else if (joy < 35) baseMood = M_SAD;
  else if (joy > 65) baseMood = M_HAPPY;
  else baseMood = M_NEUTRAL;
  if (baseMood != old && !reacting) {
    if (baseMood == M_SLEEPY || baseMood == M_SAD) sound(SND_YAWN);
    else if (baseMood == M_HAPPY) sound(SND_HAPPY);
  }
}

void touchDown(int x, int y, uint32_t now) {
  ts.down = true; ts.x = ts.sx = x; ts.y = ts.sy = y; ts.t0 = now; ts.path = 0;
  noteInteraction();
  if (asleep) wakeUp(true);
}
void touchMove(int x, int y, uint32_t now) {
  int dx = x - ts.x, dy = y - ts.y;
  ts.path += sqrtf(dx * dx + dy * dy);
  ts.x = x; ts.y = y;
  static int dir = 0, rev = 0;
  int nd = dx > 6 ? 1 : (dx < -6 ? -1 : 0);
  if (nd && nd != dir) { rev++; dir = nd; }
  if (rev >= 3 && ts.path > 60) petting = true;
}
void touchUp(uint32_t now) {
  ts.down = false;
  bool tap = (now - ts.t0) < 400 && ts.path < 30;
  if (tap && !petting) onTap();
  petting = false;
}
void pollTouch(uint32_t now) {
  if (!touchOK) return;
  static uint32_t last = 0;
  if (!(touchFlag || (ts.down && now - last >= 16))) {
    if (ts.down && now - ts.lastSeen > 80) touchUp(now);
    return;
  }
  touchFlag = false; last = now;
  int16_t px[2], py[2];
  uint8_t n = touch.getPoint(px, py, 2);
  if (n > 0) {
    ts.lastSeen = now;
    int x = constrain(px[0], 0, SCREEN_W - 1), y = constrain(py[0], 0, SCREEN_H - 1);
    if (!ts.down) touchDown(x, y, now); else touchMove(x, y, now);
  } else if (ts.down && now - ts.lastSeen > 80) touchUp(now);
}

void pollMotion(uint32_t now) {
  static uint32_t last = 0; if (!imuOK || now - last < 30) return;
  uint32_t dt = now - last; last = now;
  if (!readIMU()) return;
  float mag = sqrtf(ax * ax + ay * ay + az * az);
  float dyn = fabsf(mag - 1.f);
  if (dyn > 0.55f && prevDyn <= 0.55f && peakCount < 6) peakTimes[peakCount++] = now;
  prevDyn = dyn;
  uint8_t k = 0;
  for (uint8_t i = 0; i < peakCount; i++) if (now - peakTimes[i] < 800) peakTimes[k++] = peakTimes[i];
  peakCount = k;
  if (peakCount >= 3 && now - lastShake > 1200) {
    lastShake = now; onShake(peakCount >= 4); peakCount = 0; held = true; stillMs = 0; return;
  }
  float dot = ax * restX + ay * restY + az * restZ;
  float cosA = clampf(dot / (max(mag, 0.01f) * max(sqrtf(restX*restX + restY*restY + restZ*restZ), 0.01f)), -1, 1);
  float tilt = acosf(cosA) * 57.3f;
  if (dyn < 0.05f) stillMs += dt; else stillMs = 0;
  if (!held && stillMs > 600) { restX = lerpf(restX, ax, 0.1f); restY = lerpf(restY, ay, 0.1f); restZ = lerpf(restZ, az, 0.1f); }
  if (!held) {
    if ((dyn > 0.06f && dyn < 0.5f) || tilt > 15) liftMs += dt; else liftMs = 0;
    if (liftMs > 200) { held = true; liftMs = 0; onPickup(); }
  } else if ((stillMs > 800 && tilt < 10) || stillMs > 1600) {
    held = false; restX = ax; restY = ay; restZ = az; onPutDown();
  }
}

void updateAnim(uint32_t now, float &offX, float &offY) {
  if (reacting && now >= reactEnd) reacting = false;
  Mood shown = reacting ? reactMood : baseMood;
  Expr t = exprFor(shown);
  float e = 0.16f;
  cur.tired = lerpf(cur.tired, t.tired, e);
  cur.angry = lerpf(cur.angry, t.angry, e);
  cur.happy = lerpf(cur.happy, t.happy, e);
  cur.scale = lerpf(cur.scale, t.scale, e);
  cur.open  = lerpf(cur.open,  t.open,  e);

  // Breathing scale
  breath = 1.0f + 0.012f * sinf(now / 900.f);

  // Blinking with occasional double blink
  if (!asleep) {
    if (blinkPhase == 0 && now >= nextBlink) {
      blinkTarget = 0; blinkPhase = 1; blinkT = now + 65;
    } else if (blinkPhase == 1 && now >= blinkT) {
      blinkTarget = 1; blinkPhase = 2; blinkT = now + 80;
    } else if (blinkPhase == 2 && now >= blinkT) {
      blinkPhase = 0;
      if (blinkCount == 0 && random(4) == 0) {
        blinkCount = 1;
        nextBlink = now + 140;
      } else {
        blinkCount = 0;
        nextBlink = now + random(1600, 4800);
      }
    }
  } else {
    blinkTarget = 1;
  }
  blinkF = lerpf(blinkF, blinkTarget, 0.45f);

  // Rich looking behavior
  if (petting) {
    tLookX = clampf((ts.x - CENTER_X) * 0.16f, -32, 32);
    tLookY = clampf((ts.y - CENTER_Y) * 0.12f, -22, 22);
  } else if (held && imuOK) {
    tLookX = clampf(ax * 36, -32, 32);
    tLookY = clampf(ay * 28, -22, 22);
  } else if (shown != M_ASLEEP && shown != M_ANGRY) {
    if (now >= nextLook) {
      int kind = random(10);
      if (kind < 3) { tLookX = random(-34, 35); tLookY = random(-16, 17); }
      else if (kind < 5) { tLookX = random(-40, 41); tLookY = random(-8, 9); }   // side look
      else if (kind < 7) { tLookX = random(-10, 11); tLookY = random(-28, 5); }  // up look
      else if (kind < 9) { tLookX = 0; tLookY = 0; }                               // center
      else { tLookX = random(-20, 21); tLookY = random(4, 18); }                  // down look
      nextLook = now + random(700, 2800);
    }
    if (shown == M_SAD) tLookY = max(tLookY, 10.f);
    if (shown == M_SLEEPY) tLookY = max(tLookY, 4.f);
  } else {
    tLookX = 0; tLookY = 0;
  }
  lookX = lerpf(lookX, tLookX, 0.22f);
  lookY = lerpf(lookY, tLookY, 0.22f);

  float wobX = 0, wobY = 0;
  float rt = reacting ? (float)(now - reactStart) : 0;
  if (reacting && reactMood == M_DIZZY) {
    wobX = 18 * cosf(rt * 0.012f);
    wobY = 12 * sinf(rt * 0.012f);
  } else if (reacting && reactMood == M_ANGRY) {
    wobX = 2.8f * sinf(rt * 0.08f);
  } else if (reacting && (reactMood == M_HAPPY || reactMood == M_LOVE) && rt < 700) {
    wobY = -6.0f * fabsf(sinf(rt * 0.011f));
  } else if (reacting && reactMood == M_SURPRISED && rt < 400) {
    wobY = -8.0f * (1.0f - rt / 400.f);
  }
  if (asleep) wobY += 1.4f * sinf(now / 820.f);

  // Tiny idle micro movement
  if (!asleep && !reacting) {
    wobX += 0.6f * sinf(now / 2100.f);
    wobY += 0.4f * cosf(now / 1800.f);
  }

  offX = lookX + wobX;
  offY = lookY + wobY;
}

void drawEyes(float offX, float offY) {
  float sc = cur.scale * breath;
  int w = (int)(BASE_W * sc);
  int fullH = (int)(BASE_H * sc);
  int h = (int)(fullH * cur.open * blinkF);
  if (h < 8) h = 8;
  int r = min((int)(BASE_R * sc), min(h / 2, w / 2));

  for (int s = 0; s < 2; s++) {
    bool left = (s == 0);
    int cx = CENTER_X + (left ? -1 : 1) * (BASE_SP / 2) + (int)offX;
    int cy = CENTER_Y + (int)offY;
    int x0 = cx - w / 2;
    int y0 = cy - h / 2;

    // Main eye
    gfx->fillRoundRect(x0, y0, w, h, r, EYE);

    // Soft upper highlight (subtle)
    if (h > 20) {
      int hlW = w * 2 / 5;
      int hlH = max(6, h / 6);
      gfx->fillRoundRect(x0 + w / 6, y0 + h / 8, hlW, hlH, r / 3, HIGHLIGHT);
    }

    // Lids
    int top = y0 - 2;
    if (cur.tired > 0.05f) {
      int d = (int)(h * 0.45f * cur.tired);
      if (left) gfx->fillTriangle(x0, top, x0 + w, top, x0, top + d, BG);
      else      gfx->fillTriangle(x0, top, x0 + w, top, x0 + w, top + d, BG);
    }
    if (cur.angry > 0.05f) {
      int d = (int)(h * 0.45f * cur.angry);
      if (left) gfx->fillTriangle(x0, top, x0 + w, top, x0 + w, top + d, BG);
      else      gfx->fillTriangle(x0, top, x0 + w, top, x0, top + d, BG);
    }
    if (cur.happy > 0.05f) {
      int off = (int)(fullH * 0.40f * cur.happy);
      gfx->fillRoundRect(x0 - 2, y0 + h - off, w + 4, fullH, r, BG);
    }
  }
}

void renderFrame(uint32_t now) {
  float offX, offY;
  updateAnim(now, offX, offY);
  gfx->fillScreen(BG);
  drawEyes(offX, offY);
  gfx->flush();
}

void updatePetting(float dt, uint32_t now) {
  if (petting) {
    noteInteraction();
    joy = clampf(joy + 4.0f * dt, 0, 100);
    annoy = clampf(annoy - 5.0f * dt, 0, 100);
    if (!reacting || reactMood != M_LOVE) react(M_LOVE, 600);
    else reactEnd = now + 600;
    if (now - lastPurr > 700) { sound(SND_PURR); lastPurr = now; }
  }
}

void checkAlarm(uint32_t now) {
  if (alarmAt && now >= alarmAt && !alarmRinging) {
    alarmRinging = true;
    wakeUp(true);
    react(M_SURPRISED, 2000);
  }
  if (alarmRinging && now - lastAlarmSound > 1800) {
    sound(SND_ALARM);
    lastAlarmSound = now;
  }
}

void handleSerial() {
  static char buf[32];
  static uint8_t idx = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      buf[idx] = 0;
      if (idx > 0) {
        if (buf[0] == 'b' && idx > 1) {
          int v = atoi(buf + 1);
          brightness = constrain(v, 20, 255);
          prefs.putUChar("bri", brightness);
          applyBrightness();
          Serial.printf("brightness %d\n", brightness);
        } else if (buf[0] == 'a' && idx > 1) {
          int mins = atoi(buf + 1);
          if (mins > 0 && mins < 1440) {
            alarmAt = millis() + (uint32_t)mins * 60000UL;
            alarmRinging = false;
            Serial.printf("alarm in %d min\n", mins);
          }
        } else if (strcmp(buf, "off") == 0) {
          alarmAt = 0; alarmRinging = false;
          Serial.println("alarm off");
        }
      }
      idx = 0;
    } else if (idx < 30) {
      buf[idx++] = c;
    }
    switch (c) {
      case 'n': react(M_NEUTRAL, 2000); break;
      case 'h': react(M_HAPPY, 2000); break;
      case 's': react(M_SLEEPY, 2000); break;
      case 'g': react(M_ANGRY, 2000); break;
      case 'd': react(M_SAD, 2000); break;
      case 'u': react(M_SURPRISED, 2000); break;
      case 'l': react(M_LOVE, 2000); break;
      case 'z': asleep ? wakeUp(true) : goToSleep(); break;
      case '+': brightness = min(255, brightness + 20); prefs.putUChar("bri", brightness); applyBrightness(); break;
      case '-': brightness = max(20, brightness - 20); prefs.putUChar("bri", brightness); applyBrightness(); break;
    }
  }
}

void setup() {
  Serial.begin(115200); delay(200);
  Serial.println("Charlie alive booting...");
  prefs.begin("charlie", false);
  muted = prefs.getBool("mute", false);
  brightness = prefs.getUChar("bri", 160);

  if (!gfx->begin()) { Serial.println("Display fail"); while (1) delay(1000); }
  gfx->fillScreen(BG); gfx->flush();
  applyBrightness();

  Wire.begin(PIN_SDA, PIN_SCL); Wire.setClock(400000);

  touch.setPins(TP_RST, TP_INT);
  touchOK = touch.begin(Wire, 0x5A, PIN_SDA, PIN_SCL);
  if (touchOK) {
    touch.setMaxCoordinates(SCREEN_W, SCREEN_H);
    pinMode(TP_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TP_INT), onTouchInt, FALLING);
  }

  initIMU();
  initAudio();

  randomSeed(esp_random());
  uint32_t now = millis();
  lastInteract = now;
  nextBlink = now + 900;
  nextLook = now + 300;
  sound(SND_BOOT);

  Serial.println("Serial: n h s g d u l z  + -  b120  a30  off");
}

void loop() {
  static uint32_t lastFrame = 0, lastMood = 0;
  uint32_t now = millis();

  handleSerial();
  pollTouch(now);
  pollMotion(now);
  checkAlarm(now);

  if (now - lastMood >= 100) {
    float dt = (now - lastMood) / 1000.f;
    lastMood = now;
    updatePetting(dt, now);
    updateMood(dt, now);
  }

  if (now - lastFrame >= 33) {
    lastFrame = now;
    renderFrame(now);
  }
}
