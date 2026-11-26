/*
  Charlie - desktop companion
  Waveshare ESP32-S3-Touch-AMOLED-1.75 (466x466 CO5300 QSPI AMOLED, CST9217 touch, QMI8658 IMU)

  All the personality lives in charlie_core.h (hardware free). This file only wires up:
    display  -> software framebuffer in PSRAM, only the rows that changed get pushed
    touch    -> poke / pet / swipe-down menu
    IMU      -> shake, free-fall, auto-rotate so he is always face up
    flash    -> remembers eye color + orientation

  Serial (115200):  d demo all animations   n next animation   c next color
                    o rotate 'up' 90 deg    m mirror rotation  s sleep/wake   f fps on/off
*/

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include "SensorQMI8658.hpp"
#include "charlie_core.h"

// ---- Waveshare ESP32-S3-Touch-AMOLED-1.75 pins
#define LCD_CS    12
#define LCD_SCLK  38
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_RST   39
#define IIC_SDA   15
#define IIC_SCL   14
#define TP_INT    11
#define TP_RESET  40

static const uint8_t BRIGHTNESS = 190;  // AMOLED: lower = longer life for the pixels

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(bus, LCD_RST, 0, ch::SCR_W, ch::SCR_H, 6, 0, 0, 0);

TouchDrvCST92xx touch;
SensorQMI8658 qmi;
Preferences prefs;

static uint16_t *fb = nullptr;
static bool haveTouch = false, haveImu = false;
static volatile bool touchIrq = false;
static bool touchDown = false;
static int16_t tX[2], tY[2];
static uint32_t lastTouchSeen = 0;
static float lastAx = 0, lastAy = 0, lastAz = 1;
static uint32_t lastFrame = 0, lastSave = 0, fpsT = 0;
static int fpsCount = 0;
static bool showFps = false;
static int lastRows = 0;

static void IRAM_ATTR onTouchIrq() { touchIrq = true; }

static void loadSettings() {
  prefs.begin("charlie", true);
  ch::settings.colorIdx = prefs.getUChar("col", 0) & 7;
  ch::settings.orientOffset = prefs.getShort("ori", 0);
  ch::settings.mirror = prefs.getBool("mir", false);
  prefs.end();
}

static void saveSettings() {
  prefs.begin("charlie", false);
  prefs.putUChar("col", ch::settings.colorIdx);
  prefs.putShort("ori", ch::settings.orientOffset);
  prefs.putBool("mir", ch::settings.mirror);
  prefs.end();
  ch::settings.dirty = false;
  Serial.println("settings saved");
}

static void setupTouch() {
  pinMode(TP_RESET, OUTPUT);
  digitalWrite(TP_RESET, LOW);  delay(30);
  digitalWrite(TP_RESET, HIGH); delay(50);
  touch.setPins(TP_RESET, TP_INT);
  haveTouch = touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
  if (!haveTouch) { Serial.println("touch: NOT FOUND"); return; }
  Serial.printf("touch: %s\n", touch.getModelName());
  // same sequence as Waveshare's working LVGL example
  touch.sleep();
  touch.reset();
  touch.setMaxCoordinates(ch::SCR_W, ch::SCR_H);
  touch.setMirrorXY(true, true);
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(TP_INT, onTouchIrq, FALLING);
}

static void setupImu() {
  haveImu = qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (!haveImu) { Serial.println("imu: NOT FOUND"); return; }
  qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_1000Hz, SensorQMI8658::LPF_MODE_0);
  qmi.enableAccelerometer();
  Serial.println("imu: ok");
}

static void readInputs(uint32_t now, ch::Input &in) {
  // ---- touch: poll when the controller signalled, and keep polling while a finger is down
  if (haveTouch && (touchIrq || touchDown)) {
    touchIrq = false;
    uint8_t n = touch.getPoint(tX, tY, 1);
    if (n > 0) {
      touchDown = true;
      lastTouchSeen = now;
    } else if (touchDown && now - lastTouchSeen > 70) {
      touchDown = false;
    }
  }
  in.touch = touchDown;
  in.tx = tX[0];
  in.ty = tY[0];

  // ---- IMU (acceleration in g)
  in.imu = haveImu;
  if (haveImu && qmi.getDataReady()) {
    float x, y, z;
    if (qmi.getAccelerometer(x, y, z)) { lastAx = x; lastAy = y; lastAz = z; }
  }
  in.ax = lastAx; in.ay = lastAy; in.az = lastAz;
}

static void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'd') { ch::demoMode = !ch::demoMode; Serial.println(ch::demoMode ? "demo ON" : "demo OFF"); }
    else if (c == 'n') { static int i = 1; ch::forceAnim(i); Serial.printf("anim: %s\n", ch::animName(i)); i = i % (ch::animCount() - 1) + 1; }
    else if (c == 'c') ch::nextColor();
    else if (c == 'o') ch::orientStep();
    else if (c == 'm') ch::mirrorToggle();
    else if (c == 's') ch::toggleSleep();
    else if (c == 'f') showFps = !showFps;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Charlie waking up...");
  Serial.printf("PSRAM: %u bytes\n", (unsigned)ESP.getPsramSize());

  Wire.begin(IIC_SDA, IIC_SCL);
  Wire.setClock(400000);

  fb = (uint16_t *)ps_malloc((size_t)ch::SCR_W * ch::SCR_H * 2);
  if (!fb) fb = (uint16_t *)malloc((size_t)ch::SCR_W * ch::SCR_H * 2);
  if (!fb) { Serial.println("framebuffer alloc FAILED"); while (true) delay(1000); }

  if (!panel->begin()) Serial.println("panel begin FAILED");
  panel->setBrightness(0);
  panel->fillScreen(RGB565_BLACK);

  loadSettings();
  setupTouch();
  setupImu();

  ch::init(fb, esp_random());
  ch::start(millis());

  // first frame, then fade the backlight up so there is no flash of garbage
  ch::Input in;
  int y0, y1;
  ch::update(millis(), 0.016f, in);
  ch::render(y0, y1);
  panel->draw16bitRGBBitmap(0, y0, fb + (size_t)y0 * ch::SCR_W, ch::SCR_W, y1 - y0 + 1);
  for (int b = 0; b <= BRIGHTNESS; b += 10) { panel->setBrightness(b); delay(8); }
  panel->setBrightness(BRIGHTNESS);

  lastFrame = millis();
  Serial.println("Hi, I'm Charlie!");
}

void loop() {
  uint32_t now = millis();
  float dt = (now - lastFrame) / 1000.0f;
  lastFrame = now;

  handleSerial();

  ch::Input in;
  readInputs(now, in);
  ch::update(now, dt, in);

  int y0, y1;
  ch::render(y0, y1);
  lastRows = y1 - y0 + 1;
  panel->draw16bitRGBBitmap(0, y0, fb + (size_t)y0 * ch::SCR_W, ch::SCR_W, lastRows);

  if (ch::settings.dirty && now - lastSave > 1500) { lastSave = now; saveSettings(); }

  if (showFps) {
    fpsCount++;
    if (now - fpsT >= 1000) { Serial.printf("fps %d  rows %d\n", fpsCount, lastRows); fpsCount = 0; fpsT = now; }
  }

  // ~60 fps cap
  uint32_t spent = millis() - now;
  if (spent < 14) delay(14 - spent);
}
