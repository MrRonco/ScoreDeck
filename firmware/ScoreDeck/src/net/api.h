// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once
#include <Arduino.h>

/**
 * Spawn a poll of GET /v1/state. Call from LOOP context only — it snapshots
 * settings and checks the heap gate before creating the task.
 * Returns false when it declined (already in flight, no proxy, gate shut).
 */
bool apiPollStart();

/** Fetch one game's detail. Loop context only; same gate rules as apiPollStart. */
bool apiGameStart(const char* league, const char* id);

/** Fetch a league table. Loop context only. */
bool apiStandingsStart(const char* league);

/** Fetch headlines for the followed teams. Loop context only. */
bool apiNewsStart();
/** The league catalog, leagues-only form. Fetched when the SPORTS pane first
 *  opens; the pane falls back to (stored ∪ tonight's board) offline. */
bool apiCatalogStart();

/** Lineups and one player's card. Loop context only. */
bool apiLineupStart(const char* league, const char* id);
bool apiPlayerStart(const char* league, const char* athleteId);

/** One-shot proxy reachability check for the settings screen's Test button.
 *  Returns the HTTP status, or a negative code when it could not ask.
 *  Blocking and loop-context only — see the note on the definition. */
int netProbeProxy(uint16_t* outMs);

/** Relay one ALLOWLISTED path to the proxy for the browser portal. The caller
 *  owns the allowlist — see the definition. Loop context only. */
int netRelayGet(const String& path, String& out);

/** The last state poll's HTTP status and round-trip, for diagnostics. */
int      apiLastPollCode();
uint16_t apiLastPollMs();
