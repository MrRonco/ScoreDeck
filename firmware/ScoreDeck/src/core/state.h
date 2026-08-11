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
  // TWO timezone strings, deliberately, because two consumers need different
  // formats and neither accepts the other's:
  //
  //   tz      POSIX, for this device's own libc clock — "EST5EDT,M3.2.0,..."
  //   tzIana  IANA, for the proxy's Intl API          — "America/Toronto"
  //
  // Sending the POSIX string to the proxy is not a formatting nit: Intl rejects
  // it, the proxy falls back to UTC, and at 22:00 EDT "today" becomes tomorrow
  // — so the board fills with tomorrow's fixtures and every kickoff time is
  // shown in UTC. One control sets both; see tzApply().
  String  tz;
  String  tzIana;
  String  favs;        // "nhl:21,eng.1:359"
  String  leagues;
  String  panelPass;
  uint8_t density;
  bool    alertsOn;
  bool    focusOn;
  bool    clock24;
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
extern FieldSet    g_fields[FLD_POOL];
extern uint8_t     g_fieldCount;
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

extern Lineup        g_lineup;
extern volatile bool g_lineupReady;
extern volatile bool g_lineupInFlight;

extern PlayerCard    g_player;
extern volatile bool g_playerReady;
extern volatile bool g_playerInFlight;

extern NewsFeed      g_news;
extern volatile bool g_newsReady;
extern volatile bool g_newsInFlight;

extern Standings     g_standings;
extern volatile bool g_standingsReady;
extern volatile bool g_standingsInFlight;

/** True when the followed list contains this league/abbr pair. */
bool boardFollows(const char* league, const char* abbr);

/** Exact per-side test: is THIS team one of the user's favourites?
 *  boardFollows() marks a whole game and would ring the opponent too. */
bool sideIsFav(const char* league, const char* teamId);

// ── the favourites list ────────────────────────────────────────────────────
// Stored as "nhl:21,eng.1:359". Order is meaningful: it breaks ties on the
// board, so reordering is a real edit and not a cosmetic one.
uint8_t favCount();
/** Split entry `i` into its league and team id. False when i is out of range. */
bool favAt(uint8_t i, char* league, size_t lcap, char* id, size_t icap);
bool favMoveUp(uint8_t i);
bool favRemove(uint8_t i);

/** A timezone as the three things that need it: a person, libc, and the proxy. */
struct TzEntry { const char* label; const char* iana; const char* posix; };
extern const TzEntry kTimeZones[];
extern const uint8_t kTimeZoneCount;

/** Set both timezone strings from an IANA name, using the curated table.
 *  Returns false if the name is not one we know a POSIX rule for. */
bool tzApply(const char* iana);
/** Index into kTimeZones, or -1. */
int8_t tzIndexOf(const char* iana);
/** The IANA name to send upstream — never a POSIX rule, which the proxy
 *  rejects and then silently substitutes UTC for. */
const char* tzForProxy();


/**
 * Render Game::situation as a short chip: "BASES LOADED", "2 ON 1 OUT",
 * "RED ZONE", "POWER PLAY". Writes "" when there is nothing worth saying.
 *
 * The packing is sport-dependent and lives in types.h; this is the only place
 * that turns it into words, so the board and the game screen cannot disagree.
 * Output is CAPS + digits so it is safe in any face.
 */
/** Power play, red zone, bases loaded. `compact` picks a shortened vocabulary
 *  for tiles too narrow to carry the status and the chip side by side — see
 *  the note on the definition. */
void situationText(const Game& g, char* out, size_t cap, bool compact = false);

// ── status, for the top bar ─────────────────────────────────────────────────
enum NetStatus : uint8_t { NET_BOOT = 0, NET_NOWIFI, NET_NOPROXY, NET_ERR, NET_OK, NET_STALE };
extern NetStatus g_net;
extern char      g_netDetail[48];

/** Wall clock of the last poll the proxy served from live upstream data.
 *  Zero until the first good one lands. */
extern uint32_t  g_lastGoodUtc;

/** g_lastGoodUtc as "6:52 PM" in local time, or "" if there is none.
 *  A stale board should say WHEN its data is from — "stale" is a state,
 *  a time is something you can act on. */
const char* lastGoodClock();

void stateInit();
void settingsLoad();
void settingsSave();
/** Erase every stored setting and leave the device as it shipped. */
void settingsFactoryReset();
/** The device's own idea of the local time, "9:14 PM", or "" with no clock.
 *  The portal shows it because a wrong TZ is the usual cause of quiet hours
 *  firing at the wrong moment, and this catches it in one glance. */
const char* localClockNow();

/** Format a local time the way the user has asked for it. ONE place, so the
 *  board, the idle screen, the portal and the settings screen cannot
 *  disagree about whether it is 7:00 PM or 19:00. */
void clockFormat(const struct tm& lt, char* out, size_t cap);

/**
 * TLS/heap gate. Check in LOOP context BEFORE spawning a network task —
 * spawning one only to discover the gate is shut burns the very RAM the gate
 * protects. Tests free size AND largest free block, because mbedTLS needs a
 * ~16.4 KB contiguous buffer. INHERITED_RULES.md §14.
 */
bool netGateOpen();

/** True when the clock is inside the user's quiet window. */
bool inQuietHours();
