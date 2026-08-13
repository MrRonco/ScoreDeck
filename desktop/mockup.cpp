// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// mockup.cpp — the proposed redesign, drawn with real LVGL on the real panel
// geometry, BEFORE any of it is committed to the shipping UI.
//
// This file touches nothing in firmware/. It composes the proposed screens
// directly so they can be looked at, argued with and thrown away cheaply. An
// HTML mockup would be a guess about a renderer that quantises to RGB565,
// cannot blur, and bands any gradient it is given; this is the actual output.
//
//   ./scoredeck-ui --mock 0    proposed board  (FEATURE: hero + tiles + ledger)
//   ./scoredeck-ui --mock 1    proposed idle
//   ./scoredeck-ui --mock 2    proposed board, busy night (grid unchanged)
//   ./scoredeck-ui --mock 3    token comparison strip
//   ./scoredeck-ui --mock 4    proposed board, variant B — ledger on bare plate
//   ./scoredeck-ui --mock 5    SPIKE: team-colour dithered falloff (decides AURORA)
//   ./scoredeck-ui --mock 6    SPIKE: Solari card hairlines (decides SOLARI)
//   ./scoredeck-ui --mock 7    SPIKE: lv_arc gauge edge quality (decides FLIGHT DECK)
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <lvgl.h>
#include "mockup.h"
#include "../firmware/ScoreDeck/src/config.h"
#include "../firmware/ScoreDeck/src/ui/theme.h"

LV_FONT_DECLARE(font_hero72)
LV_FONT_DECLARE(font_clock96)

// ── the proposed token set ─────────────────────────────────────────────────
//
// The whole argument in six numbers. Measured on the current build: the board
// is 73.7% mid-grey and only 16.4% dark; the idle screen is 80.5% ONE colour.
// AirRadar on the same panel is 82.8% dark. The plate is not a background, it
// is 12px of grout between cards — so nothing reads as emissive and nothing
// reads as an object.
#define M_PLATE   lv_color_hex(0x04070E)   // was 0x0A0F18  — L* 4.3 -> 0.9
#define M_SUNK    lv_color_hex(0x080C14)
#define M_SURF    lv_color_hex(0x101825)   // final
#define M_SURF_1  lv_color_hex(0x16202E)   // scheduled
#define M_SURF_2  lv_color_hex(0x1B2636)   // live, top bar
#define M_SURF_3  lv_color_hex(0x222E40)   // hero

#define M_INK     lv_color_hex(0xF3F7FB)
#define M_INK2    lv_color_hex(0xA6B6C8)
#define M_INK3    lv_color_hex(0x7A8899)   // was 0x5D6D7E — 2.6:1 -> 4.3:1

// One accent, one meaning: "happening now, or touch this". Chosen for a hue
// band team kits do not occupy — the nearest real ones (Miami, San Jose) are
// dark and dull, and teamInk() lifts toward a colour's OWN hue, so a dull teal
// can never be lifted into a collision with this.
#define M_LIVE    lv_color_hex(0x3BE0C0)
#define M_LIVE_SD lv_color_hex(0x2A9E8C)
#define M_WARN    lv_color_hex(0xF2B441)

#define M_LINE    lv_color_hex(0xB4CDE6)   // one line colour, five opacities
#define OPA_HAIR  20
#define OPA_EDGE  46
#define OPA_SPEC  120

static lv_obj_t* s_root;

// ── primitives ─────────────────────────────────────────────────────────────
static lv_obj_t* rect(lv_obj_t* p, int x, int y, int w, int h,
                      lv_color_t c, lv_opa_t opa = LV_OPA_COVER, int r = 0) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, c, 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_radius(o, r, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

/** A surface. Depth is fill step + specular pair + border opacity — never a
 *  shadow (LV_SHADOW_CACHE_SIZE is 0, so every shadow re-blurs each draw) and
 *  never a gradient (RGB565 quantises a 128px ramp into three flat slabs). */
static lv_obj_t* card(lv_obj_t* p, int x, int y, int w, int h,
                      lv_color_t fill, int r, lv_opa_t edge = OPA_EDGE) {
  lv_obj_t* o = rect(p, x, y, w, h, fill, LV_OPA_COVER, r);
  lv_obj_set_style_border_color(o, M_LINE, 0);
  lv_obj_set_style_border_opa(o, edge, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  rect(o, r, 0, w - 2 * r, 1, M_LINE, OPA_SPEC);            // specular catch
  rect(o, r, h - 3, w - 2 * r, 2, lv_color_black(), 90);    // shade
  return o;
}

static lv_obj_t* txt(lv_obj_t* p, int x, int y, const char* s,
                     lv_color_t c, const lv_font_t* f, int track = 0,
                     lv_text_align_t al = LV_TEXT_ALIGN_LEFT, int w = 0) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_font(l, f, 0);
  if (track) lv_obj_set_style_text_letter_space(l, track, 0);
  if (w) { lv_obj_set_width(l, w); lv_obj_set_style_text_align(l, al, 0); }
  return l;
}

/** Tracked micro-caps. The rule stolen from AirRadar: +1 tracking and CAPS
 *  means "this is a LABEL naming something"; zero tracking means "this is the
 *  DATA itself". Colour says how much it matters, tracking says what kind of
 *  thing it is — two free axes the current build spends neither of. */
static lv_obj_t* label_caps(lv_obj_t* p, int x, int y, const char* s, lv_color_t c) {
  return txt(p, x, y, s, c, F_MICRO, 1);
}

static void navRail(lv_obj_t* bar) {
  // Text, not fills. Ghost until touched — chrome that recedes. Every target
  // clears 48px in BOTH axes; the shipping buttons are 36px in the short axis
  // (6.9mm), under the 9mm floor the source itself cites.
  label_caps(bar, 556, 18, "STANDINGS", M_INK3);
  rect(bar, 662, 4, 1, 40, M_LINE, 14);
  label_caps(bar, 676, 18, "NEWS", M_INK3);
  rect(bar, 738, 4, 1, 40, M_LINE, 14);
  // A baked gear glyph is a separate work item; showing a box here would
  // misrepresent the design, so the mockup uses the word it would replace.
  label_caps(bar, 748, 18, "SET", M_INK3);
}

static void topBar(lv_obj_t* parent, bool live) {
  lv_obj_t* bar = rect(parent, 0, 0, SCR_W, 48, M_SURF_2);
  rect(bar, 0, 47, SCR_W, 1, M_LINE, 30);

  txt(bar, 18, 14, "7:14", M_INK, F_ABBR);
  label_caps(bar, 74, 18, "PM  ·  MON AUG 10", M_INK3);

  // Chip 0 is LIVE n — the most-wanted filter on a scoreboard, and one that
  // does not exist today (tapping ALL clears the filter instead).
  // Bounded at four visible plus an overflow count. Unbounded, the strip
  // reaches the nav rail at six leagues and leaves the screen at nine — which
  // is what happened the first time this mockup was rendered.
  struct { const char* s; bool on; bool live; } chips[] = {
    { "LIVE 3", false, true }, { "ALL 12", true, false },
    { "NHL 3", false, false }, { "+2", false, false },
  };
  int x = 250;
  for (auto& c : chips) {
    if (c.on) {
      lv_obj_t* ch = rect(bar, x, 6, 66, 36, M_SURF_3, LV_OPA_COVER, 8);
      (void)ch;
      rect(bar, x + 8, 40, 50, 2, M_LIVE, LV_OPA_COVER, 1);
    }
    txt(bar, x + 8, 18, c.s, c.on ? M_INK : (c.live ? M_LIVE : M_INK3), F_MICRO, 1);
    x += 70;
  }
  (void)live;
  navRail(bar);
}

// ── the hero ───────────────────────────────────────────────────────────────
static void heroCell(lv_obj_t* parent) {
  lv_obj_t* h = card(parent, 16, 60, 508, 268, M_SURF_3, 16, 72);

  label_caps(h, 24, 18, "NHL  ·  SPORTSNET", M_INK3);
  rect(h, 330, 26, 6, 6, M_LIVE, LV_OPA_COVER, 3);
  txt(h, 344, 16, "3rd  04:21", M_INK, F_NUM, 0, LV_TEXT_ALIGN_RIGHT, 140);
  rect(h, 20, 46, 468, 1, M_LINE, OPA_HAIR);

  // The edge bar marks the LEADING TEAM'S ROW, at x=0. The shipping build puts
  // it at the tile's right edge when home leads, which lands it 6px from the
  // NEXT tile's left edge across a 12px gutter — nine tiles of that read as
  // column rails, not as ownership.
  rect(h, 0, 150, 4, 52, lv_color_hex(teamInk(0x00205B)), LV_OPA_COVER, 2);

  // away
  rect(h, 24, 66, 52, 52, lv_color_hex(teamFill(0xAF1E2D)), LV_OPA_COVER, 12);
  txt(h, 34, 82, "MTL", M_INK, F_ABBR);
  txt(h, 92, 66, "MONTREAL", M_INK, F_DISPLAY);
  txt(h, 92, 100, "CANADIENS  ·  18-9-3", M_INK3, F_NUM);
  txt(h, 340, 58, "2", M_INK2, &font_hero72, 0, LV_TEXT_ALIGN_RIGHT, 144);

  rect(h, 20, 148, 468, 1, M_LINE, OPA_HAIR);

  // home — the leading side. Its score is TEAM COLOUR at 72px: ~2,400px of
  // Leafs blue at the focal point of the screen. That is the signature the
  // 3px perimeter strip was reaching for, at six times the area and in the
  // middle of attention rather than at the edge.
  rect(h, 24, 150, 52, 52, lv_color_hex(teamFill(0x00205B)), LV_OPA_COVER, 12);
  txt(h, 34, 166, "TOR", M_INK, F_ABBR);
  txt(h, 92, 150, "TORONTO", M_INK, F_DISPLAY);
  txt(h, 92, 184, "MAPLE LEAFS  ·  21-6-4", M_INK3, F_NUM);
  txt(h, 340, 142, "3", lv_color_hex(teamInk(0x00205B)), &font_hero72,
      0, LV_TEXT_ALIGN_RIGHT, 144);

  rect(h, 24, 238, 6, 6, M_LIVE, LV_OPA_COVER, 3);
  txt(h, 38, 232, "POWER PLAY  ·  1:12", M_INK, F_NUM);
  txt(h, 344, 232, "SOG  24 · 31", M_INK3, F_NUM, 0, LV_TEXT_ALIGN_RIGHT, 140);
}

static void smallTile(lv_obj_t* parent, int y, const char* a, const char* h,
                      uint32_t ca, uint32_t ch, const char* sa, const char* sh,
                      const char* status, const char* right, bool homeLeads) {
  lv_obj_t* t = card(parent, 536, y, 248, 128, M_SURF_2, 12);
  rect(t, 0, homeLeads ? 62 : 12, 4, 50, lv_color_hex(teamInk(homeLeads ? ch : ca)),
       LV_OPA_COVER, 2);

  rect(t, 13, 22, 30, 30, lv_color_hex(teamFill(ca)), LV_OPA_COVER, 8);
  txt(t, 51, 28, a, M_INK, F_ABBR);
  txt(t, 157, 18, sa, homeLeads ? M_INK2 : lv_color_hex(teamInk(ca)),
      F_SCORE, 0, LV_TEXT_ALIGN_RIGHT, 78);

  rect(t, 13, 72, 30, 30, lv_color_hex(teamFill(ch)), LV_OPA_COVER, 8);
  txt(t, 51, 78, h, M_INK, F_ABBR);
  txt(t, 157, 68, sh, homeLeads ? lv_color_hex(teamInk(ch)) : M_INK2,
      F_SCORE, 0, LV_TEXT_ALIGN_RIGHT, 78);

  rect(t, 13, 110, 6, 6, M_LIVE, LV_OPA_COVER, 3);
  label_caps(t, 25, 104, status, M_INK3);
  txt(t, 125, 104, right, M_INK3, F_MICRO, 1, LV_TEXT_ALIGN_RIGHT, 110);
}

/** Scheduled and finished games are one line of information each. Giving them
 *  a 248x128 tile is not neutrality, it is inaccuracy — three of nine tiles on
 *  the current board burn 33% of the screen to say "-" and "-". */
static void ledger(lv_obj_t* parent) {
  lv_obj_t* b = card(parent, 16, 340, 768, 124, M_SURF, 16);
  label_caps(b, 24, 16, "UPCOMING", M_LIVE_SD);
  label_caps(b, 412, 16, "FINAL", M_INK3);
  rect(b, 24, 36, 348, 1, M_LINE, OPA_HAIR);
  rect(b, 412, 36, 348, 1, M_LINE, OPA_HAIR);
  rect(b, 392, 20, 1, 88, M_LINE, 14);

  const char* up[3][3] = {
    { "7:00", "BOS @ NYR", "ESPN" },
    { "8:10", "LAD @ SF",  "MLB.TV" },
    { "9:30", "VGK @ COL", "TNT" },
  };
  const char* fin[3][3] = {
    { "VAN  4", "SEA  5", "F/OT" },
    { "NYY  6", "BOS  2", "F" },
    { "DAL 17", "PHI 27", "F" },
  };
  for (int i = 0; i < 3; i++) {
    const int y = 48 + i * 26;
    txt(b, 24, y, up[i][0], M_INK3, F_NUM);
    txt(b, 78, y, up[i][1], M_INK2, F_NUM);
    txt(b, 232, y, up[i][2], M_INK3, F_NUM, 0, LV_TEXT_ALIGN_RIGHT, 140);
    txt(b, 412, y, fin[i][0], M_INK2, F_NUM);
    txt(b, 508, y, fin[i][1], M_INK2, F_NUM);
    txt(b, 620, y, fin[i][2], M_INK3, F_NUM, 0, LV_TEXT_ALIGN_RIGHT, 140);
  }
}

// ── screens ────────────────────────────────────────────────────────────────
static void ledgerBare(lv_obj_t* p);

static void mockBoard(lv_obj_t* p, bool bare) {
  topBar(p, true);
  heroCell(p);
  smallTile(p, 60,  "BUF", "KC",  0x00338D, 0xE31837, "14", "21",
            "Q2 11:03", "RED ZONE", true);
  smallTile(p, 200, "EDM", "CGY", 0xFF4C00, 0xC8102E, "1", "0",
            "1st 18:44", "SN", false);
  bare ? ledgerBare(p) : ledger(p);
}

/** Busy nights are the one case the uniform grid gets RIGHT — when everything
 *  is live, everything really is equally interesting. Only the tokens change. */
static void mockBusy(lv_obj_t* p) {
  topBar(p, true);
  struct { const char* a; const char* h; uint32_t ca, ch; const char* sa; const char* sh;
           const char* st; const char* r; bool hl; } g[9] = {
    { "MTL","TOR",0xAF1E2D,0x00205B,"2","3","3rd 04:21","POWER PLAY",true },
    { "BUF","KC", 0x00338D,0xE31837,"14","21","Q2 11:03","RED ZONE",true },
    { "EDM","CGY",0xFF4C00,0xC8102E,"1","0","1st 18:44","SN",false },
    { "CIN","WSH",0xC6011F,0xAB0003,"1","3","Bot 7","2 ON 1 OUT",true },
    { "BOS","NYR",0xFFB81C,0x0038A8,"2","2","2nd 11:02","ESPN",false },
    { "LAD","SF", 0x005A9C,0xFD5A1E,"4","1","Top 5","MLB.TV",false },
    { "PIT","NJD",0x000000,0xCE1126,"0","1","1st 06:22","SN",true },
    { "SEA","VAN",0x99D9D9,0x00205B,"3","2","3rd 12:40","SN",false },
    { "NYY","BOS",0x132448,0xBD3039,"6","2","Bot 8","YES",false },
  };
  for (int i = 0; i < 9; i++) {
    const int col = i % 3, row = i / 3;
    const int x = 16 + col * 260, y = 60 + row * 140;
    lv_obj_t* t = card(p, x, y, 248, 128, M_SURF_2, 12);
    rect(t, 0, g[i].hl ? 62 : 12, 4, 50,
         lv_color_hex(teamInk(g[i].hl ? g[i].ch : g[i].ca)), LV_OPA_COVER, 2);
    rect(t, 13, 22, 30, 30, lv_color_hex(teamFill(g[i].ca)), LV_OPA_COVER, 8);
    txt(t, 51, 28, g[i].a, M_INK, F_ABBR);
    txt(t, 157, 18, g[i].sa, g[i].hl ? M_INK2 : lv_color_hex(teamInk(g[i].ca)),
        F_SCORE, 0, LV_TEXT_ALIGN_RIGHT, 78);
    rect(t, 13, 72, 30, 30, lv_color_hex(teamFill(g[i].ch)), LV_OPA_COVER, 8);
    txt(t, 51, 78, g[i].h, M_INK, F_ABBR);
    txt(t, 157, 68, g[i].sh, g[i].hl ? lv_color_hex(teamInk(g[i].ch)) : M_INK2,
        F_SCORE, 0, LV_TEXT_ALIGN_RIGHT, 78);
    rect(t, 13, 110, 6, 6, M_LIVE, LV_OPA_COVER, 3);
    label_caps(t, 25, 104, g[i].st, M_INK3);
    txt(t, 125, 104, g[i].r, M_INK3, F_MICRO, 1, LV_TEXT_ALIGN_RIGHT, 110);
  }
}

/** The clock comes OUT of its card. Card coverage on the current idle screen
 *  is 87%, and 80.5% of the screen is one grey value — so the type becomes the
 *  hero, on bare plate, the way AirRadar's map is. */
static void mockIdle(lv_obj_t* p) {
  lv_obj_t* bar = rect(p, 0, 0, SCR_W, 48, M_SURF_2);
  rect(bar, 0, 47, SCR_W, 1, M_LINE, 30);
  label_caps(bar, 18, 18, "SCOREDECK", M_INK2);
  navRail(bar);

  txt(p, 44, 84, "4:13", M_INK, &font_clock96);
  txt(p, 300, 148, "PM", M_INK3, F_DISPLAY);
  label_caps(p, 48, 232, "MONDAY  ·  AUGUST 10", M_INK3);
  txt(p, 48, 258, "2 scheduled  ·  first at 5:13 PM", M_INK2, F_NUM);

  lv_obj_t* n = card(p, 508, 96, 276, 232, M_SURF_2, 16);
  rect(n, 0, 24, 4, 40, M_LIVE, LV_OPA_COVER, 2);
  label_caps(n, 24, 18, "NEXT UP", M_INK3);
  rect(n, 24, 54, 40, 40, lv_color_hex(teamFill(0xFFB81C)), LV_OPA_COVER, 10);
  txt(n, 76, 64, "BOS", M_INK, F_ABBR);
  txt(n, 140, 64, "@", M_INK3, F_NUM);
  txt(n, 170, 64, "NYR", M_INK, F_ABBR);
  rect(n, 220, 54, 40, 40, lv_color_hex(teamFill(0x0038A8)), LV_OPA_COVER, 10);
  txt(n, 24, 108, "1H 00M", M_LIVE, &font_hero72);
  rect(n, 24, 186, 228, 1, M_LINE, OPA_HAIR);
  label_caps(n, 24, 198, "5:13 PM  ·  NHL  ·  ESPN", M_INK3);

  lv_obj_t* b = card(p, 16, 344, 768, 120, M_SURF, 16);
  label_caps(b, 24, 14, "TODAY", M_LIVE_SD);
  label_caps(b, 412, 14, "LATEST", M_INK3);
  rect(b, 24, 34, 348, 1, M_LINE, OPA_HAIR);
  rect(b, 412, 34, 348, 1, M_LINE, OPA_HAIR);
  rect(b, 392, 18, 1, 84, M_LINE, 14);
  const char* t1[2][3] = { { "5:13", "LAD @ SF", "MLB.TV" }, { "7:00", "BOS @ NYR", "ESPN" } };
  const char* t2[2][3] = { { "DAL 17", "PHI 27", "F" }, { "VAN  4", "SEA  5", "F/OT" } };
  for (int i = 0; i < 2; i++) {
    const int y = 46 + i * 28;
    txt(b, 24, y, t1[i][0], M_INK3, F_NUM);
    txt(b, 78, y, t1[i][1], M_INK2, F_NUM);
    txt(b, 232, y, t1[i][2], M_INK3, F_NUM, 0, LV_TEXT_ALIGN_RIGHT, 140);
    txt(b, 412, y, t2[i][0], M_INK2, F_NUM);
    txt(b, 508, y, t2[i][1], M_INK2, F_NUM);
    txt(b, 620, y, t2[i][2], M_INK3, F_NUM, 0, LV_TEXT_ALIGN_RIGHT, 140);
  }
}

/**
 * Variant B. Mock 0 fixed hierarchy, type and colour — and measured WORSE on
 * the thing the audit named as cause #1: card coverage went 84.4% -> 86.8%,
 * because a hero plus a ledger card covers as much board as nine tiles did.
 *
 * So the ledger loses its card. The rows sit on bare plate under a single
 * hairline, which is what a ledger is: a list, not an object. Coverage falls
 * to 62% and the plate becomes a background rather than grout.
 */
static void ledgerBare(lv_obj_t* p) {
  label_caps(p, 24, 348, "UPCOMING", M_INK3);
  label_caps(p, 412, 348, "FINAL", M_INK3);
  rect(p, 24, 368, 348, 1, M_LINE, OPA_HAIR);
  rect(p, 412, 368, 348, 1, M_LINE, OPA_HAIR);

  const char* up[3][3] = {
    { "7:00", "BOS @ NYR", "ESPN" },
    { "8:10", "LAD @ SF",  "MLB.TV" },
    { "9:30", "VGK @ COL", "TNT" },
  };
  const char* fin[3][3] = {
    { "VAN  4", "SEA  5", "F/OT" },
    { "NYY  6", "BOS  2", "F" },
    { "DAL 17", "PHI 27", "F" },
  };
  for (int i = 0; i < 3; i++) {
    const int y = 384 + i * 30;
    txt(p, 24,  y, up[i][0],  M_INK3, F_NUM);
    txt(p, 78,  y, up[i][1],  M_INK2, F_NUM);
    txt(p, 232, y, up[i][2],  M_INK3, F_NUM, 0, LV_TEXT_ALIGN_RIGHT, 140);
    txt(p, 412, y, fin[i][0], M_INK2, F_NUM);
    txt(p, 508, y, fin[i][1], M_INK2, F_NUM);
    txt(p, 620, y, fin[i][2], M_INK3, F_NUM, 0, LV_TEXT_ALIGN_RIGHT, 140);
  }
}

/** The tokens, side by side, so the argument can be checked rather than
 *  believed: the surface ladder, the ink ladder, and what teamInk() does. */
static void mockTokens(lv_obj_t* p) {
  label_caps(p, 24, 20, "SURFACE LADDER  ·  CURRENT vs PROPOSED", M_INK3);
  struct { const char* n; uint32_t cur; uint32_t prop; } S[] = {
    { "PLATE",  0x0A0F18, 0x04070E }, { "FINAL",  0x0E141D, 0x101825 },
    { "PRE",    0x151D29, 0x16202E }, { "LIVE",   0x101C29, 0x1B2636 },
    { "HERO",   0x1A2432, 0x222E40 },
  };
  for (int i = 0; i < 5; i++) {
    const int x = 24 + i * 152;
    rect(p, x, 48, 140, 56, lv_color_hex(S[i].cur), LV_OPA_COVER, 8);
    label_caps(p, x + 8, 110, S[i].n, M_INK3);
    rect(p, x, 132, 140, 56, lv_color_hex(S[i].prop), LV_OPA_COVER, 8);
    lv_obj_t* o = lv_obj_get_child(p, -1);
    lv_obj_set_style_border_color(o, M_LINE, 0);
    lv_obj_set_style_border_opa(o, OPA_EDGE, 0);
    lv_obj_set_style_border_width(o, 1, 0);
  }
  label_caps(p, 24, 196, "CURRENT above  ·  PROPOSED below, on the new plate", M_INK3);

  label_caps(p, 24, 236, "ACCENT  ·  ONE HUE, ONE MEANING", M_INK3);
  const uint32_t acc[3] = { 0x3BE0C0, 0xF2B441, 0xFF6B78 };
  const char* an[3] = { "LIVE / TOUCH", "STALE / QUIET", "ERROR" };
  for (int i = 0; i < 3; i++) {
    rect(p, 24 + i * 250, 264, 40, 40, lv_color_hex(acc[i]), LV_OPA_COVER, 8);
    label_caps(p, 76 + i * 250, 278, an[i], M_INK2);
  }

  label_caps(p, 24, 328, "teamInk()  ·  RAW KIT COLOUR vs LIFTED, HUE KEPT", M_INK3);
  const uint32_t T[6] = { 0x00205B, 0xAF1E2D, 0x000000, 0xE31837, 0x99D9D9, 0xFF4C00 };
  const char* TN[6] = { "TOR", "MTL", "PIT", "KC", "SEA", "EDM" };
  for (int i = 0; i < 6; i++) {
    const int x = 24 + i * 126;
    rect(p, x, 356, 56, 44, lv_color_hex(T[i]), LV_OPA_COVER, 8);
    rect(p, x + 60, 356, 56, 44, lv_color_hex(teamInk(T[i])), LV_OPA_COVER, 8);
    label_caps(p, x, 408, TN[i], M_INK3);
  }
  label_caps(p, 24, 440, "LEFT raw  ·  RIGHT lifted to 3.5:1, saturated first so the hue survives", M_INK3);
}


// ── the three decisive spikes ──────────────────────────────────────────────
//
// The seven-concept study (design-concepts.md) ends with three cheap renders
// that each decide a whole concept. These are those renders, in the same
// RGB565 pipeline the panel uses, so the answer is quantisation-faithful.

/** One full-screen buffer, generated per spike. Desktop-only, so a plain
 *  malloc is fine; the firmware equivalent would be the plate.cpp pattern. */
static uint8_t* spikeBuf(int w, int h) {
  static uint8_t* buf;
  if (!buf) buf = (uint8_t*)malloc((size_t)w * h * 2);
  return buf;
}

static const uint8_t kBayer4[16] = { 0,8,2,10, 12,4,14,6, 3,11,1,9, 15,7,13,5 };

static inline uint32_t sp_hash(uint32_t x, uint32_t y) {
  uint32_t h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

/**
 * SPIKE 1 — a saturated team-colour radial falloff, the worst case for 5-bit
 * RGB565 quantisation. Split screen: LEFT is the proposed treatment (4x4
 * Bayer dither + grain, exactly plate.cpp's math), RIGHT is the same field
 * plainly quantised. If the left half shows contour rings, AURORA is dead;
 * if only the right half does, the dither is load-bearing and AURORA lives.
 */
static void spikeAurora(lv_obj_t* p) {
  const int W = SCR_W, H = SCR_H;
  uint8_t* buf = spikeBuf(W, H);
  const int cr8 = 0xAF, cg8 = 0x1E, cb8 = 0x2D;      // Montreal red, saturated
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      const float dx = (x - 400) / 380.0f, dy = (y - 240) / 380.0f;
      float r = sqrtf(dx * dx + dy * dy);
      if (r > 1.0f) r = 1.0f;
      const float f = (1.0f - r) * (1.0f - r) * 0.85f;
      int r8 = (int)(cr8 * f), g8 = (int)(cg8 * f), b8 = (int)(cb8 * f);
      int d = 0;
      if (x < 400) {                                  // dithered half
        d = kBayer4[((y & 3) << 2) | (x & 3)];
        const int grain = (int)(sp_hash(x, y) & 3u) - 1;
        r8 += grain; g8 += grain; b8 += grain;
        if (r8 < 0) r8 = 0; if (g8 < 0) g8 = 0; if (b8 < 0) b8 = 0;
      }
      const int r5 = (r8 * 31 + d * 255 / 16) / 255;
      const int g6 = (g8 * 63 + d * 255 / 16) / 255;
      const int b5 = (b8 * 31 + d * 255 / 16) / 255;
      const uint16_t px = (uint16_t)((r5 > 31 ? 31 : r5) << 11) |
                          (uint16_t)((g6 > 63 ? 63 : g6) << 5) |
                          (uint16_t)(b5 > 31 ? 31 : b5);
      uint8_t* q = buf + ((size_t)y * W + x) * 2;
      q[0] = (uint8_t)(px & 0xFF); q[1] = (uint8_t)(px >> 8);
    }
  }
  static lv_img_dsc_t dsc;
  dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  dsc.header.always_zero = 0;
  dsc.header.w = W; dsc.header.h = H;
  dsc.data_size = (uint32_t)W * H * 2;
  dsc.data = buf;
  lv_obj_t* im = lv_img_create(p);
  lv_img_set_src(im, &dsc);
  lv_obj_set_pos(im, 0, 0);
  rect(p, 399, 0, 2, SCR_H, lv_color_hex(0x404040));
  label_caps(p, 24, 452, "DITHER + GRAIN", lv_color_hex(0x808080));
  label_caps(p, 424, 452, "PLAIN QUANTISE", lv_color_hex(0x808080));
}

/**
 * SPIKE 2 — Solari flap cards at exact concept spec. The whole concept hangs
 * on three 1 px lines per card (top catch, split, hinge lift) surviving
 * RGB565 against a #0F0F13 face. If they vanish, the cards are plain dark
 * rectangles and SOLARI collapses.
 */
static void spikeSolari(lv_obj_t* p) {
  // Weave ground — per-pixel f(x,y), the concept's material.
  const int W = SCR_W, H = SCR_H;
  uint8_t* buf = spikeBuf(W, H);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int r8 = 0x19, g8 = 0x19, b8 = 0x20;
      const int weave = (((x / 3) % 2) ^ ((y / 3) % 2)) ? 2 : -1;
      const int knock = (int)(sp_hash(x, y) & 1u);
      r8 += weave + knock; g8 += weave + knock; b8 += weave + knock;
      const uint16_t px = (uint16_t)((r8 * 31 / 255) << 11) |
                          (uint16_t)((g8 * 63 / 255) << 5) |
                          (uint16_t)(b8 * 31 / 255);
      uint8_t* q = buf + ((size_t)y * W + x) * 2;
      q[0] = (uint8_t)(px & 0xFF); q[1] = (uint8_t)(px >> 8);
    }
  }
  static lv_img_dsc_t dsc;
  dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  dsc.header.always_zero = 0;
  dsc.header.w = W; dsc.header.h = H;
  dsc.data_size = (uint32_t)W * H * 2;
  dsc.data = buf;
  lv_obj_t* im = lv_img_create(p);
  lv_img_set_src(im, &dsc);
  lv_obj_set_pos(im, 0, 0);

  auto card = [&](int x, int y, int w, int h, const char* glyph,
                  const lv_font_t* f, lv_color_t ink) {
    lv_obj_t* c = rect(p, x, y, w, h, lv_color_hex(0x0F0F13), LV_OPA_COVER, 4);
    rect(c, 3, 0, w - 6, 1, lv_color_hex(0x3A3A45));            // top catch
    rect(c, 0, h / 2 - 1, w, 1, lv_color_hex(0x000000));         // split
    rect(c, 0, h / 2, w, 1, lv_color_hex(0x23232B));             // hinge lift
    rect(p, x - 2, y + h / 2 - 4, 2, 8, lv_color_hex(0x3A3A45)); // axle L
    rect(p, x + w, y + h / 2 - 4, 2, 8, lv_color_hex(0x3A3A45)); // axle R
    if (glyph[0]) {
      lv_obj_t* l = txt(c, 0, 0, glyph, ink, f);
      lv_obj_center(l);
    }
  };

  label_caps(p, 24, 18, "ROW SCALE 76 - SCORE CARDS 52x64, F_SCORE 46", lv_color_hex(0x8A8A94));
  const char* big[2] = { "2", "3" };
  for (int i = 0; i < 2; i++)
    card(60 + i * 64, 44, 52, 64, big[i], F_SCORE_BIG, lv_color_hex(0xF0EDE4));
  const char* word = "POWERPLAY";
  for (int i = 0; word[i]; i++) {
    char g[2] = { word[i], 0 };
    card(220 + i * 40, 52, 34, 48, g, F_ABBR, lv_color_hex(0xF5C518));
  }

  label_caps(p, 24, 138, "ROW SCALE 56 - DENSE, CARDS 30x40, F_SCORE 38", lv_color_hex(0x8A8A94));
  const char* row2 = "17";
  for (int i = 0; row2[i]; i++) {
    char g[2] = { row2[i], 0 };
    card(60 + i * 38, 162, 30, 40, g, F_SCORE, lv_color_hex(0xF0EDE4));
  }
  const char* word2 = "REDZONE";
  for (int i = 0; word2[i]; i++) {
    char g[2] = { word2[i], 0 };
    card(220 + i * 30, 166, 24, 34, g, F_MICRO, lv_color_hex(0xE2574C));
  }

  // Mid-flip frames: the 4-step illusion, half-blank cards.
  label_caps(p, 24, 240, "MID-FLIP FRAMES - TOP-HALF BLANK / BOTTOM-HALF BLANK", lv_color_hex(0x8A8A94));
  {
    lv_obj_t* c = rect(p, 60, 264, 52, 64, lv_color_hex(0x0F0F13), LV_OPA_COVER, 4);
    rect(c, 3, 0, 46, 1, lv_color_hex(0x3A3A45));
    rect(c, 0, 31, 52, 1, lv_color_hex(0x000000));
    rect(c, 0, 32, 52, 1, lv_color_hex(0x23232B));
    lv_obj_t* l = txt(c, 0, 0, "4", lv_color_hex(0xF0EDE4), F_SCORE_BIG);
    lv_obj_center(l);
    rect(c, 1, 1, 50, 30, lv_color_hex(0x0B0B0F));               // top blanked
  }
  {
    lv_obj_t* c = rect(p, 124, 264, 52, 64, lv_color_hex(0x0F0F13), LV_OPA_COVER, 4);
    rect(c, 3, 0, 46, 1, lv_color_hex(0x3A3A45));
    rect(c, 0, 31, 52, 1, lv_color_hex(0x000000));
    rect(c, 0, 32, 52, 1, lv_color_hex(0x23232B));
    lv_obj_t* l = txt(c, 0, 0, "4", lv_color_hex(0xF0EDE4), F_SCORE_BIG);
    lv_obj_center(l);
    rect(c, 1, 33, 50, 30, lv_color_hex(0x0B0B0F));              // bottom blanked
  }

  // Brass footer plate.
  lv_obj_t* brass = rect(p, 0, SCR_H - 56, SCR_W, 56, lv_color_hex(0x2A2620));
  rect(brass, 0, 0, SCR_W, 1, lv_color_hex(0x4A4436));
  lv_obj_t* eng = txt(brass, 0, 0, "S C O R E D E C K", lv_color_hex(0x6A5F4A), F_MICRO, 6);
  lv_obj_center(eng);
  rect(brass, 24, 24, 6, 6, lv_color_hex(0x141210), LV_OPA_COVER, 3);
  rect(brass, SCR_W - 30, 24, 6, 6, lv_color_hex(0x141210), LV_OPA_COVER, 3);
}

/**
 * SPIKE 3 — lv_arc ring gauges at FLIGHT DECK spec: r=46, 9 px stroke, flat
 * ends, avionics colours on near-black. Decides whether arcs read as Garmin
 * or as a cheap OBD2 pod. One r=96 ring stands in for the attitude sphere rim.
 */
static void spikeArcs(lv_obj_t* p) {
  lv_obj_set_style_bg_color(p, lv_color_hex(0x000306), 0);

  struct G { int x, v; uint32_t col; const char* cap; const char* val; };
  const G gs[4] = {
    { 100, 68, 0x3FD8EE, "SOG",  "31-28" },
    { 260, 42, 0x27E07E, "XG",   "2.4-1.1" },
    { 420, 54, 0xFFB300, "FO%",  "54" },
    { 580, 80, 0xFF5FD2, "PP",   "1:12" },
  };
  for (const G& g : gs) {
    lv_obj_t* a = lv_arc_create(p);
    lv_obj_set_size(a, 92, 92);
    lv_obj_set_pos(a, g.x, 80);
    lv_arc_set_bg_angles(a, 120, 60);
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_value(a, g.v);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, 9, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 9, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(0x164653), LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(g.col), LV_PART_INDICATOR);
    lv_obj_t* v = txt(p, g.x, 116, g.val, lv_color_hex(0xE8EEF8), F_NUM,
                      0, LV_TEXT_ALIGN_CENTER, 92);
    (void)v;
    label_caps(p, g.x + 30, 184, g.cap, lv_color_hex(0x2E5A66));
  }

  // The sphere-scale rim.
  lv_obj_t* big = lv_arc_create(p);
  lv_obj_set_size(big, 192, 192);
  lv_obj_set_pos(big, 304, 240);
  lv_arc_set_bg_angles(big, 0, 360);
  lv_arc_set_range(big, 0, 100);
  lv_arc_set_value(big, 71);
  lv_obj_remove_style(big, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(big, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(big, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(big, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(big, false, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(big, false, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(big, lv_color_hex(0x164653), LV_PART_MAIN);
  lv_obj_set_style_arc_color(big, lv_color_hex(0x3FD8EE), LV_PART_INDICATOR);

  label_caps(p, 24, 452, "R=46 STROKE 9 FLAT ENDS  ·  R=96 RIM  ·  LV_ARC ON RGB565",
             lv_color_hex(0x2E5A66));
}


/**
 * SPIKE 4 — PAPER's light ground. Near-white is the WORST case for the 5-bit
 * channels in the opposite direction: the corner falloff #E9E5DB -> #E4DFD4
 * spans barely one 5-bit level, so plain quantisation collapses it into 1-2
 * giant slabs. Left = dither + grain (the concept's mandated treatment),
 * right = plain quantise. Ink samples sit on top to check text on texture.
 */
static void spikePaper(lv_obj_t* p) {
  const int W = SCR_W, H = SCR_H;
  uint8_t* buf = spikeBuf(W, H);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      // Radial falloff from centre #E9E5DB to corner #E4DFD4.
      const float dx = (x - 400) / 400.0f, dy = (y - 240) / 240.0f;
      float t = sqrtf(dx * dx + dy * dy);
      if (t > 1.0f) t = 1.0f;
      int r8 = 0xE9 + (int)((0xE4 - 0xE9) * t);
      int g8 = 0xE5 + (int)((0xDF - 0xE5) * t);
      int b8 = 0xDB + (int)((0xD4 - 0xDB) * t);
      int d = 0;
      if (x < 400) {
        d = kBayer4[((y & 3) << 2) | (x & 3)];
        const int grain = (int)(sp_hash(x, y) & 3u) - 1;
        r8 += grain; g8 += grain; b8 += grain;
      }
      const int r5 = (r8 * 31 + d * 255 / 16) / 255;
      const int g6 = (g8 * 63 + d * 255 / 16) / 255;
      const int b5 = (b8 * 31 + d * 255 / 16) / 255;
      const uint16_t px = (uint16_t)((r5 > 31 ? 31 : r5) << 11) |
                          (uint16_t)((g6 > 63 ? 63 : g6) << 5) |
                          (uint16_t)(b5 > 31 ? 31 : b5);
      uint8_t* q = buf + ((size_t)y * W + x) * 2;
      q[0] = (uint8_t)(px & 0xFF); q[1] = (uint8_t)(px >> 8);
    }
  }
  static lv_img_dsc_t dsc;
  dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  dsc.header.always_zero = 0;
  dsc.header.w = W; dsc.header.h = H;
  dsc.data_size = (uint32_t)W * H * 2;
  dsc.data = buf;
  lv_obj_t* im = lv_img_create(p);
  lv_img_set_src(im, &dsc);
  lv_obj_set_pos(im, 0, 0);
  rect(p, 399, 0, 2, SCR_H, lv_color_hex(0xB0AB9E));

  // Ink tiers on the paper, both halves, PAPER's spec colours.
  txt(p, 60, 80, "3", lv_color_hex(0x15171B), F_SCORE_BIG);
  txt(p, 60, 160, "MAPLE LEAFS", lv_color_hex(0x15171B), F_DISPLAY);
  txt(p, 60, 210, "trailing side, secondary ink", lv_color_hex(0x6E6A5F), F_BODY);
  txt(p, 460, 80, "3", lv_color_hex(0x15171B), F_SCORE_BIG);
  txt(p, 460, 160, "MAPLE LEAFS", lv_color_hex(0x15171B), F_DISPLAY);
  txt(p, 460, 210, "trailing side, secondary ink", lv_color_hex(0x6E6A5F), F_BODY);
  label_caps(p, 24, 452, "DITHER + GRAIN", lv_color_hex(0x6E6A5F));
  label_caps(p, 424, 452, "PLAIN QUANTISE", lv_color_hex(0x6E6A5F));
}


// ── the refresh (phase C): real-pixel renders of refresh-spec.md ───────────
//
// The SVG mockups cannot answer three questions the spec itself flags:
// C_LIVE_SD (#2A9E8C) as body text, a 2 px heartbeat against the plate, and
// 2 px chip underlines. These renders answer them in the real pipeline.

#define R_LIVE    lv_color_hex(0x3BE0C0)
#define R_LIVE_SD lv_color_hex(0x2A9E8C)
#define R_WARN    lv_color_hex(0xF2B441)
#define R_SURF1   lv_color_hex(0x16202E)
#define R_SURF2   lv_color_hex(0x1B2636)
#define R_SURF3   lv_color_hex(0x222E40)
#define R_EDGEHI  lv_color_hex(0x46566A)
#define R_INK     lv_color_hex(0xF3F7FB)
#define R_INK2    lv_color_hex(0xA6B6C8)
#define R_INK3    lv_color_hex(0x7A8899)

struct RChip { const char* label; bool sel; bool live; };

/** The refreshed header: live organ, ranked filled chips with live
 *  underlines, delta ledger, nav pills, poll heartbeat. */
static void refreshHeader(lv_obj_t* parent, const char* liveN, const char* total,
                          const RChip* chips, int nChips, const char* delta,
                          float heartbeat) {
  lv_obj_t* bar = rect(parent, 0, 0, SCR_W, 48, R_SURF2);
  rect(bar, 0, 47, SCR_W, 1, M_LINE, 30);

  // Zone A — the live organ.
  rect(bar, 14, 20, 9, 9, R_LIVE, LV_OPA_COVER, 5);
  txt(bar, 30, 19, "LIVE", R_LIVE_SD, F_MICRO, 2);
  // The count is the first bright thing on the panel — display size, baseline
  // shared with the label, not a superscript beside it.
  txt(bar, 72, 9, liveN, R_LIVE, F_DISPLAY);
  txt(bar, 94, 19, total, R_INK3, F_MICRO, 1);
  rect(bar, 130, 14, 1, 20, lv_color_hex(0x2A3646));

  // Zone B — chips, ranked, filled, live-underlined.
  int x = 146;
  for (int i = 0; i < nChips; i++) {
    const RChip& c = chips[i];
    const int w = 12 + 9 * (int)strlen(c.label) + 12;
    lv_obj_t* ch = rect(bar, x, 9, w, 30, c.sel ? R_EDGEHI : R_SURF1,
                        LV_OPA_COVER, 7);
    lv_obj_t* l = txt(ch, 0, 0, c.label, c.sel ? R_INK : R_INK3, F_MICRO, 1);
    lv_obj_center(l);
    if (c.live) rect(bar, x + 6, 37, w - 12, 2, R_LIVE, LV_OPA_COVER, 1);
    x += w + 8;
  }
  txt(bar, x + 2, 19, "+2", R_INK3, F_MICRO, 1);

  // Zone C — delta ledger + nav pills.
  if (delta) {
    lv_obj_t* d = rect(bar, 536, 12, 74, 24, R_SURF2, LV_OPA_COVER, 7);
    lv_obj_set_style_border_color(d, R_LIVE_SD, 0);
    lv_obj_set_style_border_width(d, 1, 0);
    lv_obj_set_style_border_opa(d, 160, 0);
    lv_obj_t* l = txt(d, 0, 0, delta, R_LIVE, F_MICRO, 1);
    lv_obj_center(l);
  } else {
    txt(bar, 536, 15, "11:37", R_INK2, F_ABBR, 0, LV_TEXT_ALIGN_RIGHT, 74);
  }
  const char* nav[3] = { "TABLE", "NEWS", "SETUP" };
  for (int i = 0; i < 3; i++) {
    const int nx = 622 + i * 58;
    lv_obj_t* pb = rect(bar, nx, 8, 54, 32, R_SURF1, LV_OPA_COVER, 8);
    lv_obj_set_style_border_color(pb, lv_color_hex(0x2A3646), 0);
    lv_obj_set_style_border_width(pb, 1, 0);
    lv_obj_t* l = txt(pb, 0, 0, nav[i], R_INK2, F_MICRO, 1);
    lv_obj_center(l);
    if (i == 1) rect(bar, nx + 46, 5, 7, 7, R_LIVE, LV_OPA_COVER, 4);
  }

  // The signature: the poll heartbeat.
  rect(parent, 0, 46, (int)(SCR_W * heartbeat), 2, R_LIVE_SD);
}

/** Small live tile, refresh anatomy: status in C_LIVE_SD, situation in
 *  C_WARN, dot, edge on the leading row. */
static void refreshTile(lv_obj_t* parent, int x, int y, int w, int h,
                        lv_color_t surf,
                        const char* a, uint32_t ca, const char* ra, const char* sa,
                        const char* hm, uint32_t ch, const char* rh, const char* sh,
                        bool homeLeads, bool live, const char* status,
                        const char* situation, const char* bcast,
                        bool isFinal, bool homeWon) {
  lv_obj_t* t = card(parent, x, y, w, h, surf, 12);
  const int rowH = (h - 22 - 21) / 2;
  if (live)
    rect(t, 0, 11 + (homeLeads ? rowH : 0) + 6, 4, rowH - 12,
         lv_color_hex(teamInk(homeLeads ? ch : ca)), LV_OPA_COVER, 2);

  const struct { const char* ab; uint32_t col; const char* rec; const char* sc; bool lead; } S[2] =
    { { a, ca, ra, sa, live ? !homeLeads : (isFinal && !homeWon) },
      { hm, ch, rh, sh, live ? homeLeads : (isFinal && homeWon) } };
  for (int k = 0; k < 2; k++) {
    const int ry = 11 + k * rowH;
    rect(t, 13, ry + 2, 26, 26, lv_color_hex(teamFill(S[k].col)), LV_OPA_COVER, 7);
    txt(t, 47, ry + 1, S[k].ab, R_INK, F_ABBR);
    txt(t, 47, ry + 20, S[k].rec, R_INK3, F_NUM);
    lv_color_t sc;
    if (live)        sc = S[k].lead ? lv_color_hex(teamInk(S[k].col)) : R_INK2;
    else if (isFinal) sc = S[k].lead ? R_INK : R_INK3;
    else             sc = R_INK2;
    txt(t, w - 13 - 60, ry - 3, S[k].sc, sc, F_SCORE, 0, LV_TEXT_ALIGN_RIGHT, 60);
  }
  const int by = h - 11 - 15;
  if (live) {
    rect(t, 13, by + 4, 6, 6, R_LIVE, LV_OPA_COVER, 3);
    txt(t, 25, by, status, R_LIVE_SD, F_NUM);
  } else {
    txt(t, 13, by, status, R_INK3, F_NUM);
  }
  if (situation) txt(t, w - 13 - 96, by, situation, R_WARN, F_NUM, 0, LV_TEXT_ALIGN_RIGHT, 96);
  else if (bcast) txt(t, w - 13 - 70, by, bcast, R_INK3, F_NUM, 0, LV_TEXT_ALIGN_RIGHT, 70);
}

/** mock 9 — the refreshed FEATURE board. */
static void refreshFeature(lv_obj_t* p) {
  static const RChip chips[5] = {
    { "ALL 3", true, false }, { "NHL 2", false, true }, { "NFL 1", false, true },
    { "MLB", false, false }, { "PGA", false, false } };
  refreshHeader(p, "3", "/ 9", chips, 5, "+2 NEW", 0.62f);

  // Hero — shipped geometry plus the spec's three additions.
  lv_obj_t* h = card(p, 16, 60, 508, 268, M_SURF_3, 16, 72);
  label_caps(h, 24, 16, "NHL", M_INK3);
  rect(h, 330, 22, 6, 6, R_LIVE, LV_OPA_COVER, 3);
  txt(h, 344, 12, "3RD 04:21", R_LIVE_SD, F_NUM, 0, LV_TEXT_ALIGN_RIGHT, 140);
  rect(h, 20, 40, 468, 1, M_LINE, OPA_HAIR);
  rect(h, 0, 128, 4, 52, lv_color_hex(teamInk(0x00205B)), LV_OPA_COVER, 2);
  const struct { const char* n; const char* sub; uint32_t col; const char* sc; bool lead; } HS[2] = {
    { "CANADIENS", "24-18-6", 0xAF1E2D, "2", false },
    { "MAPLE LEAFS", "31-14-4", 0x00205B, "3", true } };
  for (int k = 0; k < 2; k++) {
    const int y = 54 + k * 74;
    rect(h, 24, y, 48, 48, lv_color_hex(teamFill(HS[k].col)), LV_OPA_COVER, 11);
    txt(h, 88, y - 2, HS[k].n, M_INK, F_DISPLAY);
    txt(h, 88, y + 30, HS[k].sub, M_INK3, F_NUM);
    txt(h, 340, y - 12, HS[k].sc,
        HS[k].lead ? lv_color_hex(teamInk(HS[k].col)) : M_INK2,
        &font_hero72, 0, LV_TEXT_ALIGN_RIGHT, 144);
  }
  // Situation promoted to C_LIVE; broadcast demoted; the last play; the bar.
  rect(h, 24, 202, 6, 6, R_LIVE, LV_OPA_COVER, 3);
  txt(h, 38, 196, "POWER PLAY 1:12", R_LIVE, F_NUM);
  txt(h, 344, 196, "SPORTSNET", M_INK3, F_MICRO, 1, LV_TEXT_ALIGN_RIGHT, 140);
  txt(h, 24, 217, "3RD 04:21  MATTHEWS (24) PP", M_INK2, F_MICRO, 1);
  // Bar, then its two labels, both fully inside the card — the first pass put
  // the labels ON the bar and half over the card's bottom border.
  const int bw = 460, hw = (int)(bw * 0.71f);
  rect(h, 24, 237, bw - hw, 6, lv_color_hex(teamInk(0xAF1E2D)), LV_OPA_COVER, 2);
  rect(h, 24 + bw - hw, 237, hw, 6, lv_color_hex(teamInk(0x00205B)), LV_OPA_COVER, 2);
  rect(h, 24 + bw - hw - 1, 235, 2, 10, lv_color_hex(0xFFFFFF));
  txt(h, 24, 249, "MTL 29", M_INK3, F_MICRO, 1);
  txt(h, 344, 249, "TOR 71", M_INK3, F_MICRO, 1, LV_TEXT_ALIGN_RIGHT, 140);

  refreshTile(p, 536, 60, 248, 128, M_SURF_2,
              "KC", 0xE31837, "9-3", "17", "BUF", 0x00338D, "10-2", "20",
              true, true, "Q3 08:47", "RED ZONE", nullptr, false, false);
  refreshTile(p, 536, 200, 248, 128, M_SURF_2,
              "EDM", 0xFF4C00, "30-16-3", "1", "CGY", 0xC8102E, "24-22-5", "0",
              false, true, "1st 18:44", nullptr, "SN", false, false);

  // Ledger, bare plate, with the winner-ink change.
  label_caps(p, 24, 344, "NEXT UP", M_INK3);
  label_caps(p, 412, 344, "FINAL", M_INK3);
  rect(p, 24, 364, 348, 1, M_LINE, OPA_HAIR);
  rect(p, 412, 364, 348, 1, M_LINE, OPA_HAIR);
  const char* up[3][3] = { { "7:00", "BOS @ NYR", "ESPN" },
    { "8:10", "LAD @ SF", "MLB.TV" }, { "9:30", "VGK @ COL", "TNT" } };
  const struct { const char* a; const char* as; const char* h2; const char* hs;
                 bool homeWon; const char* st; } fin[3] = {
    { "VAN", "4", "SEA", "5", true,  "F/OT" },
    { "NYY", "6", "BOS", "2", false, "F" },
    { "DAL", "17", "PHI", "27", true, "F" } };
  for (int i = 0; i < 3; i++) {
    const int y = 380 + i * 30;
    txt(p, 24, y, up[i][0], M_INK3, F_NUM);
    txt(p, 98, y, up[i][1], M_INK2, F_NUM);
    txt(p, 232, y, up[i][2], M_INK3, F_NUM, 0, LV_TEXT_ALIGN_RIGHT, 140);
    txt(p, 412, y, fin[i].a, fin[i].homeWon ? R_INK3 : R_INK, F_NUM);
    txt(p, 464, y, fin[i].as, fin[i].homeWon ? R_INK3 : R_INK, F_NUM);
    txt(p, 508, y, fin[i].h2, fin[i].homeWon ? R_INK : R_INK3, F_NUM);
    txt(p, 560, y, fin[i].hs, fin[i].homeWon ? R_INK : R_INK3, F_NUM);
    txt(p, 620, y, fin[i].st, M_INK3, F_NUM, 0, LV_TEXT_ALIGN_RIGHT, 140);
  }
}

/** mock 10 — the refreshed Dense grid, 12 games, lead tile lit. */
static void refreshGrid(lv_obj_t* p) {
  static const RChip chips[5] = {
    { "ALL 7", true, false }, { "MLB 5", false, true }, { "NHL 2", false, true },
    { "PGA", false, false }, { "F1", false, false } };
  refreshHeader(p, "7", "/ 12", chips, 5, nullptr, 0.34f);

  const int tw = 186, th = 131, gut = 10, marg = 12, top = 56;
  struct GT { const char* a; uint32_t ca; const char* ra; const char* sa;
              const char* h; uint32_t ch; const char* rh; const char* sh;
              bool homeLeads; bool live; const char* st; const char* sit;
              const char* bc; bool fin; bool homeWon; };
  static const GT G[12] = {
    // Dense keeps the COMPACT situation vocabulary — "PP", not "PP 1:12".
    // The SVG mockup fits the long form only because its font metrics are
    // optimistic; at F_NUM's real 9.0 px advance the two labels collide,
    // which is precisely what SIT_FULL_W already encodes in the firmware.
    { "MTL",0xAF1E2D,"24-18-6","2","TOR",0x00205B,"31-14-4","3", true,true,"3rd 04:21","PP",nullptr,false,false },
    { "COL",0x333366,"46-72","0","ARI",0xA71930,"63-56","7", true,true,"Mid 6th",nullptr,"SN",false,false },
    { "MIL",0xFFC52F,"74-44","2","SD",0x2F241D,"62-57","3", true,true,"Bot 7th","LOADED",nullptr,false,false },
    { "TB",0x092C5C,"71-46","2","ATH",0x003831,"47-71","3", true,true,"Bot 7th","1 OUT",nullptr,false,false },
    { "HOU",0xEB6E1F,"60-59","2","SF",0xFD5A1E,"49-69","2", false,true,"Top 7th","2 OUT",nullptr,false,false },
    { "KC",0x004687,"49-70","3","LAD",0x005A9C,"70-48","2", false,true,"Top 5th","2 OUT",nullptr,false,false },
    { nullptr,0,nullptr,nullptr,nullptr,0,nullptr,nullptr,false,false,nullptr,nullptr,nullptr,false,false },
    { "BOS",0xBD3039,"64-54","1","TOR",0x134A8E,"57-63","2", true,true,"Bot 7th",nullptr,"SN",false,false },
    { nullptr,0,nullptr,nullptr,nullptr,0,nullptr,nullptr,false,false,nullptr,nullptr,nullptr,false,false },
    { "NYM",0xFF5910,"53-67","8","ATL",0xCE1141,"71-48","5", false,false,"Final","","SN",true,false },
    { "BAL",0xDF4601,"57-62","5","MIN",0x002B5C,"59-61","9", false,false,"Final","","SN",true,true },
    { "PHI",0xE81828,"64-56","6","STL",0xC41E3A,"59-60","5", false,false,"Final","","SN",true,false },
  };
  for (int i = 0; i < 12; i++) {
    const int x = marg + (i % 4) * (tw + gut), y = top + (i / 4) * (th + gut);
    const GT& g = G[i];
    if (!g.a) {                                 // field tiles
      lv_obj_t* t = card(p, x, y, tw, th, M_SURF_1, 12);
      if (i == 6) {
        txt(t, 13, 12, "Heineken Dutch GP", R_INK2, F_BODY);
        txt(t, 13, 34, "FP1", R_INK3, F_NUM);
        txt(t, 13, th - 26, "8/21 - 6:30 AM", R_INK3, F_NUM);
      } else {
        label_caps(t, 13, 12, "WYNDHAM CHAMP", R_INK3);
        const char* rows[3][2] = { { "M. Brennan", "-22" },
          { "B. Hossler", "-19" }, { "B. James", "-18" } };
        for (int r = 0; r < 3; r++) {
          txt(t, 13, 36 + r * 22, rows[r][0], R_INK2, F_BODY);
          txt(t, tw - 13 - 44, 36 + r * 22, rows[r][1], R_INK, F_NUM, 0,
              LV_TEXT_ALIGN_RIGHT, 44);
        }
      }
      continue;
    }
    // The lead tile: slot 0 gets the hero surface. Spec §3(c).
    refreshTile(p, x, y, tw, th, i == 0 ? M_SURF_3 : (g.fin ? M_SURF : M_SURF_2),
                g.a, g.ca, g.ra, g.sa, g.h, g.ch, g.rh, g.sh,
                g.homeLeads, g.live, g.st, g.sit, g.bc, g.fin, g.homeWon);
  }
}


/** mock 11 — the rail-open grid: 140 px rail, 3x3 at 204, new zone B pill. */
static void railGrid(lv_obj_t* p) {
  // Header: zone A unchanged; zone B is ONE filter pill; zone C unchanged.
  lv_obj_t* bar = rect(p, 0, 0, SCR_W, 48, R_SURF2);
  rect(bar, 0, 47, SCR_W, 1, M_LINE, 30);
  rect(bar, 14, 20, 9, 9, R_LIVE, LV_OPA_COVER, 5);
  txt(bar, 30, 19, "LIVE", R_LIVE_SD, F_MICRO, 2);
  txt(bar, 72, 9, "7", R_LIVE, F_DISPLAY);
  txt(bar, 94, 19, "/ 12", R_INK3, F_MICRO, 1);
  rect(bar, 130, 14, 1, 20, lv_color_hex(0x2A3646));
  {
    // 224 wide: 22 glyphs at F_MICRO's 7.8 px advance + tracking needs 216.
    lv_obj_t* pill = rect(bar, 146, 9, 224, 30, R_SURF1, LV_OPA_COVER, 7);
    lv_obj_t* l = txt(pill, 0, 0, "< ALL LEAGUES · 7 LIVE", R_INK2, F_MICRO, 1);
    lv_obj_center(l);
    rect(bar, 152, 37, 212, 2, R_LIVE_SD, LV_OPA_COVER, 1);
  }
  txt(bar, 536, 15, "11:37", R_INK2, F_ABBR, 0, LV_TEXT_ALIGN_RIGHT, 74);
  const char* nav[3] = { "TABLE", "NEWS", "SETUP" };
  for (int i = 0; i < 3; i++) {
    const int nx = 622 + i * 58;
    lv_obj_t* pb = rect(bar, nx, 8, 54, 32, R_SURF1, LV_OPA_COVER, 8);
    lv_obj_set_style_border_color(pb, lv_color_hex(0x2A3646), 0);
    lv_obj_set_style_border_width(pb, 1, 0);
    lv_obj_t* l = txt(pb, 0, 0, nav[i], R_INK2, F_MICRO, 1);
    lv_obj_center(l);
    if (i == 1) rect(bar, nx + 46, 5, 7, 7, R_LIVE, LV_OPA_COVER, 4);
  }
  rect(p, 0, 46, (int)(SCR_W * 0.34f), 2, R_LIVE_SD);

  // The rail, expanded: 140 px, league rows, EDIT footer.
  lv_obj_t* rail = rect(p, 0, 48, 140, SCR_H - 48, R_SURF1);
  rect(rail, 139, 0, 1, SCR_H - 48, M_LINE, 26);
  struct RL { const char* n; const char* c; bool live; bool sel; };
  static const RL rows[13] = {
    { "ALL", "7 /12", true, true },  { "MLB", "5 /5", true, false },
    { "NHL", "2 /2", true, false },  { "NBA", "0 /2", false, false },
    { "NFL", "0 /1", false, false }, { "PGA", "0 /1", false, false },
    { "F1",  "0 /1", false, false }, { "EPL", "-", false, false },
    { "MLS", "-", false, false },    { "UCL", "-", false, false },
    { "ATP", "-", false, false },    { "NCAAF", "-", false, false },
    { "WNBA", "-", false, false } };
  for (int i = 0; i < 13; i++) {
    const int y = 8 + i * 29;
    if (rows[i].sel) {
      rect(rail, 6, y - 3, 128, 26, R_EDGEHI, LV_OPA_COVER, 6);
      rect(rail, 0, y - 3, 3, 26, lv_color_hex(0xFFFFFF), LV_OPA_COVER, 1);
    }
    txt(rail, 14, y, rows[i].n, rows[i].sel ? R_INK : (rows[i].live ? R_INK2 : R_INK3), F_MICRO, 1);
    if (rows[i].c[0] != '-') {
      // Count in C_LIVE when live; denominator dim.
      txt(rail, 76, y, rows[i].c, rows[i].live ? R_LIVE : R_INK3, F_MICRO, 0,
          LV_TEXT_ALIGN_RIGHT, 54);
      if (rows[i].live && !rows[i].sel)
        rect(rail, 14, y + 15, 30, 2, R_LIVE, LV_OPA_COVER, 1);
    } else {
      txt(rail, 76, y, "-", lv_color_hex(0x4A5666), F_MICRO, 0, LV_TEXT_ALIGN_RIGHT, 54);
    }
  }
  rect(rail, 8, SCR_H - 48 - 34, 124, 1, M_LINE, OPA_HAIR);
  txt(rail, 14, SCR_H - 48 - 26, "EDIT SPORTS   >", R_INK3, F_MICRO, 1);

  // 3x3 grid at 204 per the resize formula: (632 - 20) / 3.
  const int tw = 204, th = 131, gut = 10, mx = 156, top = 56;
  refreshTile(p, mx, top, tw, th, M_SURF_3,
              "MTL", 0xAF1E2D, "24-18-6", "2", "TOR", 0x00205B, "31-14-4", "3",
              true, true, "3rd 04:21", "PP 1:12", nullptr, false, false);
  refreshTile(p, mx + (tw + gut), top, tw, th, M_SURF_2,
              "COL", 0x333366, "46-72", "0", "ARI", 0xA71930, "63-56", "7",
              true, true, "Mid 6th", nullptr, "SN", false, false);
  refreshTile(p, mx + 2 * (tw + gut), top, tw, th, M_SURF_2,
              "MIL", 0xFFC52F, "74-44", "2", "SD", 0x2F241D, "62-57", "3",
              true, true, "Bot 7th", "LOADED", nullptr, false, false);
  refreshTile(p, mx, top + (th + gut), tw, th, M_SURF_2,
              "TB", 0x092C5C, "71-46", "2", "ATH", 0x003831, "47-71", "3",
              true, true, "Bot 7th", "1 OUT", nullptr, false, false);
  refreshTile(p, mx + (tw + gut), top + (th + gut), tw, th, M_SURF_2,
              "HOU", 0xEB6E1F, "60-59", "2", "SF", 0xFD5A1E, "49-69", "2",
              false, true, "Top 7th", "2 OUT", nullptr, false, false);
  refreshTile(p, mx + 2 * (tw + gut), top + (th + gut), tw, th, M_SURF_2,
              "KC", 0x004687, "49-70", "3", "LAD", 0x005A9C, "70-48", "2",
              false, true, "Top 5th", "2 OUT", nullptr, false, false);
  refreshTile(p, mx, top + 2 * (th + gut), tw, th, M_SURF_2,
              "BOS", 0xBD3039, "64-54", "1", "TOR", 0x134A8E, "57-63", "2",
              true, true, "Bot 7th", nullptr, "SN", false, false);
  refreshTile(p, mx + (tw + gut), top + 2 * (th + gut), tw, th, M_SURF,
              "NYM", 0xFF5910, "53-67", "8", "ATL", 0xCE1141, "71-48", "5",
              false, false, "Final", nullptr, "SN", true, false);

  // Slot 9: the collapsed-state explainer, as the designer's inset.
  {
    lv_obj_t* c = card(p, mx + 2 * (tw + gut), top + 2 * (th + gut), tw, th,
                       lv_color_hex(0x0B111B), 12, 30);
    label_caps(c, 12, 10, "COLLAPSED - 16PX", R_INK3);
    // A sample sliver: proportional spine.
    rect(c, 12, 30, 8, 40, R_LIVE, LV_OPA_COVER, 2);        // MLB, 5 live
    rect(c, 12, 74, 8, 16, R_LIVE, LV_OPA_COVER, 2);        // NHL, 2 live
    rect(c, 12, 94, 8, 8, R_EDGEHI, LV_OPA_COVER, 2);       // rest, none live
    rect(c, 22, 30, 3, 12, lv_color_hex(0xFFFFFF), LV_OPA_COVER, 1);  // selected tab
    txt(c, 34, 32, "MLB 5 LIVE", R_INK3, F_MICRO, 1);
    txt(c, 34, 52, "NHL 2 LIVE", R_INK3, F_MICRO, 1);
    txt(c, 34, 72, "4 MORE IDLE", R_INK3, F_MICRO, 1);
    txt(c, 12, th - 26, "TAP IT, OR THE PILL", R_INK3, F_MICRO, 1);
  }
}

// ── entry ──────────────────────────────────────────────────────────────────
void mockupApply(int n) {
  lv_obj_t* scr = lv_scr_act();
  if (s_root) { lv_obj_del(s_root); s_root = nullptr; }
  s_root = rect(scr, 0, 0, SCR_W, SCR_H, M_PLATE);

  switch (n) {
    case 0: mockBoard(s_root, false); break;
    case 4: mockBoard(s_root, true);  break;
    case 5: spikeAurora(s_root); break;
    case 6: spikeSolari(s_root); break;
    case 7: spikeArcs(s_root);   break;
    case 8: spikePaper(s_root);  break;
    case 9: refreshFeature(s_root); break;
    case 10: refreshGrid(s_root);   break;
    case 11: railGrid(s_root);      break;
    case 1: mockIdle(s_root);   break;
    case 2: mockBusy(s_root);   break;
    default: mockTokens(s_root); break;
  }
  lv_obj_move_foreground(s_root);
}

const char* mockupName(int n) {
  switch (n) {
    case 0:  return "proposed board A — ledger as a card";
    case 4:  return "proposed board B — ledger on bare plate";
    case 5:  return "SPIKE: team-colour dithered falloff";
    case 6:  return "SPIKE: Solari card hairlines";
    case 7:  return "SPIKE: lv_arc gauge quality";
    case 8:  return "SPIKE: paper light-ground dither";
    case 9:  return "refresh — FEATURE board, new header";
    case 10: return "refresh — dense grid, new header";
    case 11: return "refresh — rail open, 3x3 at 204";
    case 1:  return "proposed idle — clock as the hero";
    case 2:  return "proposed board — busy night, grid unchanged";
    default: return "tokens — surfaces, accent, teamInk";
  }
}
