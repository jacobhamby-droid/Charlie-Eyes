/*
 * Charlie Eyes
 * Waveshare ESP32-S3-Touch-AMOLED-1.75
 * Uses Canvas (recommended for CO5300) + newer GFX library
 */

#include <Arduino.h>
#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#include <Arduino_GFX_Library.h>

#define LCD_CS   12
#define LCD_SCK  38
#define LCD_D0   4
#define LCD_D1   5
#define LCD_D2   6
#define LCD_D3   7
#define LCD_RST  39
#define TP_RST   40

XPowersPMU pmu;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3
);

Arduino_CO5300 *panel = new Arduino_CO5300(
  bus, LCD_RST, 0,
  466, 466,
  6, 0, 0, 0
);

// Draw on canvas, then flush to panel
Arduino_Canvas *gfx = new Arduino_Canvas(466, 466, panel);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Charlie Eyes v2");

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
    Serial.println("PMU FAIL");
  }

  pinMode(LCD_RST, OUTPUT);
  pinMode(TP_RST, OUTPUT);
  digitalWrite(LCD_RST, LOW);
  digitalWrite(TP_RST, LOW);
  delay(50);
  digitalWrite(LCD_RST, HIGH);
  digitalWrite(TP_RST, HIGH);
  delay(100);

  if (!panel->begin()) {
    Serial.println("panel begin FAIL");
  } else {
    Serial.println("panel begin OK");
  }
  panel->setBrightness(255);

  if (!gfx->begin()) {
    Serial.println("canvas begin FAIL");
  } else {
    Serial.println("canvas begin OK");
  }

  // Color test – stay on each color 2 seconds
  gfx->fillScreen(RGB565_RED);
  gfx->flush();
  Serial.println("RED");
  delay(2000);

  gfx->fillScreen(RGB565_GREEN);
  gfx->flush();
  Serial.println("GREEN");
  delay(2000);

  gfx->fillScreen(RGB565_BLUE);
  gfx->flush();
  Serial.println("BLUE");
  delay(2000);

  // Black + eyes
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(160, 233, 70, RGB565_WHITE);
  gfx->fillCircle(160, 233, 28, RGB565_BLACK);
  gfx->fillCircle(306, 233, 70, RGB565_WHITE);
  gfx->fillCircle(306, 233, 28, RGB565_BLACK);
  gfx->flush();
  Serial.println("EYES");
}

void loop() {
  delay(5000);
}
