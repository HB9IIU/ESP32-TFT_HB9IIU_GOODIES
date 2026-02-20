#include <Arduino.h>
#include <TFT_eSPI.h>
#include <HB9IIU_BacklightControl.h>
#ifndef LOAD_GFXFF // in case not defined as a build flag, define here to avoid compile errors in TFT_eSPI.h when including GFX fonts
#define LOAD_GFXFF
#endif
#include <HB9IIU_RobustWIfiConnection.h>


static TFT_eSPI tft = TFT_eSPI();


void setup()
{
  Serial.begin(115200);
  delay(100);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(0);
  //tft.setSwapBytes(true);
  backlightInit();
  tft.fillScreen(TFT_GOLD);
  Serial.println("Hello World!");
  // Default: connect to first available (fixed order)
  HB9IIUWifiConnection(false);
  // To force strongest-known (scan), use true:



}

void loop()
{
  uint16_t x, y;

  // IMPORTANT: release any TFT transaction before reading touch
  tft.endWrite();

  if (tft.getTouch(&x, &y))
  {

    Serial.printf("Touch at x=%u, y=%u\n", x, y);
  }

  delay(10);
}
