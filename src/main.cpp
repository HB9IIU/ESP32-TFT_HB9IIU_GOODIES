// GLOBALS
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "HB9IIU_BacklightControl.h"
#ifndef LOAD_GFXFF
#define LOAD_GFXFF
#endif
#include "HB9IIU_RobustWIfiConnection.h"
#include "HB9IIU_OTA_TFT.h"

const char* VERSION_URL     = "https://raw.githubusercontent.com/HB9IIU/ESP32-TFT_HB9IIU_GOODIES/main/firmware/version.json";
const char* CURRENT_VERSION = "1.0.10";
static TFT_eSPI tft = TFT_eSPI();

void setup()
{
  Serial.begin(115200);
  delay(3000);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(0);
  backlightInit();
  tft.fillScreen(TFT_GOLD);
  Serial.println("Hello World!");
  HB9IIUWifiConnection(false);

  HB9IIU_OTA_TFT_checkAndUpdate(VERSION_URL, CURRENT_VERSION, tft);


}

void loop()
{
  uint16_t x, y;
  tft.endWrite();
  if (tft.getTouch(&x, &y)) {
    Serial.printf("Touch at x=%u, y=%u\n", x, y);
  }
  delay(10);
}
