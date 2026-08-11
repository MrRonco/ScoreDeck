// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// theme.h — palette and font tokens.
//
// Colour encodes team; luminance encodes state. UI.md §2.
#pragma once
#include <lvgl.h>

// ── SURFACES — a ladder that actually climbs ───────────────────────────────
//
// The previous set was not a ladder. Measured in CIE L*, it read:
//
//   plate 4.26   final 6.16   pre 10.54   LIVE 9.80   hero 13.89
//
// A live game's tile was DARKER than a scheduled one — the state channel ran
// backwards at exactly the step that matters most, and the plate sat only
// 1.9 L* below the dimmest card. That is why the board measured 73.7% mid-grey
// with a 16.4% ground: the plate was not a background, it was 12 px of grout
// between cards, so nothing read as an object and nothing read as emissive.
//
// The ladder below is monotonic and evenly spaced:
//
//   plate 1.89   final 8.10   pre 11.96   live 14.87   hero 18.65
//
// The plate drops to near-black (L* 4.26 -> 1.89) so that every surface above
// it is legible AS a surface. Do not close these gaps up again.
#define C_PLATE   lv_color_hex(0x04070E)   // the ground; everything sits ON it
#define C_SURF    lv_color_hex(0x101825)   // final
#define C_SURF_1  lv_color_hex(0x16202E)   // scheduled
#define C_SURF_2  lv_color_hex(0x1B2636)   // live, top bar — the default glass
#define C_SURF_3  lv_color_hex(0x222E40)   // hero cell
#define C_FROST   C_SURF_2
#define C_FROST_2 lv_color_hex(0x141C28)   // INSET fields: text areas, keyboard

// Ink. Every tier clears WCAG AA (4.5:1) against every surface it is used on —
// see kStateInk, which varies each tier per surface to hold that floor. These
// globals are the values for chrome sitting on the plate itself.
#define C_INK     lv_color_hex(0xF3F7FB)
#define C_INK2    lv_color_hex(0xA6B6C8)
#define C_INK3    lv_color_hex(0x7A8899)   // was 0x5D6D7E — 2.58:1 on the hero
#define C_EDGE    lv_color_hex(0x2A3646)
#define C_EDGE_HI lv_color_hex(0x46566A)

// ── THE ONE ACCENT ─────────────────────────────────────────────────────────
//
// This file used to open by declaring that ScoreDeck has NO accent colour, on
// the reasoning that ~3,000 team colours arrive as content so the chrome must
// stay neutral. The reasoning was sound and the conclusion was wrong: with no
// accent, "live" and "touchable" had nothing to say them except luminance, and
// luminance was already fully spent encoding state.
//
// So there is exactly ONE accent, with exactly one meaning — "happening now,
// or touch this" — and nothing else in the UI may use it.
//
// What keeps it separable is LUMINANCE, not hue. The first draft of this
// comment claimed the hue was chosen for a band team kits do not occupy; that
// was measured and it is false. Teal is a CROWDED band in sport: of twelve
// sampled kits, seven land within 25 degrees of this hue once teamInk() has
// lifted them — Philadelphia, San Jose, Miami, Charlotte, Jacksonville, the
// Jets and Minnesota among them.
//
// The real guarantee is that teamInk() stops the moment it clears 3.5:1, so
// EVERY lifted team colour sits at almost exactly that ratio against its tile,
// while this accent sits at 9.16:1 — 2.6x brighter. Philadelphia's midnight
// green renders #00828C beside a #3BE0C0 dot: same hue family, obviously not
// the same thing. That margin is the invariant. If teamInk()'s target ratio
// ever rises, or this colour is ever darkened, re-check it.
#define C_LIVE    lv_color_hex(0x3BE0C0)
#define C_LIVE_SD lv_color_hex(0x2A9E8C)   // the same hue, for LABELS not dots
#define C_WARN    lv_color_hex(0xF2B441)   // stale — never used for team data

// One line colour at several opacities, rather than several near-identical
// greys. Hairlines, borders and the specular catch are all this hue.
#define C_LINE    lv_color_hex(0xB4CDE6)
#define OPA_HAIR   20
#define OPA_EDGE   46
#define OPA_SPEC  120

// ── STATE INK — the state channel (UI.md §2) ───────────────────────────────
//
// This used to be three opacity values applied to the whole tile. That fades
// the frost and the text toward the plate together, which is fine for the top
// tier and fatal for the bottom one: a final's record and broadcast landed at
// 1.73:1 and vanished, so a finished game read as a tile that had failed to
// load. It also forced every tile's subtree through a 63 KB composite buffer,
// which quietly defeated the change-caching the board works hard for.
//
// Instead each state names its own colours, and each tier is solved SEPARATELY
// against its own surface so the floor holds everywhere. Nothing in the table
// now drops below 4.52:1 — the WCAG AA threshold for normal text, up from the
// 2.58:1 the tertiary ink was actually sitting at on the lightest surface.
//
// This is the whole reason the table exists: a single global ink3 cannot clear
// AA on both the darkest and lightest surface without being too bright for one
// of them. Solve per surface, keep the perceptual ladder, keep the floor.
struct StateInk {
  lv_color_t plate;   // tile fill
  lv_color_t edge;    // tile border
  lv_color_t ink;     // leading score, team name
  lv_color_t ink2;    // trailing score, status
  lv_color_t ink3;    // record, broadcast
  // The same colour as `plate`, kept as a 24-bit value. teamInkOn() needs the
  // true sRGB triple to solve a contrast ratio, and lv_color_t is RGB565 on
  // this panel — round-tripping it back through lv_color_to32() would feed the
  // solver a colour that is up to 4 levels off per channel.
  uint32_t   fill;
};
// Indices 0..2 MUST match GameState (GS_PRE, GS_LIVE, GS_FINAL). SI_HERO is
// not a game state — it is the hero cell's surface, which is lighter than any
// of them and therefore needs its own solved inks.
#define SI_HERO 3
extern const StateInk kStateInk[4];

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

/** teamInk() against a surface other than the default live tile. The hero cell
 *  is the lighter C_SURF_3, so a colour lifted to clear 3.5:1 on a live tile
 *  only reaches ~3.1:1 there. Pass the surface it will actually be drawn on. */
uint32_t teamInkOn(uint32_t color, uint32_t surface, float minRatio = 3.5f);

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
//   F_HERO    digits, '-', ':', 'H', 'M'     the hero score / countdown ONLY
//   F_CLOCK   digits and ':' only            the idle clock ONLY
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
extern const lv_font_t* F_HERO;       // 72 px, 2 bpp — hero score, countdown
extern const lv_font_t* F_CLOCK;      // 96 px, 2 bpp — idle clock
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
