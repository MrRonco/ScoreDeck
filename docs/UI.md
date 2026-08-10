# Interface specification

The authoritative geometry, tokens and behaviour for the 800×480 panel.
Rendered mockups of every screen here live in the design review artifact; this
document is what the firmware is built against.

Direction: **Route B — glass as material**, at **Standard 9-up** density.

**Built so far:** board, idle, game detail, standings, news, lineups, player
cards and the alert takeover — plus all five score models. Running on hardware. Fonts are still LVGL's stock Montserrat placeholders — §9 is the
target and swapping the pointers in `theme.cpp` is the whole change.

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

---

## 11. Revision — the desk correction

Everything above was written assuming this panel is glanced at from across a
room. **It is not.** It sits on a desk, about 610 mm from the viewer's face,
and that changes several of the decisions in this document.

The useful measurement is an equivalence rather than a heuristic: at that
distance the panel subtends 14.24° and resolves **56.2 pixels per degree**. A
27-inch 1440p monitor at 700 mm resolves 55.4. **Angularly this is a desktop
display**, so desktop type sizes are the right reference, and the signage rule
`cap = D/200` over-specifies by about half.

What that changed, and what it did not:

| Section | Status |
|---|---|
| §2 no-accent rule | **Unchanged** — it was right and is held |
| §2 luminance ladder | **Replaced.** See below |
| §2 edge light | Kept at 3 px; a 3 px strip subtends 3.2 arc-min here, so width was never the problem. Colour was |
| §3 density | **Auto is now the default** and picks a layout from the game count |
| §8 alert takeover | **Split.** See below |
| §9 type ladder | Four of five faces were already right; the fifth was the defect |
| Everything on geometry | **Unchanged** — and craft errors of 1–4 px now cross the visibility threshold, so they matter more |

### The luminance ladder is a colour table, not an opacity

`OPA_LIVE/PRE/FINAL` applied one opacity to a whole tile, which faded the frost
and the text toward the plate together. The top tier survived; the bottom tier
did not — a final's record and broadcast landed at **1.73 : 1** and vanished,
so a finished game read as a tile that had failed to load while its score was
still bright.

Each state now names its own five colours (`kStateInk` in `theme.h`). The
perceptual ladder survives and nothing drops below ~3.4 : 1. It also removed
nine 63 KB composite buffers: in LVGL 8.3 a non-opaque parent forces its whole
subtree through a temporary buffer, which had been quietly defeating the
change-caching the board works hard for.

### Team colour is normalised before it is drawn

~3,000 team colours arrive as content and roughly a fifth are navy, black or
deep maroon. Toronto's `#00205B` against the tile is **1.11 : 1** — the
signature element of the product, invisible on its own flagship example.

`teamInk()` saturates first and whitens only the residual, stopping the moment
it clears **3.5 : 1**. Hue survives: Toronto comes out *more* blue than it went
in. The threshold is stated as a contrast ratio and not a brightness cutoff on
purpose — a brightness rule tuned on Toronto also fires on Kansas City and
Edmonton and washes out colours that were never the problem.

Badge fills and label ink get the same treatment. Seattle's white-on-ice goes
from 1.58 : 1 to 12.1 : 1.

### The frost is flat, and must stay flat

The glass style used a vertical gradient. On RGB565 that ramp spans one to two
steps of the 5-bit blue channel over 128 px, so it never rendered as a gradient
at all — it quantised into **three flat slabs with two hard horizontal edges**,
in every tile, at the same two heights. Confirmed by scanning a column.

Depth now comes from a specular pair instead: a 1 px bright catch along the top
and a 2 px shade along the bottom, two static children per panel with no
per-frame cost. **Do not reintroduce `bg_grad_dir`.** A background ramp on this
panel has to be baked and dithered at build time.

### §9 revised — the faces

| Face | Size | Carries |
|---|---|---|
| `F_SCORE` | 38 / 46 | digits, `-`, `:` only. Never text |
| `F_DISPLAY` | 30 | the alert verb. CAPS + digits |
| `F_ABBR` | 17 | team abbreviations. CAPS + digits |
| `F_BODY` | 15 | anything with a person or place in it |
| `F_NUM` | 15 | **data you read** — clocks, standings cells, stat values |
| `F_MICRO` | 13 | **chrome labels only** — SCORING, column headers |

`F_MICRO` was an 11 px tooltip face carrying the game clock, every team record,
every standings cell and every lineup stat value. Splitting data from labels
was the whole fix; nothing was relaid out, because the affected labels were
already fixed-width and right-aligned.

### §8 revised — the takeover is earned

A full-screen card 60 cm from the viewer, hiding the other eight games for ten
seconds and potentially firing every twelve, is not ambient — it is an
interruption. The takeover now belongs to **followed teams only**. Everyone
else gets an 800×44 banner and a flare on the scoring tile's edge light, which
occludes nothing and repaints 35,200 px instead of 156,000.

### New — auto-focus

A followed team's game opens itself when it reaches a tense state: a man
advantage, the red zone, or a runner in scoring position with two out. It hands
the screen back when the moment passes, and stands down for 90 seconds if the
user closes a game themselves.

Leverage is judged on **structured facts only**. The game clock is upstream
prose — "3rd 04:21", "Bot 7", "90'+4" — and parsing it to find "inside the
final five minutes" would be a guess that differs per sport and per locale. If
clock leverage is wanted, the honest route is a remaining-seconds field on the
wire, not a parser on the device.

### New — the board sorts and marks

Followed-live first, then live, then upcoming, then finals. The followed side
carries a 2 px ring on its badge: a ring rather than a coloured hairline
because it works for Pittsburgh black and Seattle ice alike, and it marks the
**team** rather than the game, so a favourite-vs-favourite tie correctly shows
two.

### New — the settings screen

Three panes: BOARD, NETWORK, SYSTEM. What lives here rather than in the browser
follows one test:

> Does this get touched more than twice a year, or is it needed when the
> browser is unreachable?

The original rule was "anything expressible without typing", but the argument
was never that this panel is hard to type on — it is that a better input device
is permanently two feet away. So region, timezone, cadence, clock format and
the alert-on-start/final toggles are browser-only. So are leagues: the board's
league strip already filters per session, so persistent league choice was a
set-once setting wearing a costume.
