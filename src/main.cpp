#include <Arduino.h>
#include <TFT_eSPI.h>
#include <HB9IIU_BacklightControl.h>
#ifndef LOAD_GFXFF // in case not defined as a build flag, define here to avoid compile errors in TFT_eSPI.h when including GFX fonts
#define LOAD_GFXFF
#endif
#include <HB9IIU_RobustWIfiConnection.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// Example: URL to your version.json file
const char* VERSION_URL = "https://raw.githubusercontent.com/HB9IIU/ESP32-TFT_HB9IIU_GOODIES/main/firmware/version.json";
const char* CURRENT_VERSION = "1.0.1"; // Update as needed

bool checkForFirmwareUpdate() {
    WiFiClientSecure client;
    client.setInsecure(); // For testing, disables certificate validation. Use proper certs for production!
    if (!client.connect("raw.githubusercontent.com", 443)) {
        Serial.println("[OTA] Connection failed!");
        return false;
    }
    // Send HTTP GET request
    client.print(String("GET ") + "/HB9IIU/ESP32-TFT_HB9IIU_GOODIES/main/firmware/version.json" + " HTTP/1.1\r\n" +
                 "Host: raw.githubusercontent.com\r\n" +
                 "Connection: close\r\n\r\n");
    // Wait for response
    while (client.connected() && !client.available()) delay(10);
    String payload;
    while (client.available()) {
        payload += client.readStringUntil('\n');
    }
    // Find JSON start
    int jsonStart = payload.indexOf('{');
    if (jsonStart < 0) {
        Serial.println("[OTA] No JSON found!");
        return false;
    }
    String json = payload.substring(jsonStart);
    // Parse JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.print("[OTA] JSON parse error: ");
        Serial.println(err.c_str());
        return false;
    }
    String remoteVersion = doc["version"];
    String firmwareUrl = doc["firmware_url"];
    String sha256 = doc["sha256"];
    Serial.println("[OTA] Remote version: " + remoteVersion);
    Serial.println("[OTA] Firmware URL: " + firmwareUrl);
    Serial.println("[OTA] SHA-256: " + sha256);
    // Compare versions
    if (remoteVersion != CURRENT_VERSION) {
        Serial.println("[OTA] Update available!");
        // Next step: download and verify firmware
        return true;
    } else {
        Serial.println("[OTA] Firmware is up to date.");
        return false;
    }
}

static TFT_eSPI tft = TFT_eSPI();


void setup()
{
  Serial.begin(115200);
  delay(3000);

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

  Serial.println("[OTA] Checking for firmware update...");
  checkForFirmwareUpdate();
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
