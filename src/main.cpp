/*
 * Charlie Eyes – simplest possible version
 * Board: Waveshare ESP32-S3-Touch-AMOLED-1.75
 * Goal: power the screen and draw two eyes
 */

#include <Arduino.h>
#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#include <Arduino_GFX_Library.h>

// Pins for Waveshare ESP32-S3-Touch-AMOLED-1.75
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

Arduino_CO5300 *gfx = new Arduino_CO5300(
  bus, LCD_RST, 0,
  466, 466,
  6, 0, 0, 0
);

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("Charlie Eyes starting...");

  // 1. Start I2C
  Wire.begin(15, 14);

  // 2. Turn on power rails for the display
  if (pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, 15, 14)) {
    Serial.println("PMU OK – enabling rails");
    pmu.setALDO1Voltage(3300); pmu.enableALDO1();
    pmu.setALDO2Voltage(3300); pmu.enableALDO2();
    pmu.setALDO3Voltage(3300); pmu.enableALDO3();
    pmu.setALDO4Voltage(3300); pmu.enableALDO4();
    pmu.setBLDO1Voltage(3300); pmu.enableBLDO1();
    pmu.setBLDO2Voltage(3300); pmu.enableBLDO2();
    pmu.setDLDO1Voltage(3300); pmu.enableDLDO1();
    delay(80);
  } else {
    Serial.println("PMU FAILED");
  }

  // 3. Hardware reset of display + touch
  pinMode(LCD_RST, OUTPUT);
  pinMode(TP_RST, OUTPUT);
  digitalWrite(LCD_RST, LOW);
  digitalWrite(TP_RST, LOW);
  delay(40);
  digitalWrite(LCD_RST, HIGH);
  digitalWrite(TP_RST, HIGH);
  delay(80);

  // 4. Start the display
  if (!gfx->begin()) {
    Serial.println("gfx->begin() FAILED");
  } else {
    Serial.println("gfx->begin() OK");
  }

  gfx->setBrightness(200);
  gfx->fillScreen(BLACK);

  // 5. Draw two eyes
  // Left eye
  gfx->fillCircle(160, 233, 70, WHITE);
  gfx->fillCircle(160, 233, 28, BLACK);

  // Right eye
  gfx->fillCircle(306, 233, 70, WHITE);
  gfx->fillCircle(306, 233, 28, BLACK);

  Serial.println("Eyes drawn. Done.");
}

void loop() {
  // nothing – just keep the eyes on screen
  delay(5000);
}
