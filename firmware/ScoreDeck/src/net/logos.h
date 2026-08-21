// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once
#include <lvgl.h>
#include "../ui/theme.h"

/** Set when a fetch lands, so the board repaints once rather than every tick. */
extern volatile bool g_logoArrived;

/** Cached logo for a team, or nullptr — draw the colour badge instead. */
const lv_img_dsc_t* logoGet(const char* league, const char* abbr);
/** The logo pre-scaled to `size` px, or nullptr. NEVER draw a logo through
 *  lv_img_set_zoom — the transform's draw geometry is broken in ways the
 *  calibration spike (desktop mock 12) measures; see imgscale.cpp. Scaled
 *  variants are cached per slot and freed with it. */
const lv_img_dsc_t* logoGetScaled(const char* league, const char* abbr, uint16_t size);
/** The ground this mark should be drawn on for ONE surface, solved at decode
 *  time. `surf` indexes kStateInk: GS_PRE / GS_LIVE / GS_FINAL / SI_HERO.
 *  opa 0 means "none needed" — see chipSolve() in theme.h.
 *
 *  Solved per surface rather than once, because the four plates are close but
 *  not identical: 6 of the 62 shipped marks land on opposite sides of the
 *  threshold depending on which one they sit on. `--measure chips` in the
 *  desktop harness prints the census. Four solves at decode is ~9,200 integer
 *  ops on the core-0 fetch task, after an HTTP round trip — free. */
LogoChip logoChip(const char* league, const char* abbr, uint8_t surf);
/** True once we have an answer, hit or 404. Stops us asking forever. */
bool logoKnown(const char* league, const char* abbr);
bool logoRequest(const char* league, const char* abbr);
/** Fetch at most one missing on-screen logo. Loop context only. */
void logoTick();

// ── headshots ──────────────────────────────────────────────────────────────
// One slot: only ever one player sheet is open. 68x68 RGB565A8 = 13.9 KB in
// PSRAM, never written to flash — see the note at the top of logos.cpp.
bool headshotRequest(const char* league, const char* athleteId);
const lv_img_dsc_t* headshotGet(const char* athleteId);
extern volatile bool g_headshotArrived;

/** RAM cache effectiveness, for the diagnostics page. */
uint16_t logoCacheHits();
uint16_t logoCacheMisses();
