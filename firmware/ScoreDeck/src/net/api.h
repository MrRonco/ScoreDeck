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
