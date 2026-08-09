// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// logos.cpp — team logo blobs, RAM only.
//
// Deliberately NOT cached to FATFS. Every flash write stalls the panel DMA for
// 150-220 ms regardless of size (INHERITED_RULES.md §16), and a board shows at
// most ~18 distinct teams. Holding them in PSRAM costs 24 x 6.9 KB = 166 KB out
// of 8 MB and stalls nothing. A cache miss costs one small fetch.
//
// A miss is not an error: the device renders its colour badge instead, which is
// what every install sees until the user runs the logo build. See
// docs/OPEN_SOURCE.md §1 for why the artwork is never shipped.
#include "logos.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include "../core/state.h"

#define LOGO_SLOTS 24
#define LOGO_SIZE  48
#define LOGO_BYTES (4 + LOGO_SIZE * LOGO_SIZE * 3)

struct Slot {
  char        key[16];        // "nhl:TOR"
  uint8_t*    data;
  lv_img_dsc_t dsc;
  uint32_t    lastUse;
  bool        miss;           // 404 — do not ask again this boot
};

static Slot s_slot[LOGO_SLOTS];
static uint32_t s_tick;
static char s_want[16];
static volatile bool s_inFlight;
volatile bool g_logoArrived = false;

static Slot* find(const char* key) {
  for (auto& s : s_slot) if (s.key[0] && !strcmp(s.key, key)) return &s;
  return nullptr;
}

/** Least-recently-used victim, preferring an empty slot. */
static Slot* victim() {
  Slot* best = &s_slot[0];
  for (auto& s : s_slot) {
    if (!s.key[0]) return &s;
    if (s.lastUse < best->lastUse) best = &s;
  }
  return best;
}

const lv_img_dsc_t* logoGet(const char* league, const char* abbr) {
  char key[16];
  snprintf(key, sizeof key, "%s:%s", league, abbr);
  Slot* s = find(key);
  if (!s) return nullptr;
  s->lastUse = ++s_tick;
  return (s->miss || !s->data) ? nullptr : &s->dsc;
}

bool logoKnown(const char* league, const char* abbr) {
  char key[16];
  snprintf(key, sizeof key, "%s:%s", league, abbr);
  return find(key) != nullptr;
}

static bool fetchOnce(const char* url, const char* token, uint8_t* into, int* status) {
  WiFiClient plain; WiFiClientSecure secure;
  const bool https = strncmp(url, "https:", 6) == 0;
  if (https) secure.setInsecure();
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) return false;
  http.setUserAgent("ScoreDeck/" SD_VERSION);
  if (token[0]) http.addHeader("Authorization", String("Bearer ") + token);
  *status = http.GET();
  if (*status != 200) { http.end(); return false; }

  // Do NOT gate on getSize(): it is -1 whenever the response is chunked, and
  // both the Node dev server and Cloudflare chunk by default. Gating on it
  // rejected every logo while the server was returning a perfectly good 200.
  // Read until we have the exact blob or the stream stops.
  const int declared = http.getSize();
  if (declared > 0 && declared != LOGO_BYTES) { http.end(); return false; }

  WiFiClient* st = http.getStreamPtr();
  size_t got = 0;
  uint32_t lastData = millis();
  while (got < LOGO_BYTES && millis() - lastData < 4000) {
    const int n = st->readBytes(into + got, LOGO_BYTES - got);
    if (n > 0) { got += n; lastData = millis(); }
    else if (!http.connected() && !st->available()) break;
    else delay(2);
  }
  http.end();
  return got == LOGO_BYTES;
}

static char s_url[300];
static char s_token[80];

static void logoTask(void*) {
  Slot* s = victim();
  if (s->data == nullptr) {
    s->data = (uint8_t*)heap_caps_malloc(LOGO_BYTES, MALLOC_CAP_SPIRAM);
  }
  int status = 0;
  bool ok = false;
  if (s->data) ok = fetchOnce(s_url, s_token, s->data, &status);

  strncpy(s->key, s_want, sizeof s->key - 1);
  s->key[sizeof s->key - 1] = '\0';
  s->lastUse = ++s_tick;
  s->miss = !ok;
  if (ok) {
    // The blob already carries LVGL's 4-byte header; point past it.
    s->dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    s->dsc.header.always_zero = 0;
    s->dsc.header.w = LOGO_SIZE;
    s->dsc.header.h = LOGO_SIZE;
    s->dsc.data_size = LOGO_SIZE * LOGO_SIZE * 3;
    s->dsc.data = s->data + 4;
  }
  if (ok) g_logoArrived = true;
  Serial.printf("[logo] %s %s (http %d)\n", s_want, ok ? "ok" : "miss", status);
  s_inFlight = false;
  vTaskDelete(nullptr);
}

bool logoRequest(const char* league, const char* abbr) {
  if (s_inFlight) return false;
  if (g_set.proxy.isEmpty() || WiFi.status() != WL_CONNECTED) return false;
  if (!netGateOpen()) return false;
  if (logoKnown(league, abbr)) return false;

  snprintf(s_want, sizeof s_want, "%s:%s", league, abbr);
  String url = g_set.proxy;
  if (url.endsWith("/")) url.remove(url.length() - 1);
  url += "/v1/logo/";
  url += league;
  url += "/";
  url += abbr;
  url += "@48.bin";
  if (url.length() >= sizeof s_url) return false;
  strncpy(s_url, url.c_str(), sizeof s_url - 1);
  s_url[sizeof s_url - 1] = '\0';
  strncpy(s_token, g_set.token.c_str(), sizeof s_token - 1);
  s_token[sizeof s_token - 1] = '\0';

  s_inFlight = true;
  if (xTaskCreatePinnedToCore(logoTask, "sdLogo", NET_TASK_STACK, nullptr, 1, nullptr, 0) != pdPASS) {
    s_inFlight = false;
    return false;
  }
  return true;
}

/**
 * Ask for one missing logo per call, for a team actually on screen. Called from
 * loop() so it never competes with the state poll for the single TLS slot.
 */
void logoTick() {
  if (s_inFlight) return;
  for (uint8_t i = 0; i < g_gameCount; i++) {
    const Game& g = g_board[i];
    if (!logoKnown(g.league, g.home.abbr)) { logoRequest(g.league, g.home.abbr); return; }
    if (!logoKnown(g.league, g.away.abbr)) { logoRequest(g.league, g.away.abbr); return; }
  }
}


// ── headshots ──────────────────────────────────────────────────────────────

#define HEAD_SIZE  68
#define HEAD_BYTES (4 + HEAD_SIZE * HEAD_SIZE * 3)

static uint8_t*     s_headData;
static lv_img_dsc_t s_headDsc;
static char         s_headId[12];
static char         s_headWant[12];
static char         s_headUrl[300];
static volatile bool s_headInFlight;
volatile bool g_headshotArrived = false;

const lv_img_dsc_t* headshotGet(const char* athleteId) {
  if (!s_headData || !s_headId[0]) return nullptr;
  return strcmp(s_headId, athleteId) == 0 ? &s_headDsc : nullptr;
}

static void headTask(void*) {
  if (!s_headData) s_headData = (uint8_t*)heap_caps_malloc(HEAD_BYTES, MALLOC_CAP_SPIRAM);
  int status = 0;
  bool ok = false;
  if (s_headData) {
    // Same read shape as logos: never gate on getSize(), it is -1 when chunked.
    WiFiClient plain; WiFiClientSecure secure;
    const bool https = strncmp(s_headUrl, "https:", 6) == 0;
    if (https) secure.setInsecure();
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setReuse(false);
    if (https ? http.begin(secure, s_headUrl) : http.begin(plain, s_headUrl)) {
      http.setUserAgent("ScoreDeck/" SD_VERSION);
      if (s_token[0]) http.addHeader("Authorization", String("Bearer ") + s_token);
      status = http.GET();
      if (status == 200) {
        WiFiClient* st = http.getStreamPtr();
        size_t got = 0;
        uint32_t last = millis();
        while (got < HEAD_BYTES && millis() - last < 5000) {
          const int n = st->readBytes(s_headData + got, HEAD_BYTES - got);
          if (n > 0) { got += n; last = millis(); }
          else if (!http.connected() && !st->available()) break;
          else delay(2);
        }
        ok = got == HEAD_BYTES;
      }
      http.end();
    }
  }
  if (ok) {
    s_headDsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    s_headDsc.header.always_zero = 0;
    s_headDsc.header.w = HEAD_SIZE;
    s_headDsc.header.h = HEAD_SIZE;
    s_headDsc.data_size = HEAD_SIZE * HEAD_SIZE * 3;
    s_headDsc.data = s_headData + 4;
    strncpy(s_headId, s_headWant, sizeof s_headId - 1);
    s_headId[sizeof s_headId - 1] = 0;
    g_headshotArrived = true;
  } else {
    s_headId[0] = 0;
  }
  Serial.printf("[head] %s %s (http %d)\n", s_headWant, ok ? "ok" : "miss", status);
  s_headInFlight = false;
  vTaskDelete(nullptr);
}

bool headshotRequest(const char* league, const char* athleteId) {
  if (s_headInFlight) return false;
  if (!strcmp(s_headId, athleteId)) return false;     // already have it
  if (g_set.proxy.isEmpty() || WiFi.status() != WL_CONNECTED) return false;
  if (!netGateOpen()) return false;
  strncpy(s_headWant, athleteId, sizeof s_headWant - 1);
  s_headWant[sizeof s_headWant - 1] = 0;
  String u = g_set.proxy;
  if (u.endsWith("/")) u.remove(u.length() - 1);
  u += "/v1/head/"; u += league; u += "/"; u += athleteId; u += ".bin";
  if (u.length() >= sizeof s_headUrl) return false;
  strncpy(s_headUrl, u.c_str(), sizeof s_headUrl - 1);
  s_headUrl[sizeof s_headUrl - 1] = 0;
  strncpy(s_token, g_set.token.c_str(), sizeof s_token - 1);
  s_token[sizeof s_token - 1] = 0;
  s_headInFlight = true;
  if (xTaskCreatePinnedToCore(headTask, "sdHead", NET_TASK_STACK, nullptr, 1, nullptr, 0) != pdPASS) {
    s_headInFlight = false;
    return false;
  }
  return true;
}
