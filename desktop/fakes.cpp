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
const lv_img_dsc_t* logoGet(const char*, const char*)     { return nullptr; }
bool logoKnown(const char*, const char*)                  { return true; }
bool logoRequest(const char*, const char*)                { return false; }
void logoTick()                                           {}
bool headshotRequest(const char*, const char*)            { return false; }
const lv_img_dsc_t* headshotGet(const char*)              { return nullptr; }

// ── web portal ─────────────────────────────────────────────────────────────
void webBegin() {}
void webLoop()  {}
bool webUp()    { return false; }



