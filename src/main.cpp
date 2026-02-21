// GLOBALS
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <HB9IIU_BacklightControl.h>
#ifndef LOAD_GFXFF
#define LOAD_GFXFF
#endif
#include <HB9IIU_RobustWIfiConnection.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <mbedtls/sha256.h>

const char* VERSION_URL = "https://raw.githubusercontent.com/HB9IIU/ESP32-TFT_HB9IIU_GOODIES/main/firmware/version.json";
const char* CURRENT_VERSION = "1.0.1";
static TFT_eSPI tft = TFT_eSPI();

// FUNCTION PROTOTYPES
bool checkForFirmwareUpdate();
bool downloadAndUpdateFirmware(const String& firmwareUrl, const String& expectedSha256);

// MAIN FUNCTIONS
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
  Serial.println("[OTA] Checking for firmware update...");
  checkForFirmwareUpdate();
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

// ALL FUNCTIONS
bool checkForFirmwareUpdate() {
    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect("raw.githubusercontent.com", 443)) {
        Serial.println("[OTA] Connection failed!");
        return false;
    }
    client.print(String("GET ") + "/HB9IIU/ESP32-TFT_HB9IIU_GOODIES/main/firmware/version.json" + " HTTP/1.1\r\n" +
                 "Host: raw.githubusercontent.com\r\n" +
                 "Connection: close\r\n\r\n");
    while (client.connected() && !client.available()) delay(10);
    String payload;
    while (client.available()) {
        payload += client.readStringUntil('\n');
    }
    Serial.println("📝 [OTA] Full HTTP response:");
    Serial.println(payload);
    int jsonStart = payload.indexOf('{');
    if (jsonStart < 0) {
        Serial.println("[OTA] No JSON found!");
        return false;
    }
    String json = payload.substring(jsonStart);
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
    Serial.println("🛰️ [OTA] Checking for firmware update...");
    Serial.println("📦 [OTA] Remote version: " + remoteVersion);
    Serial.println("🔒 [OTA] SHA-256: " + sha256);
    if (remoteVersion != CURRENT_VERSION) {
        Serial.println("🚀 [OTA] Update available!");
        Serial.println("🆕 [OTA] New firmware found: " + remoteVersion);
        firmwareUrl = String(VERSION_URL);
        firmwareUrl.replace("version.json", "firmware.bin");
        downloadAndUpdateFirmware(firmwareUrl, sha256);
        return true;
    } else {
        Serial.println("✅ [OTA] Firmware is up to date.");
        return false;
    }
}

bool downloadAndUpdateFirmware(const String& firmwareUrl, const String& expectedSha256) {
    WiFiClientSecure client;
    client.setInsecure();
    Serial.println("🌐 [OTA] Connecting to firmware URL...");
    if (!client.connect("raw.githubusercontent.com", 443)) {
        Serial.println("❌ [OTA] Firmware connection failed!");
        return false;
    }
    String path = firmwareUrl.substring(firmwareUrl.indexOf("/HB9IIU/"));
    client.print(String("GET ") + path + " HTTP/1.1\r\n" +
                 "Host: raw.githubusercontent.com\r\n" +
                 "Connection: close\r\n\r\n");
    while (client.connected() && !client.available()) delay(10);
    while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.length() == 1) break;
    }
    size_t firmwareSize = 0;
    size_t written = 0;
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Serial.println("❌ [OTA] Update.begin() failed!");
        return false;
    }
    uint8_t buf[1024];
    while (client.available()) {
        int len = client.read(buf, sizeof(buf));
        if (len > 0) {
            Update.write(buf, len);
            mbedtls_sha256_update(&sha_ctx, buf, len);
            written += len;
        }
    }
    mbedtls_sha256_finish(&sha_ctx, buf);
    String sha256Hex;
    for (int i = 0; i < 32; ++i) {
        char hex[3];
        sprintf(hex, "%02x", buf[i]);
        sha256Hex += hex;
    }
    Serial.println("🔒 [OTA] Downloaded firmware SHA-256: " + sha256Hex);
    if (sha256Hex != expectedSha256) {
        Serial.println("❌ [OTA] SHA-256 mismatch! Aborting update.");
        Update.abort();
        return false;
    }
    if (!Update.end(true)) {
        Serial.println("❌ [OTA] Update.end() failed!");
        return false;
    }
    Serial.println("✅ [OTA] Firmware update successful! Rebooting...");
    ESP.restart();
    return true;
}
