// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// types.h — the device half of the wire contract in proxy/src/types.ts.
// Field widths are the clamps the proxy already applies; keep them in sync.
#pragma once
#include <stdint.h>

enum ScoreModel : uint8_t { SM_CLOCK = 0, SM_INNING, SM_SET, SM_LEADERBOARD, SM_GRID };
enum GameState  : uint8_t { GS_PRE = 0, GS_LIVE, GS_FINAL };

struct Side {
  char     abbr[5];    // "TOR"
  char     name[20];   // "Maple Leafs"
  char     rec[10];    // "21-6-4" — the inline context chip
  uint16_t score;
  uint32_t color;      // 0xRRGGBB, content not chrome
  uint8_t  rank;       // 0 = unranked
  char     id[12];
};

struct Game {
  char       id[12];
  char       league[8];
  ScoreModel model;
  GameState  state;
  Side       away, home;
  char       status[16];   // "3rd 04:21" | "Bot 7" | "Final" | "7:00 PM"
  char       bcast[13];    // region-resolved; empty = unknown
  uint16_t   situation;    // packed, see proxy types.ts
  uint32_t   startUtc;
  uint8_t    winProbHome;  // 255 = unavailable
  bool       leaderHome;
  bool       isFav;
};

struct AlertEvent {
  uint32_t seq;
  char     gameId[12];
  char     verb[13];       // "GOAL" | "TOUCHDOWN"
  char     abbr[5];
  uint32_t color;
  char     who[24];
  char     detail[32];
  uint16_t scoreAway, scoreHome;
  char     status[16];
};

/** GET /v1/game/:league/:id — mirrors proxy/src/types.ts GameDetail. */
#define GD_LS_COLS   14
#define GD_PLAYS      5
#define GD_STATS      6

struct GameDetail {
  char     id[12];
  char     status[16];
  char     venue[29];
  char     awayAbbr[5], homeAbbr[5];
  uint16_t awayScore, homeScore;
  uint32_t awayColor, homeColor;
  bool     live;

  uint8_t  lsCount;
  char     lsCols[GD_LS_COLS][4];
  char     lsA[GD_LS_COLS][4];
  char     lsH[GD_LS_COLS][4];

  uint8_t  playCount;
  char     playT[GD_PLAYS][11];
  char     playX[GD_PLAYS][73];
  char     playS[GD_PLAYS][10];
  bool     playHome[GD_PLAYS];

  uint8_t  statCount;
  char     statK[GD_STATS][10];
  char     statA[GD_STATS][10];
  char     statH[GD_STATS][10];

  uint8_t  winProbHome;   // >100 = unavailable
  uint16_t situation;
};

// Standings — generic {cols, rows}. The firmware never learns what a
// faceoff, a goal difference or a games-behind is.
#define ST_MAX_COLS   8
#define ST_MAX_ROWS  32
#define ST_MAX_CUTS   3

struct StandingRow {
  char     abbr[5];
  char     name[17];
  uint32_t color;
  char     cells[ST_MAX_COLS][8];
};

struct Standings {
  bool         loading;
  uint8_t      colCount;
  uint8_t      rowCount;
  uint8_t      cutCount;
  char         cols[ST_MAX_COLS][8];
  StandingRow  rows[ST_MAX_ROWS];
  uint8_t      cutAfter[ST_MAX_CUTS];
  char         cutLabel[ST_MAX_CUTS][20];
};

#define NEWS_MAX 10

struct NewsItem {
  char     headline[91];
  char     desc[241];
  uint32_t when;
  char     abbr[5];
  uint32_t color;
};

struct NewsFeed {
  bool     loading;
  uint8_t  count;
  NewsItem items[NEWS_MAX];
};

struct LeagueCount {
  char    slug[8];
  uint8_t live;
};

// Situation bit helpers — keep the packing in one place.
static inline bool sitOnFirst (uint16_t s) { return s & 0x01; }
static inline bool sitOnSecond(uint16_t s) { return s & 0x02; }
static inline bool sitOnThird (uint16_t s) { return s & 0x04; }
static inline uint8_t sitOuts (uint16_t s) { return (s >> 3) & 0x03; }
static inline bool sitHomePoss(uint16_t s) { return s & 0x01; }
static inline bool sitRedZone (uint16_t s) { return s & 0x02; }
