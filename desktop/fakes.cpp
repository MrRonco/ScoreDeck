// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// fakes.cpp — everything the UI links against that is not the UI.
//
// The rule this file follows: stub the PLUMBING, keep the LOGIC. Anything that
// decides how a pixel looks is compiled from the real firmware source, so the
// harness renders what the device renders:
//
//   * core/state.cpp   — settings load/save, quiet hours, boardFollows()
//   * ui/*.cpp         — every screen, verbatim
//   * assets/font_*.c  — the same generated faces, same glyph ranges
//   * firmware/lv_conf.h — used VERBATIM, so LV_INV_BUF_SIZE and the colour
//                          depth match the panel exactly
//
// Only things that touch a network, a flash chip or the panel are faked here.
// Every api*Start() returns false: scenarios write g_board and friends
// directly, so a fetch would only overwrite what the scenario is trying to
// show.
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include "Arduino.h"
#include "WiFi.h"
#include "../firmware/ScoreDeck/src/core/state.h"
#include "../firmware/ScoreDeck/src/net/api.h"
#include "../firmware/ScoreDeck/src/net/logos.h"
#include "../firmware/ScoreDeck/src/ui/theme.h"
#include "../firmware/ScoreDeck/src/core/types.h"
#include "../firmware/ScoreDeck/src/svc/web.h"
#include "../firmware/ScoreDeck/src/ui/ui.h"

// ── Arduino runtime ────────────────────────────────────────────────────────
static const auto s_start = std::chrono::steady_clock::now();

uint32_t millis() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - s_start).count();
}

// Only the members the shim leaves undefined. begin/print/println are already
// inline in Arduino.h; redefining them here is a hard error, not a warning.
SerialShim Serial;
int SerialShim::printf(const char* fmt, ...) {
  va_list a; va_start(a, fmt);
  const int n = vprintf(fmt, a);
  va_end(a);
  return n;
}
int    SerialShim::available()             { return 0; }
String SerialShim::readStringUntil(char)   { return String(); }
void   SerialShim::flush()                 { fflush(stdout); }

// ── network: every entry point declines ────────────────────────────────────
// Returning false is the honest answer — the harness has no proxy. The UI
// already handles a declined fetch (it keeps whatever it has), which is
// exactly what we want while a scenario is on screen.
bool apiPollStart()                                  { return false; }
bool apiGameStart(const char*, const char*)          { return false; }
bool apiStandingsStart(const char*)                  { return false; }
bool apiNewsStart()                                  { return false; }
bool apiLineupStart(const char*, const char*)        { return false; }
bool apiPlayerStart(const char*, const char*)        { return false; }
// The Test button has nothing to reach in the harness; report it honestly.
int  netProbeProxy(uint16_t* ms)                     { if (ms) *ms = 0; return -1; }
int  netRelayGet(const String&, String&)             { return -1; }

// ── logos and headshots: always a miss ─────────────────────────────────────
// The colour badge IS the shipped fallback, and it is what every install sees
// before running the asset build. Exercising it here is more useful than
// wiring a cache the harness cannot populate.
volatile bool g_logoArrived = false;
volatile bool g_headshotArrived = false;
// Logos: synthesised, not fetched.
//
// This used to return nullptr always, which meant the harness rendered the
// colour-badge FALLBACK on every tile and the logo path — the one the panel
// actually uses — was never drawn here at all. A layering bug in it was
// therefore invisible to every screenshot this harness has ever produced.
//
// SDLOGO=0 restores the old behaviour when the badge path is what you want to
// look at.
// A SMALL POOL, keyed by team — not one shared image. A single shared logo
// would render identically on every tile, which is exactly the failure mode
// this is here to catch: a cache that hands the same descriptor back for
// different teams looks completely correct if the fixture cannot tell teams
// apart either.
#define FAKE_N 8
static uint8_t*     s_fakeData[FAKE_N];
static lv_img_dsc_t s_fakeDsc[FAKE_N];

// Real blobs, when the user has built them. assets/logos/<league>/<ABBR>@48.bin
// is exactly the payload the proxy serves, header and all, so this exercises
// the same decode path the device runs.
//
// This matters more than it looks. Every design judgement about logo size,
// contrast and containment made in this harness before now was made against
// the synthetic wedges below — the FEATURE hero had literally never rendered a
// real mark. The chip solve cannot be evaluated against a fake.
#define REAL_N 64
static uint8_t*     s_realData[REAL_N];
static lv_img_dsc_t s_realDsc[REAL_N];
static LogoChip     s_realChip[REAL_N];
static char         s_realKey[REAL_N][16];
static uint8_t      s_realState[REAL_N];      // 0 unknown, 1 loaded, 2 absent

static int realSlot(const char* league, const char* abbr) {
  char key[16];
  snprintf(key, sizeof key, "%s:%s", league ? league : "", abbr);
  uint32_t h = 2166136261u;
  for (const char* p = key; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
  for (int probe = 0; probe < REAL_N; probe++) {
    const int i = (int)((h + probe) % REAL_N);
    if (s_realState[i] && strcmp(s_realKey[i], key) == 0) return i;
    if (!s_realState[i]) {
      strncpy(s_realKey[i], key, sizeof s_realKey[i] - 1);
      char path[256];
      snprintf(path, sizeof path, "%s/assets/logos/%s/%s@48.bin",
               getenv("SDROOT") ? getenv("SDROOT") : "..", league ? league : "", abbr);
      FILE* f = fopen(path, "rb");
      if (!f) { s_realState[i] = 2; return i; }
      const size_t want = 4 + 48 * 48 * 3;
      uint8_t* buf = (uint8_t*)malloc(want);
      const size_t got = fread(buf, 1, want, f);
      fclose(f);
      if (got != want) { free(buf); s_realState[i] = 2; return i; }
      s_realData[i] = buf;
      s_realDsc[i].header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
      s_realDsc[i].header.always_zero = 0;
      s_realDsc[i].header.w = 48;
      s_realDsc[i].header.h = 48;
      s_realDsc[i].data_size = 48 * 48 * 3;
      s_realDsc[i].data = buf + 4;              // past LVGL's own header
      s_realChip[i] = chipSolve(buf + 4, 48, 48, kStateInk[GS_LIVE].fill);
      s_realState[i] = 1;
      return i;
    }
  }
  return -1;
}

const lv_img_dsc_t* logoGet(const char* league, const char* abbr) {
  if (!abbr || !*abbr) return nullptr;
  const char* env = getenv("SDLOGO");
  if (env && env[0] == '0') return nullptr;

  const int r = realSlot(league, abbr);
  if (r >= 0 && s_realState[r] == 1) return &s_realDsc[r];

  uint32_t h = 2166136261u;                       // FNV-1a over the abbr
  for (const char* p = abbr; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
  const int idx = (int)(h % FAKE_N);

  if (!s_fakeData[idx]) {
    const int W = 48;
    s_fakeData[idx] = (uint8_t*)calloc(W * W, 3);
    // A distinct wedge count per slot, so two teams sharing a descriptor is
    // visible at a glance rather than something you have to measure.
    const int wedges = idx + 2;
    for (int y = 0; y < W; y++) {
      for (int x = 0; x < W; x++) {
        const float dx = x - W / 2.0f, dy = y - W / 2.0f;
        const float d2 = dx * dx + dy * dy;
        const bool inside = d2 < (W / 2.0f) * (W / 2.0f);
        const float ang = atan2f(dy, dx) + 3.14159f;
        const bool on = inside && ((int)(ang / (6.2832f / wedges)) & 1);
        uint8_t* px = s_fakeData[idx] + (y * W + x) * 3;
        const uint16_t c = on ? 0xFFFF : 0x4A69;
        px[0] = c & 0xFF; px[1] = c >> 8;
        px[2] = inside ? 0xFF : 0x00;
      }
    }
    s_fakeDsc[idx].header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    s_fakeDsc[idx].header.always_zero = 0;
    s_fakeDsc[idx].header.w = W;
    s_fakeDsc[idx].header.h = W;
    s_fakeDsc[idx].data_size = W * W * 3;
    s_fakeDsc[idx].data = s_fakeData[idx];
  }
  return &s_fakeDsc[idx];
}

#include "../firmware/ScoreDeck/src/ui/imgscale.h"

// Scaled variants over the real blobs, mirroring the firmware store exactly.
static struct { int slot; uint16_t size; uint8_t* data; lv_img_dsc_t dsc; } s_scal[96];
static int s_scalN;

const lv_img_dsc_t* logoGetScaled(const char* league, const char* abbr, uint16_t size) {
  if (!abbr || !*abbr || !size) return nullptr;
  const char* env = getenv("SDLOGO");
  if (env && env[0] == '0') return nullptr;
  const int r = realSlot(league, abbr);
  if (r < 0 || s_realState[r] != 1) {
    // The wedge fallback is 48 px too — without the old zoom it must also be
    // pre-scaled, or fixture teams render an oversized pie over the tile.
    const lv_img_dsc_t* fb = logoGet(league, abbr);
    if (!fb || size == 48) return fb;
    const int pseudo = -1 - (int)(fb - &s_fakeDsc[0]);
    for (int i = 0; i < s_scalN; i++)
      if (s_scal[i].slot == pseudo && s_scal[i].size == size) return &s_scal[i].dsc;
    if (s_scalN >= 96) return fb;
    uint8_t* buf = (uint8_t*)malloc((size_t)size * size * 3);
    imgScaleRgb565A8((const uint8_t*)fb->data, 48, 48, buf, size, size);
    auto& v = s_scal[s_scalN++];
    v.slot = pseudo; v.size = size; v.data = buf;
    v.dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    v.dsc.header.always_zero = 0;
    v.dsc.header.w = size; v.dsc.header.h = size;
    v.dsc.data_size = (uint32_t)size * size * 3;
    v.dsc.data = buf;
    return &v.dsc;
  }
  if (size == 48) return &s_realDsc[r];
  for (int i = 0; i < s_scalN; i++)
    if (s_scal[i].slot == r && s_scal[i].size == size) return &s_scal[i].dsc;
  if (s_scalN >= 96) return &s_realDsc[r];
  uint8_t* buf = (uint8_t*)malloc((size_t)size * size * 3);
  imgScaleRgb565A8(s_realData[r] + 4, 48, 48, buf, size, size);
  auto& v = s_scal[s_scalN++];
  v.slot = r; v.size = size; v.data = buf;
  v.dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
  v.dsc.header.always_zero = 0;
  v.dsc.header.w = size; v.dsc.header.h = size;
  v.dsc.data_size = (uint32_t)size * size * 3;
  v.dsc.data = buf;
  return &v.dsc;
}

LogoChip logoChip(const char* league, const char* abbr) {
  LogoChip none = { 0, 0 };
  if (!abbr || !*abbr) return none;
  const int r = realSlot(league, abbr);
  return (r >= 0 && s_realState[r] == 1) ? s_realChip[r] : none;
}
bool logoKnown(const char*, const char*)                  { return true; }
bool logoRequest(const char*, const char*)                { return false; }
void logoTick()                                           {}
bool headshotRequest(const char*, const char*)            { return false; }
const lv_img_dsc_t* headshotGet(const char*)              { return nullptr; }

// ── web portal ─────────────────────────────────────────────────────────────
void webBegin() {}
void webLoop()  {}
bool webUp()    { return false; }




// ── catalog + poll fakes ───────────────────────────────────────────────────
bool apiCatalogStart() {
  static const struct { const char* s; const char* l; const char* f; } K[] = {
    { "nfl", "NFL", "football" },   { "ncaaf", "NCAAF", "football" },
    { "nba", "NBA", "basketball" }, { "wnba", "WNBA", "basketball" },
    { "ncaam", "NCAAM", "basketball" }, { "ncaaw", "NCAAW", "basketball" },
    { "nhl", "NHL", "hockey" },     { "ncaawh", "NCAAW H", "hockey" },
    { "mlb", "MLB", "baseball" },
    { "eng.1", "EPL", "soccer" },   { "esp.1", "LaLiga", "soccer" },
    { "ger.1", "Bund", "soccer" },  { "ita.1", "SerieA", "soccer" },
    { "fra.1", "Ligue1", "soccer" },{ "ucl", "UCL", "soccer" },
    { "uel", "UEL", "soccer" },     { "uwcl", "UWCL", "soccer" },
    { "mls", "MLS", "soccer" },     { "nwsl", "NWSL", "soccer" },
    { "atp", "ATP", "other" },      { "wta", "WTA", "other" },
    { "pga", "PGA", "other" },      { "lpga", "LPGA", "other" },
    { "f1", "F1", "other" },
  };
  g_catalogCount = 0;
  for (auto& k : K) {
    CatEntry& c = g_catalog[g_catalogCount++];
    memset(&c, 0, sizeof c);
    strncpy(c.slug, k.s, sizeof c.slug - 1);
    strncpy(c.label, k.l, sizeof c.label - 1);
    strncpy(c.family, k.f, sizeof c.family - 1);
  }
  g_catalogLoaded = true;
  g_catalogReady = true;
  return true;
}

void webPollNow() {}
