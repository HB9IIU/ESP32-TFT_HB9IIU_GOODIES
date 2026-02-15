#include <Arduino.h>
#include <TFT_eSPI.h>
#include "HB9IIU_BacklightControl.h"
#include "RadioWallpaper.h"

static TFT_eSPI tft = TFT_eSPI();

static void displayRGB565Image(int x, int y, const RGB565Image &img)
{
  backlightSetPercent(0);
  tft.startWrite();
  tft.pushImage(x, y, img.width, img.height, img.data);
  tft.endWrite();
}

void setup()
{
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(HB9_TFT_INVERT);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);
  backlightInit();
}

void loop()
{
  displayRGB565Image(0, 0, RadioWallpaper_img);
  fadeInTFT(5);
  delay(1000);
  fadeIOutTFT(5);
  delay(1000);
}
