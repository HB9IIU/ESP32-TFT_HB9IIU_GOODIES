#include <Arduino.h>
#include <TFT_eSPI.h>
#include <HB9IIU_BacklightControl.h>
#ifndef LOAD_GFXFF // in case not defined as a build flag, define here to avoid compile errors in TFT_eSPI.h when including GFX fonts
#define LOAD_GFXFF
#endif
// Select the font set folder to use from include/Generated:Fonts_For_Copy/.
// To change the font set, modify FONT_SET_DIR below.
// Example: FONT_SET_DIR Generated:Fonts_For_Copy/IBMPlexMono_ExtraLight

#define FONT_SET_DIR Generated_Fonts_For_Copy/IBMPlexMono_ExtraLight

#define _HB9_STR2(x) #x
#define _HB9_STR(x) _HB9_STR2(x)

#include _HB9_STR(FONT_SET_DIR/all_fonts.h)
#include _HB9_STR(FONT_SET_DIR/fonts_index.h)

static TFT_eSPI tft = TFT_eSPI();

static constexpr uint32_t TOUCH_DEBOUNCE_MS = 250;
static uint32_t gLastTouchMs = 0;
static size_t gFontIndex = 0;

static void drawFontPage(size_t idx)
{
  tft.fillScreen(TFT_BLACK);

  const size_t count = kFontCount;
  if (count == 0)
  {
    tft.setFreeFont(NULL);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("No fonts found in selected set", 10, 20, 2);
    tft.drawString("Check all_fonts.h and fonts_index.h", 10, 45, 2);
    tft.drawString("Then rebuild.", 10, 70, 2);
    return;
  }

  if (idx >= count)
  idx = 0;

  const FontEntry &e = kFonts[idx];

  if (!e.font)
  {
    tft.setFreeFont(NULL);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("No a??pt7b headers found", 10, 20, 2);
    tft.drawString("Run fontMaker.py to generate", 10, 45, 2);
    tft.drawString("them into include/", 10, 70, 2);
    return;
  }

  // Header (built-in font for readability)
  tft.setFreeFont(NULL);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(String("Font ") + e.name + " (" + String(idx + 1) + "/" + String(count) + ")", 10, 6, 2);
  tft.drawString("Touch to go to next size", 10, 26, 2);

  // FreeFont sample
  tft.setFreeFont(e.font);
  tft.setTextDatum(TL_DATUM);

  const int x = 10;
  int y = 50 + tft.fontHeight();
  const int dy = tft.fontHeight() + 8;

  tft.drawString(String("size=") + String((int)e.sizePt) + "pt  yAdv=" + String((int)e.font->yAdvance) + "  h=" + String((int)tft.fontHeight()), x, y);
  y += dy;
  tft.drawString("The quick brown fox", x, y);
  y += dy;
  tft.drawString("0123456789", x, y);
  y += dy;
  tft.drawString("ABCDE abcde", x, y);
  y += dy;
  tft.drawString("-+*/()[]{}", x, y);

  tft.setFreeFont(NULL);
}

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

  Serial.println("Font pager ready.");
  Serial.printf("Found %u font(s).\n", (unsigned)kFontCount);

  drawFontPage(gFontIndex);
}

void loop()
{
  uint16_t x, y;

  // IMPORTANT: release any TFT transaction before reading touch
  tft.endWrite();

  if (tft.getTouch(&x, &y))
  {
    // Match your existing convention: invert Y to make origin bottom-left-ish
    y = tft.height() - y;

    const uint32_t now = millis();
    if (now - gLastTouchMs >= TOUCH_DEBOUNCE_MS)
    {
      gLastTouchMs = now;

      const size_t count = kFontCount;
      if (count > 0)
      {
        gFontIndex = (gFontIndex + 1) % count;
        Serial.printf("Next font: %s\n", kFonts[gFontIndex].name);
      }

      tft.startWrite();
      drawFontPage(gFontIndex);
      tft.endWrite();
    }
  }

  delay(10);
}
