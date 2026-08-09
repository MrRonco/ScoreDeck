// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#include "state.h"
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <time.h>

Settings g_set;

Game        g_board[MAX_GAMES];
uint8_t     g_gameCount = 0;
LeagueCount g_leagues[MAX_LEAGUES];
uint8_t     g_leagueCount = 0;
int8_t      g_leagueFilter = -1;
FieldSet    g_fields[FLD_POOL];
uint8_t     g_fieldCount = 0;
uint8_t     g_page = 0;

SemaphoreHandle_t g_dataMux = nullptr;

volatile bool g_pendReady = false;
Game          g_pendBoard[MAX_GAMES];
uint8_t       g_pendCount = 0;
LeagueCount   g_pendLeagues[MAX_LEAGUES];
uint8_t       g_pendLeagueCount = 0;
AlertEvent    g_pendEvents[MAX_EVENTS];
uint8_t       g_pendEventCount = 0;
uint32_t      g_pendSeq = 0;
uint16_t      g_pendNextPoll = POLL_DEFAULT_S;
bool          g_pendStale = false;

volatile bool g_pollInFlight = false;

Lineup        g_lineup;
volatile bool g_lineupReady = false;
volatile bool g_lineupInFlight = false;

PlayerCard    g_player;
volatile bool g_playerReady = false;
volatile bool g_playerInFlight = false;

NewsFeed      g_news;
volatile bool g_newsReady = false;
volatile bool g_newsInFlight = false;

Standings     g_standings;
volatile bool g_standingsReady = false;
volatile bool g_standingsInFlight = false;

NetStatus g_net = NET_BOOT;
char      g_netDetail[48] = "";

void stateInit() {
  g_dataMux = xSemaphoreCreateMutex();
  settingsLoad();
}

/**
 * Every ScoreDeck owner is, right now, an AirRadar owner reflashing the same
 * board. Their Wi-Fi is already in NVS under AirRadar's namespace, so asking
 * them to retype it on a touchscreen keyboard would be gratuitous. Read-only,
 * and only when we have nothing of our own.
 */
static void importFromAirRadar() {
  Preferences ar;
  if (!ar.begin("radar", true)) return;
  const String ssid = ar.getString("ssid", "");
  const String pass = ar.getString("pass", "");
  const String tz   = ar.getString("tz", "");
  ar.end();
  if (ssid.isEmpty()) return;
  g_set.ssid = ssid;
  g_set.pass = pass;
  if (tz.length() && g_set.tz == "UTC0") g_set.tz = tz;
  settingsSave();
  Serial.printf("[sd] imported Wi-Fi '%s' from AirRadar\n", ssid.c_str());
}

void settingsLoad() {
  Preferences p;
  p.begin(NVS_NS, true);
  g_set.ssid      = p.getString(K_SSID, "");
  g_set.pass      = p.getString(K_PASS, "");
  g_set.proxy     = p.getString(K_PROXY, "");
  g_set.token     = p.getString(K_TOKEN, "");
  g_set.region    = p.getString(K_REGION, "us");
  g_set.tz        = p.getString(K_TZ, "UTC0");
  g_set.favs      = p.getString(K_FAVS, "");
  g_set.leagues   = p.getString(K_LEAGUES, "");
  g_set.panelPass = p.getString(K_PPASS, "");
  g_set.density   = p.getUChar(K_DENSITY, DEN_STANDARD);
  g_set.alertsOn  = p.getBool(K_ALERT_EN, true);
  g_set.quietOn   = p.getBool(K_QUIET_EN, false);
  g_set.quietFrom = p.getUShort(K_QUIET_FR, QUIET_DEFAULT_FROM);
  g_set.quietTo   = p.getUShort(K_QUIET_TO, QUIET_DEFAULT_TO);
  g_set.lastSeq   = p.getULong(K_SEQ, 0);
  p.end();
  if (g_set.density > DEN_DENSE) g_set.density = DEN_STANDARD;

  if (g_set.ssid.isEmpty()) importFromAirRadar();

#if __has_include("../dev_defaults.h")
  // Local development only. The header is gitignored and absent from releases.
#include "../dev_defaults.h"
#ifdef DEV_PROXY_URL
  if (g_set.proxy.isEmpty()) { g_set.proxy = DEV_PROXY_URL; settingsSave(); }
#endif
#endif
}

void settingsSave() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.putString(K_SSID, g_set.ssid);
  // A blank secret field must never overwrite a stored secret — the field
  // renders blank by design so it is not served back. INHERITED_RULES.md §20.
  if (g_set.pass.length())      p.putString(K_PASS, g_set.pass);
  if (g_set.token.length())     p.putString(K_TOKEN, g_set.token);
  if (g_set.panelPass.length()) p.putString(K_PPASS, g_set.panelPass);
  p.putString(K_PROXY, g_set.proxy);
  p.putString(K_REGION, g_set.region);
  p.putString(K_TZ, g_set.tz);
  p.putString(K_FAVS, g_set.favs);
  p.putString(K_LEAGUES, g_set.leagues);
  p.putUChar(K_DENSITY, g_set.density);
  p.putBool(K_ALERT_EN, g_set.alertsOn);
  p.putBool(K_QUIET_EN, g_set.quietOn);
  p.putUShort(K_QUIET_FR, g_set.quietFrom);
  p.putUShort(K_QUIET_TO, g_set.quietTo);
  p.putULong(K_SEQ, g_set.lastSeq);
  p.end();
}

/**
 * Favourites are stored as "<league>:<teamId>", but standings rows arrive
 * keyed by abbreviation. Resolve through the board, which carries both.
 */
bool boardFollows(const char* league, const char* abbr) {
  for (uint8_t i = 0; i < g_gameCount; i++) {
    const Game& g = g_board[i];
    if (!g.isFav || strcmp(g.league, league) != 0) continue;
    if (strcmp(g.away.abbr, abbr) == 0 || strcmp(g.home.abbr, abbr) == 0) return true;
  }
  return false;
}

bool netGateOpen() {
  const size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  return freeInt > TLS_HEAP_FLOOR && largest > TLS_LARGEST_FLOOR;
}

bool inQuietHours() {
  if (!g_set.quietOn) return false;
  time_t now = time(nullptr);
  if (now < 100000) return false;          // clock not set yet
  struct tm lt;
  localtime_r(&now, &lt);
  const uint16_t mins = lt.tm_hour * 60 + lt.tm_min;
  const uint16_t from = g_set.quietFrom, to = g_set.quietTo;
  return (from <= to) ? (mins >= from && mins < to)
                      : (mins >= from || mins < to);   // window crosses midnight
}
