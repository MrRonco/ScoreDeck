// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include "../core/types.h"

enum Screen : uint8_t { SCR_BOARD = 0, SCR_IDLE, SCR_GAME, SCR_STANDINGS, SCR_NEWS, SCR_LINEUP, SCR_SETUP, SCR_SETTINGS };

// ── change caches ──────────────────────────────────────────────────────────
// The one repaint rule on this panel: never write a property that already
// holds the value. LVGL invalidates on the WRITE, not on the change —
// lv_label_set_text re-lays the run and invalidates for a byte-identical
// string, and add/clear_flag(HIDDEN) invalidates the object's whole area
// whether or not the flag moved.
//
// These three were file-statics in ui_board.cpp, and that is the WHOLE reason
// the other two rebuilding screens grew the defects Phase 19 fixes: the idle
// screen cleared the NEXT UP card's hidden flag unconditionally every tick —
// 71,446 px once a second, 143% of pulse.cpp:16's own per-tick ceiling, on the
// screen the panel shows for twenty hours a day — and the ledger rewrote all
// three cards per poll. The discipline existed and was unreachable.
static inline void setTextCached(lv_obj_t* o, char* cache, size_t cap, const char* v) {
  if (strncmp(cache, v, cap - 1) == 0) return;
  strncpy(cache, v, cap - 1);
  cache[cap - 1] = '\0';
  lv_label_set_text(o, cache);
}
static inline void setNumCached(lv_obj_t* o, int32_t* cache, int32_t v) {
  if (*cache == v) return;
  *cache = v;
  char b[8];
  snprintf(b, sizeof b, "%ld", (long)v);
  lv_label_set_text(o, b);
}
static inline void setHiddenCached(lv_obj_t* o, bool* cache, bool hide) {
  if (!o) return;                // never panic on a tile that was not built
  if (*cache == !hide) return;   // cache stores "visible"
  *cache = !hide;
  if (hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  else      lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/** Generate the background plate into PSRAM and park it behind every screen.
 *  Call once, before uiInit(). Costs ~230 ms at boot and nothing after —
 *  see plate.cpp for why it is generated rather than shipped. */
void plateInit();
lv_obj_t* plateRoot();
bool plateReady();

// ── the bloom ──────────────────────────────────────────────────────────────
// One soft alpha sprite, recoloured per team. The signature — see bloom.cpp.
void bloomInit();
lv_obj_t* bloomCreate(lv_obj_t* parent, int w, int h);
void bloomSet(lv_obj_t* o, uint32_t colour, lv_opa_t opa);
/** Pre-composite the glow over `fill` in `colour` and draw it OPAQUE — no
 *  runtime blend, so none of lv_color_mix's 26-level quantisation. */
void bloomComposite(lv_obj_t* o, uint32_t colour, uint32_t fill);

// ── motion ─────────────────────────────────────────────────────────────────
// The live dots breathe, on ONE shared timer. See pulse.cpp for the repaint
// budget this sits inside, and for the list of things that must not animate.
void pulseRegister(lv_obj_t* dot);
/** Drop every registration. Call BEFORE tearing down the board, or the timer
 *  walks freed objects. */
void pulseForget();

void uiInit();
/** Rebuild from g_board. Loop context only. Every write is change-cached. */
void uiBoardRefresh();
void uiShow(Screen s);
Screen uiCurrent();

// Top bar
void uiSetClock(const char* hhmm, const char* date);
void uiSetStatus();
/** The poll heartbeat: pct of the interval elapsed; C_WARN when overdue.
 *  Fed once a second from loop(); writes are change-cached. */
void uiHeartbeatSet(uint8_t pct, bool overdue);
/** Register another bar's heartbeat line (the idle screen has its own). */
void uiHeartbeatAdd(lv_obj_t* line);
/** The nav pill primitive, shared by the board and idle bars. */
lv_obj_t* uiNavTrio(lv_obj_t* bar, int x, int barH,
                    lv_event_cb_t t1, lv_event_cb_t t2, lv_event_cb_t t3);

// ── the left rail ──────────────────────────────────────────────────────────
// Day-to-day league FILTERING (what the header chips used to do), as a
// hideable 140 px rail with a 16 px collapsed activity sliver. Enabling and
// disabling leagues stays in settings — see refresh-spec.md §9.
void uiRailInit(lv_obj_t* parent);
void uiRailToggle();
bool uiRailOpen();
void uiRailRefresh();          // on new data, while open: counts only (frozen order)
lv_obj_t* uiRailRoot();

// Setup / onboarding
void uiSetupInit(lv_obj_t* parent);
/** Force the on-device keyboard open — harness only, so it is reviewable. */
void uiSetupShowKeyboard();
bool uiSetupActive();
/** Root of the setup screen, or nullptr before uiSetupInit(). */
lv_obj_t* uiSetupRoot();

// Idle — what the panel shows when nothing is live. UI.md §7.
void uiIdleInit(lv_obj_t* parent);
void uiIdleRefresh();          // on new data
void uiIdleTick();             // once a second: clock + countdown
lv_obj_t* uiIdleRoot();
/** True when no game on the board is live. */
bool uiShouldIdle();

// Alert takeover — UI.md §8. Composites once, fades in four steps, holds static.
void uiAlertInit(lv_obj_t* parent);
void uiAlertEnqueue(const AlertEvent& e);
void uiAlertTick();
void uiAlertDismiss();
bool uiAlertActive();
lv_obj_t* uiAlertRoot();
/** Highest sequence safe to persist: never past an alert not yet seen. */
uint32_t uiAlertSafeSeq(uint32_t proxySeq);

// Game detail — UI.md §4. Header reuses the tile's anatomy.
void uiGameInit(lv_obj_t* parent);
void uiGameOpen(const Game& g);
void uiGameApply(const GameDetail& d);
void uiGameClose();
bool uiGameIsOpen();
const char* uiGameOpenId();
lv_obj_t* uiGameRoot();

/** Page the board. Returns false when there is nowhere to go. */
bool uiBoardPage(int delta);

// ── featured layout ────────────────────────────────────────────────────────
// One promoted game at 508x268 plus two live tiles plus a bare-plate ledger.
// Chosen by effectiveDensity() when AUTO sees between one and three live
// games; a busier night falls back to the grid, which is the right answer when
// there is nothing to promote. See ui_hero.cpp for why this exists at all.
void uiHeroInit(lv_obj_t* parent);
void uiHeroShow(int8_t gameIdx);
/** Centre the hero when the tile strip AND the results row are both empty. */
void uiHeroCentre(bool noStrip, bool noRow);
/** How many result cards the ledger last filled. */
int  uiLedgerCount();
void uiHeroHide();
lv_obj_t* uiHeroRoot();
int8_t uiHeroGameIdx();

void uiLedgerInit(lv_obj_t* parent);
/** Fill from g_board in `order`, skipping `exclude` (games already on the hero
 *  or a tile) and anything `passes` rejects. */
void uiLedgerRender(const uint8_t* order, uint8_t n,
                    const int8_t* exclude, uint8_t nExclude,
                    bool (*passes)(const Game&));
void uiLedgerHide();
/** Reposition the ledger's columns for the rail state — a one-shot layout
 *  write on toggle, not a per-poll cost. */
void uiLedgerLayout(bool railOpen);
/** As above, but for an explicit card count — the row centres when k < 3. */
void uiLedgerLayoutK(bool railOpen, int k);
lv_obj_t* uiLedgerRoot();

/** Game index shown in tile `slot`, or -1. Lets the logo fetcher bound its
 *  working set to what is actually on screen. */
int8_t uiBoardTileGame(uint8_t slot);

/** A one-line confirmation, centred, 1.2 s. For changes the user made but
 *  cannot see the cause of — density being the first. */
void uiToast(const char* text);
void uiToastTick();

/** Flare the edge light on one game's tile — the non-occluding half of a
 *  score alert. No-op when that game is not on the current page. */
void uiBoardFlash(const char* gameId);

// ── auto-focus ─────────────────────────────────────────────────────────────
/** Open a followed team's game when it reaches a tense state, and hand the
 *  screen back when it passes. Call once per loop. */
void uiFocusTick();
/** Tell auto-focus the user closed a game themselves, so it backs off. */
void uiFocusNoteUserClose();

// ── settings ───────────────────────────────────────────────────────────────
void uiSettingsInit(lv_obj_t* parent);
void uiSettingsOpen();
void uiSettingsRender();
void uiSettingsClose();
/** Debounced NVS flush — every write stalls the panel 150-220 ms, so a save
 *  per toggle would shake the screen. Call once per loop. */
void uiSettingsTick();
lv_obj_t* uiSettingsRoot();
/** Jump to a pane. Used by the harness to shoot all three. */
void uiSettingsTab(uint8_t i);
/** Open the timezone picker directly. Harness only. */
void uiSettingsTzOpen();

// Standings — UI.md §6. Generic table, labelled cut lines.
void uiStandingsInit(lv_obj_t* parent);
void uiStandingsOpen(const char* league);
void uiStandingsRender();
void uiStandingsClose();
bool uiStandingsIsOpen();
lv_obj_t* uiStandingsRoot();

// News — UI.md §5.4. Headlines plus the proxy's trimmed summary; no article body.
void uiNewsInit(lv_obj_t* parent);
/** True when a live game carries structured leverage (power play, red zone,
 *  scoring position with two out). The board colours the situation chip by
 *  this: amber is reserved for the moments that earn it. */
bool uiIsTense(const Game& g);

void uiReaderInit(lv_obj_t* parent);
void uiReaderOpen(const NewsItem& it);
void uiReaderTick();
void uiReaderHide();
bool uiReaderIsOpen();
void uiNewsOpen();
void uiNewsRender();
void uiNewsClose();
bool uiNewsIsOpen();
lv_obj_t* uiNewsRoot();

// Lineup + player sheet — UI.md §5. ESPN gives position groups, not line
// combinations; the sheet is a fixed rect that fades and never slides.
void uiLineupInit(lv_obj_t* parent);
void uiLineupOpen(const char* league, const char* gameId);
void uiLineupApply();
void uiLineupRender();
void uiLineupClose();
bool uiLineupIsOpen();
lv_obj_t* uiLineupRoot();
void uiPlayerOpen(const char* league, const char* athleteId);
void uiPlayerRender();
void uiPlayerClose();
bool uiPlayerIsOpen();
