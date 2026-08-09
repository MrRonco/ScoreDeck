// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// api.cpp — proxy client. Short-lived task on core 0; writes only into
// g_pend* under g_dataMux and sets the ready flag LAST.
#include "api.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include "../core/state.h"
#include "../config.h"

// Snapshotted in loop context before the spawn — a task never reads g_set.
struct PollJob {
  char url[420];
  char token[80];
};
static PollJob s_job;

static void copyStr(char* dst, size_t cap, JsonVariantConst v) {
  const char* s = v.is<const char*>() ? v.as<const char*>() : "";
  if (!s) s = "";
  strncpy(dst, s, cap - 1);
  dst[cap - 1] = '\0';
}

/** Discard ~95% of the payload before it is allocated. */
static void buildFilter(JsonDocument& f) {
  f["v"] = true;
  f["now"] = true;
  f["seq"] = true;
  f["next"] = true;
  f["stale"] = true;
  JsonObject g = f["games"].createNestedObject();
  for (const char* k : { "i", "l", "m", "g", "st", "sit", "t", "b", "wp", "lh", "f", "p" }) g[k] = true;
  for (const char* side : { "away", "home" }) {
    JsonObject s = g.createNestedObject(side);
    for (const char* k : { "a", "n", "s", "c", "r", "k", "id" }) s[k] = true;
  }
  JsonObject e = f["ev"].createNestedObject();
  for (const char* k : { "q", "i", "v", "a", "c", "w", "d", "s", "st" }) e[k] = true;
  JsonObject l = f["lg"].createNestedObject();
  l["l"] = true;
  l["n"] = true;
}

static void parseSide(JsonObjectConst src, Side& out) {
  copyStr(out.abbr, sizeof out.abbr, src["a"]);
  copyStr(out.name, sizeof out.name, src["n"]);
  copyStr(out.rec,  sizeof out.rec,  src["r"]);
  copyStr(out.id,   sizeof out.id,   src["id"]);
  out.score = src["s"] | 0;
  out.color = src["c"] | 0x5D6D7E;
  out.rank  = src["k"] | 0;
}

/**
 * The whole body lives here so every RAII object is destroyed on return.
 * vTaskDelete(NULL) never returns and skips destructors — a DynamicJsonDocument
 * declared in the task function leaks on every single run.
 * INHERITED_RULES.md §13, the most expensive lesson in that file.
 */
static bool pollOnce(const PollJob& job) {
  WiFiClient plain;
  WiFiClientSecure secure;
  const bool https = strncmp(job.url, "https:", 6) == 0;
  if (https) secure.setInsecure();   // TODO(P1): pin the proxy cert

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);

  bool begun = https ? http.begin(secure, job.url) : http.begin(plain, job.url);
  if (!begun) {
    snprintf(g_netDetail, sizeof g_netDetail, "bad proxy URL");
    return false;
  }
  http.setUserAgent("ScoreDeck/" SD_VERSION);
  if (job.token[0]) http.addHeader("Authorization", String("Bearer ") + job.token);

  const int code = http.GET();
  if (code != 200) {
    snprintf(g_netDetail, sizeof g_netDetail, "proxy HTTP %d", code);
    http.end();
    return false;
  }

  // Read the body through getString(): HTTPClient decodes chunked transfer
  // encoding there, and deserializeJson() on the raw stream does not — it sees
  // the chunk-length framing and stops at the first token. Both the Node dev
  // server and Cloudflare send chunked, so this is not a tunnel artefact.
  const int len = http.getSize();
  if (len > 48 * 1024) {
    snprintf(g_netDetail, sizeof g_netDetail, "body too large (%d)", len);
    http.end();
    return false;
  }
  const String body = http.getString();
  http.end();
  if (body.length() < 8) {
    snprintf(g_netDetail, sizeof g_netDetail, "empty body");
    return false;
  }

  DynamicJsonDocument filter(1536);
  buildFilter(filter);
  DynamicJsonDocument doc(20 * 1024);
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));

  if (err) {
    snprintf(g_netDetail, sizeof g_netDetail, "parse: %s", err.c_str());
    return false;
  }
  if ((doc["v"] | 0) != 1) {
    snprintf(g_netDetail, sizeof g_netDetail, "schema v%d (%u B)",
             (int)(doc["v"] | 0), (unsigned)body.length());
    return false;
  }

  // Build into the pending buffers under the mutex, ready flag LAST.
  if (xSemaphoreTake(g_dataMux, pdMS_TO_TICKS(400)) != pdTRUE) return false;

  g_pendCount = 0;
  for (JsonObjectConst gj : doc["games"].as<JsonArrayConst>()) {
    if (g_pendCount >= MAX_GAMES) break;
    Game& g = g_pendBoard[g_pendCount];
    copyStr(g.id,     sizeof g.id,     gj["i"]);
    copyStr(g.league, sizeof g.league, gj["l"]);
    copyStr(g.status, sizeof g.status, gj["st"]);
    copyStr(g.bcast,  sizeof g.bcast,  gj["b"]);
    g.model       = (ScoreModel)(gj["m"] | 0);
    g.state       = (GameState)(gj["g"] | 0);
    g.situation   = gj["sit"] | 0;
    g.startUtc    = gj["t"] | 0;
    g.winProbHome = gj["wp"] | 255;
    g.leaderHome  = gj["lh"] | false;
    g.isFav       = gj["f"] | false;
    parseSide(gj["away"], g.away);
    parseSide(gj["home"], g.home);
    g_pendCount++;
  }

  g_pendLeagueCount = 0;
  for (JsonObjectConst lj : doc["lg"].as<JsonArrayConst>()) {
    if (g_pendLeagueCount >= MAX_LEAGUES) break;
    copyStr(g_pendLeagues[g_pendLeagueCount].slug, sizeof g_pendLeagues[0].slug, lj["l"]);
    g_pendLeagues[g_pendLeagueCount].live = lj["n"] | 0;
    g_pendLeagueCount++;
  }

  g_pendEventCount = 0;
  for (JsonObjectConst ej : doc["ev"].as<JsonArrayConst>()) {
    if (g_pendEventCount >= MAX_EVENTS) break;
    AlertEvent& e = g_pendEvents[g_pendEventCount];
    e.seq = ej["q"] | 0;
    copyStr(e.gameId, sizeof e.gameId, ej["i"]);
    copyStr(e.verb,   sizeof e.verb,   ej["v"]);
    copyStr(e.abbr,   sizeof e.abbr,   ej["a"]);
    copyStr(e.who,    sizeof e.who,    ej["w"]);
    copyStr(e.detail, sizeof e.detail, ej["d"]);
    copyStr(e.status, sizeof e.status, ej["st"]);
    e.color      = ej["c"] | 0x5D6D7E;
    e.scoreAway  = ej["s"][0] | 0;
    e.scoreHome  = ej["s"][1] | 0;
    g_pendEventCount++;
  }

  g_pendSeq      = doc["seq"] | 0;
  g_pendNextPoll = constrain((int)(doc["next"] | POLL_DEFAULT_S), POLL_MIN_S, POLL_MAX_S);
  g_pendStale    = doc["stale"] | false;
  g_pendReady    = true;                       // LAST
  xSemaphoreGive(g_dataMux);

  const uint32_t serverNow = doc["now"] | 0;   // the device's clock source
  if (serverNow > 1700000000) {
    struct timeval tv = { (time_t)serverNow, 0 };
    settimeofday(&tv, nullptr);
  }
  return true;
}

static void pollTask(void*) {
  const uint32_t t0 = millis();
  const bool ok = pollOnce(s_job);            // all destructors run here
  Serial.printf("[net] poll %s in %lu ms  games=%u ev=%u heap=%u\n",
                ok ? "ok" : "FAIL", (unsigned long)(millis() - t0), g_pendCount,
                g_pendEventCount, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  if (!ok) Serial.printf("[net] reason: %s\n", g_netDetail);
  g_net = ok ? (g_pendStale ? NET_STALE : NET_OK) : NET_ERR;
  g_pollInFlight = false;
  vTaskDelete(nullptr);                        // never returns
}

static void urlEncodeInto(String& dst, const String& src) {
  for (size_t i = 0; i < src.length(); i++) {
    const char c = src[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == ':' || c == ',' || c == '/')
      dst += c;
    else {
      char b[4];
      snprintf(b, sizeof b, "%%%02X", (unsigned char)c);
      dst += b;
    }
  }
}

bool apiPollStart() {
  if (g_pollInFlight) return false;
  if (g_set.proxy.isEmpty()) { g_net = NET_NOPROXY; return false; }
  if (WiFi.status() != WL_CONNECTED) { g_net = NET_NOWIFI; return false; }
  // Check the gate BEFORE spawning — see state.h.
  if (!netGateOpen()) return false;

  String url = g_set.proxy;
  if (url.endsWith("/")) url.remove(url.length() - 1);
  url += "/v1/state?seq=" + String(g_set.lastSeq);
  url += "&rgn=";  urlEncodeInto(url, g_set.region);
  url += "&tz=";   urlEncodeInto(url, g_set.tz);
  if (g_set.favs.length())    { url += "&f=";  urlEncodeInto(url, g_set.favs); }
  if (g_set.leagues.length()) { url += "&lg="; urlEncodeInto(url, g_set.leagues); }
  if (inQuietHours()) url += "&quiet=1";

  if (url.length() >= sizeof s_job.url) return false;
  strncpy(s_job.url, url.c_str(), sizeof s_job.url - 1);
  s_job.url[sizeof s_job.url - 1] = '\0';
  strncpy(s_job.token, g_set.token.c_str(), sizeof s_job.token - 1);
  s_job.token[sizeof s_job.token - 1] = '\0';

  Serial.printf("[net] GET %s\n", s_job.url);
  g_pollInFlight = true;
  if (xTaskCreatePinnedToCore(pollTask, "sdPoll", NET_TASK_STACK, nullptr, 1, nullptr, 0) != pdPASS) {
    g_pollInFlight = false;
    return false;
  }
  return true;
}
