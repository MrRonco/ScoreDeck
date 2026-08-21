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
// INK_2/INK_3 are kStateInk[SI_HERO].ink2/.ink3 — solved on the LIGHTEST
// surface, so they clear AA on every darker one by construction. The palette
// held eleven greys inside a 25 L* band (mean spacing 2.5 L* — invisible at
// 610 mm, visibly inconsistent up close); it now holds six: the three primary
// tiers, these two, and final's own ink2 (the one exception — the global INK_2
// sits 2.9 L* ABOVE final's deliberately dim primary, which would merge
// final's first and second tiers; see kStateInk).
#define C_INK2    lv_color_hex(0xACBCCE)
#define C_INK3    lv_color_hex(0x8696A8)
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
// A_LIVE is C_LIVE under the name the token system uses. The SIGNAL cell is
// defined by rendered lightness (L* >= 76) and chroma (>= 30) — that pair is
// what separates "the system is talking" from team colour, which is clamped
// below it. See PULSE_RUNGS: the pulse is FIVE SOLID COLOURS, never an
// opacity ramp, because lv_color_mix quantises opa to (opa+4)>>3 (26 levels)
// and blends toward the surface, losing chroma as well as lightness — which
// is precisely why the pulsing dot and the flat rail bar did not read as the
// same colour. Every rung is pre-solved to stay >= 7.39:1 on every surface.
#define A_LIVE    C_LIVE
#define A_LIVE_P0 lv_color_hex(0x31D3B5)   // L* 76.4  pulse trough
#define A_LIVE_P1 lv_color_hex(0x31D7B5)   // L* 77.6
#define A_LIVE_P2 lv_color_hex(0x39DBBD)   // L* 79.2
#define A_LIVE_P3 lv_color_hex(0x39DFBD)   // L* 80.4
#define A_LIVE_P4 lv_color_hex(0x3BE0C0)   // L* 81.8  peak (== A_LIVE)
// C_LIVE_SD (#2A9E8C) IS GONE. It measured 4.63:1 on the live tile — clearing
// AA by 2.9%, the definition of marginal — and 4.15:1 on the hero, which fails
// outright; and at L* 58.8 / chroma 36 it read as a TEAM colour rather than a
// signal. Its five sites were routed by meaning, not by hue: the sports meter's
// "in use" ticks became INK_3 because they are structure, the league pill's
// border became EDGE_HI because it is a border, and the one site that genuinely
// meant "live" took C_LIVE_TX below. PLAN item 1.8.
//
// The BODY-TEXT member of the live family, solved rather than picked. This is
// the first point on the A_LIVE hue ray clearing 5.5:1 on every surface it can
// sit on: 6.15 (live), 5.52 (hero), 8.13 (plate). Use it for a live-state
// SENTENCE OR VALUE — a running clock, a "3 LIVE NOW" count — where a dot would
// be wrong and the flat accent would be too loud. PLAN 1.8 proposed deleting
// this too, on the grounds that it had zero style writes; it is kept instead
// and given the two uses it was solved for, because a solved token with a
// declared job is worth more than one fewer #define.
#define C_LIVE_TX lv_color_hex(0x30B89D)
// S_ALERT means SOMETHING DEMANDS ATTENTION RIGHT NOW, and it is rationed so
// that it can. Two uses, both rare by construction:
//   * the system is not okay — no Wi-Fi, no proxy, stale feed, league cap;
//   * a live game carries structured leverage — uiIsTense() only: a power
//     play, the red zone, scoring position with two out.
// It used to sit on EVERY situation string, which measured 15.1% of the
// board's salient chroma across two of every three live tiles. A colour that
// is always present cannot interrupt, which is why the base diamonds read as
// decoration rather than as a warning.
#define S_ALERT   lv_color_hex(0xF2B441)
#define C_WARN    S_ALERT                  // legacy spelling, same colour

// One line colour at several opacities, rather than several near-identical
// greys. Hairlines, borders and the specular catch are all this hue.
#define C_LINE    lv_color_hex(0xB4CDE6)
#define OPA_HAIR   24
#define OPA_EDGE   40
#define OPA_SPEC  120

// The full-screen dismissal ground behind a sheet or an alert. Two files had
// this as the same bare literal, one at opa 150 and one at 158; it is the
// plate with the blue pulled out, so it darkens what is behind without
// tinting it. Not C_PLATE: the scrim is drawn OVER the plate and must read as
// a step down from it.
#define C_SCRIM   lv_color_hex(0x04070C)

// ── RADII — one family, five rungs, 4 px apart ─────────────────────────────
//
// Twelve ad-hoc radii shipped (0/1/2/3/4/5/6/7/8/10/12/16); two controls on
// the same bar carried 7 and 8 — a 1 px difference nobody chose. Concentricity
// rule: a child inset d from a parent of radius R takes max(R_XS, R - d).
#define R_XS   2   // edge lights, bars, sliver segments, underlines, tabs
#define R_SM   6   // team badges, logo chips
#define R_MD  10   // controls: nav pills, filter pill, zone C, rail rows, toast
#define R_LG  14   // grid tiles, hero team badge
#define R_XL  18   // the hero card

// The return affordance's one geometry. 16 px is the frame's own horizontal
// inset, which is where a back chip belongs and where only one of the six
// sites had it; 32 px clears the 44 px minimum target with the 12 px of bar
// padding around it, and BACK_W is a MINIMUM — see backChip().
#define BACK_X 16
#define BACK_H 32
#define BACK_W 48

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
  // The bottom shade of glassPanel()'s specular pair — the card's contact
  // shadow, and the panel's ONLY elevation channel (§4 item 3: the surface
  // ladder is full, so a modal cannot have a sixth fill).
  //
  // It was `lv_color_black()` at opa 90, i.e. a MULTIPLICATIVE black, and a
  // multiplicative shade scales with the fill it sits under — so it carries
  // no information of its own. Measured off the rendered board: pre (fill
  // #102029) and live (fill #182431) both produced a bit-identical #081418,
  // and final's #080C10 is one of the two values the dithered plate itself
  // renders. Three of the four states, one strip.
  //
  // Solved instead, and solved as a SHADOW: one ground, one darkness. All
  // four land at L* 4.13 +/- 0.30 (was 3.18-5.58, a 2.40 L* spread) and no
  // two share a hex, because each keeps its own surface's chromaticity —
  // final the flattest, hero the bluest. What varies is the CONTRAST between
  // a card and its own shadow, which is what elevation is.
  lv_color_t shade;
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

// The team channel's one ratio and one ceiling. See teamInkFor().
#define TEAM_RATIO 5.5f
#define TEAM_CEIL  68.0f
// The ceiling for a badge FILL, which is a different job from a team INK: a
// fill carries no text contrast of its own (badgeInk() solves the label on top
// of it), so it keeps more of its colour. 74 still sits 2.4 L* below the
// dimmest signal rung, which is what stops a bright kit becoming the brightest
// chromatic object on the panel.
#define TEAM_CEIL_FILL 74.0f

/** The ONLY way team colour should reach the screen. Lifts to TEAM_RATIO
 *  against the surface it will actually be drawn on, then clamps lightness to
 *  TEAM_CEIL so the team channel cannot climb into the signal colours' band.
 *  Use this instead of teamInkOn() at every render site; teamInkOn remains the
 *  primitive (it is also what teamFill()/badgeInk() are built on). */
uint32_t teamInkFor(uint32_t color, uint32_t surface);

/** Badge fill: lifted just enough to sit on the plate at all (black teams). */
uint32_t teamFill(uint32_t color);

// ── LOGO CHIPS — the ground a mark is drawn on ─────────────────────────────
//
// Logos were the only coloured element in this UI that bypassed normalisation
// entirely. teamInk() and teamFill() carefully lift a team's colour; a real
// logo bitmap was then blitted raw. Measured across the 62 shipped blobs
// against the live tile fill: a mean of 36.3% of each mark's own ink falls
// below 1.5:1, 44 of 62 lose a fifth or more, and 4 render fully invisible.
//
// The obvious fix — "is this logo dark? give it a plate" — is WRONG, and the
// measurement is what says so. Ink loss barely correlates with mean
// luminance, because the loss lives in the black keylines, spokes and
// outlines INSIDE the artwork rather than in its average:
//
//   nhl:BOS   mean 4.27:1   loses 40.6% of its ink
//   nhl:PIT   mean 4.40:1   loses 26.1%
//   mlb:SF    mean 4.88:1   loses  0.0%
//
// A luminance gate fixes about five teams and misses forty. So the feature is
// not the mark's brightness, it is the fraction of the mark's ink lost against
// a candidate ground — and the ground is the only variable that acts on every
// pixel of the mark equally.
//
// chipSolve() searches candidate grounds and returns the one that minimises
// that loss. Across the shipped set this takes mean ink lost from 36.3% to
// 3.6%, and one third of marks solve to "no chip at all" — they are already
// correct on the plate and get nothing.
struct LogoChip {
  uint32_t color;   // the ground to draw behind the mark
  uint8_t  opa;     // 0 = no chip; draw the mark straight on the tile
};

/** Solve the best ground for one decoded RGB565+A8 mark.
 *
 *  ~2,300 integer ops over the pixels to build a 64-bin histogram, then 33
 *  candidates evaluated against the BINS rather than the pixels. Call once per
 *  logo at decode time, off the render path — never per draw. */
LogoChip chipSolve(const uint8_t* rgb565a8, int w, int h, uint32_t against);

/** White or plate ink, whichever survives on that fill. Seattle's #99D9D9
 *  carried white text at 1.58:1; this takes it to 12.1:1. */
lv_color_t badgeInk(uint32_t fill);

/** Truncate a badge label to what a chip of `size` px can hold without the
 *  rounded rect cutting glyphs mid-stroke. F_MICRO is 7.8 px/glyph mono;
 *  no ellipsis — two clean letters beat one letter and dots. */
void badgeLabelFit(char* dst, size_t cap, const char* src, int size);

// ── FONT COVERAGE — read before choosing a face ────────────────────────────
//
// These are generated with the narrowest glyph range each job needs, which is
// why each is small. (The claim that the whole set costs less than LVGL's own
// Montserrat was wrong by a factor of four: measured from the map, the custom
// faces are 56,880 B against Montserrat 14's 13,633 B. What is true is that
// each face carries only the glyphs its job needs.) The cost
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
extern const lv_font_t* F_TITLE;     // 20 px cond bold CAPS+digits — tile team names
extern const lv_font_t* F_BODY;
extern const lv_font_t* F_NUM;
extern const lv_font_t* F_MICRO;

void themeInit();

/** The primary action: A_LIVE fill, badgeInk()-solved label, R_MD. The only
 *  button on the panel that carries the accent — see the definition. */
void uiPrimaryButton(lv_obj_t* b);

/** Draw an lv_keyboard from the token system. Left to lv_theme_default its
 *  keys render #282B30, which measures 1.04:1 on C_FROST_2. */
void keyboardTheme(lv_obj_t* kb);

/** THE declaration that an object is a control: hit-testable, and wearing the
 *  ONE pressed treatment — a 2 px C_LIVE outline, no fill change.
 *
 *  Press was a fill (#243040) that measured +11 L* on a final tile and +0.85
 *  on the hero surface — feedback that varied 28x and vanished exactly on the
 *  lightest (most promoted) tiles. A border is state-independent, identical
 *  everywhere, and gives the accent its declared second meaning: a teal
 *  outline appears ONLY under a finger.
 *
 *  It sets LV_OBJ_FLAG_CLICKABLE too, because the two were separable and
 *  therefore separated: the panel shipped 20 glass objects that were
 *  hit-testable with a press style and no handler, and 8 objects with a
 *  handler and no press style. One call now says both. Idempotent — a glass
 *  panel already carries the style, and a second copy would cost a style-list
 *  entry and a lookup per draw for nothing. */
void uiPressable(lv_obj_t* o);

/** Take an lv_btn into the panel's language: drop lv_theme_default's OWN
 *  pressed style, then adopt uiPressable()'s.
 *
 *  lv_conf.h:101 leaves LV_USE_THEME_DEFAULT on, so every lv_btn_create is
 *  born with a fourth, undocumented press language — a 20% darken filter
 *  applied to fill, border and label, i.e. exactly the fill-press this file
 *  says was replaced. Bolting uiPressable() on top gives a button BOTH.
 *
 *  Call it straight after set_size/set_pos and before any colour: in LVGL v8
 *  those two write LV_STYLE_WIDTH/HEIGHT/X/Y as LOCAL styles, so the reset
 *  would take the button's geometry with it — this preserves them across it,
 *  which is the bug uiPrimaryButton() hit first. */
void uiButton(lv_obj_t* b);

/** The ONE return affordance.
 *
 *  It shipped six times at four sizes (54x44, 48x34 three times, 96x32,
 *  116x36), three radii (8, 9, R_MD) and three x-origins (14, 16, 632), and
 *  four of the six also carried lv_theme_default's darken on top of nothing
 *  else. Height, radius, ink, fill, press and x are the helper's; the WORD is
 *  the caller's, and the chip is content-sized around it exactly as the
 *  filter pill is — one button, not one width.
 *
 *  `word` may be null for the bare chevron. `y` defaults to vertically
 *  centred, which is right for a bar and wrong for a pane, so a pane passes
 *  its own. */
lv_obj_t* backChip(lv_obj_t* parent, const char* word, lv_event_cb_t cb,
                   lv_coord_t y = -1);

/** A whole-region input surface — a scrim, or the reader's page-turn band —
 *  made clickable and deliberately left WITHOUT the press outline.
 *
 *  This is the one exemption from uiPressable() and it has to be named rather
 *  than implied, because "clickable with no press treatment" is otherwise the
 *  exact defect phase 21 removes. The distinction is size and role: the
 *  outline is a 2 px border on the object it belongs to, and a 2 px border
 *  around 800x432 is not feedback, it is a frame. These surfaces say what
 *  they did by CHANGING THE PAGE, which is faster than any highlight. */
void uiTapZone(lv_obj_t* o);

/** The dismissal ground under a sheet: C_SCRIM at `opa`, then uiTapZone(). */
void uiScrim(lv_obj_t* o, lv_opa_t opa);

/** Frosted panel style — the baked-glass primitive. A SURFACE, not a control:
 *  it comes back inert (see glassPanel's definition) and uiPressable() is
 *  what promotes one. */
lv_obj_t* glassPanel(lv_obj_t* parent, int x, int y, int w, int h, int radius);

/** Repaint a glass panel's contact shadow for a fill it did not start with.
 *  Anything that writes bg_color onto a glassPanel() MUST call this with the
 *  matching StateInk, or the card and its own shadow disagree. */
void glassSetFill(lv_obj_t* panel, const StateInk& si);

/** Team colour glyph used when no logo blob is present (always, for now).
 *  Fill and label ink are both normalised — see teamFill()/badgeInk(). */
lv_obj_t* teamBadge(lv_obj_t* parent, const char* abbr, uint32_t color, int size);

/** Recolour an existing badge. ALWAYS use this rather than setting bg_color
 *  directly, or the label ink stops matching the fill. */
void teamBadgeSet(lv_obj_t* badge, uint32_t color);
