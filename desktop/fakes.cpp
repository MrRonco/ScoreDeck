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

const lv_img_dsc_t* logoGet(const char*, const char* abbr) {
  if (!abbr || !*abbr) return nullptr;
  const char* env = getenv("SDLOGO");
  if (env && env[0] == '0') return nullptr;

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
bool logoKnown(const char*, const char*)                  { return true; }
bool logoRequest(const char*, const char*)                { return false; }
void logoTick()                                           {}
bool headshotRequest(const char*, const char*)            { return false; }
const lv_img_dsc_t* headshotGet(const char*)              { return nullptr; }

// ── web portal ─────────────────────────────────────────────────────────────
void webBegin() {}
void webLoop()  {}
bool webUp()    { return false; }



