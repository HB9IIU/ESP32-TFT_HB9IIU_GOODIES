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

const char* VERSION_URL     = "https://raw.githubusercontent.com/HB9IIU/ESP32-TFT_HB9IIU_GOODIES/main/firmware/version.json";
const char* CURRENT_VERSION = "1.0.2";
static TFT_eSPI tft = TFT_eSPI();

// FUNCTION PROTOTYPES
bool checkForFirmwareUpdate(String& outFirmwareUrl, String& outSha256);
bool downloadAndUpdateFirmware(const String& firmwareUrl, const String& expectedSha256);

// ── OTA display helpers ───────────────────────────────────────────────────────
static void otaLine() {
    Serial.println("   ═══════════════════════════════════════════════════");
}
static void otaDivider() {
    Serial.println("   ───────────────────────────────────────────────────");
}
static void otaProgress(size_t written, size_t total) {
    int  pct    = total ? (int)(written * 100 / total) : 0;
    int  filled = pct * 30 / 100;
    char bar[31];
    for (int i = 0; i < 30; i++) bar[i] = (i < filled) ? '#' : '.';
    bar[30] = '\0';
    Serial.printf("   [%s] %3d%%   %4u / %4u KB\n",
                  bar, pct, (unsigned)(written / 1024), (unsigned)(total / 1024));
}
// ─────────────────────────────────────────────────────────────────────────────

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

  String firmwareUrl, sha256;
  if (checkForFirmwareUpdate(firmwareUrl, sha256)) {
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
        Serial.println("   ❌ OTA: connection to GitHub failed!");
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
    int jsonStart = payload.indexOf('{');
    if (jsonStart < 0) {
        Serial.println("   ❌ OTA: no JSON found in response!");
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload.substring(jsonStart));
    if (err) {
        Serial.printf("   ❌ OTA: JSON parse error: %s\n", err.c_str());
        return false;
    }

    String remoteVersion = doc["version"]      | String("?");
    String firmwareUrl   = doc["firmware_url"] | String("");
    String sha256        = doc["sha256"]       | String("");

    // ── Version check banner ─────────────────────────────────────────────────
    otaLine();
    Serial.println("        OTA Firmware Check");
    otaLine();
    Serial.printf("   Running version  :  %s\n", CURRENT_VERSION);
    Serial.printf("   Remote  version  :  %s\n", remoteVersion.c_str());
    Serial.printf("   SHA-256          :  %s...%s\n",
                  sha256.substring(0, 8).c_str(),
                  sha256.substring(sha256.length() - 8).c_str());
    otaDivider();

    if (remoteVersion != CURRENT_VERSION) {
        Serial.println("   🚀  Update available — starting download");
        otaLine();
        outFirmwareUrl = firmwareUrl;
        outSha256      = sha256;
        return true;
    } else {
        Serial.println("   ✅  Firmware is up to date");
        otaLine();
        return false;
    }
}

bool downloadAndUpdateFirmware(const String& firmwareUrl, const String& expectedSha256) {
    (void)expectedSha256;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, firmwareUrl)) {
        Serial.println("   ❌ OTA: HTTPClient.begin() failed!");
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("   ❌ OTA: HTTP error %d\n", httpCode);
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("   ❌ OTA: invalid content length!");
        http.end();
        return false;
    }

    const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
    if (!partition) {
        Serial.println("   ❌ OTA: no OTA partition found!");
        http.end();
        return false;
    }

    // Extract filename from URL for display
    String filename = firmwareUrl.substring(firmwareUrl.lastIndexOf('/') + 1);

    // ── Download banner ───────────────────────────────────────────────────────
    otaLine();
    Serial.println("        OTA Download");
    otaLine();
    Serial.printf("   File       :  %s\n",       filename.c_str());
    Serial.printf("   Size       :  %d bytes  (%d KB)\n", contentLength, contentLength / 1024);
    Serial.printf("   Partition  :  %s  (%d KB available)\n",
                  partition->label, partition->size / 1024);
    Serial.printf("   Free heap  :  %d bytes\n", esp_get_free_heap_size());
    otaDivider();

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        Serial.printf("   ❌ OTA: esp_ota_begin failed: 0x%x\n", err);
        http.end();
        return false;
    }

    static uint8_t buf[4096];
    WiFiClient* stream  = http.getStreamPtr();
    size_t      written = 0;
    int         lastPct = -1;
    unsigned long lastData = millis();

    otaProgress(0, contentLength);  // 0% starting line

    while (written < (size_t)contentLength) {
        size_t toRead = min((int)sizeof(buf), contentLength - (int)written);
        int len = stream->readBytes(buf, toRead);
        if (len > 0) {
            err = esp_ota_write(ota_handle, buf, len);
            if (err != ESP_OK) {
                Serial.printf("\n   ❌ OTA: esp_ota_write failed: 0x%x\n", err);
                esp_ota_abort(ota_handle);
                http.end();
                return false;
            }
            written  += len;
            lastData  = millis();
            int pct   = (int)(written * 100 / contentLength);
            int pct10 = (pct / 10) * 10;
            if (pct10 > lastPct) {
                lastPct = pct10;
                otaProgress(written, contentLength);
            }
        } else {
            if (millis() - lastData > 5000) {
                Serial.println("\n   ❌ OTA: stream timeout!");
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
        Serial.printf("   ❌ OTA: esp_ota_end failed: 0x%x\n", err);
        return false;
    }
    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        Serial.printf("   ❌ OTA: set_boot_partition failed: 0x%x\n", err);
        return false;
    }

    otaDivider();
    Serial.printf("   ✅  %u bytes written successfully\n", (unsigned)written);
    Serial.printf("   ✅  Boot partition  →  %s\n", partition->label);
    Serial.println("   ♻️   Rebooting into new firmware...");
    otaLine();
    delay(500);
    ESP.restart();
    return true;
}
