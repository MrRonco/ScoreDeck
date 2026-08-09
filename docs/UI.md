# Interface specification

The authoritative geometry, tokens and behaviour for the 800×480 panel.
Rendered mockups of every screen here live in the design review artifact; this
document is what the firmware is built against.

Direction: **Route B — glass as material**, at **Standard 9-up** density.

---

## 1. The rule that shapes everything

LVGL 8.3 has **no backdrop-filter**, and this board could not afford one: the RGB
panel's DMA already pulls ~32 MB/s of PSRAM bandwidth continuously, and starving
it is the whole-screen shake in [`INHERITED_RULES.md`](INHERITED_RULES.md) §17.

So glass is **baked, never blurred**:

| Tier | Technique | Runtime cost |
|---|---|---|
| Baked | Composited once at boot into the background plate — per-pixel tint, grain, specular top edge, bottom shade | zero |
| Pre-blurred plate | Blur the plate at **asset-build time** in Python, ship sharp + blurred versions; a glass panel is a crop of the blurred one | zero, ~750 KB flash |
| Live blur | — | **Not available. Design as if it does not exist.** |

**Consequence: the chrome is glass, the data is flat.** Bars, rails, tile frames,
the alert card and the player sheet are all at fixed positions and bake into the
plate. Scores, clocks, badges and logos are opaque objects on top that repaint
cheaply. Nothing frosted ever moves — a panel that needs to appear **fades in
over four discrete steps**, it never slides.

---

## 2. Palette and the no-accent rule

**ScoreDeck has no accent colour.** A sports display already receives the most
information-dense palette in software — ~3,000 team colours, arriving as content.
The chrome is strictly neutral; **every saturated pixel on screen belongs to a
team.** The proxy sends `color` in every `Side`.

| Token | Value | Use |
|---|---|---|
| `--d-void` | `#05070C` | Outside the plate |
| `--d-plate` | `#0A0F18` | Plate base |
| *frost* | resolves ≈ `#1A2432` | Not a fill — what the plate becomes under glass |
| `--d-ink` | `#F3F7FB` | Primary text, live scores |
| `--d-ink2` | `#93A5B8` | Secondary — losing side, labels |
| `--d-ink3` | `#5D6D7E` | Tertiary — units, meta |
| `--d-edge` | `rgba(255,255,255,.13)` | Panel border |
| `--d-edge-hi` | `rgba(255,255,255,.30)` | Specular top edge |

Glass composite (per AirRadar's `glassRect()`, alpha 185): tint blend → ±2 grain
→ 2 px top highlight at white/34 → 3 px bottom shade at black/40 → rounded border
→ bright top hairline.

### Luminance encodes state; colour encodes team

Two independent channels, no accent spent on either.

| State | Luminance | Edge light |
|---|---|---|
| Live | 100% | Yes — leading team's colour |
| Scheduled | 72% | No |
| Final | 55% | No |

### Signature — the edge light

A 3 px luminous strip on the **leading** side of every live tile, in that team's
colour, with a soft bloom. **It swaps sides when the lead changes**, making a lead
change visible across the room with no text and no animation. ~400 px to repaint;
the bloom is a small pre-baked alpha sprite recoloured via `img_recolor`.

The same grammar carries to other screens: a followed team keeps its edge light
in the standings table and on the player sheet.

---

## 3. Board — the home screen

### Density is a setting

| Density | Bar | Tile | Gutter | Margin | Score | Games |
|---|---:|---:|---:|---:|---:|---:|
| Roomy | 58 | 244×186 | 16 | 18 | 46 px | 6 |
| **Standard** *(default)* | **48** | **248×128** | **12** | **16** | **38 px** | **9** |
| Dense | 44 | 186×131 | 10 | 12 | 38 px | 12 |

A 3×3 and a 4×3 grid have the same row count, so density costs column width, not
tile height. Frost depth is identical across all three. **Long-press anywhere on
the board cycles density** — discoverable without a settings trip. One baked plate
per density.

Standard grid origins: bar `0,0,800,48`; grid top `60`; row tops `60 / 200 / 340`;
column lefts `16 / 276 / 536`.

### Tile anatomy (Standard, 248×128, padding 11×13)

```
┌─▍──────────────────────────────────┐   ▍ 3px edge light (leader only)
│ [30] MTL  18-9-3              2    │   badge · abbr · context chip · score 38
│ [30] TOR  21-6-4              3    │
│ ─────────────────────────────────  │
│ ● 3rd · 04:21                 SN   │   status · situation · broadcast
└────────────────────────────────────┘
```

The 248 px column reclaims the context line as an **inline chip** beside the
abbreviation — record, seed, AP rank, starting pitcher. The status strip carries
state on the left and the **broadcast channel** on the right.

Touch target is the whole 248×128 tile, well above the 9 mm minimum, with no
neighbouring target closer than the 12 px gutter.

### Top bar (48 px)

Clock · date · league strip with live counts · live badge · news badge · settings.
League chips filter; the strip replaces AirRadar's left rail, buying the full
800 px for tiles.

---

## 4. Game detail

Opened by tapping a tile. **The header keeps the tile's exact anatomy** — same
badges, same score sizes, same edge light — so the transition reads as the tile
expanding rather than a new screen arriving.

| Region | Rect |
|---|---|
| Header | `0,0,800,92` |
| Tabs | `16,104` — Summary · Stats · Plays · Lineup |
| Linescore strip | `16,140,768,56` |
| Left panel | `16,208,410,224` |
| Right panel | `438,208,346,224` |
| Win probability | `16,444,768,26` |

**Summary** — linescore, scoring plays, tonight's leaders, goalie lines.
**Stats** — team comparison bars, fed generically as `{label, away, home}` so the
firmware never learns what a faceoff is.
**Plays** — trimmed play-by-play.
**Lineup** — §5.

Only the linescore strip changes shape per sport: periods for `CLOCK`, innings
plus R/H/E for `INNING`, sets for `SET`.

---

## 5. Lineup and player sheet

`summary?event=` carries `boxscore.players` — every athlete pre-grouped by
position with a full stat array and an athlete ID — plus `injuries`, `leaders`,
and for hockey `onIce`.

> **ESPN does not return hockey line combinations.** The grouping is
> forwards / defense / goalies, not L1/L2/L3. Showing invented line pairings
> would be a lie; show the real grouping with tonight's numbers.

| Sport | Treatment |
|---|---|
| Hockey | Forwards / Defense / Goalies with G·A·+/−·TOI. `onIce` marks who is on the ice **right now** with a lit dot |
| **Soccer** | **Pitch diagram.** `formation` returns a literal string (`"4-2-3-1"`) with a `starter` flag per player, so both XIs draw facing each other |
| Basketball | Starters then bench — PTS·REB·AST·MIN |
| Football | Passing / Rushing / Receiving groups |
| Baseball | Batting order (AB·H·RBI) then pitchers (IP·ER·K) |

The soccer pitch is the strongest single screen in the product and nearly free:
markings are four bordered rectangles and a circle baked into the plate; players
are 28 px badges positioned from the formation string. It is also the screen that
proves the font range — *Ødegaard*, *Konaté*, *Dončić*.

### Player sheet — `380,140,404,324`

Fixed position, **fades in, never slides** (§1). Frost baked at that exact rect,
which must be reserved in the plate from the first build.

- 68×68 headshot with the jersey number overlaid bottom-right
- Name, position, team, height/weight/age
- **TONIGHT** — the live stat line
- **SEASON with league rank** — `/athletes/:id/stats` returns `rankDisplayValue`
  beside every number. *"31 goals, 4th in the NHL."* This is the reason to tap;
  a bare stat line is available anywhere.
- **LAST 5** — per-game chips

### Headshots

Measured: `a.espncdn.com/i/headshots/{league}/players/full/{id}.png` returns 200
for NHL, NBA, NFL. Each is **230–280 KB**, and **`?w=80` is ignored** — ESPN does
no CDN-side resizing. Soccer path unconfirmed (a trial ID 404'd); non-blocking.

| Step | Where | Size |
|---|---|---:|
| Source PNG | ESPN CDN | ~250 KB |
| Resize 68×68, encode RGB565A8 | **Build time**, same run as logos | 13.9 KB |
| Served from | The user's own Worker | 13.9 KB |
| Device cache | **RAM only, 4 slots** | 56 KB PSRAM |

Scope the build to **rosters of followed teams** — ~500 players ≈ 7 MB of static
assets.

> **Never cache headshots to FATFS.** Every flash write stalls the panel DMA for
> 150–220 ms ([`INHERITED_RULES.md`](INHERITED_RULES.md) §16) and only the open
> sheet is ever needed. Four RAM slots covers it; a miss costs one small fetch.

**Fallback is the jersey-number badge** — no empty state to design.

---

## 6. Standings

Payloads normalize cleanly across sports: every one returns `wins`, `losses`,
`gamesPlayed`, `points`, `pointDifferential`, `streak`, `playoffSeed`. The proxy
sends generic `{columns, rows}`; the firmware renders a table without knowing the
sport.

Row 32 px, 10 visible, table at `16,98,768,366`.

| Sport | Columns | Cut lines |
|---|---|---|
| Hockey | GP·W·L·OTL·PTS·DIFF·STRK·L10 | Playoff cut |
| Soccer | P·W·D·L·GD·PTS·**FORM** | Champions League · Europa · relegation |
| Basketball · Football | W·L·PCT·GB·STRK·L10 | Play-in · playoff cut |
| Baseball | W·L·PCT·GB·STRK·L10 | Division · wildcard |

Two details do real work: **followed teams keep the edge light**, so you find
yourself without reading; and the **cut line is a labelled hairline**, not a
colour band — which keeps the no-accent rule intact while stating exactly what the
boundary means.

Soccer's **form** column — last five results as W/D/L chips in team colour — is
the densest readable thing in any league table.

---

## 7. Idle

**Most of the day nothing is live.** A Tuesday morning board of dimmed finals is
the state the panel spends most of its life in, so it needs a second face: a
countdown, not a scoreboard.

| Region | Content |
|---|---|
| `16,60,372,190` | Clock, date, "4 games today · first at 7:00 PM" |
| `400,60,384,190` | **NEXT UP** — matchup, countdown as the hero, time · venue · channel |
| `16,262,372,202` | **TODAY** — upcoming games with times |
| `400,262,384,202` | **FOR YOUR TEAMS** — two headlines |

Cheapest screen in the product: a clock, a countdown and two lists, updating at
most once a minute. Polls at 5 min; dims to 40% in quiet hours.

---

## 8. Alert takeover — `140,90,520,300`

Composites once, **fades in over four discrete steps, then holds completely
static.** The only moving element afterwards is a 200×6 pulse bar (1,200 px/frame).

A 520×300 card is 156,000 px — an opacity tween on it at 30 fps is not affordable.
**Do not "improve" this with a slide-in.**

Verb is per sport and is the largest type in the product: GOAL / TOUCHDOWN /
FIELD GOAL / HOME RUN / THREE / TRY. Team colour owns the left edge and the glow
behind the new score; everything else stays neutral. Events queue one at a time,
12 s apart, so a three-goal burst does not stack.

---

## 9. Type

Five generated LVGL faces. The pairing is chosen for a reason unrelated to taste:
**athlete names break ASCII fonts.**

| Face | Suggested | Size | Codepoints | Use |
|---|---|---:|---|---|
| `font_score38` | Archivo Condensed 700 | 38 | `0x30-0x39` `-` | Scores — **tnum frozen** |
| `font_abbr17` | Archivo Condensed 600 | 17 | `0x20,0x2E,0x30-0x39,0x41-0x5A` | Team abbreviations |
| `font_clock14` | IBM Plex Mono 500 | 14 | `0x20-0x7E` | Clocks, records — **tnum frozen** |
| `font_body15` | IBM Plex Sans 400 | 15 | `0x20-0x7E, 0xC0-0xFF, 0x100-0x17F` | Player names, news — **~350 glyphs** |
| `font_micro11` | IBM Plex Mono 500 | 11 | `0x20-0x7E` | Labels, league tags |

Roomy density adds `font_score46`; Dense reuses `font_score38`.

Freeze tabular figures with `pyftfeatfreeze -f tnum` or digits visibly jitter.
Both families are OFL-1.1 — the generated `font_*.c` files need their own notice.

---

## 10. Interaction

| Gesture | Where | Action |
|---|---|---|
| Tap | Tile | Open game detail |
| Tap | League chip | Filter board |
| Tap | Player row / pitch badge | Open player sheet |
| Tap | Scrim or ✕ | Dismiss sheet / alert |
| Long-press | Board background | Cycle density |
| Long-press | League chip | Hide league for this session |
| Swipe ←→ | Board | Page through games |
| Tap | ‹ | Back |

Alerts auto-dismiss after 10 s. No scroll momentum anywhere — paged lists only,
because inertial scrolling means continuous repaints against the panel DMA.
