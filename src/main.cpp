/*
 * Charlie Eyes – with Canvas (needed for CO5300) + color test
 * Board: Waveshare ESP32-S3-Touch-AMOLED-1.75
 */

#include <Arduino.h>
#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#include <Arduino_GFX_Library.h>

#define LCD_CS     12
#define LCD_SCLK   38
#define LCD_D0     4
#define LCD_D1     5
#define LCD_D2     6
#define LCD_D3     7
#define LCD_RST    39
#define TP_RST     40

XPowersPMU pmu;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3
);

// Real panel
Arduino_CO5300 *panel = new Arduino_CO5300(
  bus, LCD_RST, 0,
  466, 466,
  6, 0, 0, 0
);

// Canvas – we draw on this, then flush to the real screen
Arduino_Canvas *gfx = new Arduino_Canvas(466, 466, panel);

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("Charlie Eyes starting...");

  // Power
  Wire.begin(15, 14);
  if (pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, 15, 14)) {
    Serial.println("PMU OK");
    pmu.setALDO1Voltage(3300); pmu.enableALDO1();
    pmu.setALDO2Voltage(3300); pmu.enableALDO2();
    pmu.setALDO3Voltage(3300); pmu.enableALDO3();
    pmu.setALDO4Voltage(3300); pmu.enableALDO4();
    pmu.setBLDO1Voltage(3300); pmu.enableBLDO1();
    pmu.setBLDO2Voltage(3300); pmu.enableBLDO2();
    pmu.setDLDO1Voltage(3300); pmu.enableDLDO1();
    delay(100);
  } else {
    Serial.println("PMU FAILED");
  }

  // Hardware reset
  pinMode(LCD_RST, OUTPUT);
  pinMode(TP_RST, OUTPUT);
  digitalWrite(LCD_RST, LOW);
  digitalWrite(TP_RST, LOW);
  delay(50);
  digitalWrite(LCD_RST, HIGH);
  digitalWrite(TP_RST, HIGH);
  delay(100);

  // Start panel first
  if (!panel->begin()) {
    Serial.println("panel->begin() FAILED");
  } else {
    Serial.println("panel->begin() OK");
  }
  panel->setBrightness(200);

  // Start canvas
  gfx->begin();
  Serial.println("Canvas ready");

  // ===== COLOR TEST =====
  // If these work, the screen can show pixels
  gfx->fillScreen(RED);
  gfx->flush();
  Serial.println("RED");
  delay(1500);

  gfx->fillScreen(GREEN);
  gfx->flush();
  Serial.println("GREEN");
  delay(1500);

  gfx->fillScreen(BLUE);
  gfx->flush();
  Serial.println("BLUE");
  delay(1500);

  // Black background
  gfx->fillScreen(BLACK);
  gfx->flush();
  delay(500);

  // ===== TWO EYES =====
  gfx->fillCircle(160, 233, 70, WHITE);
  gfx->fillCircle(160, 233, 28, BLACK);
  gfx->fillCircle(306, 233, 70, WHITE);
  gfx->fillCircle(306, 233, 28, BLACK);
  gfx->flush();

  Serial.println("Eyes drawn. Done.");
}

void loop() {
  delay(5000);
}
