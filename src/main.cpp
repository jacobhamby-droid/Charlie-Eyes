#include <Arduino.h>
/*
  Charlie - desktop companion for ESP32-S3 1.75" AMOLED (466x466, CO5300, QSPI)
  Tested layout assumes the Waveshare ESP32-S3-Touch-AMOLED-1.75 pinout.

  Libraries: "GFX Library for Arduino" (moononournation) 1.5+
  Board settings: ESP32S3 Dev Module, PSRAM = "OPI PSRAM", Flash 16MB,
                  USB CDC On Boot = Enabled

  Serial commands (115200): 0-6 pick a mood, 'a' toggles auto mood changes,
  'b' blink, 'l' look at a random spot.
*/

#include <Arduino_GFX_Library.h>
#include <math.h>

// ---------- Pins (Waveshare ESP32-S3-Touch-AMOLED-1.75) ----------
#define LCD_CS    12
#define LCD_SCLK  38
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_RST   39

#define SCREEN_W 466
#define SCREEN_H 466

// Only the horizontal band containing the eyes is redrawn/flushed each frame.
#define BAND_H 240
// Keep Y and height EVEN: the CO5300 AMOLED wants even window coordinates.
#define BAND_Y 112

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(bus, LCD_RST, 0, SCREEN_W, SCREEN_H, 6, 0, 0, 0);
Arduino_Canvas *cv = new Arduino_Canvas(SCREEN_W, BAND_H, panel, 0, BAND_Y);

// ---------- Moods ----------
struct Mood {
  const char *name;
  float wScale;     // eye width multiplier
  float hScale;     // eye height multiplier
  float lidInner;   // top lid coverage at the inner edge (0..1 of eye height)
  float lidOuter;   // top lid coverage at the outer edge
  float happy;      // 0..1 cheek squish (turns eyes into ^ ^)
  uint8_t r, g, b;  // eye color
};

const Mood MOODS[] = {
  //  name        w     h     inner outer happy  color
  { "neutral",   1.00f, 1.00f, 0.00f, 0.00f, 0.00f,  80, 220, 255 },
  { "happy",     1.05f, 1.00f, 0.00f, 0.00f, 0.55f, 120, 255, 160 },
  { "sad",       0.95f, 0.90f, 0.00f, 0.45f, 0.00f, 110, 160, 255 },
  { "angry",     1.05f, 0.85f, 0.50f, 0.05f, 0.00f, 255,  70,  60 },
  { "sleepy",    1.05f, 0.80f, 0.40f, 0.40f, 0.00f, 120, 140, 200 },
  { "surprised", 0.85f, 1.25f, 0.00f, 0.00f, 0.00f, 255, 255, 255 },
  { "love",      1.05f, 0.95f, 0.00f, 0.00f, 0.80f, 255, 110, 170 },
};
const int NUM_MOODS = sizeof(MOODS) / sizeof(MOODS[0]);

// ---------- Eye geometry ----------
const float BASE_HALF_W = 62.0f;   // half width of an eye at scale 1
const float BASE_HALF_H = 78.0f;   // half height of an eye at scale 1
const float EYE_SPACING = 95.0f;   // center distance from screen middle
const float SQUIRCLE_N  = 4.0f;    // superellipse exponent: 2 = ellipse, higher = boxier

// Animated (current) state, eased toward targets every frame
struct EyeState {
  float wScale = 1, hScale = 1, lidInner = 0, lidOuter = 0, happy = 0;
  float r = 80, g = 220, b = 255;
  float gazeX = 0, gazeY = 0;
  float blink = 0;  // 0 = open, 1 = closed
} eye;

int moodIdx = 0;
bool autoMood = true;
float gazeTX = 0, gazeTY = 0;

uint32_t lastFrame = 0, nextBlink = 0, nextGaze = 0, nextMood = 0, blinkStart = 0;
bool blinking = false;
uint16_t BG;

// ---------- Helpers ----------
static inline float approach(float cur, float target, float k, float dt) {
  return cur + (target - cur) * (1.0f - expf(-k * dt));
}

// Filled squircle drawn as horizontal scanlines.
void fillSquircle(int cx, int cy, float a, float b, uint16_t color) {
  int ib = (int)ceilf(b);
  for (int dy = -ib; dy <= ib; dy++) {
    float t = fabsf((float)dy) / b;
    if (t > 1.0f) continue;
    float hw = a * powf(1.0f - powf(t, SQUIRCLE_N), 1.0f / SQUIRCLE_N);
    int w = (int)(hw * 2.0f);
    if (w > 0) cv->drawFastHLine((int)(cx - hw), cy + dy, w, color);
  }
}

// side = -1 for left eye, +1 for right eye (inner edge faces the middle)
void drawEye(int cx, int cy, float a, float b, int side, uint16_t color) {
  fillSquircle(cx, cy, a, b, color);

  // Top lid (angry / sad / sleepy): a black quad cutting the top of the eye.
  if (eye.lidInner > 0.01f || eye.lidOuter > 0.01f) {
    float yInner = cy - b + eye.lidInner * 2.0f * b;
    float yOuter = cy - b + eye.lidOuter * 2.0f * b;
    float xInner = cx - side * (a + 3);   // inner edge x (toward screen middle)
    float xOuter = cx + side * (a + 3);
    float top = cy - b - 3;
    cv->fillTriangle(xInner, top, xOuter, top, xInner, yInner, BG);
    cv->fillTriangle(xOuter, top, xOuter, yOuter, xInner, yInner, BG);
  }

  // Cheek push-up (happy / love): big black circle rising from below.
  if (eye.happy > 0.01f) {
    float r = a * 1.25f;
    float ccy = cy + b + r - eye.happy * b * 1.05f;
    cv->fillCircle(cx, (int)ccy, (int)r, BG);
  }
}

void setMood(int idx) {
  moodIdx = (idx + NUM_MOODS) % NUM_MOODS;
  Serial.printf("Charlie is feeling %s\n", MOODS[moodIdx].name);
}

void startBlink(uint32_t now) {
  blinking = true;
  blinkStart = now;
}

void updateBehavior(uint32_t now, float dt) {
  // Blink: ~180 ms close/open
  if (!blinking && now >= nextBlink) startBlink(now);
  if (blinking) {
    float t = (now - blinkStart) / 180.0f;
    if (t >= 1.0f) {
      blinking = false;
      eye.blink = 0;
      nextBlink = now + random(2000, 6000);
      if (random(0, 5) == 0) nextBlink = now + 250;  // occasional double blink
    } else {
      eye.blink = (t < 0.5f) ? t * 2.0f : (1.0f - t) * 2.0f;
    }
  }

  // Look around
  if (now >= nextGaze) {
    gazeTX = random(-30, 31);
    gazeTY = random(-18, 19);
    if (random(0, 3) == 0) { gazeTX = 0; gazeTY = 0; }
    nextGaze = now + random(900, 3200);
  }

  // Mood changes
  if (autoMood && now >= nextMood) {
    setMood(random(0, NUM_MOODS));
    nextMood = now + random(8000, 16000);
  }

  // Ease current state toward targets
  const Mood &m = MOODS[moodIdx];
  float k = 9.0f;
  eye.wScale   = approach(eye.wScale,   m.wScale,   k, dt);
  eye.hScale   = approach(eye.hScale,   m.hScale,   k, dt);
  eye.lidInner = approach(eye.lidInner, m.lidInner, k, dt);
  eye.lidOuter = approach(eye.lidOuter, m.lidOuter, k, dt);
  eye.happy    = approach(eye.happy,    m.happy,    k, dt);
  eye.r = approach(eye.r, m.r, 6, dt);
  eye.g = approach(eye.g, m.g, 6, dt);
  eye.b = approach(eye.b, m.b, 6, dt);
  eye.gazeX = approach(eye.gazeX, gazeTX, 14, dt);
  eye.gazeY = approach(eye.gazeY, gazeTY, 14, dt);
}

void renderFrame() {
  cv->fillScreen(BG);

  float a = BASE_HALF_W * eye.wScale;
  float b = BASE_HALF_H * eye.hScale * (1.0f - 0.92f * eye.blink);
  if (b < 3) b = 3;

  uint16_t color = cv->color565((uint8_t)eye.r, (uint8_t)eye.g, (uint8_t)eye.b);
  int midX = SCREEN_W / 2;
  int midY = BAND_H / 2;
  int gx = (int)eye.gazeX, gy = (int)eye.gazeY;

  drawEye(midX - (int)EYE_SPACING + gx, midY + gy, a, b, -1, color);
  drawEye(midX + (int)EYE_SPACING + gx, midY + gy, a, b, +1, color);

  cv->flush();
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c < '0' + NUM_MOODS) { autoMood = false; setMood(c - '0'); }
    else if (c == 'a') { autoMood = !autoMood; Serial.println(autoMood ? "auto mood ON" : "auto mood OFF"); }
    else if (c == 'b') startBlink(millis());
    else if (c == 'l') nextGaze = 0;
  }
}

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());

  delay(500);
  Serial.printf("PSRAM: %u bytes\n", (unsigned)ESP.getPsramSize());

  // NOTE: Arduino_Canvas::begin() initializes the panel itself, so do NOT call
  // panel->begin() separately, and set brightness AFTER this call.
  if (!cv->begin()) {
    Serial.println("Display/canvas init failed (is PSRAM enabled?)");
    while (true) delay(1000);
  }
  panel->setBrightness(200);
  BG = cv->color565(0, 0, 0);

  // Quick color self-test, drawn straight to the panel (same path as
  // Waveshare's HelloWorld). Expect: RED, GREEN, BLUE, then Charlie.
  panel->fillScreen(RGB565_RED);   Serial.println("RED");   delay(700);
  panel->fillScreen(RGB565_GREEN); Serial.println("GREEN"); delay(700);
  panel->fillScreen(RGB565_BLUE);  Serial.println("BLUE");  delay(700);
  panel->fillScreen(BG);

  uint32_t now = millis();
  nextBlink = now + 2000;
  nextGaze = now + 1000;
  nextMood = now + 10000;
  lastFrame = now;
  Serial.println("Hi, I'm Charlie!");
}

void loop() {
  uint32_t now = millis();
  float dt = (now - lastFrame) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  lastFrame = now;

  handleSerial();
  updateBehavior(now, dt);
  renderFrame();
}
