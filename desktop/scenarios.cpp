// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// scenarios.cpp — the reason this harness exists.
//
// Live data only ever shows you the ordinary case. These are the states that
// are rare, seasonal, or actively hard to provoke: a 48-game November
// Saturday, a 12-inning linescore, a blowout with three-digit scores, a proxy
// that has gone away. Each one has broken something at least once.
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include "scenarios.h"
#include "../firmware/ScoreDeck/src/core/state.h"
#include "../firmware/ScoreDeck/src/ui/ui.h"

/**
 * Real club names for the abbreviations these scenarios use.
 *
 * The harness used to set `name` to the abbreviation, which was harmless while
 * nothing rendered it — and stopped being harmless the moment the hero cell
 * did. "TOR" at 30 px proves nothing about "MAPLE LEAFS", and the widest real
 * name is what the layout has to survive. Anything not listed falls back to
 * the abbreviation, so adding a club here is optional, not required.
 */
static const char* clubName(const char* abbr) {
  struct { const char* a; const char* n; } kNames[] = {
    { "MTL", "Canadiens" },  { "TOR", "Maple Leafs" },  { "BUF", "Sabres" },
    { "KC",  "Chiefs" },     { "EDM", "Oilers" },       { "CGY", "Flames" },
    { "BOS", "Bruins" },     { "NYR", "Rangers" },      { "LAD", "Dodgers" },
    { "SF",  "Giants" },     { "DAL", "Cowboys" },      { "PHI", "Eagles" },
    { "VAN", "Canucks" },    { "SEA", "Kraken" },       { "NYY", "Yankees" },
    { "CIN", "Reds" },       { "WSH", "Nationals" },    { "VGK", "Golden Knights" },
    { "COL", "Avalanche" },  { "NJD", "Devils" },       { "PIT", "Penguins" },
  };
  for (auto& k : kNames) if (strcmp(k.a, abbr) == 0) return k.n;
  return abbr;
}

static void setSide(Side& s, const char* abbr, const char* name, const char* rec,
                    uint16_t score, uint32_t colour, const char* id) {
  memset(&s, 0, sizeof s);
  strncpy(s.abbr, abbr, sizeof s.abbr - 1);
  strncpy(s.name, name, sizeof s.name - 1);
  strncpy(s.rec, rec, sizeof s.rec - 1);
  strncpy(s.id, id, sizeof s.id - 1);
  s.score = score;
  s.color = colour;
}

static Game& push(const char* league, GameState st, const char* status,
                  const char* aAbbr, uint16_t aScore, uint32_t aCol,
                  const char* hAbbr, uint16_t hScore, uint32_t hCol,
                  const char* bcast = "", bool fav = false) {
  Game& g = g_board[g_gameCount];
  memset(&g, 0, sizeof g);
  snprintf(g.id, sizeof g.id, "90%04u", g_gameCount);
  strncpy(g.league, league, sizeof g.league - 1);
  strncpy(g.status, status, sizeof g.status - 1);
  strncpy(g.bcast, bcast, sizeof g.bcast - 1);
  g.model = SM_CLOCK;
  g.state = st;
  g.isFav = fav;
  g.startUtc = (uint32_t)time(nullptr) + 3600;
  g.winProbHome = 255;
  // Ids must be distinct per side or the favourite ring cannot tell them apart.
  char aid[8], hid[8];
  snprintf(aid, sizeof aid, "%u", 100u + g_gameCount * 2);
  snprintf(hid, sizeof hid, "%u", 101u + g_gameCount * 2);
  setSide(g.away, aAbbr, clubName(aAbbr), "12-4-2", aScore, aCol, aid);
  setSide(g.home, hAbbr, clubName(hAbbr), "21-6-4", hScore, hCol, hid);
  g.leaderHome = hScore > aScore;
  g_gameCount++;
  return g;
}

static void league(const char* slug, uint8_t live) {
  strncpy(g_leagues[g_leagueCount].slug, slug, sizeof g_leagues[0].slug - 1);
  g_leagues[g_leagueCount].live = live;
  g_leagueCount++;
}

static void reset() {
  g_gameCount = 0;
  g_leagueCount = 0;
  g_leagueFilter = -1;
  g_page = 0;
  g_net = NET_OK;
  g_netDetail[0] = '\0';
  memset(&g_lineup, 0, sizeof g_lineup);
  memset(&g_player, 0, sizeof g_player);
  memset(&g_news, 0, sizeof g_news);
  memset(&g_standings, 0, sizeof g_standings);
  g_set.favs = "";
}

// ── secondary-screen fills ───────────────────────────────────────────────────
// Every one of these screens clears its state inside Open() and then waits on a
// fetch that never lands here, so a --screen shot of standings, news, a game
// detail or a player card would otherwise only ever show "loading". These run
// afterwards, from scenarioReapply().

static void fillStandings() {
  Standings& t = g_standings;
  memset(&t, 0, sizeof t);
  t.colCount = 5;
  static const char* kCols[5] = { "GP", "W", "L", "OTL", "PTS" };
  for (int c = 0; c < 5; c++) strncpy(t.cols[c], kCols[c], sizeof t.cols[0] - 1);

  struct R { const char* abbr; const char* name; uint32_t col; int gp, w, l, otl; };
  static const R kRows[] = {
    { "FLA", "Panthers",     0xC8102E, 68, 44, 19,  5 },
    { "TOR", "Maple Leafs",  0x00205B, 67, 42, 20,  5 },
    { "TBL", "Lightning",    0x002868, 68, 40, 22,  6 },
    { "BOS", "Bruins",       0xFFB81C, 69, 38, 24,  7 },
    { "OTT", "Senators",     0xDA1A32, 68, 36, 26,  6 },
    { "MTL", "Canadiens",    0xAF1E2D, 68, 33, 28,  7 },
    { "DET", "Red Wings",    0xCE1126, 69, 32, 30,  7 },
    { "BUF", "Sabres",       0x003087, 68, 30, 32,  6 },
    { "NYR", "Rangers",      0x0038A8, 67, 29, 31,  7 },
    { "PHI", "Flyers",       0xF74902, 68, 27, 34,  7 },
    { "PIT", "Penguins",     0x000000, 68, 26, 35,  7 },
    { "NJD", "Devils",       0xCE1126, 68, 25, 36,  7 },
  };
  t.rowCount = (uint8_t)(sizeof kRows / sizeof kRows[0]);
  for (uint8_t i = 0; i < t.rowCount; i++) {
    StandingRow& r = t.rows[i];
    strncpy(r.abbr, kRows[i].abbr, sizeof r.abbr - 1);
    strncpy(r.name, kRows[i].name, sizeof r.name - 1);
    r.color = kRows[i].col;
    snprintf(r.cells[0], sizeof r.cells[0], "%d", kRows[i].gp);
    snprintf(r.cells[1], sizeof r.cells[1], "%d", kRows[i].w);
    snprintf(r.cells[2], sizeof r.cells[2], "%d", kRows[i].l);
    snprintf(r.cells[3], sizeof r.cells[3], "%d", kRows[i].otl);
    snprintf(r.cells[4], sizeof r.cells[4], "%d", kRows[i].w * 2 + kRows[i].otl);
  }
  t.cutCount = 2;
  t.cutAfter[0] = 2; strncpy(t.cutLabel[0], "PLAYOFF LINE",  sizeof t.cutLabel[0] - 1);
  t.cutAfter[1] = 7; strncpy(t.cutLabel[1], "WILD CARD CUT", sizeof t.cutLabel[1] - 1);
  t.loading = false;
}

static void fillNews() {
  NewsFeed& n = g_news;
  memset(&n, 0, sizeof n);
  const uint32_t now = (uint32_t)time(nullptr);
  struct I { const char* h; const char* d; const char* abbr; uint32_t col; uint32_t ago; };
  static const I kItems[] = {
    { "Matthews returns to practice, questionable for Saturday",
      "The captain skated in a regular sweater for the first time since the "
      "upper-body injury that has kept him out eleven games.", "TOR", 0x00205B, 900 },
    { "Canadiens recall Demidov from Laval on emergency basis",
      "Montreal is down to eleven forwards after Thursday's collision and has "
      "burned through its taxi squad.", "MTL", 0xAF1E2D, 5400 },
    { "Ødegaard's late free kick rescues a point at Anfield",
      "Arsenal had been second best for an hour before the captain curled one "
      "over the wall in the third minute of added time.", "ARS", 0xEF0107, 14400 },
    { "Bills sign veteran tackle ahead of divisional round",
      "A one-year deal, fully guaranteed, filling the hole left by last week's "
      "ankle injury.", "BUF", 0x00338D, 43200 },
    { "Verstappen takes pole by four hundredths at Zandvoort",
      "A final sector that nobody else got close to, on a track where passing "
      "is close to theoretical.", "", 0x5D6D7E, 90000 },
    { "Judge homers twice as New York opens a four-game lead",
      "Two swings, both to the opposite field, and a division race that looked "
      "tight on Tuesday no longer does.", "NYY", 0x132448, 176000 },
  };
  n.count = (uint8_t)(sizeof kItems / sizeof kItems[0]);
  for (uint8_t i = 0; i < n.count; i++) {
    NewsItem& it = n.items[i];
    strncpy(it.headline, kItems[i].h, sizeof it.headline - 1);
    strncpy(it.desc,     kItems[i].d, sizeof it.desc - 1);
    strncpy(it.abbr,     kItems[i].abbr, sizeof it.abbr - 1);
    it.color = kItems[i].col;
    it.when  = now - kItems[i].ago;
  }
  n.loading = false;
}

static void fillDetail() {
  GameDetail d;
  memset(&d, 0, sizeof d);
  strncpy(d.id, g_gameCount ? g_board[0].id : "900000", sizeof d.id - 1);
  strncpy(d.status, "3rd 04:21", sizeof d.status - 1);
  strncpy(d.venue, "Scotiabank Arena", sizeof d.venue - 1);
  strncpy(d.awayAbbr, "MTL", sizeof d.awayAbbr - 1);
  strncpy(d.homeAbbr, "TOR", sizeof d.homeAbbr - 1);
  d.awayScore = 2; d.homeScore = 3;
  d.awayColor = 0xAF1E2D; d.homeColor = 0x00205B;
  d.live = true;
  d.winProbHome = 71;

  d.lsCount = 4;
  static const char* kCols[4] = { "1", "2", "3", "T" };
  static const char* kA[4]    = { "1", "1", "0", "2" };
  static const char* kH[4]    = { "0", "2", "1", "3" };
  for (int i = 0; i < 4; i++) {
    strncpy(d.lsCols[i], kCols[i], sizeof d.lsCols[0] - 1);
    strncpy(d.lsA[i],    kA[i],    sizeof d.lsA[0] - 1);
    strncpy(d.lsH[i],    kH[i],    sizeof d.lsH[0] - 1);
  }

  d.playCount = 5;
  static const char* kT[5] = { "3rd 04:21", "3rd 07:58", "2nd 12:30", "2nd 15:02", "1st 03:44" };
  static const char* kX[5] = {
    "Auston Matthews scores on a wrist shot from the slot, assisted by Nylander",
    "Nick Suzuki penalty for tripping — 2 minutes",
    "Cole Caufield scores short-handed, unassisted",
    "William Nylander scores on the power play, assisted by Rielly and Marner",
    "Juraj Slafkovsky scores on a tip-in, assisted by Hutson",
  };
  static const char* kS[5] = { "2-3", "2-2", "2-2", "1-2", "1-1" };
  static const bool  kHm[5] = { true, false, false, true, false };
  for (int i = 0; i < 5; i++) {
    strncpy(d.playT[i], kT[i], sizeof d.playT[0] - 1);
    strncpy(d.playX[i], kX[i], sizeof d.playX[0] - 1);
    strncpy(d.playS[i], kS[i], sizeof d.playS[0] - 1);
    d.playHome[i] = kHm[i];
  }

  d.statCount = 5;
  static const char* kK[5] = { "SOG", "PP", "FO%", "HITS", "PIM" };
  static const char* kSA[5] = { "24", "1/3", "47.2", "18", "6" };
  static const char* kSH[5] = { "31", "2/4", "52.8", "22", "4" };
  for (int i = 0; i < 5; i++) {
    strncpy(d.statK[i], kK[i],  sizeof d.statK[0] - 1);
    strncpy(d.statA[i], kSA[i], sizeof d.statA[0] - 1);
    strncpy(d.statH[i], kSH[i], sizeof d.statH[0] - 1);
  }
  uiGameApply(d);
}

static void fillPlayer() {
  PlayerCard& p = g_player;
  memset(&p, 0, sizeof p);
  strncpy(p.id,     "3024", sizeof p.id - 1);
  strncpy(p.name,   "Auston Matthews", sizeof p.name - 1);
  strncpy(p.pos,    "Center", sizeof p.pos - 1);
  strncpy(p.team,   "TOR", sizeof p.team - 1);
  strncpy(p.jersey, "34", sizeof p.jersey - 1);
  strncpy(p.height, "6' 3\"", sizeof p.height - 1);
  strncpy(p.weight, "208 lbs", sizeof p.weight - 1);
  p.color = 0x00205B;
  p.age = 28;
  p.statCount = 5;
  static const char* kK[5] = { "G",  "A",  "P",  "+/-", "TOI" };
  static const char* kV[5] = { "41", "33", "74", "+18", "21:14" };
  static const char* kR[5] = { "2nd NHL", "31st", "9th", "22nd", "6th on TOR" };
  for (int i = 0; i < 5; i++) {
    strncpy(p.statK[i], kK[i], sizeof p.statK[0] - 1);
    strncpy(p.statV[i], kV[i], sizeof p.statV[0] - 1);
    strncpy(p.statR[i], kR[i], sizeof p.statR[0] - 1);
  }
  p.loading = false;
}

static void fillLineup() {
  Lineup& L = g_lineup;
  memset(&L, 0, sizeof L);
  L.sideCount = 2;
  struct P { const char* name; const char* pos; const char* jersey; const char* g; const char* a; };
  static const P kAway[] = {
    { "Sam Montembeault", "G",  "35", "0", "0" }, { "Lane Hutson",   "D", "48", "0", "2" },
    { "Kaiden Guhle",     "D",  "21", "0", "0" }, { "Mike Matheson", "D", "8",  "1", "0" },
    { "Nick Suzuki",      "C",  "14", "1", "1" }, { "Cole Caufield", "RW","13", "1", "0" },
    { "Juraj Slafkovsky", "LW", "20", "0", "1" }, { "Alex Newhook",  "C", "15", "0", "0" },
  };
  static const P kHome[] = {
    { "Joseph Woll",       "G",  "60", "0", "0" }, { "Morgan Rielly",  "D", "44", "0", "1" },
    { "Chris Tanev",       "D",  "8",  "0", "0" }, { "Jake McCabe",    "D", "22", "0", "0" },
    { "Auston Matthews",   "C",  "34", "1", "1" }, { "William Nylander","RW","88", "1", "0" },
    { "Matthew Knies",     "LW", "23", "0", "0" }, { "John Tavares",   "C", "91", "0", "1" },
  };
  const char* kAbbr[2] = { "MTL", "TOR" };
  const uint32_t kCol[2] = { 0xAF1E2D, 0x00205B };
  for (uint8_t s = 0; s < 2; s++) {
    LineSide& S = L.sides[s];
    strncpy(S.abbr, kAbbr[s], sizeof S.abbr - 1);
    S.color = kCol[s];
    S.groupCount = 1;
    LineGroup& G = S.groups[0];
    strncpy(G.name, "SKATERS", sizeof G.name - 1);
    G.colCount = 2;
    strncpy(G.cols[0], "G", sizeof G.cols[0] - 1);
    strncpy(G.cols[1], "A", sizeof G.cols[1] - 1);
    const P* src = s ? kHome : kAway;
    G.count = 8;
    for (uint8_t i = 0; i < 8; i++) {
      LinePlayer& pl = G.players[i];
      memset(&pl, 0, sizeof pl);
      snprintf(pl.id, sizeof pl.id, "%u", 3000u + s * 100 + i);
      strncpy(pl.name,   src[i].name,   sizeof pl.name - 1);
      strncpy(pl.pos,    src[i].pos,    sizeof pl.pos - 1);
      strncpy(pl.jersey, src[i].jersey, sizeof pl.jersey - 1);
      strncpy(pl.vals[0], src[i].g, sizeof pl.vals[0] - 1);
      strncpy(pl.vals[1], src[i].a, sizeof pl.vals[0] - 1);
      pl.starter = i < 6;
    }
  }
  L.loading = false;
}

static void fireAlert(const char* gameId, const char* abbr, uint32_t colour,
                      const char* verb, const char* who, uint16_t a, uint16_t h) {
  AlertEvent e;
  memset(&e, 0, sizeof e);
  e.seq = 1;
  strncpy(e.gameId, gameId, sizeof e.gameId - 1);
  strncpy(e.verb, verb, sizeof e.verb - 1);
  strncpy(e.abbr, abbr, sizeof e.abbr - 1);
  e.color = colour;
  strncpy(e.who, who, sizeof e.who - 1);
  strncpy(e.detail, "Nylander, Rielly", sizeof e.detail - 1);
  strncpy(e.status, "3rd 04:21", sizeof e.status - 1);
  e.scoreAway = a;
  e.scoreHome = h;
  uiAlertEnqueue(e);
  for (int i = 0; i < 24 && !uiAlertActive(); i++) { uiAlertTick(); usleep(20000); }
  for (int i = 0; i < 24; i++) { uiAlertTick(); usleep(20000); }
}

/** A followed team scores — the full takeover. */
void scenarioFireAlert() {
  fireAlert(g_gameCount ? g_board[0].id : "900000", "TOR", 0x00205B,
            "GOAL", "Auston Matthews (41)", 2, 3);
}

/** Someone else scores — the banner, with the board still visible. */
void scenarioFireBanner() {
  fireAlert(g_gameCount > 1 ? g_board[1].id : "900001", "KC", 0xE31837,
            "TOUCHDOWN", "Xavier Worthy (7)", 14, 21);
}


/** Refill the screens whose Open() clears state before fetching. */
void scenarioReapply(int n) {
  if (n == SCN_ACCENTS) {
    g_lineup.loading = false;
    g_lineup.sideCount = 1;
    uiLineupApply();
    return;
  }
  fillStandings();
  fillNews();
  fillPlayer();
  fillLineup();
  if (uiStandingsIsOpen()) uiStandingsRender();
  if (uiNewsIsOpen())      uiNewsRender();
  if (uiLineupIsOpen())    uiLineupApply();
  if (uiPlayerIsOpen())    uiPlayerRender();
  if (uiGameIsOpen())      fillDetail();
}

void scenarioApply(int n) {
  reset();
  switch (n) {
    case SCN_TYPICAL: {
      league("nhl", 3); league("nfl", 1); league("mlb", 2);
      // Game 0 home side (TOR) is the followed team: id 101 by push()'s scheme.
      g_set.favs = "nhl:101";
      push("nhl", GS_LIVE,  "3rd 04:21", "MTL", 2, 0xAF1E2D, "TOR", 3, 0x00205B, "SN", true)
        .situation = 0x04;                       // power play
      push("nfl", GS_LIVE,  "Q2 11:03",  "BUF", 14, 0x00338D, "KC", 21, 0xE31837, "CBS")
        .situation = 0x02;                       // red zone
      push("nhl", GS_LIVE,  "1st 18:44", "EDM", 1, 0xFF4C00, "CGY", 0, 0xC8102E, "SN");
      Game& mlb = push("mlb", GS_LIVE, "Bot 8th", "CIN", 1, 0xC6011F, "WSH", 3, 0xAB0003, "MLB.TV");
      mlb.model = SM_INNING;
      mlb.situation = 0x01 | 0x04 | (1 << 3);    // 1st and 3rd, one out
      push("nhl", GS_PRE,   "7:00 PM",   "BOS", 0, 0xFFB81C, "NYR", 0, 0x0038A8, "ESPN");
      push("mlb", GS_PRE,   "8:10 PM",   "LAD", 0, 0x005A9C, "SF", 0, 0xFD5A1E, "MLB.TV");
      push("nfl", GS_FINAL, "Final",     "DAL", 17, 0x041E42, "PHI", 27, 0x004C54);
      push("nhl", GS_FINAL, "Final/OT",  "VAN", 4, 0x00205B, "SEA", 5, 0x99D9D9);
      push("mlb", GS_FINAL, "Final",     "NYY", 6, 0x132448, "BOS", 2, 0xBD3039);
      break;
    }

    case SCN_FEATURE: {
      // Three live games — the shape AUTO resolves to the FEATURE layout, and
      // the single most common real evening. This is the case the nine-up grid
      // served worst: three tiles of content and six of "-", which is how the
      // board came to be 84.4% cards and 16.4% ground.
      //
      // Toronto is followed and one goal ahead, so it takes the hero, and the
      // ledger carries the four games that are not worth a tile.
      league("nhl", 2); league("nfl", 1); league("mlb", 0);
      g_set.favs = "nhl:101";
      push("nhl", GS_LIVE,  "3rd 04:21", "MTL", 2, 0xAF1E2D, "TOR", 3, 0x00205B, "Sportsnet", true)
        .situation = 0x04;                       // power play
      push("nfl", GS_LIVE,  "Q2 11:03",  "BUF", 14, 0x00338D, "KC", 21, 0xE31837, "CBS")
        .situation = 0x02;                       // red zone
      push("nhl", GS_LIVE,  "1st 18:44", "EDM", 1, 0xFF4C00, "CGY", 0, 0xC8102E, "SN");
      push("nhl", GS_PRE,   "7:00 PM",   "BOS", 0, 0xFFB81C, "NYR", 0, 0x0038A8, "ESPN");
      push("mlb", GS_PRE,   "8:10 PM",   "LAD", 0, 0x005A9C, "SF", 0, 0xFD5A1E, "MLB.TV");
      push("nhl", GS_PRE,   "9:30 PM",   "VGK", 0, 0xB4975A, "COL", 0, 0x6F263D, "TNT");
      push("nhl", GS_FINAL, "Final/OT",  "VAN", 4, 0x00205B, "SEA", 5, 0x99D9D9);
      push("mlb", GS_FINAL, "Final",     "NYY", 6, 0x132448, "BOS", 2, 0xBD3039);
      push("nfl", GS_FINAL, "Final",     "DAL", 17, 0x041E42, "PHI", 27, 0x004C54);
      break;
    }

    case SCN_EMPTY:
      // Nothing on the board at all — the idle screen's empty state, which is
      // what a Tuesday morning in July actually looks like.
      league("nhl", 0);
      break;

    case SCN_ALL_LIVE:
      // Nine live games: the state the board is designed for and the one that
      // exercises every edge light at once.
      league("nhl", 9);
      for (int i = 0; i < 9; i++)
        push("nhl", GS_LIVE, "2nd 12:00", "AAA", (uint16_t)i, 0x00205B,
             "BBB", (uint16_t)(8 - i), 0xAF1E2D, "SN", i == 0);
      break;

    case SCN_EXTREMES: {
      // Three-digit scores, the longest abbreviations ESPN emits, the longest
      // status strings, and a broadcast name that fills its field.
      league("ncaam", 2); league("nfl", 1);
      push("ncaam", GS_LIVE, "2nd 00:04.7", "UCONN", 118, 0x000E2F, "TENN", 109, 0xFF8200, "ESPN2");
      push("ncaam", GS_LIVE, "OT2 03:11",   "GONZ",  99,  0x002967, "SDSU",  98,  0xA6192E, "CBSSN");
      push("nfl",   GS_FINAL, "Final/OT",   "JAX",   3,   0x006778, "WSH",   0,   0x5A1414, "Prime Video");
      break;
    }

    case SCN_CROWDED:
      // A November Saturday. The proxy caps at 48; this proves the device's
      // own cap and the pager hold, and that nothing walks off the array.
      league("ncaaf", 30); league("ncaam", 18);
      for (int i = 0; i < MAX_GAMES; i++)
        push(i < 30 ? "ncaaf" : "ncaam",
             i % 3 == 0 ? GS_LIVE : (i % 3 == 1 ? GS_PRE : GS_FINAL),
             i % 3 == 0 ? "Q3 07:12" : (i % 3 == 1 ? "3:30 PM" : "Final"),
             "AAAA", (uint16_t)(i * 3 % 60), 0x224466,
             "BBBB", (uint16_t)(i * 7 % 60), 0x884422, "ESPN+", i == 0);
      break;

    case SCN_NO_PROXY:
      // No proxy configured: the first-run state, and the one most likely to
      // be mistaken for a crash if the status line does not say so.
      g_net = NET_NOPROXY;
      league("nhl", 0);
      break;

    case SCN_STALE:
      // Upstream failed and the proxy is serving last-known-good. The board
      // must say so rather than presenting stale scores as live.
      g_net = NET_STALE;
      snprintf(g_netDetail, sizeof g_netDetail, "upstream stale");
      league("nhl", 1);
      push("nhl", GS_LIVE, "2nd 09:31", "MTL", 1, 0xAF1E2D, "TOR", 1, 0x00205B, "SN", true);
      push("nhl", GS_FINAL, "Final",    "BOS", 3, 0xFFB81C, "NYR", 2, 0x0038A8);
      break;

    case SCN_ACCENTS: {
      // The font range test. These names are boxes in a 7-bit ASCII face, and
      // the lineup screen is made of exactly this.
      league("eng.1", 1);
      push("eng.1", GS_LIVE, "67'", "ARS", 2, 0xEF0107, "LIV", 1, 0xC8102E, "Sky Sports", true);
      g_lineup.sideCount = 1;
      g_lineup.loading = false;
      LineSide& S = g_lineup.sides[0];
      strncpy(S.abbr, "ARS", sizeof S.abbr - 1);
      S.color = 0xEF0107;
      strncpy(S.formation, "4-2-3-1", sizeof S.formation - 1);
      S.groupCount = 1;
      LineGroup& G = S.groups[0];
      strncpy(G.name, "SQUAD", sizeof G.name - 1);
      G.colCount = 0;
      static const char* kNames[] = {
        "Ødegaard", "Konaté", "Dončić", "Vlašić", "Şahin",
        "Håland", "Şükür", "Mbappé", "Özil", "Piqué",
      };
      for (auto& nm : kNames) {
        LinePlayer& P = G.players[G.count];
        memset(&P, 0, sizeof P);
        snprintf(P.id, sizeof P.id, "%u", 1000 + G.count);
        strncpy(P.name, nm, sizeof P.name - 1);
        strncpy(P.pos, "MF", sizeof P.pos - 1);
        P.starter = true;
        G.count++;
      }
      break;
    }
    case SCN_FIELD: {
      // Tennis, golf and F1 on one board. F1's field cannot be checked against
      // live data outside a race weekend, so this is the only way to see the
      // GRID tile at all before Sunday.
      league("atp", 2); league("pga", 1); league("f1", 1);

      Game& t1 = push("atp", GS_LIVE, "2nd set", "SHEL", 1, 0x5D6D7E, "FONS", 0, 0x5D6D7E, "TSN");
      t1.model = SM_SET;
      strncpy(t1.away.name, "B. Shelton", sizeof t1.away.name - 1);
      strncpy(t1.home.name, "J. Fonseca", sizeof t1.home.name - 1);
      t1.setCount = 2;
      t1.setsAway[0] = 6; t1.setsHome[0] = 4;
      t1.setsAway[1] = 3; t1.setsHome[1] = 5;

      Game& t2 = push("atp", GS_LIVE, "3rd set", "DJOK", 2, 0x5D6D7E, "ALCA", 1, 0x5D6D7E, "TSN");
      t2.model = SM_SET;
      strncpy(t2.away.name, "N. Djokovic", sizeof t2.away.name - 1);
      strncpy(t2.home.name, "C. Alcaraz", sizeof t2.home.name - 1);
      t2.setCount = 3;
      t2.setsAway[0] = 7; t2.setsHome[0] = 6;
      t2.setsAway[1] = 4; t2.setsHome[1] = 6;
      t2.setsAway[2] = 6; t2.setsHome[2] = 2;

      // A scheduled F1 session, whose status is the LONGEST the wire permits:
      // "8/21 - 6:30 AM" is fourteen glyphs against a nine-glyph reserve, and
      // it printed as "8/21 -..." until the status learned to take the whole
      // row when nothing is sharing it.
      Game& sched = push("f1", GS_PRE, "8/21 - 6:30 AM", "", 0, 0x5D6D7E, "", 0, 0x5D6D7E);
      sched.model = SM_GRID;
      strncpy(sched.away.name, "Heineken Dutch GP", sizeof sched.away.name - 1);

      Game& gf = push("pga", GS_LIVE, "R4 thru 16", "", 0, 0x5D6D7E, "", 0, 0x5D6D7E, "CBS");
      gf.model = SM_LEADERBOARD;
      // The full upstream name. "Wyndham Champ" fitted, which is why the
      // title wrapping onto the leaderboard rows was invisible here.
      strncpy(gf.away.name, "Wyndham Championship", sizeof gf.away.name - 1);
      gf.fieldIdx = 0;
      g_fields[0].count = 3;
      static const char* kGolf[3][3] = {
        { "1", "M. Brennan", "-22" }, { "2", "B. Hossler", "-19" }, { "T3", "B. James", "-18" },
      };
      for (int i = 0; i < 3; i++) {
        strncpy(g_fields[0].rows[i].pos,  kGolf[i][0], 4);
        strncpy(g_fields[0].rows[i].name, kGolf[i][1], 20);
        strncpy(g_fields[0].rows[i].val,  kGolf[i][2], 10);
      }

      Game& f1 = push("f1", GS_LIVE, "Lap 52/72", "", 0, 0x5D6D7E, "", 0, 0x5D6D7E, "SN");
      f1.model = SM_GRID;
      strncpy(f1.away.name, "Dutch GP", sizeof f1.away.name - 1);
      f1.fieldIdx = 1;
      g_fields[1].count = 3;
      static const char* kF1[3][3] = {
        { "1", "M. Verstappen", "LEADER" }, { "2", "L. Norris", "+4.281" }, { "3", "C. Leclerc", "+11.9" },
      };
      for (int i = 0; i < 3; i++) {
        strncpy(g_fields[1].rows[i].pos,  kF1[i][0], 4);
        strncpy(g_fields[1].rows[i].name, kF1[i][1], 20);
        strncpy(g_fields[1].rows[i].val,  kF1[i][2], 10);
      }

      // TILE REUSE. Draw the board once with tennis in slot 0, then turn that
      // match into a leaderboard and draw again — which is what a real board
      // does every time the ordering shifts.
      //
      // The tennis per-set boxes were hidden only on the two-sided path, and
      // the field path returns before reaching it, so they survived onto the
      // golf tile and printed through the players' names. Nothing static could
      // catch this: the tile has to have been something else FIRST.
      //
      // Returns early, like the density-jump case, so the trailing uiInit()
      // does not rebuild the tiles and hide the bug.
      uiBoardRefresh();
      g_board[0].model = SM_LEADERBOARD;
      g_board[0].fieldIdx = 0;
      strncpy(g_board[0].away.name, "Wyndham Championship", sizeof g_board[0].away.name - 1);
      uiBoardRefresh();
      uiShow(SCR_BOARD);
      return;
    }
    case SCN_DENSITY_JUMP: {
      // The panel booted with an empty board, so auto density built a SIX-tile
      // Roomy grid — and then twelve games arrived, density became Dense, and
      // the refresh walked into six tiles that had never been created. Null
      // root, LoadProhibited, panic on real hardware.
      //
      // Reproduced by building the UI empty and only then filling the board,
      // which is exactly the boot order.
      league("mlb", 12);
      // The REAL boot order, which is the whole point: the UI is built while
      // the board is still empty and the games only arrive on the first poll.
      // This returns early to skip the trailing uiInit() every other scenario
      // relies on — with it, the tiles are rebuilt for the final density and
      // the bug cannot happen.
      uiInit();
      // "Bot 8th", NOT "Bot 7". The proxy sends the ordinal, and the shorter
      // fixture is exactly why the harness never caught the status running
      // through the situation chip on a 186 px Dense tile. Every tile also
      // carries a situation, because that collision needs both labels present.
      for (int i = 0; i < 12; i++) {
        Game& g = push("mlb", GS_LIVE, i % 2 ? "Bot 8th" : "Top 9th",
                       "AAA", (uint16_t)i, 0xC6011F,
                       "BBB", (uint16_t)(11 - i), 0xAB0003, "MLB.TV");
        g.model = SM_INNING;
        g.situation = 0x01 | 0x04 | (2 << 3);      // 1st and 3rd, two out
      }
      uiBoardRefresh();
      uiIdleRefresh();
      uiShow(uiShouldIdle() ? SCR_IDLE : SCR_BOARD);
      return;
    }
  }
  uiInit();
  uiBoardRefresh();
  uiIdleRefresh();
  uiShow(uiShouldIdle() ? SCR_IDLE : SCR_BOARD);
}

const char* scenarioName(int n) {
  switch (n) {
    case SCN_TYPICAL:  return "typical mixed board";
    case SCN_FEATURE:  return "three live — the FEATURE layout";
    case SCN_EMPTY:    return "empty — idle screen";
    case SCN_ALL_LIVE: return "nine live games";
    case SCN_EXTREMES: return "long names, three-digit scores";
    case SCN_CROWDED:  return "48 games — cap and pager";
    case SCN_NO_PROXY: return "no proxy configured";
    case SCN_STALE:    return "stale upstream";
    case SCN_ACCENTS:  return "accented names — font range";
    case SCN_FIELD:    return "tennis, golf, F1 — SET/LEADERBOARD/GRID";
    default:           return "?";
  }
}
