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
#include "../ui/imgscale.h"

// Cache hit rate answers "why are my tiles letter badges", which is almost
// always because the logo build was never run. Counted in logoResolve(), the
// one path both getters share — see the note there.
static uint16_t s_hits, s_misses;
uint16_t logoCacheHits()   { return s_hits; }
uint16_t logoCacheMisses() { return s_misses; }
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include "../core/state.h"
#include "../ui/ui.h"

// Must exceed the WORKING SET, not the page size. The densest layout is 4x3
// tiles = 24 logos, so a 24-slot cache sat exactly at the cap: every refresh
// evicted one entry and refetched it, forever, hammering the proxy. Auto
// density made that reachable on any busy night.
//
// 36 covers a full Dense page plus the page you just swiped away from, which
// is the real working set when paging. Each slot is ~6.9 KB of PSRAM.
#define LOGO_SLOTS 36
#define LOGO_SIZE  48
#define LOGO_BYTES (4 + LOGO_SIZE * LOGO_SIZE * 3)

struct Slot {
  char        key[16];        // "nhl:TOR"
  uint8_t*    data;
  lv_img_dsc_t dsc;
  uint32_t    lastUse;
  bool        miss;           // 404 — do not ask again this boot
  // Solved once, here, because this runs on the core-0 fetch task where a
  // 2,300-op scan is free against the HTTP round trip that precedes it. Doing
  // it at draw time would repeat it on every repaint of every tile.
  LogoChip    chip;
  // Pre-scaled variants. FOUR, not three. The inventory is: hero 52, tile
  // badge 26/30/38 (one live per density), idle NEXT UP 34, and results card
  // 24 — so a team on the hero, a tile, the idle card and a results card
  // wants four at once. The results card is the newest consumer and it is
  // what pushed this over. Freed when the slot is evicted;
  // never freed mid-tenancy, so a hidden consumer's src pointer stays valid
  // for exactly as long as the slot itself does — the same lifetime the
  // 48 px dsc already has.
  struct Scaled { uint16_t size; uint8_t* data; lv_img_dsc_t dsc; };
  Scaled      sc[4];
  uint8_t     scN;
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

/**
 * Resolve a team to a slot holding a drawable blob, counting the hit or miss.
 *
 * ONE place, because the count has to follow what the SCREEN got. It used to
 * live in logoGet(), which every consumer stopped calling when they moved to
 * logoGetScaled() for their own size — six callers, none of them counted — so
 * the figure was structurally pinned at "0 hit, 0 miss" and the console's Logo
 * cache row said nothing. That row exists to answer "why are my tiles letter
 * badges", which is the question a dead counter is worst at.
 */
static Slot* logoResolve(const char* league, const char* abbr) {
  // Leaderboard and grid tiles have no team on a side, so they ask with an
  // empty abbreviation. That is not a cache miss to be filled — the proxy has
  // nothing to give, and requesting it produced a 400 on every single refresh.
  // It is not a miss to be COUNTED either: it would dilute the ratio with rows
  // that were never going to have a mark.
  if (!abbr || !*abbr) return nullptr;
  char key[16];
  snprintf(key, sizeof key, "%s:%s", league, abbr);
  Slot* s = find(key);
  const bool got = s && !s->miss && s->data;
  got ? (void)s_hits++ : (void)s_misses++;
  if (!got) return nullptr;
  s->lastUse = ++s_tick;
  return s;
}

const lv_img_dsc_t* logoGet(const char* league, const char* abbr) {
  Slot* s = logoResolve(league, abbr);
  return s ? &s->dsc : nullptr;
}

const lv_img_dsc_t* logoGetScaled(const char* league, const char* abbr, uint16_t size) {
  if (!size) return nullptr;
  Slot* s = logoResolve(league, abbr);
  if (!s) return nullptr;
  if (size == LOGO_SIZE) return &s->dsc;
  for (uint8_t i = 0; i < s->scN; i++)
    if (s->sc[i].size == size) return &s->sc[i].dsc;
  if (s->scN >= 4) {
    // REFUSE, and return NOTHING — never the 48 px original.
    //
    // This used to hand back &s->dsc, the native descriptor, to a caller that
    // had asked for `size`. The caller has no way to detect that: it sets the
    // image source and LVGL draws it at whatever the descriptor says. On the
    // panel that rendered 48 px club marks inside 24 px results-card slots,
    // overlapping the abbreviation and the word "Final" on every card — the
    // caller was asking for a quarter of the area it got.
    //
    // Every consumer already treats nullptr as "no logo" and falls back to the
    // colour badge, which is correct and looks deliberate. A missing mark is a
    // smaller error than a mark four times its slot.
    Serial.printf("[logo] %s:%s: >4 scaled sizes requested (%u) — no logo\n",
                  league, abbr, size);
    return nullptr;
  }
  uint8_t* buf = (uint8_t*)heap_caps_malloc((size_t)size * size * 3, MALLOC_CAP_SPIRAM);
  // Same refusal as above, same reason: a caller that asked for `size` cannot
  // tell it was handed 48, so PSRAM pressure would render full-size club marks
  // inside 24 px slots rather than degrading to the colour badge.
  if (!buf) return nullptr;
  imgScaleRgb565A8(s->data + 4, LOGO_SIZE, LOGO_SIZE, buf, size, size);
  Slot::Scaled& v = s->sc[s->scN++];
  v.size = size;
  v.data = buf;
  v.dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
  v.dsc.header.always_zero = 0;
  v.dsc.header.w = size;
  v.dsc.header.h = size;
  v.dsc.data_size = (uint32_t)size * size * 3;
  v.dsc.data = buf;
  return &v.dsc;
}

LogoChip logoChip(const char* league, const char* abbr) {
  LogoChip none = { 0, 0 };
  if (!abbr || !*abbr) return none;
  char key[16];
  snprintf(key, sizeof key, "%s:%s", league, abbr);
  Slot* s = find(key);
  // Deliberately does NOT touch lastUse or the hit/miss counters — this is
  // asked alongside logoGet() for the same team on the same refresh, and
  // double-counting would make the cache-effectiveness figure on the
  // diagnostics page read half what it is.
  if (!s || s->miss || !s->data) return none;
  return s->chip;
}

bool logoKnown(const char* league, const char* abbr) {
  // A side with no team — a golf field, an F1 grid — is permanently "known":
  // there is nothing to fetch, and asking produced a 400 every refresh.
  if (!abbr || !*abbr) return true;
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

  // The slot is being re-tenanted: the old team's scaled variants die here.
  for (uint8_t vi = 0; vi < s->scN; vi++) heap_caps_free(s->sc[vi].data);
  s->scN = 0;
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
    // Solve the ground BEFORE g_logoArrived is published, or the board can
    // draw the mark for one frame with a stale chip from the evicted team.
    s->chip = chipSolve(s->data + 4, LOGO_SIZE, LOGO_SIZE, kStateInk[GS_LIVE].fill);
  } else {
    s->chip.opa = 0;
  }
  if (ok) g_logoArrived = true;
  Serial.printf("[logo] %s %s (http %d) chip=%06X opa=%u\n", s_want,
                ok ? "ok" : "miss", status, (unsigned)s->chip.color, (unsigned)s->chip.opa);
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

/** Request the first side of this game we have no blob for, away before home
 *  so the marks fill in the reading order they are drawn in. True when this
 *  game had something to ask for, which ends the tick — one logo per call. */
static bool logoWantPair(const Game& g) {
  if (!logoKnown(g.league, g.away.abbr)) { logoRequest(g.league, g.away.abbr); return true; }
  if (!logoKnown(g.league, g.home.abbr)) { logoRequest(g.league, g.home.abbr); return true; }
  return false;
}

/**
 * Ask for one missing logo per call, for a team actually on screen. Called from
 * loop() so it never competes with the state poll for the single TLS slot.
 */
void logoTick() {
  if (s_inFlight) return;
  // ONLY what is on screen. This used to walk all of g_board, which on a
  // 48-game night wants 96 logos against a 36-slot cache — every refresh would
  // evict something it was about to need and fetch it again, forever. Bounding
  // the working set to one page is what makes the cache a cache.
  //
  // "On screen" is THREE surfaces, not one. This used to walk the tile strip
  // alone — which in the FEATURE layout is the one surface whose teams the
  // user is NOT looking at. The hero is excluded from a tile slot by
  // construction (ui_board.cpp skips `i == heroIdx`), and a ledger final never
  // had a slot to begin with. So on a quiet night with a single live game the
  // strip is EMPTY, this loop had nothing to iterate, and a cold cache stayed
  // cold forever: every mark on the panel a letter badge, with no way back
  // short of a busier slate promoting the board to a grid. It survived because
  // a warm cache from an earlier grid render hid it — until a reflash, which
  // clears PSRAM and lands straight on FEATURE.
  //
  // Hero first: it carries the largest marks, so it is what the eye misses.
  const int8_t hero = uiHeroGameIdx();
  if (hero >= 0 && hero < g_gameCount && logoWantPair(g_board[hero])) return;

  for (uint8_t slot = 0; slot < TILES_PER_PAGE; slot++) {
    const int8_t gi = uiBoardTileGame(slot);
    if (gi < 0 || gi >= g_gameCount) continue;
    if (logoWantPair(g_board[gi])) return;
  }

  const int nCards = uiLedgerCount();
  for (int k = 0; k < nCards; k++) {
    const int8_t gi = uiLedgerGame((uint8_t)k);
    if (gi < 0 || gi >= g_gameCount) continue;
    if (logoWantPair(g_board[gi])) return;
  }

  // FOUR surfaces. The idle screen's NEXT UP card draws two marks of its own
  // and belongs to no board layout, so none of the walks above can reach it.
  // It is also the surface most likely to be the ONLY thing on screen — a
  // fresh install, or any morning before the first game — which is exactly
  // when nothing else is around to have warmed the cache for it.
  const Game* nx = uiIdleNextGame();
  if (nx && logoWantPair(*nx)) return;
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
