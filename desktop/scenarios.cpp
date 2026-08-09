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
#include "scenarios.h"
#include "../firmware/ScoreDeck/src/core/state.h"
#include "../firmware/ScoreDeck/src/ui/ui.h"

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
  setSide(g.away, aAbbr, aAbbr, "12-4-2", aScore, aCol, "1");
  setSide(g.home, hAbbr, hAbbr, "21-6-4", hScore, hCol, "2");
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
}

/** Refill the screens whose Open() clears state before fetching. */
void scenarioReapply(int n) {
  if (n == SCN_ACCENTS) {
    g_lineup.loading = false;
    g_lineup.sideCount = 1;
    uiLineupApply();
  }
}

void scenarioApply(int n) {
  reset();
  switch (n) {
    case SCN_TYPICAL:
      league("nhl", 3); league("nfl", 1); league("mlb", 2);
      push("nhl", GS_LIVE,  "3rd 04:21", "MTL", 2, 0xAF1E2D, "TOR", 3, 0x00205B, "SN", true);
      push("nfl", GS_LIVE,  "Q2 11:03",  "BUF", 14, 0x00338D, "KC", 21, 0xE31837, "CBS");
      push("nhl", GS_LIVE,  "1st 18:44", "EDM", 1, 0xFF4C00, "CGY", 0, 0xC8102E, "SN");
      push("mlb", GS_LIVE,  "Bot 7",     "CIN", 1, 0xC6011F, "WSH", 3, 0xAB0003, "MLB.TV");
      push("nhl", GS_PRE,   "7:00 PM",   "BOS", 0, 0xFFB81C, "NYR", 0, 0x0038A8, "ESPN");
      push("mlb", GS_PRE,   "8:10 PM",   "LAD", 0, 0x005A9C, "SF", 0, 0xFD5A1E, "MLB.TV");
      push("nfl", GS_FINAL, "Final",     "DAL", 17, 0x041E42, "PHI", 27, 0x004C54);
      push("nhl", GS_FINAL, "Final/OT",  "VAN", 4, 0x00205B, "SEA", 5, 0x99D9D9);
      push("mlb", GS_FINAL, "Final",     "NYY", 6, 0x132448, "BOS", 2, 0xBD3039);
      break;

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
  }
  uiInit();
  uiBoardRefresh();
  uiIdleRefresh();
  uiShow(uiShouldIdle() ? SCR_IDLE : SCR_BOARD);
}

const char* scenarioName(int n) {
  switch (n) {
    case SCN_TYPICAL:  return "typical mixed board";
    case SCN_EMPTY:    return "empty — idle screen";
    case SCN_ALL_LIVE: return "nine live games";
    case SCN_EXTREMES: return "long names, three-digit scores";
    case SCN_CROWDED:  return "48 games — cap and pager";
    case SCN_NO_PROXY: return "no proxy configured";
    case SCN_STALE:    return "stale upstream";
    case SCN_ACCENTS:  return "accented names — font range";
    default:           return "?";
  }
}
