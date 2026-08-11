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
#include <cstdio>
#include <cstring>
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

// ── entry ──────────────────────────────────────────────────────────────────
void mockupApply(int n) {
  lv_obj_t* scr = lv_scr_act();
  if (s_root) { lv_obj_del(s_root); s_root = nullptr; }
  s_root = rect(scr, 0, 0, SCR_W, SCR_H, M_PLATE);

  switch (n) {
    case 0: mockBoard(s_root, false); break;
    case 4: mockBoard(s_root, true);  break;
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
    case 1:  return "proposed idle — clock as the hero";
    case 2:  return "proposed board — busy night, grid unchanged";
    default: return "tokens — surfaces, accent, teamInk";
  }
}
