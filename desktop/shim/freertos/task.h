// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// FreeRTOS.h — desktop shim.
//
// ScoreDeck's state.h includes <freertos/FreeRTOS.h> and <freertos/semphr.h>
// explicitly, so those paths must resolve. The task and critical-section
// symbols already live in Arduino.h (they arrive that way on ESP32 too), so
// this file adds ONLY the semaphore types — duplicating the rest would be a
// redeclaration error, not a convenience.
#pragma once
#include "Arduino.h"
#include <stdint.h>

typedef void* SemaphoreHandle_t;
typedef int BaseType_t;
typedef unsigned int TickType_t;

#ifndef pdTRUE
#define pdTRUE  1
#define pdFALSE 0
#endif
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

// The harness is single-threaded: scenarios write g_board in the same thread
// that renders, so the mutex genuinely has nothing to protect.
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)1; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
static inline void vTaskDelay(TickType_t) {}
