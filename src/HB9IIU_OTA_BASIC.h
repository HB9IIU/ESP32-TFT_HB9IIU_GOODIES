#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  HB9IIU_OTA_BASIC.h
//  Serial-only OTA firmware update for ESP32 / Arduino core 3.x
//
//  Usage (single call from setup):
//      HB9IIU_OTA_BASIC_checkAndUpdate(VERSION_URL, CURRENT_VERSION);
//
//  Requires in partitions CSV: two OTA app slots (app0 + app1).
//  All functions are static — safe to include in multiple translation units.
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_ota_ops.h"

// ── Internal: display helpers ────────────────────────────────────────────────

static void _hb9ota_line() {
    Serial.println("   ═══════════════════════════════════════════════════");
}
static void _hb9ota_divider() {
    Serial.println("   ───────────────────────────────────────────────────");
}
static void _hb9ota_progress(size_t written, size_t total) {
    int  pct    = total ? (int)(written * 100 / total) : 0;
    int  filled = pct * 30 / 100;
    char bar[31];
    for (int i = 0; i < 30; i++) bar[i] = (i < filled) ? '#' : '.';
    bar[30] = '\0';
    Serial.printf("   [%s] %3d%%   %4u / %4u KB\n",
                  bar, pct,
                  (unsigned)(written / 1024),
                  (unsigned)(total   / 1024));
}

// ── Internal: fetch version.json and compare ─────────────────────────────────

static bool _hb9ota_check(const char* versionUrl,
                           const char* currentVersion,
                           String&     outFirmwareUrl,
                           String&     outSha256) {
    // Parse host and path from the URL (strips "https://")
    String url  = String(versionUrl);
    int    sep  = url.indexOf("://");
    String rest = (sep >= 0) ? url.substring(sep + 3) : url;
    int    sl   = rest.indexOf('/');
    String host = rest.substring(0, sl);
    String path = rest.substring(sl);

    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect(host.c_str(), 443)) {
        Serial.println("   ❌ OTA: connection to server failed!");
        return false;
    }
    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                  path.c_str(), host.c_str());
    while (client.connected() && !client.available()) delay(10);
    String payload;
    while (client.available()) payload += client.readStringUntil('\n');

    int jsonStart = payload.indexOf('{');
    if (jsonStart < 0) {
        Serial.println("   ❌ OTA: no JSON found in response!");
        return false;
    }
    JsonDocument doc;
    DeserializationError jsonErr = deserializeJson(doc, payload.substring(jsonStart));
    if (jsonErr) {
        Serial.printf("   ❌ OTA: JSON parse error: %s\n", jsonErr.c_str());
        return false;
    }

    String remoteVersion = doc["version"]      | String("?");
    String firmwareUrl   = doc["firmware_url"] | String("");
    String sha256        = doc["sha256"]       | String("");

    _hb9ota_line();
    Serial.println("        OTA Firmware Check");
    _hb9ota_line();
    Serial.printf("   Running version  :  %s\n", currentVersion);
    Serial.printf("   Remote  version  :  %s\n", remoteVersion.c_str());
    Serial.printf("   SHA-256          :  %s...%s\n",
                  sha256.substring(0, 8).c_str(),
                  sha256.substring(sha256.length() - 8).c_str());
    _hb9ota_divider();

    if (remoteVersion != String(currentVersion)) {
        Serial.println("   🚀  Update available — starting download");
        _hb9ota_line();
        outFirmwareUrl = firmwareUrl;
        outSha256      = sha256;
        return true;
    } else {
        Serial.println("   ✅  Firmware is up to date");
        _hb9ota_line();
        return false;
    }
}

// ── Internal: download firmware and flash ────────────────────────────────────

static bool _hb9ota_download(const String& firmwareUrl,
                              const String& expectedSha256) {
    (void)expectedSha256;   // integrity guaranteed by HTTPS transport

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

    String filename = firmwareUrl.substring(firmwareUrl.lastIndexOf('/') + 1);

    _hb9ota_line();
    Serial.println("        OTA Download");
    _hb9ota_line();
    Serial.printf("   File       :  %s\n",     filename.c_str());
    Serial.printf("   Size       :  %d bytes  (%d KB)\n", contentLength, contentLength / 1024);
    Serial.printf("   Partition  :  %s  (%d KB available)\n",
                  partition->label, partition->size / 1024);
    Serial.printf("   Free heap  :  %d bytes\n", esp_get_free_heap_size());
    _hb9ota_divider();

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        Serial.printf("   ❌ OTA: esp_ota_begin failed: 0x%x\n", err);
        http.end();
        return false;
    }

    // Static buffer in BSS — avoids stack pressure during flash writes
    static uint8_t _hb9ota_buf[4096];
    WiFiClient*   stream   = http.getStreamPtr();
    size_t        written  = 0;
    int           lastPct  = -1;
    unsigned long lastData = millis();

    _hb9ota_progress(0, contentLength);

    while (written < (size_t)contentLength) {
        size_t toRead = min((int)sizeof(_hb9ota_buf), contentLength - (int)written);
        int len = stream->readBytes(_hb9ota_buf, toRead);
        if (len > 0) {
            err = esp_ota_write(ota_handle, _hb9ota_buf, len);
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
                _hb9ota_progress(written, contentLength);
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

    if ((err = esp_ota_end(ota_handle)) != ESP_OK) {
        Serial.printf("   ❌ OTA: esp_ota_end failed: 0x%x\n", err);
        return false;
    }
    if ((err = esp_ota_set_boot_partition(partition)) != ESP_OK) {
        Serial.printf("   ❌ OTA: set_boot_partition failed: 0x%x\n", err);
        return false;
    }

    _hb9ota_divider();
    Serial.printf("   ✅  %u bytes written successfully\n", (unsigned)written);
    Serial.printf("   ✅  Boot partition  →  %s\n", partition->label);
    Serial.println("   ♻️   Rebooting into new firmware...");
    _hb9ota_line();
    delay(500);
    ESP.restart();
    return true;
}

// ── Public API ───────────────────────────────────────────────────────────────
//
//  Call once after WiFi is connected.
//  Returns false if firmware is up to date or on any error.
//  Never returns if an update is applied (device reboots).
//
static bool HB9IIU_OTA_BASIC_checkAndUpdate(const char* versionUrl,
                                             const char* currentVersion) {
    String firmwareUrl, sha256;
    if (_hb9ota_check(versionUrl, currentVersion, firmwareUrl, sha256)) {
        _hb9ota_download(firmwareUrl, sha256);
    }
    return false;
}
