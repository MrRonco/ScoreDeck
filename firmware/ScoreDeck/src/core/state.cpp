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
Story         g_story;
volatile bool g_storyInFlight = false;
volatile bool g_storyReady    = false;
bool          g_newsUnread = false;
volatile bool g_newsReady = false;
volatile bool g_newsInFlight = false;

Standings     g_standings;

CatEntry      g_catalog[CAT_MAX];
uint8_t       g_catalogCount = 0;
volatile bool g_catalogReady = false;
volatile bool g_catalogInFlight = false;
bool          g_catalogLoaded = false;
volatile bool g_standingsReady = false;
volatile bool g_standingsInFlight = false;

NetStatus g_net = NET_BOOT;
uint32_t  g_lastGoodUtc = 0;
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
  g_set.tzIana    = p.getString(K_TZ_IANA, "");
  g_set.favs      = p.getString(K_FAVS, "");
  g_set.leagues   = p.getString(K_LEAGUES, "");
  g_set.panelPass = p.getString(K_PPASS, "");
  g_set.density   = p.getUChar(K_DENSITY, DEN_AUTO);
  g_set.alertsOn  = p.getBool(K_ALERT_EN, true);
  g_set.focusOn   = p.getBool(K_FOCUS_EN, true);
  // 24-hour is the default outside North America; region is the best guess
  // available at first boot, and the user can override it either way.
  g_set.clock24   = p.getBool(K_CLK24, !(g_set.region == "us" || g_set.region == "ca"));
  g_set.quietOn   = p.getBool(K_QUIET_EN, false);
  g_set.tlsInsecure = p.getBool(K_TLS_INSEC, false);
  g_set.quietFrom = p.getUShort(K_QUIET_FR, QUIET_DEFAULT_FROM);
  g_set.quietTo   = p.getUShort(K_QUIET_TO, QUIET_DEFAULT_TO);
  g_set.lastSeq   = p.getULong(K_SEQ, 0);
  p.end();
  // Guards corrupt NVS. Must admit DEN_AUTO — it is a real stored value, not
  // an out-of-range one, and this clamp silently rejected it.
  if (g_set.density >= DEN_COUNT) g_set.density = DEN_AUTO;

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
  p.putString(K_TZ_IANA, g_set.tzIana);
  p.putString(K_FAVS, g_set.favs);
  p.putString(K_LEAGUES, g_set.leagues);
  p.putUChar(K_DENSITY, g_set.density);
  p.putBool(K_ALERT_EN, g_set.alertsOn);
  p.putBool(K_FOCUS_EN, g_set.focusOn);
  p.putBool(K_CLK24, g_set.clock24);
  p.putBool(K_QUIET_EN, g_set.quietOn);
  p.putBool(K_TLS_INSEC, g_set.tlsInsecure);
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

/**
 * The only place that turns packed situation bits into words.
 *
 * Kept deliberately terse. This chip sits in ~120 px beside the clock, and its
 * whole job is to be recognised, not read — "BASES LOADED" and "RED ZONE" are
 * shapes you know before you have finished reading them.
 */
/**
 * The situation chip, in one of two vocabularies.
 *
 * `compact` exists because the strings below do not fit a Dense tile. Dense is
 * 186 px wide, so after the pad and the live dot the status and the situation
 * share about 148 px — sixteen glyphs of a 15 px mono face. "Bot 8th" plus
 * "2 ON 2 OUT" is seventeen before any gap at all, and on the real panel they
 * printed straight through each other: "Bot 8tl2 ON 2 OUT".
 *
 * The harness never caught it because its fixtures use "Bot 7" and the
 * proxy sends "Bot 8th".
 */
void situationText(const Game& g, char* out, size_t cap, bool compact) {
  out[0] = '\0';
  if (!g.situation || g.state != GS_LIVE) return;
  const uint16_t s = g.situation;

  if (g.model == SM_INNING) {
    const uint8_t on = (uint8_t)sitOnFirst(s) + (uint8_t)sitOnSecond(s) + (uint8_t)sitOnThird(s);
    const uint8_t outs = sitOuts(s);
    // Compact tops out at FIVE glyphs — what a Dense tile can spare after a
    // nine-glyph status and a gutter.
    //
    // It reports the OUTS, not the runners, and that is a deliberate loss. The
    // first attempt packed both as "%uON%uO", which fits and is unreadable:
    // the mono face draws a capital O and a zero almost identically, so
    // "2ON2O" renders as "20N20". Outs is the number a glancing viewer wants
    // most, and bases-loaded is kept as its own word because it is the one
    // runner state worth interrupting for.
    if (compact) {
      if (on == 3)   snprintf(out, cap, "LOAD");
      else if (outs) snprintf(out, cap, "%u OUT", outs);
      return;
    }
    // Full form draws the BASES as glyphs — U+25C6 filled / U+25C7 open, in
    // 1st-2nd-3rd order. Nine visual columns at F_NUM's 9 px advance, so it
    // fits every slot the full vocabulary already fits. The glyphs live in
    // font_num15 (pulled from Noto Sans Symbols 2 at build time); putting
    // them in any other face renders hollow boxes — `make lint` guards this.
    if (on) {
      snprintf(out, cap, "%s%s%s %u OUT",
               sitOnFirst(s)  ? "\u25C6" : "\u25C7",
               sitOnSecond(s) ? "\u25C6" : "\u25C7",
               sitOnThird(s)  ? "\u25C6" : "\u25C7", outs);
    }
    else if (outs)   snprintf(out, cap, "%u OUT", outs);
    return;
  }

  if (g.model == SM_CLOCK) {
    // Bit 2 is set only for sports where a man advantage is a real state.
    if (s & 0x04)           snprintf(out, cap, compact ? "PP" : "POWER PLAY");
    else if (sitRedZone(s)) snprintf(out, cap, compact ? "RZ" : "RED ZONE");
  }
}

/**
 * Which league the TBL button should open.
 *
 * It used to be `g_leagues[0]`, which is proxy order — so on a board carrying
 * golf it opened the PGA leaderboard every single time, from every screen,
 * regardless of what the user follows or what is actually live.
 *
 * A field event has no standings TABLE. Golf and F1 arrive as LEADERBOARD and
 * GRID models, and a leaderboard is a live scoreboard rather than a season
 * table; putting one behind a button labelled TBL is answering a different
 * question from the one asked. So field-only leagues are skipped entirely.
 *
 * Order of preference: the league you have filtered to, then a league you
 * follow a team in, then whichever has the most live games.
 */
const char* standingsLeague() {
  auto twoSided = [](const char* slug) {
    for (uint8_t i = 0; i < g_gameCount; i++) {
      const Game& g = g_board[i];
      if (strcmp(g.league, slug) != 0) continue;
      if (g.model != SM_LEADERBOARD && g.model != SM_GRID) return true;
    }
    return false;
  };

  if (g_leagueFilter >= 0 && g_leagueFilter < g_leagueCount &&
      twoSided(g_leagues[g_leagueFilter].slug))
    return g_leagues[g_leagueFilter].slug;

  // A league you follow a team in beats a busier one you do not.
  for (uint8_t i = 0; i < g_gameCount; i++) {
    const Game& g = g_board[i];
    if (!g.isFav) continue;
    if (g.model == SM_LEADERBOARD || g.model == SM_GRID) continue;
    return g.league;
  }

  const char* best = nullptr;
  uint8_t bestLive = 0;
  for (uint8_t i = 0; i < g_leagueCount; i++) {
    const LeagueCount& lc = g_leagues[i];
    if (!twoSided(lc.slug)) continue;
    if (!best || lc.live > bestLive) { best = lc.slug; bestLive = lc.live; }
  }
  return best ? best : "nhl";
}


/**
 * Is this specific SIDE one of the user's teams?
 *
 * boardFollows() answers a different question — "does this league have a
 * followed game involving that abbreviation" — which is enough for a standings
 * table but wrong for a tile: it would ring both sides of a followed game,
 * including the opponent. Favourites are stored as `league:teamId`, and Side
 * carries the id, so this can be exact.
 */
bool sideIsFav(const char* league, const char* teamId) {
  if (!teamId || !*teamId || !g_set.favs.length()) return false;
  char key[24];
  const int n = snprintf(key, sizeof key, "%s:%s", league, teamId);
  if (n <= 0 || n >= (int)sizeof key) return false;

  const char* hay = g_set.favs.c_str();
  const size_t klen = strlen(key);
  for (const char* p = hay; (p = strstr(p, key)) != nullptr; p += klen) {
    const bool startOk = (p == hay) || p[-1] == ',';
    const char after = p[klen];
    if (startOk && (after == '\0' || after == ',')) return true;   // whole entry
  }
  return false;
}

const char* lastGoodClock() {
  static char buf[12];
  buf[0] = '\0';
  if (!g_lastGoodUtc) return buf;
  const time_t t = (time_t)g_lastGoodUtc;
  struct tm lt;
  localtime_r(&t, &lt);
  clockFormat(lt, buf, sizeof buf);
  return buf;
}

// ── the favourites list ────────────────────────────────────────────────────
//
// A comma-separated String rather than a parsed array, because that is what
// NVS holds and what the proxy expects on the wire — parsing it into a struct
// would mean two representations that can disagree. The list is short (20 max)
// and edited by hand at human speed, so scanning it is free.

/** Bounds of entry `i` within g_set.favs, or false if there is no such entry. */
static bool favSpan(uint8_t i, int& from, int& to) {
  const String& f = g_set.favs;
  if (!f.length()) return false;
  int start = 0;
  for (uint8_t n = 0;; n++) {
    int comma = f.indexOf(',', start);
    if (comma < 0) comma = f.length();
    if (n == i) { from = start; to = comma; return from < to; }
    if (comma >= (int)f.length()) return false;
    start = comma + 1;
  }
}

uint8_t favCount() {
  const String& f = g_set.favs;
  if (!f.length()) return 0;
  uint8_t n = 1;
  for (int i = 0; i < (int)f.length(); i++) if (f[i] == ',') n++;
  return n;
}

bool favAt(uint8_t i, char* league, size_t lcap, char* id, size_t icap) {
  int from, to;
  if (!favSpan(i, from, to)) return false;
  const String entry = g_set.favs.substring(from, to);
  const int colon = entry.indexOf(':');
  if (colon <= 0) return false;
  snprintf(league, lcap, "%s", entry.substring(0, colon).c_str());
  snprintf(id, icap, "%s", entry.substring(colon + 1).c_str());
  return true;
}

bool favRemove(uint8_t i) {
  int from, to;
  if (!favSpan(i, from, to)) return false;
  String out = g_set.favs.substring(0, from) +
               g_set.favs.substring(to < (int)g_set.favs.length() ? to + 1 : to);
  if (out.endsWith(",")) out.remove(out.length() - 1);
  g_set.favs = out;
  return true;
}

bool favMoveUp(uint8_t i) {
  if (i == 0) return false;
  char la[10], ia[12], lb[10], ib[12];
  if (!favAt(i - 1, la, sizeof la, ia, sizeof ia)) return false;
  if (!favAt(i, lb, sizeof lb, ib, sizeof ib)) return false;
  // Rebuild rather than splice: two swaps on a String is where off-by-ones live.
  String out;
  const uint8_t n = favCount();
  for (uint8_t k = 0; k < n; k++) {
    char l[10], d[12];
    const uint8_t src = (k == i - 1) ? i : (k == i ? i - 1 : k);
    if (!favAt(src, l, sizeof l, d, sizeof d)) continue;
    if (out.length()) out += ',';
    out += l; out += ':'; out += d;
  }
  g_set.favs = out;
  return true;
}

void settingsFactoryReset() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.clear();
  p.end();
}

const char* localClockNow() {
  static char buf[12];
  buf[0] = '\0';
  const time_t now = time(nullptr);
  if (now < 100000) return buf;          // clock not set yet
  struct tm lt;
  localtime_r(&now, &lt);
  clockFormat(lt, buf, sizeof buf);
  return buf;
}

void clockFormat(const struct tm& lt, char* out, size_t cap) {
  if (g_set.clock24) {
    strftime(out, cap, "%H:%M", &lt);
    return;
  }
  char tmp[16];
  strftime(tmp, sizeof tmp, "%l:%M %p", &lt);          // %l pads with a space
  snprintf(out, cap, "%s", tmp[0] == ' ' ? tmp + 1 : tmp);
}

// ── timezones ──────────────────────────────────────────────────────────────
//
// One row, three representations, because three things need different ones:
// a person needs a city, this device's libc needs a POSIX rule, and the proxy
// needs an IANA name. Deriving any of them from another on-device would mean
// shipping a zoneinfo database; a curated table is a few hundred bytes.
//
// Adding a row is the whole cost of supporting a new region.
const TzEntry kTimeZones[] = {
  { "Los Angeles", "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0" },
  { "Denver",      "America/Denver",      "MST7MDT,M3.2.0,M11.1.0" },
  { "Phoenix",     "America/Phoenix",     "MST7" },
  { "Chicago",     "America/Chicago",     "CST6CDT,M3.2.0,M11.1.0" },
  { "Toronto",     "America/Toronto",     "EST5EDT,M3.2.0,M11.1.0" },
  { "New York",    "America/New_York",    "EST5EDT,M3.2.0,M11.1.0" },
  { "Halifax",     "America/Halifax",     "AST4ADT,M3.2.0,M11.1.0" },
  { "St John's",   "America/St_Johns",    "NST3:30NDT,M3.2.0,M11.1.0" },
  { "Mexico City", "America/Mexico_City", "CST6" },
  { "Sao Paulo",   "America/Sao_Paulo",   "BRT3" },
  { "UTC",         "UTC",                 "UTC0" },
  { "London",      "Europe/London",       "GMT0BST,M3.5.0/1,M10.5.0" },
  { "Dublin",      "Europe/Dublin",       "GMT0IST,M3.5.0/1,M10.5.0" },
  { "Paris",       "Europe/Paris",        "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "Berlin",      "Europe/Berlin",       "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "Madrid",      "Europe/Madrid",       "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "Rome",        "Europe/Rome",         "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "Mumbai",      "Asia/Kolkata",        "IST-5:30" },
  { "Dubai",       "Asia/Dubai",          "GST-4" },
  { "Tokyo",       "Asia/Tokyo",          "JST-9" },
  { "Sydney",      "Australia/Sydney",    "AEST-10AEDT,M10.1.0,M4.1.0/3" },
  { "Auckland",    "Pacific/Auckland",    "NZST-12NZDT,M9.5.0,M4.1.0/3" },
};
const uint8_t kTimeZoneCount = sizeof kTimeZones / sizeof kTimeZones[0];

bool tzApply(const char* iana) {
  for (uint8_t i = 0; i < kTimeZoneCount; i++) {
    if (strcmp(kTimeZones[i].iana, iana) != 0) continue;
    g_set.tzIana = iana;
    g_set.tz     = kTimeZones[i].posix;
    setenv("TZ", g_set.tz.c_str(), 1);
    tzset();
    return true;
  }
  return false;
}

int8_t tzIndexOf(const char* iana) {
  for (uint8_t i = 0; i < kTimeZoneCount; i++)
    if (strcmp(kTimeZones[i].iana, iana) == 0) return (int8_t)i;
  return -1;
}

const char* tzForProxy() {
  if (g_set.tzIana.length()) return g_set.tzIana.c_str();
  // An install that predates this field still holds only a POSIX rule. Recover
  // the IANA name from it rather than silently sending something the proxy will
  // reject — that rejection is what filled the board with tomorrow's fixtures.
  for (uint8_t i = 0; i < kTimeZoneCount; i++)
    if (g_set.tz == kTimeZones[i].posix) return kTimeZones[i].iana;
  return "UTC";
}
