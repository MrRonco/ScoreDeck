// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once
#include <lvgl.h>
#include "../core/types.h"

enum Screen : uint8_t { SCR_BOARD = 0, SCR_IDLE, SCR_GAME, SCR_STANDINGS, SCR_NEWS, SCR_LINEUP, SCR_SETUP };

void uiInit();
/** Rebuild from g_board. Loop context only. Every write is change-cached. */
void uiBoardRefresh();
void uiShow(Screen s);
Screen uiCurrent();

// Top bar
void uiSetClock(const char* hhmm, const char* date);
void uiSetStatus();

// Setup / onboarding
void uiSetupInit(lv_obj_t* parent);
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

// Standings — UI.md §6. Generic table, labelled cut lines.
void uiStandingsInit(lv_obj_t* parent);
void uiStandingsOpen(const char* league);
void uiStandingsRender();
void uiStandingsClose();
bool uiStandingsIsOpen();
lv_obj_t* uiStandingsRoot();

// News — UI.md §5.4. Headlines plus the proxy's trimmed summary; no article body.
void uiNewsInit(lv_obj_t* parent);
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
