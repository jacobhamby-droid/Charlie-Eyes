/*
  Charlie - desktop companion
  Waveshare ESP32-S3-Touch-AMOLED-1.75 (466x466 CO5300 QSPI AMOLED, CST9217 touch, QMI8658 IMU)

  All the personality lives in charlie_core.h (hardware free). This file only wires up:
    display  -> software framebuffer in PSRAM, only the rows that changed get pushed
    touch    -> poke / pet / swipe-down menu
    IMU      -> shake, free-fall, auto-rotate so he is always face up
    flash    -> remembers eye color + orientation
    wifi     -> SoftAP hotspot + station mode + simple config page
    base44   -> polls a Base44 app endpoint for remote commands
    ble      -> advertises as Charlie-Eyes for Chrome Web Bluetooth control

  Serial (115200):  d demo all animations   n next animation   c next color
                    o rotate 'up' 90 deg    m mirror rotation  s sleep/wake   f fps on/off
*/

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
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

// BLE UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

static const uint8_t BRIGHTNESS = 190;  // AMOLED: lower = longer life for the pixels
static const char* AP_SSID = "Charlie-Eyes";
static const char* AP_PASS = "charlie123";  // change if you want

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(bus, LCD_RST, 0, ch::SCR_W, ch::SCR_H, 6, 0, 0, 0);

TouchDrvCST92xx touch;
SensorQMI8658 qmi;
Preferences prefs;
WebServer server(80);

static uint16_t *fb = nullptr;
static bool haveTouch = false, haveImu = false;
static volatile bool touchIrq = false;
static bool touchDown = false;
static int16_t tX[2], tY[2];
static uint32_t lastTouchSeen = 0;
static float lastAx = 0, lastAy = 0, lastAz = 1;
static uint32_t lastFrame = 0, lastSave = 0, fpsT = 0, lastPoll = 0;
static int fpsCount = 0;
static bool showFps = false;
static int lastRows = 0;

// WiFi / Base44 config (saved in Preferences)
String wifiSsid, wifiPass, base44Url, base44Token;
bool wifiConnected = false;

// BLE
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;
String bleCommand = "";

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("BLE client connected");
  }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("BLE client disconnected");
    BLEDevice::startAdvertising();
  }
};

class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      bleCommand = String(value.c_str());
      Serial.printf("BLE command: %s\n", bleCommand.c_str());
    }
  }
};

static void IRAM_ATTR onTouchIrq() { touchIrq = true; }

static void loadSettings() {
  prefs.begin("charlie", true);
  ch::settings.colorIdx = prefs.getUChar("col", 0) & 7;
  ch::settings.orientOffset = prefs.getShort("ori", 0);
  ch::settings.mirror = prefs.getBool("mir", false);
  wifiSsid = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  base44Url = prefs.getString("b44url", "");
  base44Token = prefs.getString("b44tok", "");
  prefs.end();
}

static void saveSettings() {
  prefs.begin("charlie", false);
  prefs.putUChar("col", ch::settings.colorIdx);
  prefs.putShort("ori", ch::settings.orientOffset);
  prefs.putBool("mir", ch::settings.mirror);
  prefs.putString("ssid", wifiSsid);
  prefs.putString("pass", wifiPass);
  prefs.putString("b44url", base44Url);
  prefs.putString("b44tok", base44Token);
  prefs.end();
  ch::settings.dirty = false;
  Serial.println("settings saved");
}

void setupBLE() {
  BLEDevice::init("Charlie-Eyes");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setValue("ready");
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising as Charlie-Eyes");
}

void processBleCommand() {
  if (bleCommand.length() == 0) return;
  String cmd = bleCommand;
  bleCommand = "";
  cmd.trim();
  cmd.toLowerCase();

  if (cmd == "love" || cmd == "heart") ch::forceAnim(ch::AN_LOVE);
  else if (cmd == "angry") ch::forceAnim(ch::AN_ANGRY);
  else if (cmd == "wink" || cmd == "wink_l") ch::forceAnim(ch::AN_WINK_L);
  else if (cmd == "wink_r") ch::forceAnim(ch::AN_WINK_R);
  else if (cmd == "sleep") ch::toggleSleep();
  else if (cmd == "wake") { if (ch::sleeping) ch::toggleSleep(); }
  else if (cmd.startsWith("color:")) {
    int c = cmd.substring(6).toInt();
    if (c >= 0 && c < 8) {
      ch::settings.colorIdx = c;
      ch::settings.dirty = true;
    }
  }
  else Serial.printf("unknown BLE cmd: %s\n", cmd.c_str());
}

// ---------- Config web page ----------
const char* configPage = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Charlie Config</title>
<style>body{font-family:sans-serif;max-width:420px;margin:20px auto;padding:10px}input,button{width:100%;padding:10px;margin:6px 0;box-sizing:border-box}button{background:#08c;color:#fff;border:0;border-radius:6px}</style>
</head><body>
<h2>Charlie Eyes Config</h2>
<form action="/save" method="POST">
<label>Home WiFi SSID<br><input name="ssid" value="%SSID%"></label>
<label>Password<br><input name="pass" type="password" value="%PASS%"></label>
<label>Base44 API URL<br><input name="b44url" value="%B44URL%" placeholder="https://app.base44.com/api/apps/APPID/entities/Commands"></label>
<label>Base44 Token<br><input name="b44tok" value="%B44TOK%"></label>
<button type="submit">Save & Reboot</button>
</form>
<p>Hotspot: Charlie-Eyes / charlie123<br>BLE name: Charlie-Eyes<br>Once saved, Charlie will join your WiFi and poll Base44.</p>
</body></html>
)rawliteral";

void handleRoot() {
  String page = configPage;
  page.replace("%SSID%", wifiSsid);
  page.replace("%PASS%", wifiPass);
  page.replace("%B44URL%", base44Url);
  page.replace("%B44TOK%", base44Token);
  server.send(200, "text/html", page);
}

void handleSave() {
  wifiSsid = server.arg("ssid");
  wifiPass = server.arg("pass");
  base44Url = server.arg("b44url");
  base44Token = server.arg("b44tok");
  saveSettings();
  server.send(200, "text/html", "<h2>Saved! Rebooting...</h2>");
  delay(500);
  ESP.restart();
}

void setupWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("Hotspot: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("Config page: http://192.168.4.1");

  if (wifiSsid.length() > 0) {
    Serial.printf("Connecting to %s ...\n", wifiSsid.c_str());
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  }
}

void handleWiFi() {
  server.handleClient();
  if (wifiSsid.length() > 0 && WiFi.status() == WL_CONNECTED && !wifiConnected) {
    wifiConnected = true;
    Serial.printf("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
  }
}

void pollBase44() {
  if (!wifiConnected || base44Url.length() == 0 || base44Token.length() == 0) return;
  uint32_t now = millis();
  if (now - lastPoll < 5000) return;  // every 5 s
  lastPoll = now;

  HTTPClient http;
  http.begin(base44Url);
  http.addHeader("Authorization", "Bearer " + base44Token);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      const char* anim = doc["anim"] | doc[0]["anim"] | nullptr;
      int color = doc["color"] | doc[0]["color"] | -1;
      if (anim) {
        String a = String(anim);
        a.toLowerCase();
        if (a == "love" || a == "heart") ch::forceAnim(ch::AN_LOVE);
        else if (a == "angry") ch::forceAnim(ch::AN_ANGRY);
        else if (a == "wink" || a == "wink_l") ch::forceAnim(ch::AN_WINK_L);
        else if (a == "wink_r") ch::forceAnim(ch::AN_WINK_R);
        else if (a == "sleep") ch::toggleSleep();
        else if (a == "wake") { if (ch::sleeping) ch::toggleSleep(); }
        else Serial.printf("unknown anim: %s\n", anim);
      }
      if (color >= 0 && color < 8) {
        ch::settings.colorIdx = color;
        ch::settings.dirty = true;
      }
    }
  } else {
    Serial.printf("Base44 poll failed: %d\n", code);
  }
  http.end();
}

static void setupTouch() {
  pinMode(TP_RESET, OUTPUT);
  digitalWrite(TP_RESET, LOW);  delay(30);
  digitalWrite(TP_RESET, HIGH); delay(50);
  touch.setPins(TP_RESET, TP_INT);
  haveTouch = touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
  if (!haveTouch) { Serial.println("touch: NOT FOUND"); return; }
  Serial.printf("touch: %s\n", touch.getModelName());
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
  setupWiFi();
  setupBLE();

  ch::init(fb, esp_random());
  ch::start(millis());

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
  handleWiFi();
  pollBase44();
  processBleCommand();

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

  uint32_t spent = millis() - now;
  if (spent < 14) delay(14 - spent);
}
