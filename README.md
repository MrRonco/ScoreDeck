# ScoreDeck

Live scores for the teams you follow, on a 7-inch glass panel on your desk —
with rosters, stats, a news feed, and a screen that takes over the moment they
score.

Runs on the **Waveshare ESP32-S3-Touch-LCD-7** (800×480 RGB565, GT911 touch,
16 MB flash / 8 MB OPI PSRAM) — the same board as
[AirRadar](https://github.com/MrRonco/AirRadar).

> **Status: early but working.** The board renders live games on real hardware,
> score alerts fire, and the panel falls back to an idle countdown when nothing
> is on. Game detail, standings, lineups and the browser setup portal are not
> built yet. See [Roadmap](#roadmap).

---

## How it works

```
ESP32-S3 panel  ──one TLS call per poll──▶  your proxy  ──▶  ESPN
   9-up board                               ~3 KB JSON        ~1.2 MB JSON
```

The device has roughly **17–40 KB of free internal heap** and mbedTLS needs a
~16.4 KB contiguous block for a single handshake. ESPN's golf scoreboard alone
is **1.2 MB**. So a small proxy does the aggregating, normalising and
score-diffing, and hands the panel one compact payload.

**You run your own proxy.** There is no shared instance and the firmware ships
with an empty proxy URL — a free Cloudflare Worker serves one device
comfortably and a shared one would not. See
[`docs/OPEN_SOURCE.md`](docs/OPEN_SOURCE.md) §2.

### Three ideas hold the design together

1. **Glass is baked, never blurred.** LVGL 8.3 has no backdrop-filter and the
   RGB panel's DMA already eats ~32 MB/s of PSRAM bandwidth. Chrome composites
   into the background once at boot; data is flat objects on top. Nothing
   frosted ever moves.
2. **ScoreDeck has no accent colour.** ~3,000 team colours already arrive as
   content, so the chrome stays neutral and every saturated pixel belongs to a
   team.
3. **Luminance encodes state, colour encodes team.** Live 100%, scheduled 72%,
   final 55% — you read the board before you read a number.

The signature is the **edge light**: a 3 px strip on the *leading* team's side
of a live tile that swaps ends when the lead changes.

---

## Leagues

20 in the registry today — NFL, NBA, WNBA, NHL, MLB, the NCAA men's and women's
sports, and eleven soccer competitions including NWSL and the Women's Champions
League. Adding one is a row in `proxy/src/registry.ts` plus a logo build: **no
firmware change**, because the device implements five *score models*, not thirty
leagues.

| Model | Renders as | Status |
|---|---|---|
| `CLOCK` | period + running clock | shipped |
| `INNING` | inning, bases, outs | shipped |
| `SET` · `LEADERBOARD` · `GRID` | tennis · golf · F1 | planned |

## Broadcast is regional

ESPN's broadcast data is **US-only and its API ignores a region parameter** —
verified: `?region=ca` and `?region=gb` return byte-identical responses, and a
Toronto home game comes back as being on *NESN*, the Boston regional feed.

So the proxy resolves rather than passes through: ESPN's per-game data for
`us`, and `proxy/src/rights.json` for everywhere else, with per-team overrides
where regional splits are real. **Adding your region is a ten-line pull request
to a data file** — no code, no firmware.

---

## Getting started

1. **Deploy the proxy** — a Pi, a NAS, or a free Cloudflare Worker. Full
   walkthrough in [`docs/DEPLOY.md`](docs/DEPLOY.md).
2. **Flash the firmware** — see [`firmware/BUILD.md`](firmware/BUILD.md).
3. **First boot** asks for Wi-Fi on the panel, because there is no network yet
   to serve a browser over. If you are coming from AirRadar it reads your
   existing credentials out of NVS and skips that step.

---

## Repository

| Path | What |
|---|---|
| `firmware/ScoreDeck/` | LVGL 8.3 application |
| `proxy/` | TypeScript + Hono, runs on Workers, Node, or a Pi |
| `docs/PLAN.md` | Architecture and phases |
| `docs/UI.md` | Screen geometry, tokens, interaction |
| `docs/DEPLOY.md` | Running the proxy — Pi, Workers, or laptop |
| `docs/INHERITED_RULES.md` | 22 hardware lessons AirRadar paid for — **read before touching the renderer** |
| `docs/OPEN_SOURCE.md` | Licensing, deploy-your-own, what may not be committed |

## Roadmap

- [x] Panel, touch, LVGL, Wi-Fi bring-up
- [x] Proxy: registry, `CLOCK`/`INNING`, score diffing, regional broadcast
- [x] Board screen at 9-up, live data on hardware
- [x] Alert takeover, with the sequence held back until a card is actually seen
- [x] Idle screen — the face the panel wears most of the day
- [x] Self-hosted container: Unraid template, multi-arch image, VLAN placement
- [x] Game detail on tap, with linescore, scoring plays and win probability
- [x] Standings — generic table, labelled cut lines
- [ ] Density setting UI · league filter chips
- [ ] Real fonts (tabular figures, Latin Extended-A) · logos
- [ ] Browser setup portal · OTA
- [ ] Lineups, player cards, headshots
- [ ] Tennis / golf / F1

## Licence

Firmware **GPL-3.0-or-later**; proxy **AGPL-3.0-or-later** (`proxy/LICENSE`).

Team names, logos and player likenesses are the property of their respective
owners. **No logo or headshot assets are included in this repository or in any
release binary** — the build tools fetch them onto your own machine for personal
use. See [`docs/OPEN_SOURCE.md`](docs/OPEN_SOURCE.md) §1.

Not affiliated with or endorsed by ESPN or any league. ESPN's endpoints are
undocumented and unofficial: there is no SLA, and a breaking change is a
Saturday afternoon.
