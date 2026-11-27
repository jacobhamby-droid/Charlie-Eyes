/*
  Charlie - desktop companion
  Waveshare ESP32-S3-Touch-AMOLED-1.75 (466x466 CO5300 QSPI AMOLED, CST9217 touch, QMI8658 IMU)

  All the personality lives in charlie_core.h (hardware free). This file only wires up:
    display  -> software framebuffer in PSRAM, only the rows that changed get pushed
    touch    -> poke / pet / swipe-down menu
    IMU      -> shake, free-fall, auto-rotate so he is always face up
    flash    -> remembers eye color + orientation

  Serial (115200):  h mic debug  v volume  t/a test timer/alarm ring  A-Z sound test
                    d demo all animations   n next animation   c next color
                    o rotate 'up' 90 deg    m mirror rotation  s sleep/wake   f fps on/off
*/

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Preferences.h>
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include "SensorQMI8658.hpp"
#include "SensorPCF85063.hpp"
#include "ESP_I2S.h"
#include "es8311.h"
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
#define I2S_MCLK  42
#define I2S_BCLK  9
#define I2S_WS    45
#define I2S_DOUT  8    // ESP32 -> codec (speaker)
#define I2S_DIN   10   // codec -> ESP32 (microphone)
#define PA_PIN    46   // speaker amp enable

static const uint8_t BRIGHTNESS = 190;  // AMOLED: lower = longer life for the pixels

// ---- ESP-NOW peer mood ----
struct PeerMood {
  uint8_t magic = 0xC3;
  uint8_t joy = 0, energy = 0, annoy = 0, love = 0;
  uint8_t event = 0;
};
static uint8_t myId = 0;
static uint32_t lastBroadcast = 0;
static bool espNowOk = false;

static uint8_t seenMac[6] = {0};
static bool seenAny = false;
static void onPeerRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(PeerMood)) return;
  PeerMood m;
  memcpy(&m, data, sizeof(m));
  if (m.magic != 0xC3) return;
  if (info && info->src_addr) {
    if (!seenAny || memcmp(seenMac, info->src_addr, 6) != 0) {
      memcpy(seenMac, info->src_addr, 6);
      seenAny = true;
      ch::onNewPeer(info->src_addr);
    }
  }
  ch::peerMood(m.joy / 100.0f, m.energy / 100.0f, m.annoy / 100.0f, m.love / 100.0f);
  if (m.event) ch::onPeerEvent(m.event);
  Serial.printf("peer j:%d e:%d a:%d l:%d ev:%d\n", m.joy, m.energy, m.annoy, m.love, m.event);
}

static void setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init failed"); return; }
  esp_now_register_recv_cb(onPeerRecv);
  esp_now_peer_info_t peer = {};
  memset(peer.peer_addr, 0xFF, 6);  // broadcast
  peer.channel = 0;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) { Serial.println("ESP-NOW peer add failed"); return; }
  espNowOk = true;
  Serial.println("ESP-NOW ready (broadcast mood)");
}

static void broadcastMood() {
  if (!espNowOk) return;
  PeerMood m;
  m.joy = (uint8_t)(ch::mind.joy * 100);
  m.energy = (uint8_t)(ch::mind.energy * 100);
  m.annoy = (uint8_t)(ch::mind.annoy * 100);
  m.love = (uint8_t)(ch::mind.love * 100);
  m.event = ch::pendingEvent;
  if (ch::pendingEvent) ch::pendingEvent = 0;
  uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  esp_now_send(bcast, (uint8_t*)&m, sizeof(m));
}

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(bus, LCD_RST, 0, ch::SCR_W, ch::SCR_H, 6, 0, 0, 0);

TouchDrvCST92xx touch;
SensorQMI8658 qmi;
SensorPCF85063 rtc;
I2SClass i2s;
Preferences prefs;

static bool haveRtc = false, haveAudio = false, rtcTrusted = false;
static uint8_t rtcH = 0, rtcM = 0, rtcS = 0;
static uint32_t lastRtcRead = 0;
static bool showMic = false;

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
  ch::settings.vol = prefs.getUChar("vol", 2) & 3;
  ch::settings.alarmOn = prefs.getBool("aon", false);
  ch::settings.alarmH = prefs.getUChar("ah", 7) % 24;
  ch::settings.alarmM = prefs.getUChar("am", 0) % 60;
  ch::settings.timerMin = constrain(prefs.getUChar("tmin", 5), 1, 99);
  rtcTrusted = prefs.getBool("rtct", false);
  prefs.end();
}

static void saveSettings() {
  prefs.begin("charlie", false);
  prefs.putUChar("col", ch::settings.colorIdx);
  prefs.putShort("ori", ch::settings.orientOffset);
  prefs.putBool("mir", ch::settings.mirror);
  prefs.putUChar("vol", ch::settings.vol);
  prefs.putBool("aon", ch::settings.alarmOn);
  prefs.putUChar("ah", ch::settings.alarmH);
  prefs.putUChar("am", ch::settings.alarmM);
  prefs.putUChar("tmin", ch::settings.timerMin);
  prefs.putBool("rtct", rtcTrusted);
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

static void pollRtc(uint32_t now, ch::Input &in);

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
  pollRtc(now, in);
}


// ---------------------------------------------------------------------------
// Audio: ES8311 codec. One task on core 0 reads the mic, hands it to the hearing code,
// and writes Charlie's synthesized voice to the speaker, 256 frames at a time.
// ---------------------------------------------------------------------------
#define AUDIO_RATE 16000

static bool codecInit() {
  es8311_handle_t h = es8311_create(0, ES8311_ADDRRES_0);
  if (!h) return false;
  const es8311_clock_config_t clk = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = AUDIO_RATE * 256,
    .sample_frequency = AUDIO_RATE
  };
  if (es8311_init(h, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
  es8311_sample_frequency_config(h, clk.mclk_frequency, clk.sample_frequency);
  es8311_microphone_config(h, false);
  es8311_voice_volume_set(h, 90, NULL);
  es8311_microphone_gain_set(h, (es8311_mic_gain_t)3);
  return true;
}

static void audioTask(void *) {
  static int16_t rx[256 * 2], tx[256 * 2], mono[256], out[256];
  for (;;) {
    size_t got = i2s.readBytes((char *)rx, sizeof(rx));
    if (got < sizeof(rx)) { vTaskDelay(1); continue; }
    // the mic could be on either slot: use the louder one
    int64_t el = 0, er = 0;
    for (int i = 0; i < 256; i++) { el += abs(rx[2 * i]); er += abs(rx[2 * i + 1]); }
    int off = (er > el) ? 1 : 0;
    for (int i = 0; i < 256; i++) mono[i] = rx[2 * i + off];
    snd::micFeed(mono, 256, snd::E.active);
    snd::render(out, 256);
    for (int i = 0; i < 256; i++) { tx[2 * i] = out[i]; tx[2 * i + 1] = out[i]; }
    i2s.write((uint8_t *)tx, sizeof(tx));
  }
}

static void setupAudio() {
  pinMode(PA_PIN, OUTPUT);
  digitalWrite(PA_PIN, HIGH);
  i2s.setPins(I2S_BCLK, I2S_WS, I2S_DOUT, I2S_DIN, I2S_MCLK);
  if (!i2s.begin(I2S_MODE_STD, AUDIO_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("audio: I2S FAILED");
    return;
  }
  if (!codecInit()) { Serial.println("audio: codec FAILED"); return; }
  haveAudio = true;
  xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 3, NULL, 0);
  Serial.println("audio: ok");
}

// ---------------------------------------------------------------------------
// Real-time clock (PCF85063). Only trusted once the user confirms it on the clock page.
// ---------------------------------------------------------------------------
static void setupRtc() {
  haveRtc = rtc.begin(Wire, IIC_SDA, IIC_SCL);
  if (!haveRtc) { Serial.println("rtc: NOT FOUND"); return; }
  RTC_DateTime t = rtc.getDateTime();
  if (t.getYear() < 2024) {
    // never set (or battery was out): placeholder time; the alarm only trusts it after you confirm it on the clock page
    rtc.setDateTime(2026, 1, 1, 12, 0, 0);
    rtcTrusted = false;
  }
  Serial.printf("rtc: ok (%s)\n", rtcTrusted ? "trusted" : "needs setting");
}

static void pollRtc(uint32_t now, ch::Input &in) {
  if (haveRtc && now - lastRtcRead >= 1000) {
    lastRtcRead = now;
    RTC_DateTime t = rtc.getDateTime();
    rtcH = t.getHour(); rtcM = t.getMinute(); rtcS = t.getSecond();
  }
  if (haveRtc && ch::clockReq.pending) {
    ch::clockReq.pending = false;
    rtc.setDateTime(2026, 1, 1, ch::clockReq.h, ch::clockReq.m, 0);
    rtcH = ch::clockReq.h; rtcM = ch::clockReq.m; rtcS = 0;
    rtcTrusted = true;
    ch::settings.dirty = true;
    Serial.printf("clock set to %02d:%02d\n", rtcH, rtcM);
  }
  in.rtcOK = haveRtc && rtcTrusted;
  in.hh = rtcH; in.mm = rtcM; in.ss = rtcS;
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
    else if (c == 'h') { showMic = !showMic; Serial.println(showMic ? "mic debug ON" : "mic debug OFF"); }
    else if (c == 'v') { ch::settings.vol = (ch::settings.vol + 1) & 3; ch::settings.dirty = true; Serial.printf("volume %d\n", ch::settings.vol); }
    else if (c == 't') { ch::testRing(0); Serial.println("test: timer ring"); }
    else if (c == 'a') { ch::testRing(1); Serial.println("test: alarm ring"); }
    else if (c >= 'A' && c <= 'Z') { snd::play((snd::Id)(1 + (c - 'A'))); Serial.printf("sound %d\n", 1 + (c - 'A')); }
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
  setupRtc();
  snd::setVolume(ch::settings.vol);
  setupAudio();
  setupEspNow();

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
  if (now - lastBroadcast > 600) { lastBroadcast = now; broadcastMood(); }

  ch::Input in;
  readInputs(now, in);
  ch::update(now, dt, in);

  int y0, y1;
  ch::render(y0, y1);
  lastRows = y1 - y0 + 1;
  panel->draw16bitRGBBitmap(0, y0, fb + (size_t)y0 * ch::SCR_W, ch::SCR_W, lastRows);

  if (ch::settings.dirty && now - lastSave > 1500) { lastSave = now; saveSettings(); }

  if (showMic && now - fpsT >= 500 && !showFps) {
    fpsT = now;
    Serial.printf("mic rms %.0f floor %.0f voice %d  loud# %u name# %u voice# %u\n", snd::mic.rms, snd::mic.floorv,
                  (int)snd::mic.voiceNow, (unsigned)snd::mic.loudSeq, (unsigned)snd::mic.nameSeq, (unsigned)snd::mic.voiceSeq);
  }
  if (showFps) {
    fpsCount++;
    if (now - fpsT >= 1000) { Serial.printf("fps %d  rows %d\n", fpsCount, lastRows); fpsCount = 0; fpsT = now; }
  }

  // ~60 fps cap
  uint32_t spent = millis() - now;
  if (spent < 14) delay(14 - spent);
}
