# ScoreDeck — the colour system, measured

Read-only review. Every number below is measured from RGB565 pixels: either the
already-built harness (`desktop/scoredeck-ui`, whose framebuffer is a real
`uint16_t[800*480]`, so blends quantise exactly as the panel does) or the real
panel capture `desktop/shots/panel-i2-board.png`. Source constants are quoted
only to name things; the hexes are what the eye receives.

Colour space: **CIE L\*** for lightness, **CIE LCH(ab)** for hue/chroma (OKLCH
given alongside where the two disagree materially — they do not, here). Contrast
is WCAG 2.x relative luminance, matching `theme.cpp`'s own solver.

---

## 0. The headline

**The accent is not incoherent because its hue was badly chosen. Its hue is
near-optimal. It is incoherent because 93% of it is spent on decoration and 2%
on the thing it means, and because neither the accent nor the team channel holds
a stable lightness.**

Measured, on the default mixed board:

| pure `C_LIVE` (#39E3C6) pixels | where | share |
|---|---|---|
| 3268 | the rail sliver, x < 10 | **92.7%** |
| 192 | top bar (`LIVE n` count) | 5.4% |
| 64 | the live dots — the accent's declared meaning | **1.8%** |

On the **idle screen** — the screen the panel spends most of its life showing,
by `pulse.cpp`'s own admission — 94.4% of `C_LIVE` is the *countdown to a game
that has not started*. Confirmed on the real panel capture: 3959 px of #39E3C6,
all of it the `7H 2M` numeral.

So the eye learns "teal = that stripe on the left" and "teal = the big number",
not "teal = live". When a 9 px teal dot then appears meaning *live*, it does not
read as the same thing — and it is not, physically: see §3.

---

## 1. Inventory — every colour literal and token

`firmware/ScoreDeck/src/ui/*.cpp`, `theme.h`, `theme.cpp`. `src` is the declared
hex; `565` is what LVGL's 5/6/5 round-trip actually puts on the glass (LVGL 8.3
expands with `(c*263+7)>>5` / `(c*259+3)>>6` — **every** token shifts).

### 1a. Declared tokens

| token | src | 565 | L\* | LCH C | LCH h | uses | meaning |
|---|---|---|---:|---:|---:|---:|---|
| `C_PLATE` | #04070E | #000408 | 1.89 | 3.1 | 275 | 22 | the ground |
| `C_SURF` | #101825 | #101820 | 8.10 | 10.2 | 275 | 0 (via table) | final tile |
| `C_SURF_1` | #16202E | #102029 | 11.96 | 10.7 | 271 | 5 | scheduled tile |
| `C_SURF_2` | #1B2636 | #182431 | 14.87 | 11.9 | 272 | 3 + glass | live tile, top bar |
| `C_SURF_3` | #222E40 | #202C41 | 18.65 | 13.0 | 273 | 1 | hero cell |
| `C_FROST_2` | #141C28 | #101C29 | 10.05 | 9.3 | 272 | 7 | inset fields |
| `C_INK` | #F3F7FB | #F6F6FF | 97.04 | 2.5 | 256 | 67 | primary ink |
| `C_INK2` | #ACBCCE | #ACBECD | 75.57 | 11.1 | 261 | 49 | secondary ink |
| `C_INK3` | #8696A8 | #8395AC | 61.40 | 11.5 | 261 | 78 | tertiary ink |
| `C_EDGE` | #2A3646 | #293441 | 22.20 | 11.5 | 269 | 23 | generic border |
| `C_EDGE_HI` | #46566A | #41556A | 35.98 | 13.5 | 266 | 15 | brighter border, rail idle |
| **`C_LIVE`** | **#3BE0C0** | **#39E2C5** | **81.45** | **48.7** | **177.6** | **18** | live / touch |
| `C_LIVE_SD` | #2A9E8C | #299D8B | 58.46 | 35.8 | 179.5 | 10 | tracked-caps labels |
| `C_LIVE_TX` | #30B89D | #31BA9C | 68.14 | 43.4 | 173.5 | **0** | body sentences |
| **`C_WARN`** | **#F2B441** | **#F6B641** | **78.13** | **66.6** | **79.0** | **11** | caution / stale |
| `C_LINE` | #B4CDE6 | #B4CEE6 | 81.33 | 15.5 | 259 | 14 | one line hue @ 24/40/120 |

### 1b. `kStateInk[4]` (theme.cpp:237)

| index | plate | edge | ink | ink2 | ink3 |
|---|---|---|---|---|---|
| PRE | #16202E (L\*11.96) | #2E3A4C (24.09) | #D2DEEA (87.91) | #ACBCCE (75.57) | #8696A8 (61.40) |
| LIVE | #1B2636 (14.87) | #3A4759 (29.73) | #F3F7FB (97.04) | #ACBCCE | #8696A8 |
| FINAL | #101825 (8.10) | #232E3E (18.62) | #B6C4D2 (78.51) | #95A5B7 (67.06) | #8696A8 |
| HERO | #222E40 (18.65) | #44526A (34.61) | #F3F7FB | #ACBCCE | #8696A8 |

This table is correct and should not be touched. See §5.

### 1c. Loose literals — colour that escaped the token system

| literal | 565 | where | what it means | verdict |
|---|---|---|---|---|
| `0x5D6D7E` | #5A6D7B | `ui_alert:102`, `ui_game:89/95`, `ui_news:109`, `ui_lineup:187`, `ui_idle:226/233`, `ui_standings:110` — **8 sites** | placeholder team badge | should be a token (`T_NONE`) |
| `0x334455` | #314452 | `ui_hero:218` | placeholder hero badge | a *ninth* placeholder, a different colour, same job |
| `0x04070C` | #000408 | `ui_alert:86`, `ui_lineup:173` | modal scrim | **collapses to exactly `C_PLATE` in 565.** Two literals, one rendered colour |
| `0xDCE4EE` | #DEE6EE | `theme.cpp:218` | the logo-chip rung | fine, but undeclared |
| `0x2A3646` | #293441 | `ui_game:228` | non-live rule | is `C_EDGE`, spelled out |
| `0x3A4757` | #394452 | `ui_lineup:373` | win-prob bar, unranked | one-off grey |
| `0x4A5666` | #4A5562 | `ui_rail:180` | "no games" count ink | one-off grey |
| `0x0B111B` | #081018 | `ui_settings:537` | pill off fill | one-off surface |
| `0x1E2836` | #182831 | `ui_settings:538` | pill off border | one-off border |

Nine loose greys/blues, all inside a 4–36 L\* band that the surface ladder and
`C_EDGE`/`C_EDGE_HI` already cover. This is the "eleven greys in a 25 L\* band"
failure that `theme.h` diagnosed and fixed for the *ink* tiers, still present in
the *chrome* tiers.

### 1d. Team-derived colour — the second, undeclared palette

| call | ratio | surface | used for |
|---|---|---|---|
| `teamInkOn(c, si.fill, 6.5f)` | 6.5:1 | own state fill | **score digit** (`ui_board:1353`, `ui_hero:405`) |
| `teamInkOn(c, si.fill)` | 3.5:1 | own state fill | **edge light** (`ui_board:1463`, `ui_hero:447`) |
| `teamInkOn(c, si.fill)` | 3.5:1 | own state fill | **bloom** (`ui_hero:420`, 220×220 px @ opa 200) |
| `teamInkOn(c, si.fill)` | 3.5:1 | own state fill | **win-prob bar** (`ui_hero:467/468`) |
| `teamInk(c)` | 3.5:1 | *always* the live fill | alert edge (`ui_alert:163/203`), idle next-up edge (`ui_idle:443`), favourite swatch (`ui_settings:994`) |
| `teamFill(c)` | 1.6:1 | plate | badge grounds |
| **none** | — | — | **`ui_alert.cpp:205` — `s_pulse` gets the RAW team colour** |

---

## 2. Perceptual analysis — how many hue families, and which carry meaning

Salience gate: LCH chroma ≥ 25 and L\* ≥ 40 — roughly where a 3–4 px strip stops
reading as grey at 610 mm. Clustered at 18° hue tolerance.

| screen | salient chromatic px | hue families |
|---|---:|---:|
| board, mixed (the default nightly view) | 8503 | **8** |
| dense 4×3 | 7180 | **7** |
| FEATURE (hero + ledger) | 4521 | 4 |
| nine live | 7284 | 3 |
| idle | 4744 | 3 |

The default board, family by family:

| h (deg) | px | lead colour | channel | carries meaning? |
|---:|---:|---|---|---|
| 177.6 | 3812 | #39E3C6 `C_LIVE` | chrome | yes — but 93% of it is the rail |
| 48.1 | 1351 | #FF8A5A | **team** (EDM/SF orange, lifted) | yes |
| 78.6 | 1303 | #F7B642 `C_WARN` | chrome | **two incompatible meanings — §4c** |
| 290.9 | 959 | #2171FF | **team** (TOR/BUF navy, lifted) | yes, but see §4b |
| 11.2 | 801 | #FF869C | **team** (MTL/KC red, lifted) | yes |
| 265.7 | 112 | #6B92BD | team ↔ chrome overlap | ambiguous |
| 196.2 | 88 | #6BD7D6 | team (SEA) | reads as a third teal |
| 217.4 | 60 | #31A2B5 | team | reads as a fourth teal |

Eight families on one screen. Four of them are the team channel doing its job.
Two of them (196°, 217°) are team colours that land in the accent's own hue
neighbourhood.

### The hue-space map — where the team channel actually lives

54 real team colours from `desktop/scenarios.cpp` plus the shipped leagues, each
at both lifts, after 565 (n=102 chromatic samples):

```
   0- 15  ######      6      45- 60  ##########  10     180-195  #            1     300-315  ##          2
  15- 30  ############ 12    60- 75  .            0     195-210  ####         4     315-330  .           0
  30- 45  #########    9     75- 90  ######       6     210-225  ######       6     330-345  .           0
                            90-105  .            0     225-240  ##           2     345-360  #           1
                           105-120  .            0     240-255  .            0
                           120-135  .            0     255-270  #########    9
                           135-150  ##           2     270-285  ##############  14
                           150-165  ####         4     285-300  #############   13
                           165-180  #            1
```

Two big lobes: **0–60° (red/orange, 36% of mass)** and **255–300° (blue, 35%)**.
Empty: 60–75, 90–135, 240–255, 315–345.

Collisions within ±25° of each shipped chrome hue:

| token | hue | team collisions | share |
|---|---:|---:|---:|
| `C_LIVE` | 177.6° | 6 / 102 | 5.9% |
| `C_WARN` | 79.0° | 6 / 102 | 5.9% |
| `C_LINE` / the whole neutral family | ~256–275° | **22 / 102** | **21.6%** |

**This overturns the premise in `theme.h`'s own comment.** The comment claims
"of twelve sampled kits, seven land within 25 degrees of this hue once
`teamInk()` has lifted them". Against 54 real kits it is 3/54 in LCH, 5/54 in
OKLCH — 6–9%, not 58%. That twelve-kit sample was selected *for* being teal.
The accent's hue is one of the two quietest bands the team channel leaves open.

The colliding family is not the accent. **It is the neutral chrome.** Every
surface, border, line and ink token sits at hue 256–275° — and 22 of 102 team
samples land in that same band (LAD #007DD5 at 274.6°, CLE #0081D5 at 272.1°,
TEN #4A91DE at 272.5°, HOU #0881C5 at 264.6°). A Dodgers edge light and
`C_EDGE_HI` are the same hue family, separated only by chroma (53 vs 13.5).

---

## 3. Why the owner sees the teal bar and the live dots as two different colours

They **are** two different colours. `pulse.cpp:46` modulates the dot's
`bg_opa` from **150 to 255** on a 2 s triangle. Opacity blends toward the tile
fill, which is a dark blue-grey, so the dot loses lightness *and* chroma:

| opa | over live tile #182431 | L\* | LCH C | contrast |
|---:|---|---:|---:|---:|
| 150 (trough) | **#299183** | **54.4** | 32.5 | 3.97:1 |
| 181 | #31AA94 | 62.96 | 38.2 | 5.31:1 |
| 213 | #31C2AC | 71.02 | 42.4 | 6.86:1 |
| 255 (peak) | #39E2C5 | 81.45 | 48.7 | 9.34:1 |

**The dot travels 27.0 L\* and 16.2 chroma every two seconds.** The rail sliver
is pinned flat at the top of that range (#39E2C5, L\* 81.5, no opacity at all).
The two objects never match; the dot spends most of its cycle 20+ L\* below the
bar. The owner's eye is correct.

Two more consequences nobody solved:

- At trough the dot is **#299183**, which is `C_LIVE_SD` (#299D8B) to within
  1 LSB. The accent's flagship object spends part of every cycle rendering as
  the *label* token.
- At trough on the **hero** surface the dot measures **3.79:1** — below AA.
  `theme.h` rejected `C_LIVE_SD` for being "marginal" at 4.63:1 and solved
  `C_LIVE_TX` to clear 5.5:1 everywhere. The dot, which is the accent's whole
  point, is worse than the value that was rejected. Nobody solved the pulse.

And `C_LIVE_TX` — the token that carries a 9-line justification in `theme.h` —
**is used zero times in the entire codebase.** The only match is a comment
(`ui_board.cpp:1171`). The job it was solved for (the hero's situation line) is
instead done by `C_LIVE` at `text_opa 180`, which renders **#31AA9C at 4.91:1** —
an undeclared colour, below the 5.5:1 floor the token was created to hold.

The C_LIVE family is three teals at L\* 58.5 / 68.1 / 81.5 — a 23 L\* spread
across three rungs of one hue. That is precisely the "eleven greys in a 25 L\*
band" mistake `theme.h` diagnosed for the neutrals, repeated in the accent, one
file below the comment that diagnosed it.

---

## 4. Diagnosis against the brief's taxonomy

### (a) Genuinely unsystematised chrome — real, and the largest single defect

**The same team gets two different colours on the same tile.** The score digit
lifts to 6.5:1 (`ui_board:1353`); the edge light, the bloom and the win-prob bar
lift to 3.5:1 (`ui_board:1463`, `ui_hero:420/467`). Across 54 teams:

> mean ΔL\* **16.13**, max **20.36** · mean Δhue **8.8°**, max **21.7°** ·
> mean Δchroma **22.68**, max **51.59**

Toronto, on one live tile, renders as **#2071FF** (L\* 51.1, C 82.2, h 290.9) in
the edge light and **#7BAAFF** (L\* 69.6, C 47.6, h 279.2) in the score. Both
measured off `/tmp/colorsys-s0.bmp` at (14,118)–(18,150) and (222,112)–(252,152).
Two blues, one team, one tile. That *is* the "blue card accents" complaint.

Other unsystematised chrome, all measured:

- **`ui_alert.cpp:205` draws the raw, unnormalised team colour.** `s_pulse` is a
  200×6 bar at `LV_OPA_COVER` set from `e.color` with no lift. Against the alert
  card's `C_SURF_2` fill: **39 of 54 teams fall below 3.0:1**; Toronto and
  Vancouver measure **1.01:1** — literally invisible. It is the only place in the
  product that bypasses `teamInk()`.
- **Nine placeholder badge colours, two values.** `0x5D6D7E` at eight sites,
  `0x334455` at a ninth (`ui_hero:218`), same job.
- **The modal scrim is the plate.** `0x04070C` and `C_PLATE` #04070E both
  quantise to **#000408**. Two constants, one rendered colour. (It is the only
  565 collision among all declared tokens — the palette is otherwise well
  separated for 5/6/5.)
- **The win-probability bar has no luminance step at its join.** MTL #FF2839
  (L\* 55.15) meets TOR #3179FF (L\* 53.54): **ΔL\* 1.61, contrast 1.06:1**. The
  division between the two teams' shares is carried by hue alone. At the desk it
  reads; at 2–3 m it is one undifferentiated stripe.
- **The same fact renders in two hues on one screen.** `situationText()` output
  is `C_WARN` amber on grid tiles (`ui_board:555`) and `C_LIVE` teal on the hero
  (`ui_hero:274`). On the FEATURE screen both are visible simultaneously:
  "RED ZONE" amber in a grid tile, "POWER PLAY" teal in the hero footer.

### (b) Hue-family collision — real, but not where `theme.h` thinks

Not accent-vs-team. Two other collisions, both measured:

1. **The bright-kit ceiling is missing.** `lift()` only ever raises; it never
   lowers. So kits that are already bright pass through untouched and land in
   the chrome's own band:

   | team | renders | L\* | Δhue vs `C_WARN` | ΔL\* vs `C_WARN` |
   |---|---|---:|---:|---:|
   | NHL Boston / Nashville #FFB81C | **#FFBA18** | 79.9 | **1.5°** | **1.7** |
   | Vegas #B4975A | #B4955A | 63.3 | 4.6° | 14.8 |
   | Anaheim #F47A38 | #F67939 | 64.6 | 27.4° | 13.5 |
   | Seattle Kraken #99D9D9 | #9CDADE | 83.2 | — | Δhue vs `C_LIVE` 26.4° |

   **Boston's gold and `C_WARN` are, to the eye, the same colour** — 1.5° of hue
   and 1.7 L\* apart. On any board with the Bruins, the caution colour and a team
   colour are indistinguishable.

2. **The neutral chrome family and the navy lobe share a hue band.** 22 of 102
   team samples land at 250–280°, the exact hue of every surface, border and ink
   token. Separated by chroma only.

The teal accent versus team teals (GB 181.6°, NYJ 163.9°, MIN 162.4°, SEA 204.0°,
MIA 204.0°) *is* a real adjacency — but `theme.h`'s stated guarantee holds and is
worth restating with the measured number: lifted team ink sits at **min 3.50,
mean 4.05, max 9.64** against the live tile, while `C_LIVE` sits at **9.16**. The
margin is genuine. It is only lost because the dot's pulse drops it to 3.97 —
straight into the team band.

### (c) Insufficient separation of the team channel from the chrome channel — the root cause

The mechanism, measured:

**`lift()` stops the instant it clears its target ratio against the fill. Because
every team is lifted against the *same* fill to the *same* ratio, every lifted
team colour comes out at the *same lightness*.**

At 3.5:1: **45 of 54 land inside L\* 50–54** (mean 54.27, sd 7.12).
Chroma, by contrast, runs **25 to 103** (mean 66.1).

So the team channel is a set of ~3000 colours at one lightness and near-maximum
chroma, differing only in hue. The eye's strongest grouping cue — lightness —
has been neutralised, leaving arbitrary hue as the only signal. **That is why
they read as noise rather than as identity.** It is also why 70% of lifted team
colours are *more chromatic than `C_LIVE`* (38/54) and 63% more chromatic than
`C_WARN` (34/54): the content channel out-shouts the chrome channel that is
supposed to sit above it.

The two channels then cross in both directions:

- **Upward:** 16 of 54 score digits reach L\* ≥ 70 at the 6.5:1 lift — into the
  accent's band. Team colour arrives on the largest, brightest element in every
  tile, plus a 220×220 px bloom on the hero.
- **Downward:** `C_LIVE_SD` (L\* 58.5) and the dot at pulse trough (L\* 54.4) sit
  *inside* the team band.

There is no rule anywhere that says which lightnesses belong to which channel.

### A separate, measurable liability: `teamInk()` destroys team identity

Distinct raw colours that collapse to one lifted colour:

| lifted | teams |
|---|---|
| **#2071FF** | TOR #00205B, VAN #00205B, NHL BUF #003087, NFL BUF #00338D |
| #0871FF | DAL #041E42, TEX #003278 |
| #0079F6 | NFL SEA #002244, NE #002244 |
| **#7B797B** (pure grey, C 1.4) | PIT #000000, LAK #111111 |
| #F60000 | ARS #EF0107, WSH #AB0003 |
| #F61029 | DET #CE1126, NJD #CE1126 |

Nine navy teams across three groups land within 5° of each other at h 286–291.
They are one blue on the panel. Pittsburgh and Los Angeles lose their colour
entirely and render as a chrome grey at L\* 51 — within 6 L\* of the placeholder
`0x5D6D7E`, so "black team" and "no data" look the same.

---

## 5. The token system

Two rules carry the whole design. Both are hue-independent, which is the only
kind of rule that can survive ~3000 arbitrary team hues.

> **Rule 1 — the channel is the lightness band.**
> `TEAM` lives at **L\* 46–62**. `SIGNAL` lives at **L\* ≥ 72**. Ten L\* of moat.
> Nothing may cross.
>
> **Rule 2 — the channel is the chroma.**
> `INK` and `STRUCTURE` are **chroma ≤ 16**, any lightness. `SIGNAL` is
> **chroma ≥ 30**. `TEAM` is unconstrained.

Three cells, no overlap: *bright and saturated = the system is talking; mid and
saturated = a team; unsaturated = text and structure.* That is learnable in one
evening of looking at the panel, and it is exactly what is missing today.

### 5a. Ground and surfaces — renamed, not changed

Every value below is the existing constant. The rename exists so the ladder has
names that say what it is, and so `kStateInk` stops being the only place the
system is written down.

| new token | = old | hex | 565 | L\* |
|---|---|---|---|---:|
| `BG` | `C_PLATE` | #04070E | #000408 | 1.89 |
| `SURF_FINAL` | `C_SURF` / `kStateInk[FINAL].plate` | #101825 | #101820 | 8.10 |
| `SURF_PRE` | `C_SURF_1` / `kStateInk[PRE].plate` | #16202E | #102029 | 11.96 |
| `SURF_LIVE` | `C_SURF_2` / `C_FROST` / `kStateInk[LIVE].plate` | #1B2636 | #182431 | 14.87 |
| `SURF_HERO` | `C_SURF_3` / `kStateInk[HERO].plate` | #222E40 | #202C41 | 18.65 |
| `SURF_INSET` | `C_FROST_2` | #141C28 | #101C29 | 10.05 |
| `SCRIM` | *delete* `0x04070C` → use `BG` | #04070E | #000408 | 1.89 |

### 5b. Structure and ink — renamed, not changed

| new token | = old | hex | L\* | chroma |
|---|---|---|---:|---:|
| `LINE` | `C_LINE` @ `OPA_HAIR 24` / `OPA_EDGE 40` / `OPA_SPEC 120` | #B4CDE6 | 81.33 | 15.5 |
| `EDGE` | `C_EDGE` | #2A3646 | 22.20 | 11.5 |
| `EDGE_HI` | `C_EDGE_HI` | #46566A | 35.98 | 13.5 |
| `INK_1` | `C_INK` | #F3F7FB | 97.04 | 2.5 |
| `INK_2` | `C_INK2` | #ACBCCE | 75.57 | 11.1 |
| `INK_3` | `C_INK3` | #8696A8 | 61.40 | 11.5 |

Retire onto these six: `0x3A4757` → `EDGE_HI`; `0x4A5666` → `INK_3`;
`0x2A3646` (`ui_game:228`) → `EDGE`; `0x1E2836` → `EDGE`; `0x0B111B` → `BG`.
Five loose greys gone, zero rendered change worth seeing (max ΔL\* 6.5, all on
1 px borders and off-state fills).

### 5c. Accents — one primary, one secondary, and the secondary is achromatic

**`A_LIVE = #3BE0C0`** (565 #39E2C5, L\* 81.45, C 48.7, h 177.6) — unchanged hex.
The measurement says its hue is one of the two best available (5.9% team
collisions), it holds 9.16:1 on the live tile, and it is the product's identity.
Keep the value; fix where it is spent (§5f).

Meaning, narrowed to one sentence: **this is happening now — including the
instant of a touch.** `theme.h`'s argument for the press outline is sound and a
press genuinely *is* "now", so press stays `A_LIVE`.

**`A_SELECT = #F3F7FB`** (= `INK_1`; 565 #F6F6FF, L\* 97.1, C 4.6) —
**chosen / current / you are here.** Persistent selection, as distinct from the
instant of touch.

This is deliberately not a new hue, and the measurement is why. The emptiest
chromatic bands are 90–135° (0–2 collisions) and 315–345° (2–3). Both are bad:
a spring green at 115° sits 62° from `A_LIVE` and will read as "another teal" at
2–3 m, and a magenta at 330° reads as an error state. Adding either takes the
board from three chromatic channels to four for a job that does not need a hue.

Precedent already exists in the codebase for exactly this: `ui_board.cpp:1527`
gives the current page dot `C_INK`, and `ui_rail.cpp` gives the active filter tab
`lv_color_white()`. The token formalises what two call sites already do.

**Retire `C_LIVE_SD` and `C_LIVE_TX`.** Three teals at L\* 58.5 / 68.1 / 81.5 is a
23 L\* ramp of one hue — the same error the neutral ink tiers were fixed for.
`C_LIVE_TX` has zero uses. `C_LIVE_SD`'s ten uses split cleanly:

| `C_LIVE_SD` site | becomes | why |
|---|---|---|
| `ui_board:1706`, `ui_board:784/790`, `ui_idle:200/206` — the heartbeat rule | `EDGE_HI` when nominal, `S_ALERT` when overdue | a healthy heartbeat is not "live", it is "nothing wrong". Removes a teal from the top of every screen |
| `ui_board:695/1639` — the `LIVE` caps label | `A_LIVE` | it is the live channel; at 13 px F_MICRO, 9.6:1 is right |
| `ui_settings:468/532/538` — pill borders, meter | `A_SELECT` | persistent selection, not liveness |

### 5d. Semantics

**`S_LIVE` = `A_LIVE` = #3BE0C0.** Live, and only live.

**`S_FINAL` = #B6C4D2** (= `kStateInk[FINAL].ink`; 565 #B4C6D5, L\* 78.98,
chroma 10.0). *Declared neutral, on purpose.* A settled game is already
unambiguous from the surface ladder (L\* 8.10 vs 14.87) and from the score
having stopped. Giving it a hue would add a fourth chromatic channel to encode
something two other channels already encode. The statement the system makes is:
**colour means live; the absence of colour means settled.** This also fixes
`ui_idle.cpp:443`, which today gives a *scheduled* favourite a `teamInk()` edge
light while a *final* correctly gets none.

**`S_ALERT` = #F2B441** (= `C_WARN`, unchanged hex; 565 #F6B641, L\* 78.13,
C 66.6, h 79.0). **The system is not okay. Nothing else.**

Today `C_WARN` carries two incompatible jobs. Measured on the default board, the
game-situation chip is **1303 px — 15.3% of all salient chroma on screen** — and
it is present in two of three live tiles. Amber is therefore a *constant*
presence, which means it has no interrupt value left for the case it exists for
(`NO WI-FI`, `PROXY UNREACHABLE`, stale upstream, league cap).

Resolution, using logic that already exists in the repo:

- The situation chip's **default** ink becomes `si.ink2`. "POWER PLAY" is a fact,
  like the clock and the broadcast; those are neutral.
- It goes `S_ALERT` **only when `ui_focus.cpp`'s `isTense()` is true** — runners
  in scoring position with two out, power play, red zone. That function is
  already written and already computes exactly "something is at stake right now".

This removes an entire hue family from the steady state while *strengthening*
what amber means, and costs one call to a function that already runs. It is the
single largest reduction in hue-family noise available: the default board drops
from **8 salient hue families to 6**, and the two that go are both chrome.

### 5e. The team channel — `T_INK`, one ratio, two clamps

**`T_INK(colour, surface) = lift(colour, surface, 4.5f)`, clamped to L\* ≤ 62.**

One ratio for *every* team-derived element — edge light, score digit, bloom,
win-prob bar, alert edge, idle edge, favourite swatch. Justification, measured:

| target | L\* p5 / median / p95 | median chroma | inside L\* 46–62 |
|---:|---|---:|---:|
| 3.5:1 (today's edge) | 50.8 / 51.8 / 79.9 | 80.2 | 49/54 |
| **4.5:1** | **57.7 / 58.4 / 79.9** | **67.5** | **49/54** |
| 5.0:1 | 60.9 / 61.4 / 79.9 | 62.4 | 39/54 |
| 5.5:1 | 63.5 / 64.2 / 79.9 | 57.5 | **0/54** |
| 6.5:1 (today's score) | 69.2 / 69.6 / 79.9 | 48.0 | 0/54 |

**4.5:1 is the highest ratio that keeps the whole team channel inside one
lightness band.** It is also WCAG AA for normal text and AAA for large text, so
the same value serves a 46 px score digit and a 4 px edge stub. Toronto becomes
one blue on its tile instead of two.

**The ceiling clamp is the other half, and it is the part that is missing today.**
`lift()` only raises. Add: after lifting, if L\* > 62, scale the triple down
(hue preserved) until L\* = 62.

| team | today | with ceiling | contrast vs live tile | vs `S_ALERT` |
|---|---|---|---:|---:|
| NHL BOS / NSH #FFB81C | #FFBA18 (L\* 79.9, **1.5° from `S_ALERT`**) | **#C58D10** (L\* 62.5) | 5.38:1 | **1.62:1** |
| SEA #99D9D9 | #9CDADE (L\* 83.2) | #739D9C (L\* 61.8) | 5.27:1 | 1.66:1 |
| ANA #F47A38 | #F67939 (L\* 64.6) | #EE7531 (L\* 62.6) | 5.41:1 | 1.62:1 |
| VGK #B4975A | #B4955A (L\* 63.3) | #AC9152 (L\* 61.3) | 5.19:1 | 1.69:1 |
| SF, EDM | unchanged (L\* 60.3, 58.4) | unchanged | 5.01 / 4.63:1 | — |

Boston's gold stops being `S_ALERT` and stays legible at 5.38:1. Only four teams
in 54 move at all.

Also: **`teamInk()` must go, or take a surface.** Three sites (`ui_alert:163/203`,
`ui_idle:443`, `ui_settings:994`) hard-code the live-tile fill regardless of what
they draw on. Replace with `T_INK(colour, actual_surface)`.

**`T_NONE = #5D6D7E`** — the placeholder badge, promoted from a literal repeated
at eight sites. `ui_hero.cpp:218`'s `0x334455` becomes `T_NONE`.

**Fix `ui_alert.cpp:205`.** `s_pulse` must use `T_INK(e.color, SURF_LIVE)`, not
`e.color`. 39 of 54 teams are currently below 3:1 there; Toronto is at 1.01:1.

### 5f. Where the accent is spent

The accent's meaning is destroyed by its budget, not its hue. Two changes.

**The rail sliver.** `ui_rail.cpp:89` fills the whole segment `C_LIVE` when a
league has anything live. On a typical night that is every league, so the entire
left spine is the accent — **3268 px, 92.7% of all `A_LIVE` on the board**.
Change: the segment body is always `EDGE_HI` (structure band, encodes *which
leagues and in what proportion*); liveness is a `A_LIVE` cap, 8 × 3 px, at the
top of each live segment. Budget falls from 3268 px to ~72 px — a 45× reduction —
and the accent regains scarcity.

**This is one of the two places the desk view and the across-the-room view
genuinely conflict, so state it plainly:** an 8 × 3 px cap subtends 0.013° at
2.5 m and is invisible from across the room. But so is the current 8 px-wide
sliver *as a structure* (0.14° at 610 mm → 0.035° at 2.5 m — at the acuity limit;
you see a line, you cannot read it). The rail is a desk-only affordance either
way. The across-the-room liveness signal is the top-bar `LIVE n` count and the
pulsing dots, and those are exactly what starving the rail makes louder. The
trade is strictly in the room-glance's favour.

**The idle countdown.** `ui_idle.cpp:242` renders the time-until-first-game in
`C_LIVE` at `F_HERO` — 3810 px, **94.4% of the idle screen's accent**, on the
screen the panel shows most, meaning the exact opposite of the token. Change to
`INK_1`. It is already the largest type on the screen; it does not need colour to
be found. `A_LIVE` on the idle screen then means what it says — and, correctly,
appears nowhere, because nothing is live.

**The pulse floor.** `pulse.cpp:46` must not let the dot leave the signal band.
Measured floor for L\* ≥ 72 on all three surfaces it appears on:

| floor opa | live tile | hero | plate |
|---:|---|---|---|
| 150 (today) | L\* 54.4, 3.97:1 | L\* 56.1, **3.79:1** | L\* 49.9, 4.48:1 |
| 215 | L\* 71.0, 7.1:1 | L\* 72.2, 6.5:1 | L\* 69.6, 8.8:1 |
| **225** | **L\* 73.5, 7.6:1** | **L\* 73.8, 6.9:1** | **L\* 72.2, 9.6:1** |

`stepOpa()` becomes `225 + (30 * up) / half`. Amplitude falls from 27.0 ΔL\* to
8.0 ΔL\*. That is still ~10× the Weber threshold for a static luminance step, and
temporal change is detected far below static discrimination threshold — the dot
still visibly breathes. What it stops doing is dropping into the team band twice
a second.

### 5g. RGB565 survival

Every proposed token round-tripped through 5/6/5:

| token | src | 565 | exact? | L\* | C | h |
|---|---|---|---|---:|---:|---:|
| `A_LIVE` / `S_LIVE` | #3BE0C0 | #39E2C5 | shifts 1 LSB | 81.45 | 48.7 | 177.6 |
| `S_ALERT` | #F2B441 | #F6B641 | shifts | 78.13 | 66.6 | 79.0 |
| `A_SELECT` / `INK_1` | #F3F7FB | #F6F6FF | shifts | 97.12 | 4.6 | 290.5 |
| `S_FINAL` | #B6C4D2 | #B4C6D5 | shifts | 78.98 | 10.0 | 252.4 |
| team ceiling example #C58D10 | — | #C58D10 | **exact** | 62.5 | 66.0 | 79.6 |

**No proposed token collapses onto any neighbour in 565.** The closest pair is
`S_ALERT` #F6B641 and the clamped Boston gold #C58D10 — same hue family by
design, separated by 15.6 L\* and 1.62:1, which is the whole point of the band
rule.

The one collision that exists today — `C_PLATE` #04070E and the scrim #04070C
both → **#000408** — is removed by deleting the scrim literal.

Note for anyone re-solving: the tokens themselves shift by 1 LSB on the
round-trip, but the *ladder* survives intact. The rendered L\* values above are
what the ladder actually is; the declared 8-bit values are notation.

### 5h. Migration table

| today | becomes | files touched |
|---|---|---|
| `C_PLATE` | `BG` | rename only |
| `C_SURF` / `C_SURF_1` / `C_SURF_2` / `C_SURF_3` / `C_FROST` / `C_FROST_2` | `SURF_FINAL` / `SURF_PRE` / `SURF_LIVE` / `SURF_HERO` / `SURF_INSET` | rename only |
| `C_INK` / `C_INK2` / `C_INK3` | `INK_1` / `INK_2` / `INK_3` | rename only |
| `C_EDGE` / `C_EDGE_HI` / `C_LINE` | `EDGE` / `EDGE_HI` / `LINE` | rename only |
| `C_LIVE` | `A_LIVE` (= `S_LIVE`) | rename; **narrow the uses** |
| `C_LIVE_SD` | **delete** → `EDGE_HI` (heartbeat) / `A_LIVE` (LIVE label) / `A_SELECT` (settings) | `ui_board`, `ui_idle`, `ui_settings` |
| `C_LIVE_TX` | **delete** (zero uses) | `theme.h` |
| `C_WARN` | `S_ALERT`, meaning narrowed to system health | `ui_board:555` gated on `isTense()` |
| — | `A_SELECT` = #F3F7FB | new; replaces ad-hoc `C_INK`/`lv_color_white()` at page dots + filter tab |
| — | `S_FINAL` = #B6C4D2 | new; declares finals achromatic |
| `teamInk(c)` | `T_INK(c, surface)` @ 4.5:1 + L\* ≤ 62 ceiling | `theme.cpp`, 3 call sites |
| `teamInkOn(c, s, 6.5f)` | `T_INK(c, s)` | `ui_board:1353`, `ui_hero:405` |
| `teamInkOn(c, s)` | `T_INK(c, s)` | `ui_board:1463`, `ui_hero:420/447/467/468`, `ui_game:202` |
| `0x5D6D7E` ×8, `0x334455` | `T_NONE` | 9 sites |
| `0x04070C` ×2 | `BG` | `ui_alert:86`, `ui_lineup:173` |
| `0x3A4757`, `0x4A5666`, `0x1E2836`, `0x0B111B`, `0x2A3646` | `EDGE_HI` / `INK_3` / `EDGE` / `BG` / `EDGE` | 5 sites |
| `e.color` raw | `T_INK(e.color, SURF_LIVE)` | `ui_alert:205` — **bug fix** |

Flash cost: zero. No new fonts, no new assets, no new geometry. Runtime cost:
one extra L\* comparison inside `lift()`, on a path that is already
change-cached.

---

## 6. What I would NOT change, and why

1. **The surface ladder** — #04070E / #101825 / #16202E / #1B2636 / #222E40.
   Measured L\* 1.89 / 8.10 / 11.96 / 14.87 / 18.65: monotonic, evenly spaced
   (mean step 4.2 L\*), and it is the *only* thing carrying game state. Renaming
   it is the whole change.

2. **`kStateInk[4]`** — every tier holds its floor (ink ≥ 10:1, ink2 ≥ 7:1,
   ink3 ≥ 4.5:1) against its own surface, and `final.ink2`'s separate solve is
   correctly reasoned. Re-solving would break the guarantee the brief asks me to
   preserve. Untouched.

3. **`C_LIVE`'s hex.** Measurement contradicts the file's own comment: teal is
   one of the two *quietest* bands the team channel leaves open (5.9% collisions
   vs 21.6% for the neutral family). Changing it would be changing the one thing
   that is right.

4. **`C_WARN`'s hex.** Same: 5.9% collisions, near-optimal. Only its *meaning*
   is overloaded. #F2B441 stays.

5. **`C_LINE` at three opacities.** One hue at 24/40/120 is exactly right, and
   its 21.6% nominal hue collision with the navy lobe is not a real collision —
   at chroma 15.5 and those opacities it never reads as a colour.

6. **The plate's Bayer dither** (`plate.cpp`). It spreads the ground across
   #000408–#182021, hue 199–300°, which looks alarming in a histogram and is
   invisible and correct: sub-1 ΔE at L\* < 7, and it is what makes near-black
   read as material. `LIFT_MAX 10` is correctly derived from the FINAL surface's
   red channel. Leave it.

7. **The logo-chip solver and its single light rung** #DCE4EE. The "two
   treatments only" argument is right and the measured result (36.3% → 3.6% ink
   lost) is real.

8. **`teamFill()` at 1.6:1 and `badgeInk()`.** Badges are small, bounded, and
   carry their own label ink. They are the one place a team colour can be loud
   without competing with chrome. Unchanged.

9. **The press treatment** — a 2 px `A_LIVE` outline, no fill. `theme.h`'s
   argument (state-independent, identical everywhere) is correct, and "the
   instant of a touch" genuinely is "now". Keep.

10. **The bloom.** It is 220×220 px of team hue and it *is* the largest
    chromatic object on the panel — but it is also the one soft element against
    an all-hard-edged design, and at opa 200 over `SURF_HERO` it is light, not
    paint. Under `T_INK`'s single ratio it will simply agree with the score digit
    beside it instead of contradicting it, which is the actual fix.

---

## 7. Note on the hero staged reveal (colour consequence only)

Not a palette problem, and the washed-out look in a screenshot is the animation
caught mid-flight. But it has one colour consequence worth recording: because
`ui_hero.cpp:505` sets **root** opacity to 85, the entire card subtree composites
through one transient layer, so for ~80 ms *every* contrast floor on the card —
`kStateInk[HERO]`, `A_LIVE`, `T_INK` — is multiplied by 0.33 simultaneously.
Nothing on the card meets AA during that window. `revKill()` does restore
`LV_OPA_COVER` and `revTick()` always terminates there, so it self-heals; the
exposure is a rebuild landing between the two 80 ms rungs. Cheap belt-and-braces:
have the rebuild path call `revKill()`. Layout/motion's call, not mine.

---

## Appendix — reproduction

```
desktop/scoredeck-ui --shot /tmp/colorsys-s0.bmp --scenario 0     # mixed board
desktop/scoredeck-ui --shot /tmp/colorsys-feature.bmp --scenario 10
desktop/scoredeck-ui --shot /tmp/colorsys-idle.bmp --screen idle
```

Analysis scripts (scratchpad, not in-repo): `color.py` (sRGB/CIELAB/OKLCH/565
round-trip, plus an exact Python port of `theme.cpp`'s `lift()`), `inventory.py`,
`teams.py`, `split.py`, `sweep.py`, `families.py`, `pulse_dot.py`, `sample.py`.

Harness fidelity confirmed against `desktop/shots/panel-i2-board.png` (a real
panel capture): #39E3C6 3959 px (countdown), #299E8C 214 px (heartbeat),
#1879EF 176 px (Toronto next-up edge) — all three exactly as the harness renders
them.
