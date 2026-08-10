# ScoreDeck — implementation plan

A 7" desk panel that shows live scores, game detail, rosters, stats and news
for the teams you follow, and takes over the screen when they score.

Same hardware as AirRadar: **Waveshare ESP32-S3-Touch-LCD-7**, 800×480 RGB565,
GT911 touch, 16 MB flash / 8 MB OPI PSRAM. Clean-slate firmware, LVGL 8.3.

| Decision | Choice |
|---|---|
| Data path | Cloud proxy — device makes one TLS call per poll |
| Leagues | All of them: US majors, college, soccer, women's, F1/tennis/golf |
| Alerts | On-screen takeover (no onboard audio on this board) |
| Codebase | Clean slate, carrying the hardware rules in [`INHERITED_RULES.md`](INHERITED_RULES.md) |
| Release | Public, like AirRadar — see [`OPEN_SOURCE.md`](OPEN_SOURCE.md) for what that changes |
| Interface | Glass as material, Standard 9-up — full spec in [`UI.md`](UI.md) |

| Document | Covers |
|---|---|
| [`UI.md`](UI.md) | Screen geometry, tokens, tile anatomy, interaction map |
| [`OPEN_SOURCE.md`](OPEN_SOURCE.md) | Licensing, deploy-your-own proxy, what may not be committed |
| [`INHERITED_RULES.md`](INHERITED_RULES.md) | The 22 hardware lessons AirRadar paid for |
| [`HARDWARE.md`](HARDWARE.md) | Pin map, GT911 reset, toolchain |

---

## 1. The idea that makes "all sports" tractable

Thirty leagues is not thirty firmware features. Every ESPN payload has the same
skeleton — `events[] → competitions[] → competitors[]` with
`status.type.state ∈ {pre, in, post}` — and the only thing that genuinely
differs is **how a score-in-progress is described**. There are five of those:

| Model | Renders as | Leagues |
|---|---|---|
| `CLOCK` | period + running clock | NFL, NCAAF, NBA, WNBA, NCAAM/W BB, NHL, NCAAW hockey, all soccer, lacrosse, volleyball |
| `INNING` | inning + top/bot arrow + bases + outs | MLB, college baseball, college softball |
| `SET` | per-set boxes + current game score | ATP, WTA |
| `LEADERBOARD` | to-par, thru, position | PGA, LPGA |
| `GRID` | position, lap, gap to leader | F1 |

**The firmware implements five score models. The proxy owns a league registry.**
Adding a league after v1 is a row in a TypeScript table plus a logo build — no
reflash, no firmware release. That is the single most important structural
decision in this plan, and it is why the "everything" scope is affordable.

Of the chosen scope, only `SET`, `LEADERBOARD` and `GRID` are new firmware work
(Phase 7). Everything else — the four majors, every soccer competition, every
NCAA sport, every women's league — is `CLOCK` or `INNING` and lands as registry
rows in Phase 6.

---

## 2. Why a proxy is not optional

Measured against the live ESPN API on 2026-08-09:

| Endpoint | Bytes |
|---|---|
| `golf/pga/scoreboard` | **1,222,947** |
| `tennis/atp/scoreboard` | **657,924** |
| `hockey/nhl/teams/21/roster` | 114,532 |
| `basketball/wnba/scoreboard` | 82,791 |
| `hockey/nhl/summary?event=…` | 71,738 |
| one 500px team logo PNG | 56,235 |

The device has roughly **17–40 KB of free internal heap** and mbedTLS needs a
~16.4 KB contiguous block for a single handshake. It cannot hold two TLS
connections, cannot buffer a 1.2 MB response, and has no business decoding
56 KB PNGs. The proxy turns all of the above into **one ~3 KB JSON per poll and
one connection**.

Three benefits beyond size, worth stating because they justify the ops cost:

1. **ESPN's API is undocumented and unofficial.** When it changes shape or
   starts blocking, you edit a Worker and redeploy in 60 seconds. Without the
   proxy, every break is a firmware flash. The proxy is the blast shield.
2. **Score-diffing needs memory across reboots.** "TOR scored" is a *difference*
   between two observations. The proxy holds prior state and hands the device a
   monotonic event sequence, so an alert survives a power cycle and a device
   that was asleep doesn't replay yesterday's goals.
3. **Logos become free.** Pre-converted RGB565A8 blobs, no decoder on device.

**Honest risk:** this is an unofficial API with no SLA and no ToS blessing for
redistribution, and a breaking change is a Saturday afternoon, not an outage you
can escalate.

Because the project is public, **each user deploys their own proxy** — there is
no shared instance and the firmware ships with an empty proxy URL. A single free
Cloudflare account serves somewhere between 14 and 55 devices before it dies,
and running the shared one would make this project the entity redistributing a
third party's data at scale. The data source is also pluggable rather than
hardwired to ESPN. Both consequences are worked through in
[`OPEN_SOURCE.md`](OPEN_SOURCE.md) §2–3.

---

## 3. Proxy architecture

**Cloudflare Workers + TypeScript + Hono.** Free tier, deployed with Wrangler.
Written to stay portable — Hono runs unmodified on Node/Deno/Bun/Fly.io, and the
only Cloudflare-specific surfaces are the Cache API and Cron Triggers, both
behind a `Store` interface. If Workers ever becomes the wrong home, moving to a
VPS or the Pi that already runs the AirRadar feeder is an afternoon.

### The CPU-limit problem and its answer

Workers free tier allows **10 ms CPU per request** and **1,000 KV writes/day**.
Parsing 1.2 MB of golf JSON per device request blows the first; a 30-second cron
writing to KV blows the second (2,880 writes/day/league).

The design routes around both:

```
Cron Trigger (30 s)                      Device request
  ├─ fetch ESPN per ACTIVE league          GET /v1/state?f=…&seq=…
  ├─ normalize → compact board                  │
  ├─ diff vs prior → score events               ▼
  └─ write to Cache API ────────────────▶ read cache, filter to
     (no write cap, unlike KV)            favorites, emit ~3 KB
```

Heavy work happens once in the cron, not per request. The device path is a cache
read plus a filter — well inside 10 ms. "Active league" means in-season and
followed by at least one device, tracked with a short-TTL cache key so a league
nobody follows costs nothing.

> **Phase-1 spike, must run before committing:** measure actual cron CPU for the
> golf and tennis normalizers. If a scheduled invocation is also capped at 10 ms
> on the free plan, take the **$5/mo Workers Paid** plan (30 s CPU) — this is a
> $60/year decision, not an architecture change. Alternative if you prefer $0:
> run the proxy on the Pi and reach it from outside via Tailscale.

### Endpoints

| Route | Returns | Approx |
|---|---|---|
| `GET /v1/state?f=<favs>&lg=<leagues>&seq=<n>&rgn=<region>&tz=<tz>` | board + pending alert events | 3 KB |
| `GET /v1/game/:league/:id` | linescore, scoring plays, leaders, situation, win probability | 2 KB |
| `GET /v1/lineup/:league/:id` | both lineups by position group, or formation + XI for soccer | 3 KB |
| `GET /v1/player/:league/:id` | season stats **with league rank**, tonight's line, last 5 | 1 KB |
| `GET /v1/standings/:league?grp=<group>` | generic `{columns, rows}` + cut-line positions | 2.5 KB |
| `GET /v1/team/:league/:id` | record, standing, next game, season stats, form guide | 1.5 KB |
| `GET /v1/team/:league/:id/roster` | trimmed roster, paged 25/page | 2 KB |
| `GET /v1/news?f=<favs>&since=<ts>` | headlines + 600-char summaries | 2 KB |
| `GET /v1/logo/:league/:abbr@:size.bin` | LVGL RGB565A8 blob, immutable | 7 KB |
| `GET /v1/head/:league/:athlete.bin` | 68×68 headshot blob, immutable | 14 KB |
| `GET /v1/catalog` | full league + team catalog for the setup portal | — |
| `GET /v1/health` | upstream freshness per league | — |

The proxy is **stateless with respect to device config**. Favorites travel in
the query string (`f=nhl:21,nfl:12,eng.1:359`), capped at 20 entries ≈ 220
chars. No accounts, no database, no per-device state to lose.

### Region — measured, and it changes the design

**ESPN's broadcast data is US-only and the API ignores a region parameter.**
`?region=ca` and `?region=gb` return byte-identical responses, and every
`geoBroadcasts` entry carries `"region":"us"`. On a real November slate it listed
a **Toronto home game as being on NESN** — the Boston regional feed. Premier
League fixtures come back as Peacock and USA Network.

Passing that field through would be actively wrong for everyone outside the US,
so the proxy **resolves** the channel:

| Region | Source | Precision |
|---|---|---|
| `us` | ESPN `geoBroadcasts`, per game, filtered by market — national first, then home/away matched to the side you follow | Exact, per game |
| everything else | `proxy/src/rights.json` — `league × region → networks`, with per-team overrides where regional splits exist | Rights holder, not always the exact channel |

Canada needs the per-team map because its splits are real (SN Ontario, RDS,
TSN's regional feeds). The UK is coarser — a fixture is on Sky *or* TNT and a
static table cannot say which, so it shows both.

`rights.json` is a flat data file with no code around it, which makes adding a
region the best first contribution in the repo.

**Region controls more than the channel.** It seeds which leagues onboarding
offers, it sets 24-hour vs 12-hour time, and — the subtle one — **"today" must be
the device's local day.** Sunday Night Football kicks off at 1:20 a.m. Monday in
London; sorting by US Eastern would make the board disagree with the idle
screen's "games today" count.

**Access control:** a shared bearer token in the `Authorization` header, stored
in NVS and set during onboarding, plus Cloudflare rate limiting. It is a hobby
device, not a bank — but an open proxy that proxies a third-party API is a thing
someone will find and abuse, so the token is not optional.

### Logo pipeline — build-time, not request-time

Workers cannot decode PNG within the free CPU budget, and there is no reason to
try: logos change about once a year.

`tools/build-logos.ts` (Node, run manually or on a monthly GitHub Action):

```
for each league in registry:
    GET /sports/{path}/teams              → team list + logo URLs
    fetch a.espncdn.com/i/teamlogos/…/500/{abbr}.png
    sharp → contain-fit 48×48 and 96×96, preserve alpha
    encode → LVGL LV_IMG_CF_TRUE_COLOR_ALPHA (3 B/px @ 16-bit colour)
    write assets/logos/{league}/{abbr}@{size}.bin
```

48×48 = 6.9 KB, 96×96 = 27.6 KB. Served by Workers Static Assets with
`Cache-Control: immutable`. The device only ever fetches 48px for tiles and 96px
for the alert card and team page.

> **Public-repo constraint:** the generated blobs are derivative works of
> ESPN-hosted assets carrying ~3,000 league and club trademarks. `assets/logos/`
> is **gitignored and never distributed** — the tool ships, the artwork does not,
> and it runs on the user's machine into their own Worker. The device therefore
> must render correctly with zero logos, via a fallback glyph: the team
> abbreviation set in the team's primary colour. Colours are facts the proxy
> already sends; artwork is not. See [`OPEN_SOURCE.md`](OPEN_SOURCE.md) §1 —
> this is the single most likely cause of a takedown and the fallback is a
> better first-paint design anyway.

---

## 4. Device data model

```c
enum ScoreModel { SM_CLOCK, SM_INNING, SM_SET, SM_LEADERBOARD, SM_GRID };
enum GameState  { GS_PRE, GS_LIVE, GS_FINAL };

struct Side {
  char     abbr[5];        // "TOR"
  char     name[20];       // "Maple Leafs"
  uint16_t score;
  uint32_t color;          // team primary, from proxy
  char     record[10];     // "12-4-2"
  uint8_t  rank;           // 0 = unranked (college)
};

struct Game {
  char       id[12];       // ESPN event id
  uint8_t    league;       // index into g_leagues
  ScoreModel model;
  GameState  state;
  Side       away, home;
  char       statusShort[16];  // "3rd 04:21", "Bot 7", "FT", "7:00 PM"
  uint8_t    period;
  uint16_t   situation;    // packed: bases 3b | outs 2b | possession 1b | RZ 1b
  uint32_t   startUtc;
  char       broadcast[10];// "SN", "TNT", "Sky Sports" — region-resolved
  uint8_t    winProbHome;  // 0-100, 255 = unavailable
  bool       leaderIsHome; // drives which side the edge light sits on
  bool       isFavorite;   // drives sort order and alerting
};
```

`Side.rank` carries the AP rank for college and the table position for soccer —
it renders as the inline context chip beside the abbreviation.

**Hard cap of 48 games in the device model.** A Saturday in November has 60+
FBS games and 300+ D1 basketball games. The proxy sorts by favorite → live →
starting-soonest and truncates; the device never sees a list it cannot hold.
This is a real constraint, not a theoretical one, and getting it wrong means an
OOM on exactly the busiest day of the year.

---

## 5. Screens (800 × 480)

Full geometry, tokens, tile anatomy and interaction map live in
[`UI.md`](UI.md) — that document is what the firmware is built against. The
summary that matters at plan level:

**Direction: Route B — glass as material, at Standard 9-up density.**

| Screen | Role |
|---|---|
| **Board** | Home. 3×3 grid of 248×128 tiles, 48 px top bar carrying the league strip. Density is a setting — Roomy 6 / **Standard 9** / Dense 12 — cycled by long-press. |
| **Game detail** | Tap a tile. Header reuses the tile's exact anatomy so it reads as the tile expanding. Tabs: Summary · Stats · Plays · Lineup. |
| **Lineup** | Position groups with tonight's stats; for soccer a **pitch diagram** driven by the `formation` string. Every name is a touch target. |
| **Player sheet** | Fixed 404×324 panel, fades in. 68 px headshot, tonight's line, season stats **with league rank**, last five. |
| **Standings** | Generic `{columns, rows}` table with labelled cut lines. Followed teams keep their edge light. |
| **Idle** | What the panel shows when nothing is live — which is most of the day. Clock, next-up countdown as the hero, today's slate, news. |
| **Alert takeover** | 520×300, four-step fade then completely static. |
| **News · Team · Settings** | As originally planned. |

Three ideas hold the whole thing together, and each is also a performance
decision:

1. **Glass is baked, never blurred.** LVGL 8.3 has no backdrop-filter and the
   panel DMA could not afford one. Chrome bakes into the plate at boot; data is
   flat objects on top. Nothing frosted ever moves — panels fade, never slide.
2. **ScoreDeck has no accent colour.** ~3,000 team colours already arrive as
   content; the chrome stays neutral so every saturated pixel belongs to a team.
3. **Luminance encodes state, colour encodes team.** Live 100%, scheduled 72%,
   final 55%. You read the board peripherally before reading a number, and it
   costs an opacity value.

The signature is the **edge light**: a 3 px luminous strip on the *leading*
team's side of a live tile that **swaps sides when the lead changes**. Visible
across a room, no text, no animation, ~400 px to repaint.


## 6. Onboarding — the part that is easy to get wrong

There are ~3,000 selectable teams across this league scope. A touchscreen
keyboard on an 800×480 panel is a bad way to find "Notts County", and building a
scrolling 360-item picker is worse.

**Setup happens in the browser.** The phone or laptop does the work:

> **Corrected.** This paragraph used to say the device boots into AP mode. It
> does not, and it should not: it boots into the on-panel setup screen, which
> takes Wi-Fi credentials and nothing else. That avoids a captive-portal DNS
> hijack, AP/STA mode switching and a second IP stack — and there is no softAP
> anywhere in the firmware. The browser portal takes over the moment there is a
> network to serve it on.

1. Wi-Fi credentials.
2. Proxy URL + token.
3. **Favorites picker** — a search box over `/v1/catalog`, with type-ahead over
   3,000 teams, league grouping, and drag-to-reorder. Trivial in a browser,
   miserable on the panel.
4. Alert rules per favorite: score / game start / final.
5. Push config to NVS, reboot into the Board.

The on-device settings screen can toggle leagues and reorder existing favorites,
but adding a team always points you at the portal. Accept this asymmetry — it is
the difference between a 30-second setup and a 10-minute one.

**Security carry-over:** the portal has the same exposure AirRadar's did, and
those bugs are documented in [`INHERITED_RULES.md`](INHERITED_RULES.md) §Web —
validate `Host` against the names the device actually answers to *before* the
origin check (DNS rebinding defeats an Origin/Host equality test), and never let
a blank secret field overwrite a stored secret.

---

## 7. Polling and alert latency

| Condition | Device poll |
|---|---|
| A followed team is live | **12 s** |
| Any game live in a followed league | 30 s |
| Game today, not started | 60 s |
| Nothing today | 5 min |
| Quiet hours | 15 min, alerts suppressed |

Alert latency ≈ ESPN's own lag (10–40 s) + cron age (≤30 s) + poll (≤12 s).
Realistically **30–80 seconds behind the broadcast**, which is *ahead of a
streaming feed* and behind live TV. State this expectation up front; it is the
one thing that will feel wrong if it is unexplained.

The device sends its last-seen `seq`; the proxy returns only events after it and
the device commits the new `seq` to NVS **after** the alert renders, so a reboot
mid-alert replays rather than swallows it.

---

## 8. Firmware structure

```
firmware/ScoreDeck/
  ScoreDeck.ino
  src/
    config.h              all geometry, timings, NVS keys — no magic numbers
    hal/hal_display.*     panel init, CH422G, GT911 reset sequence
    core/types.h          Game, Side, Alert, ScoreModel
    core/state.*          g_set, g_board, g_dataMux, TLS gate
    core/board.*          list ownership, sort, lifecycle, 48-cap
    net/api.*             proxy client + ArduinoJson filters
    net/logos.*           RAM(24) → FATFS → network, 3-tier LRU
    ui/theme.*            palette + font tokens
    ui/ui_board.cpp   ui/ui_game.cpp   ui/ui_team.cpp
    ui/ui_news.cpp    ui/ui_alert.cpp  ui/ui_settings.cpp
    svc/web.*             setup portal + OTA
    assets/               fonts, icons
proxy/
  src/index.ts  src/registry.ts  src/normalize/{clock,inning,set,board,grid}.ts
  src/diff.ts   src/news.ts      src/store.ts
  tools/build-logos.ts
  assets/logos/…
```

### Threading contract (carried from AirRadar unchanged — it is correct)

```
core 1  loop()  ──▶ LVGL · touch · g_board · NVS
                     ▲  pending buffers, guarded by g_dataMux
core 0  tasks   ──▶ state poll · game detail · roster · news · logos
```

`loop()` owns all LVGL, all of `g_board`, and all NVS. Network tasks are
short-lived on core 0, snapshot what they need before spawning, write only into
`g_pending*` under the mutex, and set the ready flag last. Check the TLS gate in
loop context *before* spawning — see the inherited rules; this one cost a real
debugging session.

### Fonts — five faces, and one trap

`font_score64` (tnum frozen), `font_head28`, `font_val22` (tnum frozen),
`font_body18`, `font_micro13` (mono). Freeze tabular figures with
`pyftfeatfreeze -f tnum` or scores visibly jitter as digits change.

**The trap:** AirRadar's fonts are 7-bit ASCII, which is fine for callsigns and
fatal for athlete names. `Dončić`, `Kaprizov`, `Şahin`, `Ødegaard`, `Vlašić`
render as garbage boxes. Body and micro faces need
`0x20-0x7E, 0xC0-0xFF, 0x100-0x17F` (Latin-1 Supplement + Latin Extended-A),
~350 glyphs. Score and value faces need digits and punctuation only. Get this
right in Phase 0 — retrofitting a font range means regenerating every asset.

### Storage

16 MB flash: 2× ~3.5 MB OTA app slots + ~6 MB FATFS. Logo cache at 6.9 KB each
holds ~800 teams. **FATFS writes stall the panel DMA for 150–220 ms regardless
of size** (inherited rule — flash and PSRAM share the MSPI bus). Rate-limit logo
persistence to one write per 45 s and never chunk a small write.

---

## 9. Phases

| # | Deliverable | Gate |
|---|---|---|
| **0** | Bring-up: panel, GT911, LVGL 8.3, Wi-Fi, web portal, OTA, font pipeline. **Public-repo additions:** licence + notices scaffold, CI with gitleaks, `boards/` abstraction, fallback team glyph, and the ESP Web Tools flasher | Screen renders, touch lands, OTA works, **a stranger can flash it from Chrome** |
| **1** | Proxy v1: registry, `CLOCK`+`INNING` normalizers, `/v1/state`, cron+cache, **the `SportsSource` interface around the ESPN adapter**. **Includes the CPU spike.** | `curl` returns a 3 KB board for NHL/NFL/NBA/MLB |
| **1.5** | **Deploy story:** Deploy-to-Cloudflare button, `docker-compose.yml`, `docs/DEPLOY.md`, secret handling | Someone who is not you gets a working proxy in 5 minutes — **publish the repo here** |
| **2** | Logo build pipeline (user-run, outputs gitignored) + 3-tier device cache | Tiles show real logos, no DMA shake |
| **2.5** | **Region + broadcast:** `rights.json`, region in onboarding, local-day handling | A Canadian device says SN, not NESN |
| **3** | Board screen at Standard 9-up, league strip, paging, tile states, density setting | 9 live games render and update |
| **4** | Diff engine + `seq` + alert takeover | A real goal fires a card within 80 s |
| **4.5** | **Idle screen** — the state the panel holds most of the day | Tuesday morning looks deliberate, not broken |
| **5** | Game detail (linescore, plays, situation, win probability) + Team + form guide | |
| **5.5** | **Standings** — generic `{columns, rows}` table with cut lines | |
| **6** | News feed | |
| **6.5** | **Lineups + player sheet + headshot pipeline** | Tapping a scorer shows his league rank |
| **7** | Registry expansion: all soccer, all NCAA, all women's leagues | Proxy-only change, zero firmware |
| **8** | `SET` / `LEADERBOARD` / `GRID` models + their screens (tennis, golf, F1) | The only remaining firmware work |

Phases 0–6.5 are the product. Phase 7 is a config afternoon. Phase 8 is a second
product and should be scheduled as one, not tacked on.

The `.5` phases are the interface work added after the design review
([`UI.md`](UI.md)). They are sequenced where they are because each one depends on
the phase before it and nothing after it depends on them — so any of them can
slip a release without blocking the rest.

> **Reserve the player sheet's rect in the baked plate from Phase 0.** Its frost
> is baked at a fixed 404×324 position; discovering that in Phase 6.5 means
> rebuilding every plate.

---

## 10. League registry — verified live 2026-08-09

Every path below returned HTTP 200 from
`site.api.espn.com/apis/site/v2/sports/{path}/scoreboard`.

**`CLOCK`** — `football/nfl`, `football/college-football`, `basketball/nba`,
`basketball/wnba`, `basketball/mens-college-basketball`,
`basketball/womens-college-basketball`, `basketball/nba-development`,
`hockey/nhl`, `hockey/womens-college-hockey`,
`volleyball/womens-college-volleyball`, `lacrosse/mens-college-lacrosse`,
and soccer: `soccer/eng.1`, `esp.1`, `ger.1`, `ita.1`, `fra.1`,
`uefa.champions`, `uefa.europa`, `uefa.wchampions`, `usa.1` (MLS),
`usa.nwsl`, `mex.1`, `fifa.world`, `fifa.wwc`, `eng.fa`.

**`INNING`** — `baseball/mlb`, `baseball/college-baseball`,
`baseball/college-softball` (note: softball lives under the `baseball/` root —
`softball/college-softball` returns 400).

**`SET`** — `tennis/atp`, `tennis/wta`.
**`LEADERBOARD`** — `golf/pga`, `golf/lpga`.
**`GRID`** — `racing/f1`.

**Unresolved:** the **PWHL** is not exposed at `hockey/pwhl` (HTTP 400). Needs a
path hunt against espn.com's own network traffic, or a second source
(TheSportsDB has PWHL). Flagged rather than assumed.

---

## 11. Still to plan

Settled so far: architecture, leagues, name, licensing, the open-source model,
alerts, and the entire interface ([`UI.md`](UI.md)). What has **not** been
designed:

1. **The proxy JSON contract.** The wire schema both sides are built against —
   field names, types, the generic `{columns, rows}` shape, the alert event
   shape, `seq` semantics. **This is the highest-value thing left**, because
   firmware and proxy can then be built in parallel against a frozen contract.
2. **Error and empty states.** No proxy configured, proxy unreachable, Wi-Fi
   down, upstream stale, no favourites yet, no games today, logo build never run.
   This is where devices feel broken, and it is entirely unplanned.
3. **The setup portal.** §6 states the strategy but there is no design — and it
   is the first thing every user touches.
4. **NVS schema.** Keys, types, defaults, migration. AirRadar documents its own;
   ScoreDeck has none yet. Now needs `rgn`, `dens`, `bcast`.
5. **Quiet hours and sleep.** Still open, and now more concrete: the backlight is
   **on/off only** — no PWM — so "dim" has to be a software scrim.
6. **OTA and release process.** Versioning, the flasher manifest, rollback.

Also open, unchanged:

7. **Proxy plan** — settle after the Phase-1 CPU spike: Workers free, Workers
   Paid at $5/mo, or self-host on the Pi behind Tailscale.
8. **Second device or reflash?** If the AirRadar unit gets reflashed, build the
   flasher page early so switching firmware is a browser click, not a cable.
9. **PWHL** — worth a source hunt, or drop from v1?
10. **Soccer headshot path** — one trial ID returned 404. Non-blocking, since the
    jersey-number badge is the fallback.
