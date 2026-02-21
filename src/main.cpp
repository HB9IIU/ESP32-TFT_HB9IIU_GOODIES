// GLOBALS
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <vector>
#include <HTTPClient.h>
#include <HB9IIU_BacklightControl.h>
#ifndef LOAD_GFXFF
#define LOAD_GFXFF
#endif
#include <HB9IIU_RobustWIfiConnection.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "esp_ota_ops.h"

const char* VERSION_URL = "https://raw.githubusercontent.com/HB9IIU/ESP32-TFT_HB9IIU_GOODIES/main/firmware/version.json";
const char* CURRENT_VERSION = "1.0.1";
static TFT_eSPI tft = TFT_eSPI();

// FUNCTION PROTOTYPES
bool checkForFirmwareUpdate(String& outFirmwareUrl, String& outSha256);
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
  String firmwareUrl, sha256;
  if (checkForFirmwareUpdate(firmwareUrl, sha256)) {
    // First SSL context is fully destroyed before starting the download
    downloadAndUpdateFirmware(firmwareUrl, sha256);
  }
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
bool checkForFirmwareUpdate(String& outFirmwareUrl, String& outSha256) {
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
    String firmwareUrl   = doc["firmware_url"];
    String sha256        = doc["sha256"];
    Serial.println("🛰️ [OTA] Checking for firmware update...");
    Serial.println("📦 [OTA] Remote version: " + remoteVersion);
    Serial.println("🔒 [OTA] SHA-256: " + sha256);
    if (remoteVersion != CURRENT_VERSION) {
        Serial.println("🚀 [OTA] Update available!");
        Serial.println("🆕 [OTA] New firmware found: " + remoteVersion);
        outFirmwareUrl = firmwareUrl;
        outSha256 = sha256;
        return true;
    } else {
        Serial.println("✅ [OTA] Firmware is up to date.");
        return false;
    }
}

bool downloadAndUpdateFirmware(const String& firmwareUrl, const String& expectedSha256) {
    (void)expectedSha256;

    // Arduino WiFiClientSecure handles the HTTPS connection (setInsecure = skip cert check).
    // We bypass the Arduino Update library entirely and call esp_ota_begin/write/end
    // directly — this avoids the assertion failure that fired inside the Arduino Update
    // stack at the first 4 KB flash-sector write boundary.
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    Serial.println("🌐 [OTA] Connecting to firmware URL...");
    if (!http.begin(client, firmwareUrl)) {
        Serial.println("❌ [OTA] HTTPClient.begin() failed!");
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("❌ [OTA] HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("❌ [OTA] Invalid content length!");
        http.end();
        return false;
    }
    Serial.printf("[OTA] %d bytes to download, free heap: %d bytes\n",
                  contentLength, esp_get_free_heap_size());

    const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
    if (!partition) {
        Serial.println("❌ [OTA] No OTA partition found!");
        http.end();
        return false;
    }
    Serial.printf("[OTA] Writing to partition '%s' (size: %d bytes)\n",
                  partition->label, partition->size);

    esp_ota_handle_t ota_handle;
    // OTA_WITH_SEQUENTIAL_WRITES: no upfront full-partition erase; sectors are
    // erased lazily as each one is written, keeping the watchdog happy.
    esp_err_t err = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        Serial.printf("❌ [OTA] esp_ota_begin failed: 0x%x\n", err);
        http.end();
        return false;
    }

    // Static buffer lives in BSS, not on the stack, keeping stack usage low.
    static uint8_t buf[4096];
    WiFiClient* stream = http.getStreamPtr();
    size_t written = 0;
    int lastLogged = 0;
    unsigned long lastData = millis();

    while (written < (size_t)contentLength) {
        size_t toRead = min((int)sizeof(buf), contentLength - (int)written);
        int len = stream->readBytes(buf, toRead);
        if (len > 0) {
            err = esp_ota_write(ota_handle, buf, len);
            if (err != ESP_OK) {
                Serial.printf("❌ [OTA] esp_ota_write failed: 0x%x\n", err);
                esp_ota_abort(ota_handle);
                http.end();
                return false;
            }
            written += len;
            lastData = millis();
            if ((int)written - lastLogged >= 65536) {
                Serial.printf("[OTA] %u / %d bytes written\n", written, contentLength);
                lastLogged = written;
            }
        } else {
            if (millis() - lastData > 5000) {
                Serial.println("❌ [OTA] Stream timeout!");
                esp_ota_abort(ota_handle);
                http.end();
                return false;
            }
            delay(10);
        }
    }
    http.end();

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        Serial.printf("❌ [OTA] esp_ota_end failed: 0x%x\n", err);
        return false;
    }

    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        Serial.printf("❌ [OTA] esp_ota_set_boot_partition failed: 0x%x\n", err);
        return false;
    }

    Serial.println("✅ [OTA] Firmware update complete! Rebooting...");
    ESP.restart();
    return true;
}
