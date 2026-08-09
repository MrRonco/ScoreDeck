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

/** Lineups and one player's card. Loop context only. */
bool apiLineupStart(const char* league, const char* id);
bool apiPlayerStart(const char* league, const char* athleteId);
