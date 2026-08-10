// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once
#include <lvgl.h>

/** Set when a fetch lands, so the board repaints once rather than every tick. */
extern volatile bool g_logoArrived;

/** Cached logo for a team, or nullptr — draw the colour badge instead. */
const lv_img_dsc_t* logoGet(const char* league, const char* abbr);
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
