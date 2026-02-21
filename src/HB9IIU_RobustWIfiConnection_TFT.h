#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  HB9IIU_RobustWIfiConnection_TFT.h
//  TFT display Wi-Fi connection — ESP32 / Arduino core 3.x / 480×320
//
//  Usage (single call from setup, before OTA):
//      HB9IIUWifiConnection_TFT(false, tft);
//
//  Credentials and hostname come from config.h:
//      SSIDdefault / PASSdefault / SSIDalt / PASSalt / WIFI_HOSTNAME
//  All internal functions are static — safe to include alongside
//  HB9IIU_RobustWIfiConnection.h in the same project.
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPping.h>
#include "config.h"

// ─── Tunable ─────────────────────────────────────────────────────────────
static const uint32_t _WIFT_TIMEOUT_MS  = 6000;  // per-attempt timeout
static const uint8_t  _WIFT_CYCLES      = 3;     // full cycles across candidates
static const uint16_t _WIFT_PAUSE_MS    = 700;   // pause between attempts
static const int      _WIFT_MIN_RSSI    = -95;   // skip if weaker (scan mode)

// ─── Colour palette (consistent with HB9IIU_OTA_TFT) ────────────────────
#define _WIFT_BG          TFT_BLACK
#define _WIFT_HDR_BG      TFT_NAVY
#define _WIFT_HDR_TXT     TFT_WHITE
#define _WIFT_LABEL       0x8410    // mid-grey
#define _WIFT_VALUE       TFT_WHITE
#define _WIFT_OK          TFT_GREEN
#define _WIFT_WARN        TFT_ORANGE
#define _WIFT_ERROR       TFT_RED
#define _WIFT_DIM         0xC618    // light grey

// ─── Layout constants (480 × 320) ────────────────────────────────────────
#define _WIFT_W           480
#define _WIFT_H           320
#define _WIFT_HDR_H        50
#define _WIFT_M            20
#define _WIFT_COL_LBL      20

#define _WIFT_Y_STATUS     80    // main status    (Font 4, centred)
#define _WIFT_Y_SSID      118    // SSID name      (Font 4, centred)
#define _WIFT_Y_DIV       144    // divider y
#define _WIFT_Y_ROW1      165    // detail row 1   (Font 2)
#define _WIFT_Y_ROW2      187    // detail row 2   (Font 2)
#define _WIFT_Y_ROW3      209    // detail row 3   (Font 2)
#define _WIFT_Y_PROG      248    // progress line  (Font 2, centred)
#define _WIFT_Y_BOT       285    // bottom line    (Font 2, centred)
//  Verify: _WIFT_Y_BOT + 8 = 293 < 320 ✓

// ─── Drawing primitives ──────────────────────────────────────────────────

static void _wift_header(TFT_eSPI& tft) {
    tft.fillScreen(_WIFT_BG);
    tft.fillRect(0, 0, _WIFT_W, _WIFT_HDR_H, _WIFT_HDR_BG);
    tft.setTextColor(_WIFT_HDR_TXT, _WIFT_HDR_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.drawString("WI-FI CONNECTION", _WIFT_W / 2, _WIFT_HDR_H / 2);
}

static void _wift_status(TFT_eSPI& tft, const char* msg, uint16_t col) {
    tft.fillRect(0, _WIFT_Y_STATUS - 15, _WIFT_W, 32, _WIFT_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(col, _WIFT_BG);
    tft.drawString(msg, _WIFT_W / 2, _WIFT_Y_STATUS);
}

static void _wift_ssid_row(TFT_eSPI& tft, const char* ssid, uint16_t col) {
    tft.fillRect(0, _WIFT_Y_SSID - 15, _WIFT_W, 32, _WIFT_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(col, _WIFT_BG);
    tft.drawString(ssid, _WIFT_W / 2, _WIFT_Y_SSID);
}

static void _wift_divider(TFT_eSPI& tft) {
    tft.drawFastHLine(_WIFT_M, _WIFT_Y_DIV, _WIFT_W - 2 * _WIFT_M, _WIFT_DIM);
}

// Small info row: grey label left, coloured value centred.
static void _wift_row(TFT_eSPI& tft, int y,
                       const char* label, const char* value, uint16_t vcol) {
    tft.fillRect(0, y - 10, _WIFT_W, 22, _WIFT_BG);
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(_WIFT_LABEL, _WIFT_BG);
    tft.drawString(label, _WIFT_COL_LBL, y);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(vcol, _WIFT_BG);
    tft.drawString(value, _WIFT_W / 2, y);
}

static void _wift_progress(TFT_eSPI& tft, const char* msg) {
    tft.fillRect(0, _WIFT_Y_PROG - 10, _WIFT_W, 22, _WIFT_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(_WIFT_DIM, _WIFT_BG);
    tft.drawString(msg, _WIFT_W / 2, _WIFT_Y_PROG);
}

static void _wift_bottom(TFT_eSPI& tft, const char* msg, uint16_t col) {
    tft.fillRect(0, _WIFT_Y_BOT - 10, _WIFT_W, 22, _WIFT_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(col, _WIFT_BG);
    tft.drawString(msg, _WIFT_W / 2, _WIFT_Y_BOT);
}

static void _wift_countdown(TFT_eSPI& tft, const char* prefix, int secs) {
    for (int i = secs; i >= 1; i--) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%s %d...", prefix, i);
        _wift_bottom(tft, buf, _WIFT_DIM);
        delay(1000);
    }
}

// ─── Helpers ─────────────────────────────────────────────────────────────

static const char* _wift_rssi_quality(int rssi) {
    if (rssi >= -50) return "excellent";
    if (rssi >= -60) return "good";
    if (rssi >= -70) return "fair";
    if (rssi >= -80) return "weak";
    return "very weak";
}

struct _WiftCandidate {
    const char* ssid;
    const char* pass;
    int  rssi;
    bool present;
};

// ─── One connection attempt ───────────────────────────────────────────────

static bool _wift_try_one(TFT_eSPI& tft, const _WiftCandidate& c,
                           uint8_t cycle, uint8_t totalCycles) {
    if (!c.ssid || c.ssid[0] == '\0') return false;

    _wift_status(tft, "Connecting...", _WIFT_DIM);
    _wift_ssid_row(tft, c.ssid, _WIFT_WARN);
    _wift_divider(tft);
    tft.fillRect(0, _WIFT_Y_ROW1 - 10, _WIFT_W, _WIFT_Y_PROG - _WIFT_Y_ROW1 + 20, _WIFT_BG);

    Serial.printf("[WiFi] Trying \"%s\"  (cycle %u/%u)\n", c.ssid, cycle, totalCycles);

    WiFi.disconnect(true, true);
    delay(50);
    WiFi.begin(c.ssid, c.pass);

    const uint32_t t0   = millis();
    const uint32_t totS = (_WIFT_TIMEOUT_MS + 999) / 1000;

    while (millis() - t0 < _WIFT_TIMEOUT_MS) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WiFi] Connected to \"%s\"\n", c.ssid);
            return true;
        }
        uint32_t elS = (millis() - t0) / 1000;
        char buf[56];
        snprintf(buf, sizeof(buf), "Cycle %u / %u     %lus / %lus",
                 (unsigned)cycle, (unsigned)totalCycles,
                 (unsigned long)elS, (unsigned long)totS);
        _wift_progress(tft, buf);
        delay(250);
    }

    Serial.printf("[WiFi] Timeout — \"%s\"\n", c.ssid);
    return false;
}

// ─── Robust connect loop ─────────────────────────────────────────────────

static bool _wift_connect_robust(TFT_eSPI& tft, bool scanFirst) {
    WiFi.mode(WIFI_STA);

    _WiftCandidate cand[2] = {
        { SSIDdefault, PASSdefault, -999, false },
        { SSIDalt,     PASSalt,     -999, false }
    };

    if (scanFirst) {
        _wift_status(tft, "Scanning...", _WIFT_DIM);
        _wift_ssid_row(tft, "", _WIFT_BG);
        _wift_divider(tft);
        tft.fillRect(0, _WIFT_Y_ROW1 - 10, _WIFT_W, 80, _WIFT_BG);
        _wift_progress(tft, "Looking for networks...");

        WiFi.disconnect(true, true);
        delay(50);
        int n = WiFi.scanNetworks(false, true);
        if (n > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Found %d network%s", n, n == 1 ? "" : "s");
            _wift_progress(tft, buf);
            Serial.printf("[WiFi] Scan: %d networks\n", n);

            for (int i = 0; i < n; i++) {
                int rssi = WiFi.RSSI(i);
                for (int k = 0; k < 2; k++) {
                    if (cand[k].ssid && WiFi.SSID(i) == cand[k].ssid) {
                        cand[k].present = true;
                        cand[k].rssi    = rssi;
                    }
                }
            }
            WiFi.scanDelete();

            // Sort strongest known SSID first
            auto score = [](const _WiftCandidate& c) -> int {
                if (!c.ssid || c.ssid[0] == '\0') return -2000;
                if (!c.present)                    return -1000;
                return c.rssi;
            };
            if (score(cand[1]) > score(cand[0])) {
                _WiftCandidate tmp = cand[0]; cand[0] = cand[1]; cand[1] = tmp;
            }
        }
        delay(400);
    }

    for (uint8_t cycle = 1; cycle <= _WIFT_CYCLES; cycle++) {
        for (int k = 0; k < 2; k++) {
            if (!cand[k].ssid || cand[k].ssid[0] == '\0') continue;
            if (scanFirst && cand[k].present && cand[k].rssi < _WIFT_MIN_RSSI) continue;

            if (_wift_try_one(tft, cand[k], cycle, _WIFT_CYCLES))
                return true;

            delay(_WIFT_PAUSE_MS);
        }
    }
    return false;
}

// ─── Public API ──────────────────────────────────────────────────────────
//
//  Call once from setup() before OTA.
//  - Takes over the full screen for the duration of the connection.
//  - On success: shows connected info for 2.5 s then returns.
//  - On failure: shows error + 3 s countdown, then reboots.
//
static void HB9IIUWifiConnection_TFT(bool strongestFirst, TFT_eSPI& tft) {
    _wift_header(tft);
    _wift_status(tft, "Starting...", _WIFT_DIM);

    bool ok = _wift_connect_robust(tft, strongestFirst);
    if (!ok) {
        // Fallback: try the other strategy
        ok = _wift_connect_robust(tft, !strongestFirst);
    }

    if (!ok) {
        _wift_status(tft, "Connection Failed!", _WIFT_ERROR);
        _wift_ssid_row(tft, "All attempts exhausted", _WIFT_DIM);
        _wift_divider(tft);
        tft.fillRect(0, _WIFT_Y_ROW1 - 10, _WIFT_W, 80, _WIFT_BG);
        _wift_countdown(tft, "Rebooting in", 3);
        ESP.restart();
        return;
    }

    // ── Connected screen ─────────────────────────────────────────────────
    _wift_status(tft, "Connected!", _WIFT_OK);
    _wift_ssid_row(tft, WiFi.SSID().c_str(), _WIFT_OK);
    _wift_divider(tft);

    // IP address
    _wift_row(tft, _WIFT_Y_ROW1, "IP Address:",
              WiFi.localIP().toString().c_str(), _WIFT_VALUE);

    // Signal strength
    int rssi = WiFi.RSSI();
    char rssiBuf[40];
    snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm  (%s)", rssi, _wift_rssi_quality(rssi));
    uint16_t rssiCol = rssi >= -60 ? _WIFT_OK : rssi >= -75 ? _WIFT_WARN : _WIFT_ERROR;
    _wift_row(tft, _WIFT_Y_ROW2, "Signal:", rssiBuf, rssiCol);

    // mDNS hostname
    char mdnsBuf[40];
    if (MDNS.begin(WIFI_HOSTNAME)) {
        snprintf(mdnsBuf, sizeof(mdnsBuf), "%s.local", WIFI_HOSTNAME);
        _wift_row(tft, _WIFT_Y_ROW3, "Hostname:", mdnsBuf, _WIFT_DIM);
        Serial.printf("[WiFi] mDNS: http://%s.local\n", WIFI_HOSTNAME);
    }

    // Ping 8.8.8.8
    _wift_progress(tft, "Pinging 8.8.8.8...");
    IPAddress pingAddr(8, 8, 8, 8);
    if (Ping.ping(pingAddr, 3)) {
        float avgMs = Ping.averageTime();
        const char* q = avgMs < 50  ? "excellent" :
                        avgMs < 100 ? "good"      :
                        avgMs < 200 ? "fair"      :
                        avgMs < 500 ? "weak"      : "very weak";
        char pingBuf[40];
        snprintf(pingBuf, sizeof(pingBuf), "8.8.8.8  —  %.1f ms  (%s)", avgMs, q);
        _wift_progress(tft, pingBuf);
        Serial.printf("[WiFi] Ping: %.1f ms (%s)\n", avgMs, q);
    } else {
        _wift_progress(tft, "Ping failed");
        Serial.println("[WiFi] Ping: FAILED");
    }

    _wift_bottom(tft, "Continuing...", _WIFT_DIM);
    delay(2500);
}
