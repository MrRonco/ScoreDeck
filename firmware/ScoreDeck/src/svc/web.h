// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once
#include <Arduino.h>

/** Start the setup portal and mDNS. Call once Wi-Fi is up. */
void webBegin();
/** Service pending requests. LOOP CONTEXT ONLY — handlers touch NVS and LVGL. */
void webLoop();
bool webUp();

/**
 * Everything the diagnostics page reports that only the sketch knows.
 *
 * INHERITED_RULES.md §19: a counter with no increment site must render as a
 * DASH, never as zero. Two declared-but-never-incremented counters cost
 * AirRadar three sessions of blaming the wrong subsystem. `known` says which
 * of these are real; anything unset is reported as unknown rather than 0.
 */
struct WebDiag {
  const char* resetReason;
  uint32_t pollAgeS;
  int      pollCode;
  uint16_t pollMs;
  uint16_t declGate, declFlight, declNoProxy;
  bool     stale;
  uint32_t proxySeq;
  uint16_t logoHit, logoMiss;
};

/** Filled by the sketch, which owns the poll loop and its counters. */
void webCollectDiag(WebDiag& out);
/** Seconds until the next scheduled poll. */
uint16_t webNextPollSecs();
/** Queue an immediate poll — a settings change should show up now. */
void webPollNow();
