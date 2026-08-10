// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// config.h — every geometry, timing and NVS key. No magic numbers in modules.
#pragma once
#include <stdint.h>

#define SD_VERSION "0.1.0-alpha.1"

// ── panel ───────────────────────────────────────────────────────────────────
#define SCR_W 800
#define SCR_H 480

// ── board layout: Standard 9-up (UI.md §3) ──────────────────────────────────
// Roomy/Dense are the same anatomy at different constants — see DENSITY below.
#define BAR_H        48
#define GRID_TOP     60
#define TILE_W      248
#define TILE_H      128
#define TILE_GUT     12
#define TILE_MARG    16
#define GRID_COLS     3
#define GRID_ROWS     3
#define TILES_PER_PAGE 12   // largest density (Dense 4x3), not the default

#define TILE_PAD_X   13
#define TILE_PAD_Y   11
#define BADGE_S      30
#define EDGE_W        3   // the signature edge light
#define STATUS_H     21

// Hard cap — a November Saturday has 300+ D1 basketball games (PLAN.md §4).
#define MAX_GAMES    48
#define MAX_LEAGUES  12
#define MAX_EVENTS    8

// ── density presets ─────────────────────────────────────────────────────────
enum Density : uint8_t { DEN_ROOMY = 0, DEN_STANDARD = 1, DEN_DENSE = 2 };
struct DensitySpec {
  uint16_t barH, tileW, tileH, gut, marg, gridTop;
  uint8_t  cols, rows, badge, scoreFont;
};
// scoreFont: index into theme's score face table (0 = 46px, 1 = 38px).
// DEN_AUTO is the default and is not a layout of its own — it picks one of
// the three below from how many games there actually are. At the wall distance
// the design was originally reasoned about, the answer to a quiet night was to
// make the tiles bigger; on a desk that reads as an accessibility mode, so the
// rule is to recruit content instead. See uiBoardRefresh().
#define DEN_ROOMY    0
#define DEN_STANDARD 1
#define DEN_DENSE    2
#define DEN_AUTO     3
#define DEN_COUNT    4

static const DensitySpec kDensity[3] = {
  { 58, 244, 186, 16, 18, 72, 3, 2, 38, 0 },
  { 48, 248, 128, 12, 16, 60, 3, 3, 30, 1 },
  { 44, 186, 131, 10, 12, 56, 4, 3, 26, 1 },
};

// ── networking ──────────────────────────────────────────────────────────────
#define HTTP_TIMEOUT_MS   9000
#define POLL_MIN_S          10
#define POLL_MAX_S         900
#define POLL_DEFAULT_S      60
#define WIFI_RETRY_MS    15000

// mbedTLS needs a ~16.4 KB contiguous block. Check the gate in LOOP context
// before spawning — INHERITED_RULES.md §14.
#define TLS_HEAP_FLOOR      52000
#define TLS_LARGEST_FLOOR   20000
#define NET_TASK_STACK      12288

// ── alerts ──────────────────────────────────────────────────────────────────
#define ALERT_W        520
#define ALERT_H        300
#define ALERT_X        ((SCR_W - ALERT_W) / 2)
#define ALERT_Y         90
#define ALERT_HOLD_MS 10000
#define ALERT_GAP_MS  12000
#define ALERT_BANNER_MS 4500   // non-followed score: banner, no takeover

// Auto-focus: how long a tense game may hold the screen, and how long to
// leave the user alone after they close one themselves.
#define FOCUS_MAX_MS      180000UL
#define FOCUS_COOLDOWN_MS  90000UL
#define ALERT_FADE_STEPS   4   // discrete. Never a tween — UI.md §8.

// ── NVS (Preferences namespace "sdeck") ─────────────────────────────────────
#define NVS_NS      "sdeck"
#define K_SSID      "ssid"
#define K_PASS      "pass"
#define K_PROXY     "proxy"     // base URL, empty until onboarding
#define K_TOKEN     "token"     // bearer
#define K_REGION    "rgn"       // "ca" | "us" | "gb" …
#define K_TZ        "tz"        // POSIX TZ string
#define K_FAVS      "favs"      // "nhl:21,eng.1:359"
#define K_LEAGUES   "lgs"
#define K_DENSITY   "dens"
#define K_SEQ       "seq"       // last alert sequence seen
#define K_PPASS     "ppass"     // panel/API password
#define K_QUIET_EN  "qen"
#define K_QUIET_FR  "qfr"       // minutes from midnight
#define K_QUIET_TO  "qto"
#define K_ALERT_EN  "alen"
#define K_FOCUS_EN  "focus"
#define K_CLK24     "clk24"
#define K_TZ_IANA   "tzi"

#define FAVS_MAX_LEN 240
#define FAVS_MAX      20   // the proxy caps at 20 pairs too
#define QUIET_DEFAULT_FROM (23 * 60)
#define QUIET_DEFAULT_TO   (7 * 60)
