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
// The last poll's outcome, for the diagnostics page. "Proxy unreachable",
// "proxy answered 401" and "proxy answered slowly" are three different
// problems and the status line cannot tell them apart.
static int      s_lastPollCode = 0;
static uint16_t s_lastPollMsV  = 0;

int      apiLastPollCode() { return s_lastPollCode; }
uint16_t apiLastPollMs()   { return s_lastPollMsV; }

static PollJob s_job;

static void copyStr(char* dst, size_t cap, JsonVariantConst v) {
  const char* s = v.is<const char*>() ? v.as<const char*>() : "";
  if (!s) s = "";
  strncpy(dst, s, cap - 1);
  dst[cap - 1] = '\0';
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

/** Discard ~95% of the payload before it is allocated. */
static void buildFilter(JsonDocument& f) {
  f["v"] = true;
  f["now"] = true;
  f["seq"] = true;
  f["next"] = true;
  f["stale"] = true;
  JsonObject g = f["games"].createNestedObject();
  for (const char* k : { "i", "l", "m", "g", "st", "sit", "t", "b", "wp", "lh", "f", "p", "lp" }) g[k] = true;
  JsonObject fl = g["fld"].createNestedObject();
  for (const char* k : { "p", "n", "v", "d" }) fl[k] = true;
  g["sets"][0][0] = true;
  g["sets"][1][0] = true;
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
  g_fieldCount = 0;
  for (JsonObjectConst gj : doc["games"].as<JsonArrayConst>()) {
    if (g_pendCount >= MAX_GAMES) break;
    Game& g = g_pendBoard[g_pendCount];
    copyStr(g.id,     sizeof g.id,     gj["i"]);
    copyStr(g.league, sizeof g.league, gj["l"]);
    copyStr(g.status, sizeof g.status, gj["st"]);
    copyStr(g.bcast,  sizeof g.bcast,  gj["b"]);
    copyStr(g.lastPlay, sizeof g.lastPlay, gj["lp"]);
    g.model       = (ScoreModel)(gj["m"] | 0);
    g.state       = (GameState)(gj["g"] | 0);
    g.situation   = gj["sit"] | 0;
    g.startUtc    = gj["t"] | 0;
    g.winProbHome = gj["wp"] | 255;
    g.leaderHome  = gj["lh"] | false;
    g.isFav       = gj["f"] | false;
    parseSide(gj["away"], g.away);
    parseSide(gj["home"], g.home);

    // SET: per-set scores.
    g.setCount = 0;
    JsonArrayConst sa = gj["sets"][0], sh = gj["sets"][1];
    for (JsonVariantConst v : sa) {
      if (g.setCount >= 5) break;
      g.setsAway[g.setCount] = v | 0;
      g.setsHome[g.setCount] = sh[g.setCount] | 0;
      g.setCount++;
    }

    // LEADERBOARD / GRID: take a slot from the pool while one is free.
    g.fieldIdx = -1;
    JsonArrayConst fld = gj["fld"];
    if (!fld.isNull() && fld.size() && g_fieldCount < FLD_POOL) {
      FieldSet& F = g_fields[g_fieldCount];
      F.count = 0;
      for (JsonObjectConst r : fld) {
        if (F.count >= FLD_ROWS) break;
        copyStr(F.rows[F.count].pos,    sizeof F.rows[0].pos,    r["p"]);
        copyStr(F.rows[F.count].name,   sizeof F.rows[0].name,   r["n"]);
        copyStr(F.rows[F.count].val,    sizeof F.rows[0].val,    r["v"]);
        copyStr(F.rows[F.count].detail, sizeof F.rows[0].detail, r["d"]);
        F.count++;
      }
      g.fieldIdx = (int8_t)g_fieldCount;
      g_fieldCount++;
    }
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

// ── game detail ────────────────────────────────────────────────────────────
static char s_gameUrl[300];
static char s_gameToken[80];
volatile bool g_gameInFlight = false;
GameDetail g_pendGame;
volatile bool g_pendGameReady = false;

static void gameFilter(JsonDocument& f) {
  for (const char* k : { "i", "st", "live", "wp", "sit", "venue" }) f[k] = true;
  for (const char* side : { "away", "home" }) {
    JsonObject o = f.createNestedObject(side);
    o["a"] = true; o["s"] = true; o["c"] = true;
  }
  JsonObject ls = f.createNestedObject("ls");
  ls["cols"][0] = true; ls["a"][0] = true; ls["h"][0] = true;
  JsonObject pl = f["plays"].createNestedObject();
  for (const char* k : { "t", "x", "s", "hm" }) pl[k] = true;
  JsonObject st = f["stats"].createNestedObject();
  for (const char* k : { "k", "a", "h" }) st[k] = true;
}

static bool gameOnce(const char* url, const char* token) {
  WiFiClient plain; WiFiClientSecure secure;
  const bool https = strncmp(url, "https:", 6) == 0;
  if (https) secure.setInsecure();
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) return false;
  http.setUserAgent("ScoreDeck/" SD_VERSION);
  if (token[0]) http.addHeader("Authorization", String("Bearer ") + token);
  if (http.GET() != 200) { http.end(); return false; }
  const String body = http.getString();   // decodes chunked; see pollOnce
  http.end();
  if (body.length() < 8) return false;

  DynamicJsonDocument filter(1024); gameFilter(filter);
  DynamicJsonDocument doc(12 * 1024);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;

  GameDetail& d = g_pendGame;
  memset(&d, 0, sizeof d);
  copyStr(d.id,     sizeof d.id,     doc["i"]);
  copyStr(d.status, sizeof d.status, doc["st"]);
  copyStr(d.venue,  sizeof d.venue,  doc["venue"]);
  copyStr(d.awayAbbr, sizeof d.awayAbbr, doc["away"]["a"]);
  copyStr(d.homeAbbr, sizeof d.homeAbbr, doc["home"]["a"]);
  d.awayScore = doc["away"]["s"] | 0;   d.homeScore = doc["home"]["s"] | 0;
  d.awayColor = doc["away"]["c"] | 0x5D6D7E;
  d.homeColor = doc["home"]["c"] | 0x5D6D7E;
  d.live = doc["live"] | false;
  d.winProbHome = doc["wp"] | 255;
  d.situation = doc["sit"] | 0;

  JsonArrayConst cols = doc["ls"]["cols"], la = doc["ls"]["a"], lh = doc["ls"]["h"];
  for (JsonVariantConst v : cols) {
    if (d.lsCount >= GD_LS_COLS) break;
    copyStr(d.lsCols[d.lsCount], sizeof d.lsCols[0], v);
    d.lsCount++;
  }
  for (uint8_t i = 0; i < d.lsCount; i++) {
    // Values may be numbers or strings depending on the sport.
    if (la[i].is<const char*>()) copyStr(d.lsA[i], sizeof d.lsA[0], la[i]);
    else snprintf(d.lsA[i], sizeof d.lsA[0], "%d", la[i] | 0);
    if (lh[i].is<const char*>()) copyStr(d.lsH[i], sizeof d.lsH[0], lh[i]);
    else snprintf(d.lsH[i], sizeof d.lsH[0], "%d", lh[i] | 0);
  }

  for (JsonObjectConst p : doc["plays"].as<JsonArrayConst>()) {
    if (d.playCount >= GD_PLAYS) break;
    const uint8_t i = d.playCount;
    copyStr(d.playT[i], sizeof d.playT[0], p["t"]);
    copyStr(d.playX[i], sizeof d.playX[0], p["x"]);
    snprintf(d.playS[i], sizeof d.playS[0], "%d-%d", (int)(p["s"][0] | 0), (int)(p["s"][1] | 0));
    d.playHome[i] = p["hm"] | false;
    d.playCount++;
  }

  for (JsonObjectConst st : doc["stats"].as<JsonArrayConst>()) {
    if (d.statCount >= GD_STATS) break;
    const uint8_t i = d.statCount;
    copyStr(d.statK[i], sizeof d.statK[0], st["k"]);
    copyStr(d.statA[i], sizeof d.statA[0], st["a"]);
    copyStr(d.statH[i], sizeof d.statH[0], st["h"]);
    d.statCount++;
  }

  g_pendGameReady = true;
  return true;
}

static void gameTask(void*) {
  const uint32_t t0 = millis();
  const bool ok = gameOnce(s_gameUrl, s_gameToken);
  Serial.printf("[net] game %s in %lu ms\n", ok ? "ok" : "FAIL",
                (unsigned long)(millis() - t0));
  g_gameInFlight = false;
  vTaskDelete(nullptr);
}

// ── standings ──────────────────────────────────────────────────────────────
static char s_stUrl[260];
static char s_stToken[80];

static bool standingsOnce(const char* url, const char* token) {
  WiFiClient plain; WiFiClientSecure secure;
  const bool https = strncmp(url, "https:", 6) == 0;
  if (https) secure.setInsecure();
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) return false;
  http.setUserAgent("ScoreDeck/" SD_VERSION);
  if (token[0]) http.addHeader("Authorization", String("Bearer ") + token);
  if (http.GET() != 200) { http.end(); return false; }
  const String body = http.getString();
  http.end();
  if (body.length() < 8) return false;

  DynamicJsonDocument doc(24 * 1024);
  if (deserializeJson(doc, body)) return false;

  Standings& t = g_standings;
  t.colCount = 0; t.rowCount = 0; t.cutCount = 0;

  // rows[] are [ABBR, TEAM, COLOR, ...stats]; the first three are structural.
  JsonArrayConst cols = doc["cols"];
  for (JsonVariantConst v : cols) {
    const char* c = v | "";
    if (!strcmp(c, "ABBR") || !strcmp(c, "TEAM") || !strcmp(c, "COLOR")) continue;
    if (t.colCount >= ST_MAX_COLS) break;
    copyStr(t.cols[t.colCount], sizeof t.cols[0], v);
    t.colCount++;
  }

  for (JsonArrayConst r : doc["rows"].as<JsonArrayConst>()) {
    if (t.rowCount >= ST_MAX_ROWS) break;
    StandingRow& row = t.rows[t.rowCount];
    copyStr(row.abbr, sizeof row.abbr, r[0]);
    copyStr(row.name, sizeof row.name, r[1]);
    row.color = r[2] | 0x5D6D7E;
    for (uint8_t c = 0; c < t.colCount; c++) {
      JsonVariantConst v = r[3 + c];
      if (v.is<const char*>()) copyStr(row.cells[c], sizeof row.cells[0], v);
      else snprintf(row.cells[c], sizeof row.cells[0], "%d", v | 0);
    }
    t.rowCount++;
  }

  for (JsonObjectConst c : doc["cuts"].as<JsonArrayConst>()) {
    if (t.cutCount >= ST_MAX_CUTS) break;
    t.cutAfter[t.cutCount] = c["after"] | 0;
    copyStr(t.cutLabel[t.cutCount], sizeof t.cutLabel[0], c["label"]);
    t.cutCount++;
  }

  t.loading = false;
  g_standingsReady = true;
  return true;
}

static void standingsTask(void*) {
  const bool ok = standingsOnce(s_stUrl, s_stToken);
  if (!ok) { g_standings.loading = false; g_standingsReady = true; }
  Serial.printf("[net] standings %s rows=%u\n", ok ? "ok" : "FAIL", g_standings.rowCount);
  g_standingsInFlight = false;
  vTaskDelete(nullptr);
}

bool apiStandingsStart(const char* league) {
  if (g_standingsInFlight) return false;
  if (g_set.proxy.isEmpty() || WiFi.status() != WL_CONNECTED) return false;
  if (!netGateOpen()) return false;
  String url = g_set.proxy;
  if (url.endsWith("/")) url.remove(url.length() - 1);
  url += "/v1/standings/";
  url += league;
  if (url.length() >= sizeof s_stUrl) return false;
  strncpy(s_stUrl, url.c_str(), sizeof s_stUrl - 1);
  s_stUrl[sizeof s_stUrl - 1] = '\0';
  strncpy(s_stToken, g_set.token.c_str(), sizeof s_stToken - 1);
  s_stToken[sizeof s_stToken - 1] = '\0';
  g_standingsInFlight = true;
  if (xTaskCreatePinnedToCore(standingsTask, "sdStand", NET_TASK_STACK, nullptr, 1, nullptr, 0) != pdPASS) {
    g_standingsInFlight = false;
    return false;
  }
  return true;
}

// ── lineup and player ─────────────────────────────────────────────────────
static char s_luUrl[300], s_luToken[80];
static char s_pcUrl[300], s_pcToken[80];

static bool getJson(const char* url, const char* token, DynamicJsonDocument& doc) {
  WiFiClient plain; WiFiClientSecure secure;
  const bool https = strncmp(url, "https:", 6) == 0;
  if (https) secure.setInsecure();
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) return false;
  http.setUserAgent("ScoreDeck/" SD_VERSION);
  if (token[0]) http.addHeader("Authorization", String("Bearer ") + token);
  if (http.GET() != 200) { http.end(); return false; }
  const String body = http.getString();   // decodes chunked — see pollOnce
  http.end();
  return body.length() > 8 && !deserializeJson(doc, body);
}

static bool lineupOnce() {
  DynamicJsonDocument doc(28 * 1024);
  if (!getJson(s_luUrl, s_luToken, doc)) return false;

  Lineup& L = g_lineup;
  L.sideCount = 0;
  copyStr(L.gameId, sizeof L.gameId, doc["i"]);
  for (JsonObjectConst sd : doc["sides"].as<JsonArrayConst>()) {
    if (L.sideCount >= 2) break;
    LineSide& S = L.sides[L.sideCount];
    memset(&S, 0, sizeof S);
    copyStr(S.abbr, sizeof S.abbr, sd["a"]);
    copyStr(S.formation, sizeof S.formation, sd["f"]);
    S.color = sd["c"] | 0x5D6D7E;
    for (JsonObjectConst gp : sd["groups"].as<JsonArrayConst>()) {
      if (S.groupCount >= LU_GROUPS) break;
      LineGroup& G = S.groups[S.groupCount];
      memset(&G, 0, sizeof G);
      copyStr(G.name, sizeof G.name, gp["n"]);
      for (JsonVariantConst cv : gp["cols"].as<JsonArrayConst>()) {
        if (G.colCount >= LU_COLS) break;
        copyStr(G.cols[G.colCount], sizeof G.cols[0], cv);
        G.colCount++;
      }
      for (JsonObjectConst pv : gp["players"].as<JsonArrayConst>()) {
        if (G.count >= LU_PLAYERS) break;
        LinePlayer& P = G.players[G.count];
        memset(&P, 0, sizeof P);
        copyStr(P.id,     sizeof P.id,     pv["id"]);
        copyStr(P.name,   sizeof P.name,   pv["n"]);
        copyStr(P.pos,    sizeof P.pos,    pv["p"]);
        copyStr(P.jersey, sizeof P.jersey, pv["j"]);
        P.starter = pv["s"] | false;
        for (uint8_t k = 0; k < G.colCount; k++)
          copyStr(P.vals[k], sizeof P.vals[0], pv["v"][k]);
        G.count++;
      }
      if (G.count) S.groupCount++;
    }
    if (S.groupCount) L.sideCount++;
  }
  L.loading = false;
  g_lineupReady = true;
  return true;
}

static void lineupTask(void*) {
  const bool ok = lineupOnce();
  if (!ok) { g_lineup.loading = false; g_lineup.sideCount = 0; g_lineupReady = true; }
  Serial.printf("[net] lineup %s sides=%u\n", ok ? "ok" : "FAIL", g_lineup.sideCount);
  g_lineupInFlight = false;
  vTaskDelete(nullptr);
}

static bool playerOnce() {
  DynamicJsonDocument doc(8 * 1024);
  if (!getJson(s_pcUrl, s_pcToken, doc)) return false;
  PlayerCard& P = g_player;
  const bool wasLoading = P.loading;
  memset(&P, 0, sizeof P);
  (void)wasLoading;
  copyStr(P.id,     sizeof P.id,     doc["id"]);
  copyStr(P.name,   sizeof P.name,   doc["n"]);
  copyStr(P.pos,    sizeof P.pos,    doc["pos"]);
  copyStr(P.team,   sizeof P.team,   doc["team"]);
  copyStr(P.jersey, sizeof P.jersey, doc["j"]);
  copyStr(P.height, sizeof P.height, doc["ht"]);
  copyStr(P.weight, sizeof P.weight, doc["wt"]);
  P.color    = doc["c"] | 0x5D6D7E;
  P.age      = doc["age"] | 0;
  P.hasImage = doc["img"] | false;
  for (JsonObjectConst st : doc["season"].as<JsonArrayConst>()) {
    if (P.statCount >= PC_STATS) break;
    copyStr(P.statK[P.statCount], sizeof P.statK[0], st["k"]);
    copyStr(P.statV[P.statCount], sizeof P.statV[0], st["v"]);
    copyStr(P.statR[P.statCount], sizeof P.statR[0], st["r"]);
    P.statCount++;
  }
  P.loading = false;
  g_playerReady = true;
  return true;
}

static void playerTask(void*) {
  const bool ok = playerOnce();
  if (!ok) { g_player.loading = false; g_player.name[0] = '\0'; g_playerReady = true; }
  Serial.printf("[net] player %s %s\n", ok ? "ok" : "FAIL", g_player.name);
  g_playerInFlight = false;
  vTaskDelete(nullptr);
}

static bool startSimple(const char* path, char* url, char* token, size_t cap,
                        volatile bool* flag, TaskFunction_t fn, const char* taskName) {
  if (*flag) return false;
  if (g_set.proxy.isEmpty() || WiFi.status() != WL_CONNECTED) return false;
  if (!netGateOpen()) return false;
  String u = g_set.proxy;
  if (u.endsWith("/")) u.remove(u.length() - 1);
  u += path;
  if (u.length() >= cap) return false;
  strncpy(url, u.c_str(), cap - 1); url[cap - 1] = '\0';
  strncpy(token, g_set.token.c_str(), 79); token[79] = '\0';
  *flag = true;
  if (xTaskCreatePinnedToCore(fn, taskName, NET_TASK_STACK, nullptr, 1, nullptr, 0) != pdPASS) {
    *flag = false;
    return false;
  }
  return true;
}

bool apiLineupStart(const char* league, const char* id) {
  char p[64];
  snprintf(p, sizeof p, "/v1/lineup/%s/%s", league, id);
  return startSimple(p, s_luUrl, s_luToken, sizeof s_luUrl, &g_lineupInFlight, lineupTask, "sdLineup");
}

bool apiPlayerStart(const char* league, const char* athleteId) {
  char p[64];
  snprintf(p, sizeof p, "/v1/player/%s/%s", league, athleteId);
  return startSimple(p, s_pcUrl, s_pcToken, sizeof s_pcUrl, &g_playerInFlight, playerTask, "sdPlayer");
}

// ── the league catalog ─────────────────────────────────────────────────────
static char s_catUrl[300];
static char s_catToken[80];

static bool catalogOnce(const char* url, const char* token) {
  WiFiClient plain; WiFiClientSecure secure;
  const bool https = strncmp(url, "https:", 6) == 0;
  if (https) secure.setInsecure();
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) return false;
  http.setUserAgent("ScoreDeck/" SD_VERSION);
  if (token[0]) http.addHeader("Authorization", String("Bearer ") + token);
  if (http.GET() != 200) { http.end(); return false; }

  // getString(), NEVER getStream(): the proxy answers Transfer-Encoding:
  // chunked, and getStream() hands the parser the raw chunk framing — the
  // first bytes are a hex chunk length, not '{', so the parse failed on
  // every attempt while curl (which decodes chunking) said the endpoint was
  // fine. This was the one fetch in the file reading the stream directly;
  // see the pollOnce comment that already warns about exactly this.
  const String body = http.getString();
  http.end();
  if (body.length() < 8) return false;

  // ~1.3 KB payload; 6 KB of document is generous headroom.
  DynamicJsonDocument doc(6144);
  StaticJsonDocument<192> filter;
  JsonObject e = filter["leagues"].createNestedObject();
  e["slug"] = true; e["label"] = true; e["family"] = true;
  const auto err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) return false;

  uint8_t n = 0;
  for (JsonObjectConst l : doc["leagues"].as<JsonArrayConst>()) {
    if (n >= CAT_MAX) break;
    CatEntry& c = g_catalog[n];
    strncpy(c.slug,   l["slug"]   | "", sizeof c.slug - 1);
    strncpy(c.label,  l["label"]  | "", sizeof c.label - 1);
    strncpy(c.family, l["family"] | "", sizeof c.family - 1);
    c.slug[sizeof c.slug - 1] = c.label[sizeof c.label - 1] = c.family[sizeof c.family - 1] = '\0';
    if (c.slug[0]) n++;
  }
  g_catalogCount = n;
  g_catalogLoaded = n > 0;
  return n > 0;
}

static void catalogTask(void*) {
  const bool ok = catalogOnce(s_catUrl, s_catToken);
  Serial.printf("[net] catalog %s n=%u\n", ok ? "ok" : "FAIL", g_catalogCount);
  g_catalogReady = true;                 // even on failure — the pane must
  g_catalogInFlight = false;             // stop saying "loading" either way
  vTaskDelete(nullptr);
}

bool apiCatalogStart() {
  return startSimple("/v1/catalog", s_catUrl, s_catToken, sizeof s_catUrl,
                     &g_catalogInFlight, catalogTask, "sdCatalog");
}

// ── news ───────────────────────────────────────────────────────────────────
static char s_nwUrl[380];
static char s_nwToken[80];

static bool newsOnce(const char* url, const char* token) {
  WiFiClient plain; WiFiClientSecure secure;
  const bool https = strncmp(url, "https:", 6) == 0;
  if (https) secure.setInsecure();
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) return false;
  http.setUserAgent("ScoreDeck/" SD_VERSION);
  if (token[0]) http.addHeader("Authorization", String("Bearer ") + token);
  if (http.GET() != 200) { http.end(); return false; }
  const String body = http.getString();
  http.end();
  if (body.length() < 8) return false;

  DynamicJsonDocument doc(16 * 1024);
  if (deserializeJson(doc, body)) return false;

  NewsFeed& n = g_news;
  n.count = 0;
  for (JsonObjectConst it : doc["items"].as<JsonArrayConst>()) {
    if (n.count >= NEWS_MAX) break;
    NewsItem& o = n.items[n.count];
    memset(&o, 0, sizeof o);
    copyStr(o.id,       sizeof o.id,       it["id"]);
    copyStr(o.headline, sizeof o.headline, it["h"]);
    copyStr(o.desc,     sizeof o.desc,     it["d"]);
    copyStr(o.abbr,     sizeof o.abbr,     it["a"]);
    o.when  = it["t"] | 0;
    o.color = it["c"] | 0x5D6D7E;
    n.count++;
  }
  n.loading = false;
  g_newsReady = true;
  return true;
}

// ── one article's body ─────────────────────────────────────────────────────
static char s_stUrl[340];
static char s_stToken[80];

static bool storyOnce(const char* url, const char* token) {
  WiFiClient plain; WiFiClientSecure secure;
  const bool https = strncmp(url, "https:", 6) == 0;
  if (https) secure.setInsecure();
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) return false;
  http.setUserAgent("ScoreDeck/" SD_VERSION);
  if (token[0]) http.addHeader("Authorization", String("Bearer ") + token);
  if (http.GET() != 200) { http.end(); return false; }
  const String body = http.getString();   // decodes chunked — see pollOnce
  http.end();
  if (body.length() < 8) return false;

  // Body is ~6 KB of text inside ~6.2 KB of JSON; 10 KB of document copes
  // with the escaping overhead.
  DynamicJsonDocument doc(10 * 1024);
  if (deserializeJson(doc, body)) return false;
  copyStr(g_story.headline, sizeof g_story.headline, doc["h"]);
  copyStr(g_story.body,     sizeof g_story.body,     doc["body"]);
  return g_story.body[0] != '\0';
}

static void storyTask(void*) {
  g_story.ok = storyOnce(s_stUrl, s_stToken);
  Serial.printf("[net] story %s len=%u\n", g_story.ok ? "ok" : "FAIL",
                (unsigned)strlen(g_story.body));
  g_storyReady = true;
  g_storyInFlight = false;
  vTaskDelete(nullptr);
}

bool apiStoryStart(const char* id) {
  if (!id || !id[0]) return false;
  g_story.ok = false;
  g_story.headline[0] = g_story.body[0] = '\0';
  strncpy(g_story.id, id, sizeof g_story.id - 1);
  g_story.id[sizeof g_story.id - 1] = '\0';
  char p[40];
  snprintf(p, sizeof p, "/v1/story/%s", id);
  return startSimple(p, s_stUrl, s_stToken, sizeof s_stUrl,
                     &g_storyInFlight, storyTask, "sdStory");
}

static void newsTask(void*) {
  const bool ok = newsOnce(s_nwUrl, s_nwToken);
  if (!ok) { g_news.loading = false; g_newsReady = true; }
  Serial.printf("[net] news %s items=%u\n", ok ? "ok" : "FAIL", g_news.count);
  g_newsInFlight = false;
  vTaskDelete(nullptr);
}

bool apiNewsStart() {
  if (g_newsInFlight) return false;
  if (g_set.proxy.isEmpty() || WiFi.status() != WL_CONNECTED) return false;
  if (!netGateOpen()) return false;
  String url = g_set.proxy;
  if (url.endsWith("/")) url.remove(url.length() - 1);
  url += "/v1/news?";
  if (g_set.favs.length())    { url += "f=";  urlEncodeInto(url, g_set.favs); }
  if (g_set.leagues.length()) { url += "&lg="; urlEncodeInto(url, g_set.leagues); }
  if (url.length() >= sizeof s_nwUrl) return false;
  strncpy(s_nwUrl, url.c_str(), sizeof s_nwUrl - 1);
  s_nwUrl[sizeof s_nwUrl - 1] = '\0';
  strncpy(s_nwToken, g_set.token.c_str(), sizeof s_nwToken - 1);
  s_nwToken[sizeof s_nwToken - 1] = '\0';
  g_newsInFlight = true;
  if (xTaskCreatePinnedToCore(newsTask, "sdNews", NET_TASK_STACK, nullptr, 1, nullptr, 0) != pdPASS) {
    g_newsInFlight = false;
    return false;
  }
  return true;
}


bool apiGameStart(const char* league, const char* id) {
  if (g_gameInFlight) return false;
  if (g_set.proxy.isEmpty() || WiFi.status() != WL_CONNECTED) return false;
  if (!netGateOpen()) return false;
  String url = g_set.proxy;
  if (url.endsWith("/")) url.remove(url.length() - 1);
  url += "/v1/game/";
  url += league;
  url += "/";
  url += id;
  url += "?rgn=" + g_set.region;
  if (url.length() >= sizeof s_gameUrl) return false;
  strncpy(s_gameUrl, url.c_str(), sizeof s_gameUrl - 1);
  s_gameUrl[sizeof s_gameUrl - 1] = '\0';
  strncpy(s_gameToken, g_set.token.c_str(), sizeof s_gameToken - 1);
  s_gameToken[sizeof s_gameToken - 1] = '\0';
  g_gameInFlight = true;
  if (xTaskCreatePinnedToCore(gameTask, "sdGame", NET_TASK_STACK, nullptr, 1, nullptr, 0) != pdPASS) {
    g_gameInFlight = false;
    return false;
  }
  return true;
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
  // IANA, not the POSIX rule — see tzForProxy().
  url += "&tz=";   urlEncodeInto(url, tzForProxy());
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

/**
 * A one-shot GET {proxy}/v1/health from loop context, for the settings screen's
 * Test button. Blocking on purpose: it is a deliberate user action, it is
 * bounded at 4 s, and it must not race the polling task for the TLS buffer.
 */
int netProbeProxy(uint16_t* outMs) {
  if (outMs) *outMs = 0;
  if (!g_set.proxy.length() || WiFi.status() != WL_CONNECTED) return -1;
  if (!netGateOpen()) return -2;

  HTTPClient http;
  String url = g_set.proxy;
  if (url.endsWith("/")) url.remove(url.length() - 1);
  url += "/v1/health";
  if (!http.begin(url)) return -1;
  http.setTimeout(4000);
  if (g_set.token.length()) http.addHeader("Authorization", "Bearer " + g_set.token);
  const uint32_t t0 = millis();
  const int code = http.GET();
  if (outMs) *outMs = (uint16_t)min<uint32_t>(millis() - t0, 65535);
  http.end();
  return code;
}

/**
 * Fetch one allowlisted path from the configured proxy and hand back the body.
 *
 * This is the browser portal's fallback for when it can reach the panel but
 * not the proxy — a phone on a guest VLAN, or a proxy behind Tailscale. The
 * caller has ALREADY checked the path against an allowlist; never accept a
 * host from a client here, or this becomes an SSRF primitive aimed at the
 * user's own network.
 *
 * Body is read with getString() rather than streamed, because the response is
 * chunked and deserialising a stream stops at the first chunk marker — that
 * has cost this project three separate debugging sessions.
 */
int netRelayGet(const String& path, String& out) {
  if (!g_set.proxy.length() || WiFi.status() != WL_CONNECTED) return -1;
  HTTPClient http;
  String url = g_set.proxy;
  if (url.endsWith("/")) url.remove(url.length() - 1);
  url += path;
  if (!http.begin(url)) return -1;
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (g_set.token.length()) http.addHeader("Authorization", "Bearer " + g_set.token);
  const int code = http.GET();
  if (code == 200) out = http.getString();
  http.end();
  return code;
}
