// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// state.h — the threading contract, carried from AirRadar unchanged.
//
//   core 1  loop()  ──▶ LVGL · touch · g_board · NVS
//                        ▲  pending buffers, guarded by g_dataMux
//   core 0  tasks   ──▶ state poll · standings · logos
//
// loop() owns ALL LVGL, ALL of g_board, and ALL NVS access. No exceptions.
// A task never reads g_set (String/double torn-read risk) — everything it needs
// is snapshotted into a job struct in loop context before the spawn.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../config.h"
#include "types.h"

struct Settings {
  String  ssid, pass;
  String  proxy;       // "http://192.168.1.50:8787" — empty until onboarded
  String  token;
  String  region;      // "ca"
  String  tz;          // POSIX TZ, e.g. "EST5EDT,M3.2.0,M11.1.0"
  String  favs;        // "nhl:21,eng.1:359"
  String  leagues;
  String  panelPass;
  uint8_t density;
  bool    alertsOn;
  bool    quietOn;
  uint16_t quietFrom, quietTo;
  uint32_t lastSeq;
};

extern Settings g_set;

// ── board, owned by loop() ──────────────────────────────────────────────────
extern Game        g_board[MAX_GAMES];
extern uint8_t     g_gameCount;
extern LeagueCount g_leagues[MAX_LEAGUES];
extern uint8_t     g_leagueCount;
extern int8_t      g_leagueFilter;   // -1 = all
extern uint8_t     g_page;

// ── pending buffers, written by tasks under g_dataMux ───────────────────────
extern SemaphoreHandle_t g_dataMux;

extern volatile bool g_pendReady;      // set LAST, always
extern Game          g_pendBoard[MAX_GAMES];
extern uint8_t       g_pendCount;
extern LeagueCount   g_pendLeagues[MAX_LEAGUES];
extern uint8_t       g_pendLeagueCount;
extern AlertEvent    g_pendEvents[MAX_EVENTS];
extern uint8_t       g_pendEventCount;
extern uint32_t      g_pendSeq;
extern uint16_t      g_pendNextPoll;
extern bool          g_pendStale;

extern volatile bool g_pollInFlight;   // one of its kind at a time

// Game detail, written by its own task, read in loop context.
extern GameDetail    g_pendGame;
extern volatile bool g_pendGameReady;
extern volatile bool g_gameInFlight;

// ── status, for the top bar ─────────────────────────────────────────────────
enum NetStatus : uint8_t { NET_BOOT = 0, NET_NOWIFI, NET_NOPROXY, NET_ERR, NET_OK, NET_STALE };
extern NetStatus g_net;
extern char      g_netDetail[48];

void stateInit();
void settingsLoad();
void settingsSave();

/**
 * TLS/heap gate. Check in LOOP context BEFORE spawning a network task —
 * spawning one only to discover the gate is shut burns the very RAM the gate
 * protects. Tests free size AND largest free block, because mbedTLS needs a
 * ~16.4 KB contiguous buffer. INHERITED_RULES.md §14.
 */
bool netGateOpen();

/** True when the clock is inside the user's quiet window. */
bool inQuietHours();
