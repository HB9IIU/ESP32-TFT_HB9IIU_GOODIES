#include <Arduino.h>
#include <TFT_eSPI.h>
#include <HB9IIU_BacklightControl.h>

#ifndef LOAD_GFXFF
#define LOAD_GFXFF
#endif

#include <TFT_eSPI.h>
#include <FS.h>
#include <HB9IIU_BacklightControl.h>

#include "SFProTextBold160pt7b.h" 

static TFT_eSPI tft = TFT_eSPI();

void setup()
{
  Serial.begin(115200);
  delay(100);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(HB9_TFT_INVERT);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);
  backlightInit();
backlightSetPercent(15);
  tft.setFreeFont(&SFProTextBold160pt7b); // Use the GFXfont variable from your generated header
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM); // Centered

  // Calculate center
  int16_t x = tft.width() / 2;
  int16_t y = tft.height() / 2;

  tft.drawString("12:35", x, y);
}

void loop()
{
  // Nothing else to do
}
