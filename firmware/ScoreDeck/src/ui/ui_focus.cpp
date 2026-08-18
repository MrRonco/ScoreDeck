// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_focus.cpp — open a game on its own when it starts mattering.
//
// This replaces the "hero mode" the audit originally proposed: a full-screen
// layout with a 140 px score. At 610 mm that cap is 150 arc-minutes — roughly
// the angular size of a paragraph rendered as a single digit — so the size
// half of the idea was wrong. The valuable half was never size, it was
// ATTENTION, and the screen for that already exists: ui_game.cpp is a complete
// single-game view with a linescore, scoring plays, team stats and win
// probability.
//
// So the whole feature collapses from a screen into a trigger, and it consumes
// the `situation` field that the proxy has always sent and nothing ever read.
//
// WHAT COUNTS AS LEVERAGE
//
// Only structured facts. `situation` is packed bits from the proxy and can be
// trusted; the game clock is upstream prose ("3rd 04:21", "Bot 7", "90'+4")
// and parsing it to find "inside the final five minutes" would be a guess that
// differs per sport and per locale. If clock-based leverage is wanted later,
// the honest route is a structured remaining-seconds field on the wire, not a
// parser here.
#include "ui.h"
#include "../config.h"
#include "../core/state.h"

static char     s_focusId[12];      // game we opened, "" when idle
static uint32_t s_openedAt;
static bool     s_userDismissed;    // do not reopen what was just closed

/** Structured leverage only — see the file header. Exported: the board uses
 *  the same test to decide when a situation earns the alert colour. */
bool uiIsTense(const Game& g) {
  if (g.state != GS_LIVE || !g.situation) return false;
  if (g.model == SM_INNING) {
    // Runners in scoring position with two out is the moment; bases empty is
    // not, however many outs there are.
    const bool scoring = sitOnSecond(g.situation) || sitOnThird(g.situation);
    return scoring && sitOuts(g.situation) == 2;
  }
  if (g.model == SM_CLOCK) {
    return (g.situation & 0x04) ||          // power play / man advantage
           sitRedZone(g.situation);
  }
  return false;
}

static bool followedSide(const Game& g) {
  return sideIsFav(g.league, g.away.id) || sideIsFav(g.league, g.home.id);
}

void uiFocusNoteUserClose() { s_userDismissed = true; }

void uiFocusTick() {
  if (!g_set.focusOn) return;

  // Only ever act from the board or the idle screen. If the user has opened
  // something themselves, that is a stronger signal than anything we infer.
  const Screen cur = uiCurrent();
  const bool onFocus = (cur == SCR_GAME) && s_focusId[0];
  if (cur != SCR_BOARD && cur != SCR_IDLE && !onFocus) return;

  if (onFocus) {
    const Game* g = nullptr;
    for (uint8_t i = 0; i < g_gameCount; i++)
      if (strcmp(g_board[i].id, s_focusId) == 0) { g = &g_board[i]; break; }

    // Go back when the moment passes, when the game does, or when it has held
    // the screen long enough that it is no longer a moment.
    const bool over = !g || !uiIsTense(*g) ||
                      millis() - s_openedAt > FOCUS_MAX_MS;
    if (over) {
      s_focusId[0] = '\0';
      uiGameClose();
    }
    return;
  }

  if (s_userDismissed) {
    // One quiet period after a manual close, so we are not fighting the user.
    if (millis() - s_openedAt < FOCUS_COOLDOWN_MS) return;
    s_userDismissed = false;
  }

  for (uint8_t i = 0; i < g_gameCount; i++) {
    const Game& g = g_board[i];
    if (!followedSide(g) || !uiIsTense(g)) continue;
    strncpy(s_focusId, g.id, sizeof s_focusId - 1);
    s_focusId[sizeof s_focusId - 1] = '\0';
    s_openedAt = millis();
    uiGameOpen(g);
    return;
  }
}
