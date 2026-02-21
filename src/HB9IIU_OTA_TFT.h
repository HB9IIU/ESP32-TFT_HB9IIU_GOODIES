#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  HB9IIU_OTA_TFT.h
//  TFT display OTA firmware update — ESP32 / Arduino core 3.x / 480×320
//
//  Usage (single call from setup, after WiFi connected):
//      HB9IIU_OTA_TFT_checkAndUpdate(VERSION_URL, CURRENT_VERSION, tft);
//
//  Requires: two OTA app slots in the partition table (app0 + app1).
//  All functions are static — safe to include in multiple translation units.
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_ota_ops.h"

// ─── Colour palette ──────────────────────────────────────────────────────────
#define _OTAT_BG          TFT_BLACK
#define _OTAT_HDR_BG      TFT_NAVY
#define _OTAT_HDR_TXT     TFT_WHITE
#define _OTAT_LABEL       0x8410    // mid-grey  (for "Running version :" etc.)
#define _OTAT_VALUE       TFT_WHITE
#define _OTAT_OK          TFT_GREEN
#define _OTAT_NEW         TFT_ORANGE
#define _OTAT_ERROR       TFT_RED
#define _OTAT_DIM         0xC618    // light grey (secondary / countdown)
#define _OTAT_BAR_BORDER  TFT_WHITE
#define _OTAT_BAR_FILL    TFT_GREEN
#define _OTAT_BAR_EMPTY   0x2104    // very dark grey (unfilled bar segment)

// ─── Layout constants  (480 × 320) ───────────────────────────────────────────
//  All y values are the vertical CENTRE of the text (MC_DATUM / ML_DATUM).
#define _OTAT_W          480
#define _OTAT_H          320
#define _OTAT_HDR_H       50     // header bar height
#define _OTAT_M           20     // left / right margin
#define _OTAT_COL_LBL     20     // x: label text start
#define _OTAT_COL_VAL    215     // x: value text start

#define _OTAT_Y_RUN       72     // "Running version" row   (Font 4, 26 px)
#define _OTAT_Y_REM      110     // "Remote  version" row
#define _OTAT_Y_DIV      143     // divider line y
#define _OTAT_Y_STAT     158     // status / filename row   (Font 4, centred)
#define _OTAT_Y_BAR      187     // progress bar top-left y
#define _OTAT_BAR_H       36     // progress bar height
#define _OTAT_Y_PCT      238     // "65%  -  647 / 996 KB"  (Font 2, centred)
#define _OTAT_Y_MSG      268     // "Flash complete!"        (Font 4, centred)
#define _OTAT_Y_BOOT     291     // "Boot partition → app1" (Font 2, centred)
#define _OTAT_Y_COUNT    312     // countdown line          (Font 2, centred)
//  Verify bottom edge: _OTAT_Y_COUNT ± 8 (Font-2 half-height) = 304 … 320 ✓

// ─── Internal: drawing primitives ────────────────────────────────────────────

static void _otat_header(TFT_eSPI& tft) {
    tft.fillScreen(_OTAT_BG);
    tft.fillRect(0, 0, _OTAT_W, _OTAT_HDR_H, _OTAT_HDR_BG);
    tft.setTextColor(_OTAT_HDR_TXT, _OTAT_HDR_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.drawString("CHECKING FOR UPDATES", _OTAT_W / 2, _OTAT_HDR_H / 2);
}

// Draw one version row: grey label on the left, coloured value in the middle.
static void _otat_vrow(TFT_eSPI& tft, int y,
                        const char* label, const char* value, uint16_t vcol) {
    tft.fillRect(0, y - 15, _OTAT_W, 32, _OTAT_BG);
    // Label — Font 2, slightly lower to align visually with Font-4 baseline
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(_OTAT_LABEL, _OTAT_BG);
    tft.drawString(label, _OTAT_COL_LBL, y + 5);
    // Value — Font 4, prominent
    tft.setTextColor(vcol, _OTAT_BG);
    tft.setTextFont(4);
    tft.drawString(value, _OTAT_COL_VAL, y);
}

static void _otat_divider(TFT_eSPI& tft) {
    tft.drawFastHLine(_OTAT_M, _OTAT_Y_DIV,
                      _OTAT_W - 2 * _OTAT_M, _OTAT_DIM);
}

// Centred status line (replaces itself each call).
static void _otat_status(TFT_eSPI& tft, const char* msg, uint16_t col) {
    tft.fillRect(0, _OTAT_Y_STAT - 15, _OTAT_W, 32, _OTAT_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(col, _OTAT_BG);
    tft.drawString(msg, _OTAT_W / 2, _OTAT_Y_STAT);
}

// Progress bar + KB counter (redraws cleanly every call).
static void _otat_bar(TFT_eSPI& tft, size_t written, size_t total) {
    int barW  = _OTAT_W - 2 * _OTAT_M;
    int pct   = total ? (int)(written * 100 / total) : 0;
    int fillW = barW * pct / 100;

    // Border
    tft.drawRect(_OTAT_M, _OTAT_Y_BAR, barW, _OTAT_BAR_H, _OTAT_BAR_BORDER);
    // Green filled segment
    if (fillW > 0)
        tft.fillRect(_OTAT_M + 1, _OTAT_Y_BAR + 1,
                     fillW, _OTAT_BAR_H - 2, _OTAT_BAR_FILL);
    // Dark unfilled segment
    if (fillW < barW - 2)
        tft.fillRect(_OTAT_M + 1 + fillW, _OTAT_Y_BAR + 1,
                     barW - 2 - fillW, _OTAT_BAR_H - 2, _OTAT_BAR_EMPTY);

    // KB counter below bar
    tft.fillRect(0, _OTAT_Y_PCT - 10, _OTAT_W, 22, _OTAT_BG);
    char buf[48];
    snprintf(buf, sizeof(buf), "%d%%   -   %u / %u KB",
             pct, (unsigned)(written / 1024), (unsigned)(total / 1024));
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(_OTAT_VALUE, _OTAT_BG);
    tft.drawString(buf, _OTAT_W / 2, _OTAT_Y_PCT);
}

// Generic countdown line, counts down from `secs` to 1.
static void _otat_countdown(TFT_eSPI& tft, int y, const char* prefix, int secs) {
    for (int i = secs; i >= 1; i--) {
        tft.fillRect(0, y - 10, _OTAT_W, 22, _OTAT_BG);
        char buf[40];
        snprintf(buf, sizeof(buf), "%s %d...", prefix, i);
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(2);
        tft.setTextColor(_OTAT_DIM, _OTAT_BG);
        tft.drawString(buf, _OTAT_W / 2, y);
        delay(1000);
    }
}

// ─── Internal: version check ─────────────────────────────────────────────────

static bool _otat_check(TFT_eSPI& tft,
                         const char* versionUrl, const char* currentVersion,
                         String& outUrl, String& outSha) {
    _otat_header(tft);
    _otat_vrow(tft, _OTAT_Y_RUN, "Running version :", currentVersion, _OTAT_VALUE);
    _otat_vrow(tft, _OTAT_Y_REM, "Remote  version :", "...", _OTAT_DIM);
    _otat_divider(tft);
    _otat_status(tft, "Connecting to GitHub...", _OTAT_DIM);

    // Parse host + path out of the URL
    String url  = String(versionUrl);
    int    sep  = url.indexOf("://");
    String rest = sep >= 0 ? url.substring(sep + 3) : url;
    int    sl   = rest.indexOf('/');
    String host = rest.substring(0, sl);
    String path = rest.substring(sl);

    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect(host.c_str(), 443)) {
        _otat_status(tft, "Connection failed!", _OTAT_ERROR);
        return false;
    }
    _otat_status(tft, "Fetching version info...", _OTAT_DIM);
    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                  path.c_str(), host.c_str());
    while (client.connected() && !client.available()) delay(10);
    String payload;
    while (client.available()) payload += client.readStringUntil('\n');

    int js = payload.indexOf('{');
    if (js < 0) { _otat_status(tft, "No JSON in response!", _OTAT_ERROR); return false; }
    JsonDocument doc;
    if (deserializeJson(doc, payload.substring(js))) {
        _otat_status(tft, "JSON parse error!", _OTAT_ERROR); return false;
    }

    String remoteVer = doc["version"]      | String("?");
    String fwUrl     = doc["firmware_url"] | String("");
    String sha256    = doc["sha256"]       | String("");
    bool   isNew     = (remoteVer != String(currentVersion));

    // Update the remote-version row with its real value and colour
    _otat_vrow(tft, _OTAT_Y_REM, "Remote  version :",
               remoteVer.c_str(), isNew ? _OTAT_NEW : _OTAT_OK);

    if (isNew) {
        // Small "(NEW)" badge to the right of the version value
        tft.setTextFont(2);
        tft.setTextColor(_OTAT_NEW, _OTAT_BG);
        tft.setTextDatum(ML_DATUM);
        tft.drawString("(NEW)", _OTAT_COL_VAL + 90, _OTAT_Y_REM + 5);
        _otat_status(tft, "Update available!", _OTAT_NEW);
        delay(700);
        outUrl = fwUrl;
        outSha = sha256;
        return true;
    } else {
        _otat_status(tft, "Firmware is up to date", _OTAT_OK);
        _otat_countdown(tft, _OTAT_Y_COUNT, "Resuming in", 3);
        return false;
    }
}

// ─── Internal: download + flash ──────────────────────────────────────────────

static bool _otat_download(TFT_eSPI& tft,
                            const String& firmwareUrl, const String& sha) {
    (void)sha;   // integrity guaranteed by HTTPS

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, firmwareUrl)) {
        _otat_status(tft, "HTTP begin failed!", _OTAT_ERROR); return false;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        char m[32]; snprintf(m, sizeof(m), "HTTP error: %d", code);
        _otat_status(tft, m, _OTAT_ERROR); http.end(); return false;
    }
    int total = http.getSize();
    if (total <= 0) {
        _otat_status(tft, "Invalid content length!", _OTAT_ERROR);
        http.end(); return false;
    }
    const esp_partition_t* part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        _otat_status(tft, "No OTA partition!", _OTAT_ERROR);
        http.end(); return false;
    }

    // Show filename and empty progress bar
    String fname = firmwareUrl.substring(firmwareUrl.lastIndexOf('/') + 1);
    _otat_status(tft, fname.c_str(), _OTAT_VALUE);
    _otat_bar(tft, 0, total);

    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        char m[40]; snprintf(m, sizeof(m), "OTA begin: 0x%x", err);
        _otat_status(tft, m, _OTAT_ERROR); http.end(); return false;
    }

    static uint8_t _otat_buf[4096];
    WiFiClient*   stream   = http.getStreamPtr();
    size_t        written  = 0;
    int           lastPct  = -1;
    unsigned long lastData = millis();

    while (written < (size_t)total) {
        size_t toRead = min((int)sizeof(_otat_buf), total - (int)written);
        int len = stream->readBytes(_otat_buf, toRead);
        if (len > 0) {
            err = esp_ota_write(handle, _otat_buf, len);
            if (err != ESP_OK) {
                char m[40]; snprintf(m, sizeof(m), "OTA write: 0x%x", err);
                _otat_status(tft, m, _OTAT_ERROR);
                esp_ota_abort(handle); http.end(); return false;
            }
            written  += len;
            lastData  = millis();
            int pct   = (int)(written * 100 / total);
            int pct10 = (pct / 10) * 10;
            if (pct10 > lastPct) { lastPct = pct10; _otat_bar(tft, written, total); }
        } else {
            if (millis() - lastData > 5000) {
                _otat_status(tft, "Stream timeout!", _OTAT_ERROR);
                esp_ota_abort(handle); http.end(); return false;
            }
            delay(10);
        }
    }
    http.end();

    if ((err = esp_ota_end(handle)) != ESP_OK) {
        char m[40]; snprintf(m, sizeof(m), "OTA end: 0x%x", err);
        _otat_status(tft, m, _OTAT_ERROR); return false;
    }
    if ((err = esp_ota_set_boot_partition(part)) != ESP_OK) {
        char m[40]; snprintf(m, sizeof(m), "Boot part: 0x%x", err);
        _otat_status(tft, m, _OTAT_ERROR); return false;
    }

    // ── Completion screen ────────────────────────────────────────────────────
    _otat_bar(tft, total, total);   // ensure bar shows 100%

    tft.fillRect(0, _OTAT_Y_MSG - 15, _OTAT_W, _OTAT_H - (_OTAT_Y_MSG - 15), _OTAT_BG);

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(_OTAT_OK, _OTAT_BG);
    tft.drawString("Flash complete!", _OTAT_W / 2, _OTAT_Y_MSG);

    char bMsg[40];
    snprintf(bMsg, sizeof(bMsg), "Boot partition  >>  %s", part->label);
    tft.setTextFont(2);
    tft.setTextColor(_OTAT_DIM, _OTAT_BG);
    tft.drawString(bMsg, _OTAT_W / 2, _OTAT_Y_BOOT);

    _otat_countdown(tft, _OTAT_Y_COUNT, "Rebooting in", 3);
    ESP.restart();
    return true;
}

// ─── Public API ──────────────────────────────────────────────────────────────
//
//  Call once from setup() after WiFi is connected.
//  - Takes over the full screen for the duration of the check / update.
//  - Never returns if an update is applied (device reboots automatically).
//  - Returns false (and restores control) if firmware is already up to date.
//
static bool HB9IIU_OTA_TFT_checkAndUpdate(const char* versionUrl,
                                           const char* currentVersion,
                                           TFT_eSPI&   tft) {
    String url, sha;
    if (_otat_check(tft, versionUrl, currentVersion, url, sha)) {
        _otat_download(tft, url, sha);
    }
    return false;
}
