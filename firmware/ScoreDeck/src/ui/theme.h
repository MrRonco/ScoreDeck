// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// theme.h — palette and font tokens.
//
// ScoreDeck has NO accent colour. ~3,000 team colours arrive as content, so the
// chrome stays strictly neutral and every saturated pixel belongs to a team.
// Luminance encodes state; colour encodes team. UI.md §2.
#pragma once
#include <lvgl.h>

#define C_VOID    lv_color_hex(0x05070C)
#define C_PLATE   lv_color_hex(0x0A0F18)
#define C_FROST   lv_color_hex(0x1A2432)
#define C_FROST_2 lv_color_hex(0x141C28)
#define C_INK     lv_color_hex(0xF3F7FB)
#define C_INK2    lv_color_hex(0x93A5B8)
#define C_INK3    lv_color_hex(0x5D6D7E)
#define C_EDGE    lv_color_hex(0x2A3646)
#define C_EDGE_HI lv_color_hex(0x46566A)

// ── STATE INK — the state channel (UI.md §2) ───────────────────────────────
//
// This used to be three opacity values applied to the whole tile. That fades
// the frost and the text toward the plate together, which is fine for the top
// tier and fatal for the bottom one: a final's record and broadcast landed at
// 1.73:1 and vanished, so a finished game read as a tile that had failed to
// load. It also forced every tile's subtree through a 63 KB composite buffer,
// which quietly defeated the change-caching the board works hard for.
//
// Instead each state names its own colours. The perceptual ladder survives —
// score luminance still steps down — but nothing drops below ~3.4:1.
struct StateInk {
  lv_color_t plate;   // tile fill
  lv_color_t edge;    // tile border
  lv_color_t ink;     // leading score, team name
  lv_color_t ink2;    // trailing score, status
  lv_color_t ink3;    // record, broadcast
};
extern const StateInk kStateInk[3];   // GS_PRE, GS_LIVE, GS_FINAL — matches GameState

/** The rule that keeps ~3,000 team colours legible without a per-team table.
 *
 *  Saturates the colour first (pure value lift, hue exact) and only whitens
 *  the residual, stopping the moment it clears `minRatio` against the tile —
 *  so a colour that is already bright enough is returned untouched. Stating
 *  the threshold as a contrast ratio rather than a brightness cutoff matters:
 *  a brightness rule calibrated on Toronto navy also lifted Kansas City and
 *  Edmonton, washing out colours that were already fine.
 */
uint32_t teamInk(uint32_t color);

/** Badge fill: lifted just enough to sit on the plate at all (black teams). */
uint32_t teamFill(uint32_t color);

/** White or plate ink, whichever survives on that fill. Seattle's #99D9D9
 *  carried white text at 1.58:1; this takes it to 12.1:1. */
lv_color_t badgeInk(uint32_t fill);

// ── FONT COVERAGE — read before choosing a face ────────────────────────────
//
// These are generated with the narrowest glyph range each job needs, which is
// why the whole set costs less flash than LVGL's built-in Montserrat. The cost
// is that picking the wrong face renders hollow boxes, silently. Three separate
// bugs have been exactly this.
//
//   F_SCORE   digits, '-', ':' only          NEVER for text
//   F_DISPLAY CAPS + digits + ' and .        large headline text, no lowercase
//   F_ABBR    CAPS + digits, no lowercase    team abbreviations ONLY
//   F_BODY    0x20-7E + Latin-1 + Ext-A      anything with a person or place
//   F_NUM     0x20-7E ASCII, mono 15px       DATA you read: clocks, cells, stats
//   F_MICRO   0x20-7E ASCII, mono 13px       chrome LABELS only: SCORING, headers
//
// F_NUM and F_MICRO differ only in size, and the split is deliberate. F_MICRO
// was an 11px tooltip face carrying the game clock, team records, every
// standings cell and every lineup stat value — primary data at half the size a
// desk viewer wants. Labels stay small; data moved up.
//
// Rule of thumb: if the string can come from upstream, it needs F_BODY. If it
// contains a letter at all, it is not F_SCORE — that face has no letters, and
// LVGL renders a missing glyph as a hollow box without warning. Five shipped
// labels were exactly this bug; `make lint` in desktop/ is the guard.
extern const lv_font_t* F_SCORE;      // tabular, Standard/Dense density
extern const lv_font_t* F_SCORE_BIG;  // tabular, Roomy density
extern const lv_font_t* F_DISPLAY;    // the alert verb, and nothing smaller
extern const lv_font_t* F_ABBR;
extern const lv_font_t* F_BODY;
extern const lv_font_t* F_NUM;
extern const lv_font_t* F_MICRO;

void themeInit();

/** Frosted panel style — the baked-glass primitive. */
lv_obj_t* glassPanel(lv_obj_t* parent, int x, int y, int w, int h, int radius);

/** Team colour glyph used when no logo blob is present (always, for now).
 *  Fill and label ink are both normalised — see teamFill()/badgeInk(). */
lv_obj_t* teamBadge(lv_obj_t* parent, const char* abbr, uint32_t color, int size);

/** Recolour an existing badge. ALWAYS use this rather than setting bg_color
 *  directly, or the label ink stops matching the fill. */
void teamBadgeSet(lv_obj_t* badge, uint32_t color);
