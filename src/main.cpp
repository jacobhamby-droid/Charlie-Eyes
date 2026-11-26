/*
 * Charlie Eyes – direct panel only (no canvas)
 * Ends on solid RED so we know if any pixels work
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

Arduino_CO5300 *gfx = new Arduino_CO5300(
  bus, LCD_RST, 0,
  466, 466,
  6, 0, 0, 0
);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Charlie Eyes ===");

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
    delay(120);
  } else {
    Serial.println("PMU FAILED");
  }

  pinMode(LCD_RST, OUTPUT);
  pinMode(TP_RST, OUTPUT);
  digitalWrite(LCD_RST, LOW);
  digitalWrite(TP_RST, LOW);
  delay(60);
  digitalWrite(LCD_RST, HIGH);
  digitalWrite(TP_RST, HIGH);
  delay(120);

  if (!gfx->begin()) {
    Serial.println("begin() FAILED");
  } else {
    Serial.println("begin() OK");
  }

  gfx->setBrightness(255);   // max brightness

  // Solid RED – if you see red, pixels work
  gfx->fillScreen(RED);
  Serial.println("fillScreen(RED) done");

  delay(3000);

  // Try a big white rectangle in the middle
  gfx->fillRect(100, 100, 266, 266, WHITE);
  Serial.println("white rect done");

  delay(2000);

  // Two eyes on black
  gfx->fillScreen(BLACK);
  gfx->fillCircle(160, 233, 70, WHITE);
  gfx->fillCircle(160, 233, 28, BLACK);
  gfx->fillCircle(306, 233, 70, WHITE);
  gfx->fillCircle(306, 233, 28, BLACK);
  Serial.println("eyes done");
}

void loop() {
  delay(5000);
}
