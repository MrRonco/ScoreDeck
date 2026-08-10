// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
//
// ScoreDeck — live sports on a 7" ESP32-S3 glass panel.
//
// loop() owns ALL LVGL, ALL of g_board, and ALL NVS access. Network runs as
// short-lived tasks on core 0 that write only into g_pend* under g_dataMux.
// See src/core/state.h for the full threading contract.
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <time.h>
#include <esp_heap_caps.h>

#include "src/config.h"
#include "src/core/state.h"
#include "src/hal/hal_display.h"
#include "src/ui/theme.h"
#include "src/ui/ui.h"
#include "src/net/api.h"
#include <esp_system.h>
#include "src/svc/web.h"
#include "src/net/logos.h"

static uint32_t s_nextPollMs = 0;
static uint16_t s_pollGapS   = POLL_DEFAULT_S;

// Diagnostics counters. Every one of these has an increment site below; the
// portal renders a dash for anything unwired rather than a zero you cannot
// trust. INHERITED_RULES.md §19.
static uint16_t s_declGate, s_declFlight, s_declNoProxy;
static uint32_t s_lastPollMs;
static uint32_t s_lastWifiTry = 0;
static uint32_t s_lastClockMs = 0;

static void wifiConnect() {
  if (g_set.ssid.isEmpty()) { Serial.println("[net] no ssid stored"); return; }
  Serial.printf("[net] connecting to '%s'\n", g_set.ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_set.ssid.c_str(), g_set.pass.c_str());
  // Modem-sleep wake bursts contend with the RGB panel's continuous PSRAM DMA
  // and cause visible screen wiggle. INHERITED_RULES.md §5.
  WiFi.setSleep(false);
}

static void applyPending() {
  if (!g_pendReady) return;
  if (xSemaphoreTake(g_dataMux, pdMS_TO_TICKS(50)) != pdTRUE) return;

  memcpy(g_board, g_pendBoard, sizeof(Game) * g_pendCount);
  g_gameCount = g_pendCount;
  memcpy(g_leagues, g_pendLeagues, sizeof(LeagueCount) * g_pendLeagueCount);
  g_leagueCount = g_pendLeagueCount;
  s_pollGapS = g_pendNextPoll;
  // Only fresh data updates the mark — a stale reply is the proxy telling us
  // it could not reach upstream, and stamping it would hide exactly that.
  if (!g_pendStale) g_lastGoodUtc = (uint32_t)time(nullptr);
  s_lastPollMs = millis();

  // The sequence is per-proxy-instance, not global. A restarted container, a
  // redeployed Worker, or simply pointing at a different proxy all reset it to
  // zero — and a device still holding a higher number would filter out every
  // event the new proxy ever produces, silently, forever. If the proxy reports
  // a lower sequence than we hold, it is not the same run: adopt its number.
  if (g_pendSeq < g_set.lastSeq) {
    Serial.printf("[net] proxy seq %lu < stored %lu — proxy restarted, resyncing\n",
                  (unsigned long)g_pendSeq, (unsigned long)g_set.lastSeq);
    g_set.lastSeq = g_pendSeq;
    settingsSave();
  }

  // Queue any score events for the takeover.
  for (uint8_t i = 0; i < g_pendEventCount; i++) uiAlertEnqueue(g_pendEvents[i]);

  // Commit the sequence only as far as is safe. uiAlertSafeSeq() holds it
  // behind anything still queued or on screen, so losing power mid-card
  // replays the alert rather than swallowing it. Committing g_pendSeq
  // unconditionally here is what made every event vanish before the takeover
  // existed.
  const uint32_t safe = uiAlertSafeSeq(g_pendSeq);
  if (safe > g_set.lastSeq) {
    g_set.lastSeq = safe;
    settingsSave();
  }
  g_pendReady = false;
  xSemaphoreGive(g_dataMux);

  uiBoardRefresh();
  uiIdleRefresh();
  // Most of the day nothing is live. A board of dimmed finals is a sad object,
  // so the panel becomes a countdown instead. UI.md §7.
  // Never navigate away from a game the user is reading.
  if (!uiSetupActive() && !uiGameIsOpen() && !uiStandingsIsOpen() && !uiNewsIsOpen() && !uiLineupIsOpen())
    uiShow(uiShouldIdle() ? SCR_IDLE : SCR_BOARD);
  if (uiAlertActive()) lv_obj_move_foreground(uiAlertRoot());
}

/**
 * Serial console. The USB port already allows reflashing, so this adds no
 * trust boundary — it just removes a two-minute build cycle from every
 * config change during bring-up.
 *   proxy <url> | token <t> | favs <l:id,..> | region <cc> | show | poll | reboot
 */
/**
 * Low-res framebuffer dump. Reads the live panel 8 rows at a time and prints a
 * 100x30 luminance map. Enough to verify structure — top bar, tile grid, text
 * presence — over a serial line, without a camera pointed at the glass.
 */
static void dumpScreen() {
  static uint16_t row[SCR_W * 8];
  const int CW = SCR_W / 100, CH = SCR_H / 30;   // 8 x 16 px cells
  Serial.println("[shot] 100x30 luminance  ( .:-=+*#@ dark->bright )");
  const char* ramp = " .:-=+*#@";
  for (int cy = 0; cy < 30; cy++) {
    uint32_t acc[100] = {0};
    for (int sub = 0; sub < CH; sub += 8) {
      halReadRect(0, cy * CH + sub, SCR_W, 8, row);
      for (int r = 0; r < 8; r++)
        for (int x = 0; x < SCR_W; x++) {
          const uint16_t p = row[r * SCR_W + x];
          const uint32_t lum = ((p >> 11) & 0x1F) * 2 + ((p >> 5) & 0x3F) + (p & 0x1F) * 2;
          acc[x / CW] += lum;
        }
    }
    char line[101];
    for (int cx = 0; cx < 100; cx++) {
      const uint32_t avg = acc[cx] / (CW * CH);      // 0..~127
      int idx = (int)(avg * 8 / 90);
      if (idx > 8) idx = 8;
      line[cx] = ramp[idx];
    }
    line[100] = 0;
    Serial.println(line);
  }
}

static void dumpGames() {
  Serial.printf("[games] %u  filter=%d page=%u net=%d\n", g_gameCount, g_leagueFilter, g_page, (int)g_net);
  for (uint8_t i = 0; i < g_gameCount && i < 12; i++) {
    const Game& g = g_board[i];
    Serial.printf("  %-6s %-4s %-8s %3u  @ %-4s %-8s %3u  %-14s %-10s %s%s\n",
                  g.league, g.away.abbr, g.away.rec, g.away.score,
                  g.home.abbr, g.home.rec, g.home.score,
                  g.status, g.bcast, g.state == GS_LIVE ? "LIVE " : g.state == GS_PRE ? "pre  " : "final",
                  g.isFav ? " *FAV" : "");
  }
}

static void serialConsole() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (!line.length()) return;
  const int sp = line.indexOf(' ');
  const String cmd = sp < 0 ? line : line.substring(0, sp);
  const String arg = sp < 0 ? String() : line.substring(sp + 1);

  if (cmd == "proxy")       { g_set.proxy = arg;  settingsSave(); }
  else if (cmd == "token")  { g_set.token = arg;  settingsSave(); }
  else if (cmd == "favs")   { g_set.favs = arg;   settingsSave(); }
  else if (cmd == "lgs")    { g_set.leagues = arg; settingsSave(); }
  else if (cmd == "region") { g_set.region = arg; settingsSave(); }
  else if (cmd == "tz")     {
    // Prefer an IANA name and set BOTH forms through the table, exactly as the
    // settings screen and the portal do. Writing g_set.tz directly is what a
    // POSIX rule needs, but it leaves tzIana empty — and then the proxy is
    // sent "UTC" and answers with the wrong day's fixtures.
    if (tzApply(arg.c_str())) {
      Serial.printf("[cli] tz %s (%s)\n", g_set.tzIana.c_str(), g_set.tz.c_str());
    } else {
      g_set.tz = arg;                       // raw POSIX, for anything off-table
      setenv("TZ", g_set.tz.c_str(), 1); tzset();
      Serial.printf("[cli] tz POSIX %s — no IANA name, the proxy will get %s\n",
                    g_set.tz.c_str(), tzForProxy());
    }
    settingsSave();
  }
  else if (cmd == "poll")   { s_nextPollMs = 0; Serial.println("[cli] poll queued"); return; }
  else if (cmd == "reboot") { Serial.println("[cli] rebooting"); delay(50); ESP.restart(); }
  else if (cmd == "shot")   { dumpScreen(); return; }
  else if (cmd == "games")  { dumpGames(); return; }
  else if (cmd == "open") {
    const int n = arg.toInt();
    if (n >= 0 && n < g_gameCount) { uiGameOpen(g_board[n]); Serial.printf("[cli] opened %s\n", g_board[n].id); }
    else Serial.println("[cli] no such game");
    return;
  }
  else if (cmd == "back") {
    if (uiPlayerIsOpen())        uiPlayerClose();
    else if (uiLineupIsOpen())   uiLineupClose();
    else if (uiGameIsOpen())     uiGameClose();
    else if (uiNewsIsOpen())     uiNewsClose();
    else                         uiStandingsClose();
    return;
  }
  else if (cmd == "table") { uiStandingsOpen(arg.length() ? arg.c_str() : "mlb"); return; }
  else if (cmd == "news")  { uiNewsOpen(); return; }
  else if (cmd == "player") { uiPlayerOpen(arg.length() ? arg.c_str() : "mlb", "41996"); return; }
  else if (cmd == "lineup") {
    const int n = arg.toInt();
    if (n >= 0 && n < g_gameCount) uiLineupOpen(g_board[n].league, g_board[n].id);
    else Serial.println("[cli] no such game");
    return;
  }
  else if (cmd == "page")  { Serial.printf("[cli] paged=%d page=%u\n", uiBoardPage(arg.toInt() ? arg.toInt() : 1), g_page); return; }
  else if (cmd == "testalert") {
    AlertEvent e{};
    e.seq = 0;                       // 0 = never advances the committed sequence
    strncpy(e.verb, arg.length() ? arg.c_str() : "GOAL", sizeof e.verb - 1);
    strncpy(e.abbr, g_gameCount ? g_board[0].home.abbr : "TOR", sizeof e.abbr - 1);
    strncpy(e.who, "Auston Matthews", sizeof e.who - 1);
    strncpy(e.detail, "asst. Nylander, Rielly", sizeof e.detail - 1);
    strncpy(e.status, "3rd - 04:21", sizeof e.status - 1);
    if (g_gameCount) {
      strncpy(e.gameId, g_board[0].id, sizeof e.gameId - 1);
      e.color = g_board[0].home.color;
      e.scoreAway = g_board[0].away.score;
      e.scoreHome = g_board[0].home.score + 1;
    } else { e.color = 0x00205B; e.scoreHome = 1; }
    uiAlertEnqueue(e);
    Serial.println("[cli] alert queued");
    return;
  }
  else if (cmd != "show")   { Serial.println("[cli] proxy|token|favs|lgs|region|tz|poll|show|games|open|back|page|table|news|lineup|player|shot|testalert|reboot"); return; }

  Serial.printf("[cli] proxy='%s' token=%s favs='%s' lgs='%s' rgn=%s games=%u net=%d\n",
                g_set.proxy.c_str(), g_set.token.length() ? "set" : "none",
                g_set.favs.c_str(), g_set.leagues.c_str(), g_set.region.c_str(),
                g_gameCount, (int)g_net);
  if (cmd != "show") s_nextPollMs = 0;
}

static void applyLineup() {
  if (g_lineupReady) { g_lineupReady = false; if (uiLineupIsOpen()) uiLineupApply(); }
  if (g_playerReady) { g_playerReady = false; if (uiPlayerIsOpen()) uiPlayerRender(); }
}

static void applyNews() {
  if (!g_newsReady) return;
  g_newsReady = false;
  if (uiNewsIsOpen()) uiNewsRender();
}

static void applyStandings() {
  if (!g_standingsReady) return;
  g_standingsReady = false;
  if (uiStandingsIsOpen()) uiStandingsRender();
}

static void applyGame() {
  if (!g_pendGameReady) return;
  g_pendGameReady = false;
  uiGameApply(g_pendGame);
}

/** Refresh the open game roughly as often as the board polls. */
static void tickOpenGame() {
  static uint32_t next = 0;
  if (!uiGameIsOpen()) { next = 0; return; }
  if (millis() < next) return;
  next = millis() + 15000;
  for (uint8_t i = 0; i < g_gameCount; i++)
    if (strcmp(g_board[i].id, uiGameOpenId()) == 0) {
      apiGameStart(g_board[i].league, g_board[i].id);
      return;
    }
}

static void tickClock() {
  if (millis() - s_lastClockMs < 1000) return;
  s_lastClockMs = millis();
  time_t now = time(nullptr);
  if (now < 100000) { uiSetClock("--:--", "no clock"); return; }
  struct tm lt;
  localtime_r(&now, &lt);
  char hhmm[12], date[24];
  clockFormat(lt, hhmm, sizeof hhmm);
  strftime(date, sizeof date, "%a %b %e", &lt);
  uiSetClock(hhmm, date);
  uiIdleTick();
}

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.printf("\n[sd] ScoreDeck %s\n", SD_VERSION);

  stateInit();

  if (!halDisplayInit()) {
    // The #1 black-screen cause is PSRAM not set to OPI. Say so on the wire.
    Serial.println("[sd] FATAL: display init failed — check PSRAM = OPI PSRAM");
  }
  themeInit();
  uiInit();
  uiIdleInit(lv_scr_act());
  uiAlertInit(lv_scr_act());
  uiGameInit(lv_scr_act());
  uiStandingsInit(lv_scr_act());
  uiNewsInit(lv_scr_act());
  uiLineupInit(lv_scr_act());
  uiSettingsInit(lv_scr_act());

  if (g_set.tz.length()) setenv("TZ", g_set.tz.c_str(), 1);
  tzset();

  // No credentials yet → onboarding must happen on the panel, because there is
  // no network to serve a browser portal over.
  if (g_set.ssid.isEmpty()) {
    uiSetupInit(lv_scr_act());
    uiShow(SCR_SETUP);
  } else {
    wifiConnect();
    g_net = NET_NOWIFI;
  }
  Serial.printf("[sd] ssid='%s' proxy='%s' rgn=%s tz=%s favs='%s'\n",
                g_set.ssid.c_str(), g_set.proxy.c_str(), g_set.region.c_str(),
                g_set.tz.c_str(), g_set.favs.c_str());
  s_nextPollMs = millis() + 1500;
}

void loop() {
  lv_timer_handler();
  webLoop();
  serialConsole();
  tickClock();
  uiAlertTick();
  uiToastTick();
  uiFocusTick();
  uiSettingsTick();

  if (!uiSetupActive()) {
    if (WiFi.status() != WL_CONNECTED) {
      if (g_net != NET_NOWIFI) { g_net = NET_NOWIFI; uiSetStatus(); }
      if (millis() - s_lastWifiTry > WIFI_RETRY_MS) {
        s_lastWifiTry = millis();
        wifiConnect();
      }
    } else {
      static bool announced = false;
      if (!announced) {
        announced = true;
        webBegin();
        Serial.printf("[net] wifi up, ip=%s rssi=%d heap=%u largest=%u\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
      }
      // One missing logo at a time, and only when nothing else needs the
      // single TLS slot.
      static uint32_t nextLogo = 0;
      if (millis() > nextLogo && !g_pollInFlight) { logoTick(); nextLogo = millis() + 1200; }

      if (millis() > s_nextPollMs) {
      // Check the gate in LOOP context before spawning — spawning only to find
      // it shut burns the very RAM the gate protects. INHERITED_RULES.md §14.
      if (apiPollStart()) {
        s_nextPollMs = millis() + (uint32_t)s_pollGapS * 1000UL;
      } else {
        // Break the decline down by cause. "Poll declined" on its own is
        // unactionable — three causes need three counters, and the
        // diagnostics page reports them separately for exactly that reason.
        if (!netGateOpen())            s_declGate++;
        else if (g_pollInFlight)       s_declFlight++;
        else if (g_set.proxy.isEmpty()) s_declNoProxy++;
        Serial.printf("[net] poll declined (gate=%d inflight=%d proxy=%d)\n",
                      netGateOpen(), g_pollInFlight, !g_set.proxy.isEmpty());
        s_nextPollMs = millis() + 3000;
      }
      }
    }
    applyPending();
    applyGame();
    applyStandings();
    applyNews();
    applyLineup();
    // Repaint once when a logo lands, not on every tick — a pointless
    // traversal every 4 ms is the shape of the bug in INHERITED_RULES.md 8.
    if (g_logoArrived) { g_logoArrived = false; uiBoardRefresh(); }
    // A headshot lands AFTER the player sheet has already drawn its fallback,
    // so without this the picture is fetched, stored, and never shown. The
    // flag was being set by the fetch task and read by nothing —
    // INHERITED_RULES.md §19, the same shape as the situation field.
    if (g_headshotArrived) {
      g_headshotArrived = false;
      if (uiPlayerIsOpen()) uiPlayerRender();
    }
    tickOpenGame();
    uiSetStatus();
  }

  delay(4);
}

/** Distinguishes a crash loop from a user reboot — one esp_reset_reason(). */
static const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "POWER_ON";
    case ESP_RST_SW:       return "SW_RESTART";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP:return "DEEP_SLEEP";
    default:               return "UNKNOWN";
  }
}

// ── diagnostics ────────────────────────────────────────────────────────────
// The sketch owns the poll loop, so it owns these numbers. web.cpp only
// formats them.

uint16_t webNextPollSecs() {
  const uint32_t now = millis();
  return s_nextPollMs > now ? (uint16_t)((s_nextPollMs - now) / 1000) : 0;
}

void webCollectDiag(WebDiag& d) {
  d.resetReason  = resetReasonName();
  d.pollAgeS     = s_lastPollMs ? (millis() - s_lastPollMs) / 1000 : 0;
  d.pollCode     = apiLastPollCode();
  d.pollMs       = apiLastPollMs();
  d.declGate     = s_declGate;
  d.declFlight   = s_declFlight;
  d.declNoProxy  = s_declNoProxy;
  d.stale        = (g_net == NET_STALE);
  d.proxySeq     = g_pendSeq;
  d.logoHit      = logoCacheHits();
  d.logoMiss     = logoCacheMisses();
}
