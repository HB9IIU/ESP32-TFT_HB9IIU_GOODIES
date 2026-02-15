#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <stdarg.h>
#include <ESPmDNS.h>
#include <ESPping.h>
#include "config.h"

// ============================================================
// HB9IIU Wi-Fi Robust Connection Helper (2 SSIDs)
// - Clean, readable Serial output (phases, explanations, emojis)
// - Optional TFT/UI banner callback hook
// - Optional scan-first ranking by RSSI
// ============================================================


// =========================
// Tunable behavior knobs
// =========================
static const uint32_t WIFI_CONNECT_TIMEOUT_MS   = 6000;  // per-attempt connect timeout
static const uint8_t  WIFI_CYCLES               = 3;     // full cycles across candidates
static const uint16_t WIFI_RETRY_PAUSE_MS       = 700;   // pause between attempts
static const int      WIFI_MIN_RSSI_TO_TRY_DBM  = -95;   // only used if scanFirst=true

static String g_connectedSSID = "";

// ============================================================
// Optional UI hook: bottom banner status callback
// (Main sketch can register a function that writes to TFT)
// ============================================================
typedef void (*WifiStatusBannerFn)(const char* msg);
static WifiStatusBannerFn g_wifiBanner = nullptr;

static void wifiBanner(const char* msg)
{
  if (g_wifiBanner) g_wifiBanner(msg);
}

static void wifiBannerf(const char* fmt, ...)
{
  if (!g_wifiBanner) return;
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  g_wifiBanner(buf);
}

void setWifiStatusBannerCallback(WifiStatusBannerFn fn)
{
  g_wifiBanner = fn;
}

// ============================================================
// Pretty logging helpers (Serial)
// ============================================================
static void logHeader(const char* title)
{
  Serial.println();
  Serial.println("══════════════════════════════════════════════");
  Serial.print("  "); Serial.println(title);
  Serial.println("══════════════════════════════════════════════");
}

static void logSection(const char* title)
{
  Serial.println();
  Serial.print("— "); Serial.println(title);
}

static void logInfo(const char* msg) { Serial.print("ℹ️  "); Serial.println(msg); }
static void logOk(const char* msg)   { Serial.print("✅ ");  Serial.println(msg); }
static void logWarn(const char* msg) { Serial.print("⚠️  "); Serial.println(msg); }
static void logErr(const char* msg)  { Serial.print("❌ ");  Serial.println(msg); }


static void logInfof(const char* fmt, ...)
{
  Serial.print("ℹ️  ");
  char buf[256];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
  Serial.println();
}


static void logOkf(const char* fmt, ...)
{
  Serial.print("✅ ");
  char buf[256];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
  Serial.println();
}


static void logWarnf(const char* fmt, ...)
{
  Serial.print("⚠️  ");
  char buf[256];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
  Serial.println();
}


static void logErrf(const char* fmt, ...)
{
  Serial.print("❌ ");
  char buf[256];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
  Serial.println();
}

// ============================================================
// WiFi status helpers
// ============================================================
static const char* wifiStatusToStr(wl_status_t s)
{
  switch (s)
  {
    case WL_CONNECTED:       return "CONNECTED";
    case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "UNKNOWN";
  }
}

static void wifiPrintStatusLine()
{
  wl_status_t s = WiFi.status();
  Serial.printf("ℹ️  WiFi.status(): %s (%d)\n", wifiStatusToStr(s), (int)s);
}

static void wifiPrintIpInfo()
{
  Serial.println("📡 Network details:");
  Serial.printf("   • IP   : %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("   • GW   : %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("   • Mask : %s\n", WiFi.subnetMask().toString().c_str());
  Serial.printf("   • RSSI : %d dBm\n", WiFi.RSSI());
}

static bool wifiIsEnabledSSID(const char* ssid)
{
  return (ssid != nullptr) && (ssid[0] != '\0');
}

struct WifiCandidate
{
  const char* ssid;
  const char* pass;
  int rssi;          // from scan, or -999 if unknown
  bool present;      // seen in scan
};

static bool wifiCandidateEnabled(const WifiCandidate& c)
{
  return wifiIsEnabledSSID(c.ssid);
}

// ============================================================
// One connect attempt (one SSID)
// ============================================================
static bool wifiTryConnectOne(const WifiCandidate& c, uint32_t timeoutMs)
{
  if (!wifiCandidateEnabled(c)) return false;

  Serial.println();
  if (c.rssi != -999) logInfof("📶 Attempting SSID \"%s\" (RSSI %d dBm)", c.ssid, c.rssi);
  else                logInfof("📶 Attempting SSID \"%s\" (RSSI unknown)", c.ssid);

  wifiBannerf("                   WiFi: try \"%s\"", c.ssid);

  // Hard reset connection state (clears old association + DHCP)
  logInfo("Resetting Wi-Fi connection state (disconnect + clear config) …");
  WiFi.disconnect(true, true);
  delay(50);

  logInfo("Calling WiFi.begin(ssid, pass) …");
  WiFi.begin(c.ssid, c.pass);

  const uint32_t t0 = millis();
  uint32_t lastPrint = 0;

  while (millis() - t0 < timeoutMs)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      g_connectedSSID = String(c.ssid);
      uint32_t elapsedMs = millis() - t0;
      float elapsedSec = elapsedMs / 1000.0f;
      logOk("Connected!");
      Serial.printf("   ⏱️  Time to connect: %.2f seconds\n", elapsedSec);
      // Print MAC address
      uint8_t mac[6];
      WiFi.macAddress(mac);
      Serial.printf("   🆔  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      wifiPrintIpInfo();
      // Start mDNS responder
      if (MDNS.begin(WIFI_HOSTNAME)) {
        Serial.printf("   🌐 mDNS responder started: http://%s.local\n", WIFI_HOSTNAME);
      } else {
        Serial.println("   ⚠️  mDNS responder failed to start");
      }
      // Ping a known host (e.g., 8.8.8.8)
      IPAddress pingAddr(8,8,8,8);
      Serial.print("   📡 Pinging 8.8.8.8 ... ");
      bool pingOk = Ping.ping(pingAddr, 3);
      float avgMs = Ping.averageTime();
      if (pingOk) {
        const char* rating = "excellent";
        if      (avgMs < 50)   rating = "excellent";
        else if (avgMs < 100)  rating = "good";
        else if (avgMs < 200)  rating = "fair";
        else if (avgMs < 500)  rating = "weak";
        else                   rating = "very weak";
        Serial.printf("OK, avg %.1f ms (%s)\n", avgMs, rating);
      } else {
        Serial.println("FAILED");
      }
      wifiBannerf("                    WiFi: CONNECTED \"%s\"", c.ssid);
      return true;
    }

    // Bottom banner progress: "xs / tot s"
    const uint32_t elapsedS = (millis() - t0) / 1000;
    const uint32_t totalS   = (timeoutMs + 999) / 1000; // round up
    wifiBannerf("                    WiFi: \"%s\"  %lus / %lus",
                c.ssid,
                (unsigned long)elapsedS,
                (unsigned long)totalS);

    // Serial progress (print ~1/sec, not spam)
    if (millis() - lastPrint >= 1000)
    {
      lastPrint = millis();
      Serial.printf("   ⏳ waiting… %lus / %lus  (status=%s)\n",
                    (unsigned long)elapsedS,
                    (unsigned long)totalS,
                    wifiStatusToStr(WiFi.status()));
    }

    delay(250);
  }

  logWarnf("Timeout after %lu ms — not connected.", (unsigned long)timeoutMs);
  wifiPrintStatusLine();
  wifiBannerf("                    WiFi: FAILED \"%s\"", c.ssid);
  return false;
}

// ------------------------------------------------------------
// Robust connector with optional scan
// - scanFirst = true  -> scan, rank known SSIDs by RSSI, try strongest first
// - scanFirst = false -> no scan; try in fixed order: default then alt
// - Empty SSIDs ("") are treated as DISABLED and skipped.
//
// NOTE: SSIDdefault / PASSdefault / SSIDalt / PASSalt must exist in your project.
// ------------------------------------------------------------
bool connectWiFiRobust(bool scanFirst)
{
  g_connectedSSID = "";
  WiFi.mode(WIFI_STA);

  logHeader("Wi-Fi Robust Connect (2 SSIDs)");
  logInfof("Mode: STA | Scan-first: %s | Cycles: %u | Timeout/try: %lu ms",
           scanFirst ? "YES" : "NO",
           (unsigned)WIFI_CYCLES,
           (unsigned long)WIFI_CONNECT_TIMEOUT_MS);

  wifiBanner("          WiFi: starting...");

  WifiCandidate cand[2] = {
    { SSIDdefault, PASSdefault, -999, false },
    { SSIDalt,     PASSalt,     -999, false }
  };

  // Quick sanity: at least one SSID enabled
  if (!wifiCandidateEnabled(cand[0]) && !wifiCandidateEnabled(cand[1]))
  {
    logErr("No SSIDs configured (both are empty).");
    wifiBanner("          WiFi: no SSIDs configured");
    return false;
  }

  // ---------- Optional scan ----------
  if (scanFirst)
  {
    logSection("🔎 Scan for known SSIDs (rank by signal)");
    logInfo("Scanning nearby networks…");
    wifiBanner("          WiFi: scanning...");

    // Ensure we start clean before scanning
    WiFi.disconnect(true, true);
    delay(50);

    int n = WiFi.scanNetworks(false, true);
    if (n <= 0)
    {
      logWarn("Scan returned no networks (or failed). Will still try configured SSIDs.");
      wifiBanner("          WiFi: scan empty -> trying anyway");
    }
    else
    {
      logOkf("Scan found %d networks.", n);
      wifiBannerf("          WiFi: scan found %d", n);

      // --- Print all found networks nicely ---
      Serial.println("\n📶 Found WiFi networks:");
      Serial.println("   #   RSSI   ENC   SSID");
      for (int i = 0; i < n; i++) {
        String s = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        String enc;
        switch (WiFi.encryptionType(i)) {
          case WIFI_AUTH_OPEN: enc = "OPEN"; break;
          case WIFI_AUTH_WEP: enc = "WEP"; break;
          case WIFI_AUTH_WPA_PSK: enc = "WPA"; break;
          case WIFI_AUTH_WPA2_PSK: enc = "WPA2"; break;
          case WIFI_AUTH_WPA_WPA2_PSK: enc = "WPA/WPA2"; break;
          case WIFI_AUTH_WPA2_ENTERPRISE: enc = "WPA2-E"; break;
          case WIFI_AUTH_WPA3_PSK: enc = "WPA3"; break;
          case WIFI_AUTH_WPA2_WPA3_PSK: enc = "WPA2/3"; break;
          default: enc = "?"; break;
        }
        const char* shown_ssid = s.length() > 0 ? s.c_str() : "<hidden>";
        Serial.printf("  %2d) %4d  %-7s %s\n", i+1, rssi, enc.c_str(), shown_ssid);
      }
      Serial.println();

      // Mark candidates as present + store RSSI if found
      for (int i = 0; i < n; i++)
      {
        String s = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);

        for (int k = 0; k < 2; k++)
        {
          if (!wifiCandidateEnabled(cand[k])) continue;
          if (s == cand[k].ssid)
          {
            cand[k].present = true;
            cand[k].rssi = rssi;
          }
        }
      }

      Serial.println("📋 Candidate summary:");
      for (int k = 0; k < 2; k++)
      {
        if (!wifiCandidateEnabled(cand[k]))
        {
          Serial.println("   • (disabled)");
          continue;
        }

        if (cand[k].present) {
          const char* quality = "";
          int rssi = cand[k].rssi;
          if      (rssi >= -50)      quality = "excellent";
          else if (rssi >= -60)      quality = "good";
          else if (rssi >= -70)      quality = "fair";
          else if (rssi >= -80)      quality = "weak";
          else                       quality = "very weak";
          Serial.printf("   • \"%s\"  seen (RSSI %d dBm, %s)\n", cand[k].ssid, rssi, quality);
        } else {
          Serial.printf("   • \"%s\"  not seen\n", cand[k].ssid);
        }
      }
    }

    WiFi.scanDelete();

    // Sort strongest present first (only meaningful if scanFirst)
    auto score = [](const WifiCandidate& c) -> int {
      if (!wifiCandidateEnabled(c)) return -2000; // disabled goes last always
      if (!c.present) return -1000;               // not seen goes after seen
      return c.rssi;                              // higher is better
    };

    if (score(cand[1]) > score(cand[0]))
    {
      WifiCandidate tmp = cand[0];
      cand[0] = cand[1];
      cand[1] = tmp;
      logInfo("Reordered candidates: strongest known SSID first.");
    }
  }

  // ---------- Print attempt order ----------
  logSection("🧭 Attempt order");
  int orderIndex = 1;
  for (int k = 0; k < 2; k++)
  {
    if (!wifiCandidateEnabled(cand[k])) continue;
    Serial.printf("   %d) \"%s\"\n", orderIndex++, cand[k].ssid);
  }

  // ---------- Connect cycles ----------
  for (uint8_t cycle = 1; cycle <= WIFI_CYCLES; cycle++)
  {
    logSection("🔁 Connect cycle");
    logInfof("Cycle %u / %u", (unsigned)cycle, (unsigned)WIFI_CYCLES);
    wifiBannerf("          WiFi: cycle %u/%u", (unsigned)cycle, (unsigned)WIFI_CYCLES);

    for (int k = 0; k < 2; k++)
    {
      if (!wifiCandidateEnabled(cand[k])) continue;

      // Only apply RSSI skip if scanFirst is on and we actually saw it
      if (scanFirst && cand[k].present && cand[k].rssi < WIFI_MIN_RSSI_TO_TRY_DBM)
      {
        logWarnf("Skipping \"%s\" (RSSI %d dBm < %d dBm threshold)",
                 cand[k].ssid, cand[k].rssi, WIFI_MIN_RSSI_TO_TRY_DBM);
        wifiBannerf("          WiFi: skip \"%s\" (weak)", cand[k].ssid);
        continue;
      }

      if (wifiTryConnectOne(cand[k], WIFI_CONNECT_TIMEOUT_MS))
      {
        logOkf("Connected to \"%s\"", g_connectedSSID.c_str());
        // Banner already says CONNECTED; keep it.
        return true;
      }

      delay(WIFI_RETRY_PAUSE_MS);
    }
  }

  logErr("All attempts failed. Staying offline.");
  wifiBanner("          WiFi: all attempts failed");
  Serial.println("\n🔄 Rebooting in 2 seconds...");
  delay(2000); // Give user a moment to see the error
  ESP.restart();
  return false; // Not reached, but keeps compiler happy
}

String getConnectedSSID()
{
  return g_connectedSSID;
}

// ============================================================
// One-call helper used by your sketches
// ============================================================
// Improved: allow user to force strongest-first (scan) or use default (fixed order)
void HB9IIUWifiConnection(bool strongestFirst = false)
{
  logHeader("Wi-Fi bring-up");
  uint32_t t0 = millis();
  bool ok = false;
  if (strongestFirst) {
    logInfo("User requested: scan and connect to strongest known SSID.");
    ok = connectWiFiRobust(true);
    if (!ok) {
      logWarn("Scan/strongest connect failed. Trying fixed order as fallback.");
      wifiBanner("          WiFi: fallback to fixed order...");
      ok = connectWiFiRobust(false);
    }
  } else {
    logInfo("Fast path: try configured SSIDs in fixed order (no scan).");
    ok = connectWiFiRobust(false);
    if (!ok) {
      logWarn("Fast connect failed. Slow path: scan + strongest-first.");
      wifiBanner("          WiFi: retry with scan...");
      ok = connectWiFiRobust(true);
    }
  }
  float elapsedSec = (millis() - t0) / 1000.0f;
  if (!ok)
  {
    logWarn("Wi-Fi not available — continuing in OFFLINE MODE.");
    wifiBanner("          WiFi: OFFLINE MODE");
    Serial.printf("   ⏱️  Total time for WiFi attempts: %.2f seconds\n", elapsedSec);
    // Optional: you could start AP mode / captive portal here later.
  }
  else
  {
    logOkf("Using SSID: %s", getConnectedSSID().c_str());
    wifiBannerf("          WiFi: OK  SSID \"%s\"", getConnectedSSID().c_str());
    Serial.printf("   ⏱️  Total time for WiFi connect: %.2f seconds\n", elapsedSec);
  }
}
