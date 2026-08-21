// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// main.cpp — run ScoreDeck's UI in a window on a Mac, or render it headless.
//
//   ./scoredeck-ui                        interactive, SDL window
//   ./scoredeck-ui --shot out.bmp         headless: one frame to a BMP
//   ./scoredeck-ui --scenario 4           pick the starting scenario
//   ./scoredeck-ui --screen lineup        jump straight to a screen
//
// The headless mode is not an afterthought. It means a UI change can be
// looked at in the same turn it is written, without a device, and it means
// `make shots` produces a reviewable contact sheet of every edge case.
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <lvgl.h>
#include "Arduino.h"
#include "scenarios.h"
#include "lint_fonts.h"
#include "mockup.h"
#include "spike.h"
#include "../firmware/ScoreDeck/src/config.h"
#include "../firmware/ScoreDeck/src/core/state.h"
#include "../firmware/ScoreDeck/src/ui/ui.h"
#include "../firmware/ScoreDeck/src/ui/theme.h"

static SDL_Window*   s_win;
static SDL_Renderer* s_ren;
static SDL_Texture*  s_tex;
static uint16_t      s_fb[SCR_W * SCR_H];      // our own copy, for --shot
static int           s_scenario = SCN_TYPICAL;

// Read by spike.cpp to measure pixels-flushed per redraw against the
// documented ~50,000 px/tick budget (pulse.cpp). Not used outside --spike.
volatile uint32_t g_spikePx = 0;
volatile uint32_t g_spikeFlushN = 0;
volatile int32_t  g_spikeX1 = 0, g_spikeY1 = 0, g_spikeX2 = 0, g_spikeY2 = 0;

/**
 * Flush handler. Writes into s_fb as well as the texture so --shot works with
 * no window at all — SDL_INIT_VIDEO is never called in headless mode.
 */
static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
  g_spikePx += (uint32_t)(a->x2 - a->x1 + 1) * (uint32_t)(a->y2 - a->y1 + 1);
  if (g_spikeFlushN == 0) { g_spikeX1 = a->x1; g_spikeY1 = a->y1; g_spikeX2 = a->x2; g_spikeY2 = a->y2; }
  else {
    if (a->x1 < g_spikeX1) g_spikeX1 = a->x1;
    if (a->y1 < g_spikeY1) g_spikeY1 = a->y1;
    if (a->x2 > g_spikeX2) g_spikeX2 = a->x2;
    if (a->y2 > g_spikeY2) g_spikeY2 = a->y2;
  }
  g_spikeFlushN++;
  for (int y = a->y1; y <= a->y2; y++) {
    for (int x = a->x1; x <= a->x2; x++) {
      s_fb[y * SCR_W + x] = px[(y - a->y1) * (a->x2 - a->x1 + 1) + (x - a->x1)].full;
    }
  }
  if (s_tex) {
    SDL_Rect r{ a->x1, a->y1, a->x2 - a->x1 + 1, a->y2 - a->y1 + 1 };
    SDL_UpdateTexture(s_tex, &r, px, (a->x2 - a->x1 + 1) * sizeof(lv_color_t));
  }
  lv_disp_flush_ready(drv);
}

// Mouse becomes the touch panel, so click and drag exercise the same handlers
// a finger does — including the gestures that page the board.
static void read_cb(lv_indev_drv_t*, lv_indev_data_t* data) {
  int x, y;
  const uint32_t b = SDL_GetMouseState(&x, &y);
  data->point.x = x;
  data->point.y = y;
  data->state = (b & SDL_BUTTON(SDL_BUTTON_LEFT)) ? LV_INDEV_STATE_PRESSED
                                                  : LV_INDEV_STATE_RELEASED;
}

/** 24-bit BMP, bottom-up — the same shape the device's own capture uses. */
bool writeBmp(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "cannot write %s\n", path); return false; }
  const int rowBytes = (SCR_W * 3 + 3) & ~3;
  const uint32_t dataSize = rowBytes * SCR_H;
  const uint32_t fileSize = 54 + dataSize;
  uint8_t h[54] = {};
  h[0] = 'B'; h[1] = 'M';
  memcpy(h + 2, &fileSize, 4);
  const uint32_t off = 54; memcpy(h + 10, &off, 4);
  const uint32_t hdr = 40;  memcpy(h + 14, &hdr, 4);
  const int32_t w = SCR_W, ht = SCR_H;
  memcpy(h + 18, &w, 4); memcpy(h + 22, &ht, 4);
  const uint16_t planes = 1, bpp = 24;
  memcpy(h + 26, &planes, 2); memcpy(h + 28, &bpp, 2);
  memcpy(h + 34, &dataSize, 4);
  fwrite(h, 1, 54, f);

  uint8_t* row = (uint8_t*)calloc(1, rowBytes);
  for (int y = SCR_H - 1; y >= 0; y--) {
    for (int x = 0; x < SCR_W; x++) {
      const uint16_t p = s_fb[y * SCR_W + x];
      // RGB565 -> RGB888 with the low bits replicated, so greys stay neutral.
      const uint8_t r = ((p >> 11) & 0x1f) << 3 | ((p >> 13) & 0x07);
      const uint8_t g = ((p >> 5) & 0x3f) << 2 | ((p >> 9) & 0x03);
      const uint8_t b = (p & 0x1f) << 3 | ((p >> 2) & 0x07);
      row[x * 3 + 0] = b; row[x * 3 + 1] = g; row[x * 3 + 2] = r;
    }
    fwrite(row, 1, rowBytes, f);
  }
  free(row);
  fclose(f);
  return true;
}

static void showScreen(const char* name) {
  if (!name) return;
  if      (!strcmp(name, "board"))     uiShow(SCR_BOARD);
  else if (!strcmp(name, "idle"))      uiShow(SCR_IDLE);
  else if (!strcmp(name, "standings")) uiStandingsOpen("nhl");
  else if (!strcmp(name, "news"))      uiNewsOpen();
  else if (!strcmp(name, "reader")) {
    uiNewsOpen();
    static NewsItem it;
    memset(&it, 0, sizeof it);
    strcpy(it.id, "49632941");
    strcpy(it.headline, "Guardians' DeLauter exits with left hamstring tightness");
    strcpy(it.desc, "The rookie left Sunday's game in the third inning.");
    strcpy(it.abbr, "CLE");
    it.color = 0xE31937;
    it.when = (uint32_t)time(nullptr) - 7200;
    uiReaderOpen(it);
  }
  else if (!strcmp(name, "lineup"))    uiLineupOpen("nhl", "900000");
  // The player sheet is an overlay on the lineup screen, not a screen of its
  // own — open the lineup underneath it or the sheet has nothing to sit on.
  else if (!strcmp(name, "player"))  { uiLineupOpen("nhl", "900000");
                                       uiPlayerOpen("nhl", "3024"); }
  else if (!strcmp(name, "setup"))     uiShow(SCR_SETUP);
  // The on-device keyboard is 41.7% of the panel and sits in the first-run
  // flow, and there was no way to look at it without a device — which is why
  // it was the one surface still drawn by lv_theme_default.
  else if (!strcmp(name, "setup-kb"))  { uiShow(SCR_SETUP); uiSetupShowKeyboard(); }
  else if (!strcmp(name, "settings"))  uiSettingsOpen();
  else if (!strcmp(name, "settings-sports")) { uiSettingsOpen(); uiSettingsTab(1); }
  else if (!strcmp(name, "settings-teams")) { uiSettingsOpen(); uiSettingsTab(2); }
  else if (!strcmp(name, "settings-net")) { uiSettingsOpen(); uiSettingsTab(3); }
  else if (!strcmp(name, "settings-sys")) { uiSettingsOpen(); uiSettingsTab(4); }
  else if (!strcmp(name, "settings-tz"))  { uiSettingsOpen(); uiSettingsTab(3); uiSettingsTzOpen(); }
  else if (!strcmp(name, "alert"))     scenarioFireAlert();
  else if (!strcmp(name, "banner"))    scenarioFireBanner();
  else if (!strcmp(name, "game") && g_gameCount) uiGameOpen(g_board[0]);
  else fprintf(stderr, "unknown screen '%s'\n", name);
}

static void help() {
  printf("\nkeys:  1..%d scenario   SPACE next   b/i/g/s/n/l screen   p shot.bmp   q quit\n",
         SCN_COUNT);
  for (int i = 0; i < SCN_COUNT; i++) printf("   %d  %s\n", i + 1, scenarioName(i));
  printf("\nnow: %s\n", scenarioName(s_scenario));
}

int main(int argc, char** argv) {
  const char* shot = nullptr;
  int settleMs = 0;
  const char* screen = nullptr;
  bool lint = false;
  bool spike = false;
  int  mock = -1;
  int  density = -1;
  bool railOpen = false;
  const char* measure = nullptr;
  for (int i = 1; i < argc; i++) {
    if      (!strcmp(argv[i], "--shot") && i + 1 < argc)     shot = argv[++i];
    else if (!strcmp(argv[i], "--settle") && i + 1 < argc)   settleMs = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--scenario") && i + 1 < argc) s_scenario = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--screen") && i + 1 < argc)   screen = argv[++i];
    else if (!strcmp(argv[i], "--lint"))                     lint = true;
    else if (!strcmp(argv[i], "--spike"))                    spike = true;
    else if (!strcmp(argv[i], "--mock") && i + 1 < argc)     mock = atoi(argv[++i]);
    // AUTO now resolves to the FEATURE layout whenever one to three games are
    // live, which is most evenings — so the three grid densities became hard
    // to reach from a scenario alone. 0=roomy 1=standard 2=dense 3=auto.
    else if (!strcmp(argv[i], "--density") && i + 1 < argc)  density = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--rail"))                     railOpen = true;
    // --measure <what>: report pixels flushed for one interaction, so a
    // redraw-cost claim can be a number rather than an argument. The counters
    // are the ones spike.cpp already reads.
    else if (!strcmp(argv[i], "--measure") && i + 1 < argc)   measure = argv[++i];
    else if (!strcmp(argv[i], "--help")) { help(); return 0; }
  }
  if (s_scenario < 0 || s_scenario >= SCN_COUNT) s_scenario = SCN_TYPICAL;

  if (SDL_Init((shot || lint || spike || measure) ? 0 : SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  lv_init();
  static lv_disp_draw_buf_t db;
  static lv_color_t buf[SCR_W * 30];      // 800x30, same as the panel's
  lv_disp_draw_buf_init(&db, buf, nullptr, SCR_W * 30);
  static lv_disp_drv_t dd;
  lv_disp_drv_init(&dd);
  dd.draw_buf = &db;
  dd.flush_cb = flush_cb;
  dd.hor_res = SCR_W;
  dd.ver_res = SCR_H;
  lv_disp_drv_register(&dd);

  if (!shot && !lint && !measure) {
    s_win = SDL_CreateWindow("ScoreDeck", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             SCR_W, SCR_H, SDL_WINDOW_ALLOW_HIGHDPI);
    s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED);
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_RGB565,
                              SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
    static lv_indev_drv_t id;
    lv_indev_drv_init(&id);
    id.type = LV_INDEV_TYPE_POINTER;
    id.read_cb = read_cb;
    lv_indev_drv_register(&id);
  }

  stateInit();
  // Before uiInit(), which builds the tile array for whatever density is in
  // force — setting it afterwards would leave the array and the layout
  // disagreeing, which is the shape of the LoadProhibited panic scenario 9
  // exists to catch.
  if (density >= 0 && density < DEN_COUNT) g_set.density = (uint8_t)density;
  themeInit();
  if (railOpen) uiRailToggle();     // before uiInit builds the first layout
  plateInit();      // behind every screen; must precede uiInit()
  bloomInit();
  uiInit();
  uiIdleInit(lv_scr_act());
  uiAlertInit(lv_scr_act());
  uiGameInit(lv_scr_act());
  uiStandingsInit(lv_scr_act());
  uiNewsInit(lv_scr_act());
  uiReaderInit(lv_scr_act());
  uiLineupInit(lv_scr_act());
  uiSetupInit(lv_scr_act());
  uiSettingsInit(lv_scr_act());

  scenarioApply(s_scenario);
  showScreen(screen);
  scenarioReapply(s_scenario);
  if (mock >= 0) mockupApply(mock);

  if (spike) {
    uiShow(SCR_BOARD);
    lv_refr_now(nullptr);
    spikeRun();
    SDL_Quit();
    return 0;
  }

  if (lint) {
    // Every screen, on every scenario. A face is only wrong for the text it is
    // actually holding, so coverage here means visiting both.
    static const char* kScreens[] = { "board", "idle", "game", "standings",
                                      "news", "lineup", "player", "alert", "setup" };
    for (int s = 0; s < SCN_COUNT; s++) {
      scenarioApply(s);
      for (const char* sc : kScreens) {
        showScreen(sc);
        scenarioReapply(s);
        uiIdleTick();
    uiReaderTick();
        lv_refr_now(nullptr);
        char label[64];
        snprintf(label, sizeof label, "%-10s  %s", sc, scenarioName(s));
        lintScreen(label);
      }
    }
    const int bad = lintTotal();
    printf("\n%d label%s the assigned face cannot render\n", bad, bad == 1 ? "" : "s");
    SDL_Quit();
    return bad ? 1 : 0;
  }

  if (measure) {
    // Settle the board first so the numbers below are the INTERACTION's cost,
    // not the boot paint's.
    uiIdleTick(); uiReaderTick(); uiAlertTick();
    for (int i = 0; i < 3; i++) { lv_refr_now(nullptr); lv_timer_handler(); }

    uint32_t total = 0, worstPass = 0, passes = 0;
    auto pump = [&](int ms) {
      const uint32_t t0 = millis();
      while (millis() - t0 < (uint32_t)ms) {
        g_spikePx = 0;
        uiAlertTick(); uiIdleTick(); uiReaderTick();
        lv_timer_handler();
        const uint32_t d = g_spikePx;
        if (d) { passes++; total += d; if (d > worstPass) worstPass = d; }
        usleep(2000);
      }
    };

    if (!strcmp(measure, "alert")) {
      scenarioFireAlert();
      pump(600);                      // 4 rungs x 70 ms, with room to settle
      printf("alert-present  %u px total, %u flushing passes, worst pass %u px\n",
             total, passes, worstPass);
    } else if (!strcmp(measure, "idle")) {
      showScreen("idle");
      for (int i = 0; i < 3; i++) { lv_refr_now(nullptr); lv_timer_handler(); }
      pump(3000);                     // three simulated seconds
      printf("idle-3s        %u px total, %u flushing passes, worst pass %u px\n",
             total, passes, worstPass);
    } else if (!strcmp(measure, "poll")) {
      scenarioReapply(s_scenario);
      for (int i = 0; i < 2; i++) { lv_refr_now(nullptr); lv_timer_handler(); }
      total = 0; passes = 0; worstPass = 0;
      scenarioReapply(s_scenario);    // identical data, second time
      pump(200);
      printf("poll-nochange  %u px total, %u flushing passes, worst pass %u px\n",
             total, passes, worstPass);
    } else if (!strcmp(measure, "logos")) {
      // What the logo fetcher can SEE. logoTick() asks each on-screen surface
      // which game it is showing and fetches the first mark it has no blob
      // for; a surface it does not ask about can never fill in. It used to ask
      // the tile strip alone, and in the featured layout the tile strip is the
      // one surface whose teams are not the ones being looked at — the hero is
      // excluded from a slot by construction and a ledger final never had one.
      // So this prints the reachable set per surface: if TILES is empty while
      // HERO and LEDGER are not, the old walk fetched nothing at all.
      for (int i = 0; i < 3; i++) { lv_refr_now(nullptr); lv_timer_handler(); }
      const int8_t hero = uiHeroGameIdx();
      int tiles = 0, ledger = 0;
      printf("HERO    %s", hero >= 0 ? "" : "(none)");
      if (hero >= 0) printf("%s @ %s", g_board[hero].away.abbr, g_board[hero].home.abbr);
      printf("\n");
      printf("TILES   ");
      for (uint8_t t = 0; t < TILES_PER_PAGE; t++) {
        const int8_t gi = uiBoardTileGame(t);
        if (gi < 0 || gi >= g_gameCount) continue;
        printf("%s%s @ %s", tiles++ ? ", " : "", g_board[gi].away.abbr, g_board[gi].home.abbr);
      }
      printf("%s\n", tiles ? "" : "(none)");
      printf("LEDGER  ");
      for (int k = 0; k < uiLedgerCount(); k++) {
        const int8_t gi = uiLedgerGame((uint8_t)k);
        if (gi < 0 || gi >= g_gameCount) continue;
        printf("%s%s @ %s", ledger++ ? ", " : "", g_board[gi].away.abbr, g_board[gi].home.abbr);
      }
      printf("%s\n", ledger ? "" : "(none)");
      const Game* nx = uiIdleNextGame();
      printf("IDLE    %s\n", nx ? "" : "(none, idle not showing)");
      if (nx) printf("        %s @ %s\n", nx->away.abbr, nx->home.abbr);
      printf("reachable: old walk %d game(s), new walk %d game(s)\n",
             tiles, tiles + ledger + (hero >= 0 ? 1 : 0) + (nx ? 1 : 0));
    } else {
      fprintf(stderr, "--measure: expected alert|idle|poll|logos\n");
      SDL_Quit(); return 2;
    }
    SDL_Quit();
    return 0;
  }

  if (shot) {
    // Two passes: the first lays out, the second paints everything the layout
    // moved. One pass leaves half the board unflushed.
    // Tick the per-second surfaces once so the clock and countdown are
    // populated, then two refresh passes: the first lays out, the second
    // paints what the layout moved.
    uiIdleTick();
    uiReaderTick();
    uiAlertTick();
    for (int i = 0; i < 2; i++) { lv_refr_now(nullptr); lv_timer_handler(); }
    // --settle <ms>: let one-shot animations FINISH before the capture.
    // Without it every FEATURE screenshot is a rung-0 frame: the hero's staged
    // reveal runs on an 80 ms lv_timer and the two passes above complete in
    // microseconds, so the card is caught at 33% opacity — which is why the
    // glow's banding was under-reported for so long (the plate's Bayer dither
    // shows through a translucent card and accidentally dithers it).
    if (settleMs > 0) {
      const uint32_t t0 = millis();
      while (millis() - t0 < (uint32_t)settleMs) { lv_timer_handler(); usleep(2000); }
      lv_refr_now(nullptr);
    }
    const bool ok = writeBmp(shot);
    printf("%s  %s  (%s)\n", ok ? "wrote" : "FAILED", shot, scenarioName(s_scenario));
    SDL_Quit();
    return ok ? 0 : 1;
  }

  help();
  bool run = true;
  while (run) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) run = false;
      else if (e.type == SDL_KEYDOWN) {
        const SDL_Keycode k = e.key.keysym.sym;
        if (k == SDLK_q || k == SDLK_ESCAPE) run = false;
        else if (k == SDLK_SPACE) { s_scenario = (s_scenario + 1) % SCN_COUNT; scenarioApply(s_scenario); help(); }
        else if (k >= SDLK_1 && k < SDLK_1 + SCN_COUNT) { s_scenario = k - SDLK_1; scenarioApply(s_scenario); help(); }
        else if (k == SDLK_b) uiShow(SCR_BOARD);
        else if (k == SDLK_i) uiShow(SCR_IDLE);
        else if (k == SDLK_s) uiStandingsOpen("nhl");
        else if (k == SDLK_n) uiNewsOpen();
        else if (k == SDLK_l) uiLineupOpen("eng.1", "900000");
        else if (k == SDLK_g && g_gameCount) uiGameOpen(g_board[0]);
        else if (k == SDLK_p) { lv_refr_now(nullptr); writeBmp("shot.bmp"); printf("wrote shot.bmp\n"); }
      }
    }
    lv_timer_handler();
    uiAlertTick();
    uiIdleTick();
    uiReaderTick();
    if (s_ren) {
      SDL_RenderClear(s_ren);
      SDL_RenderCopy(s_ren, s_tex, nullptr, nullptr);
      SDL_RenderPresent(s_ren);
    }
    SDL_Delay(5);
  }
  SDL_Quit();
  return 0;
}
