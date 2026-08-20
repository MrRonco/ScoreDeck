# ScoreDeck review — glanceability

*Area: contrast and type size. Read-only pass. All numbers measured from the
already-built harness binary (`desktop/scoredeck-ui --shot`), from the checked-in
LVGL font tables, and from `ui/theme.cpp` / `ui/ui_board.cpp` / `ui/ui_hero.cpp`.
No repo file was modified, no build was run, the panel was not contacted.*

---

## 0. The measuring stick

**Geometry.** The brief fixes 56.2 px/degree at 610 mm. Back-solving gives a pixel
pitch of **0.18946 mm** (≈134 ppi, consistent with a 7″ 800×480 panel). From that:

| distance | arcmin per pixel | px per degree |
|---|---|---|
| 610 mm (desk) | 1.0677′ | 56.2 |
| 2500 mm (room) | 0.2605′ | 230 |

Ratio 4.098. Everything below follows from those two numbers.

A useful translation: **1 ScoreDeck pixel = 0.716 CSS pixels** at the same distance
on a 96-dpi screen. The panel is a high-DPI display, and the type sizes in
`theme.cpp` were chosen as if it were not. `F_MICRO` at 13 px lands on the retina
like **9.3 px** of a normal monitor; `F_BODY`/`F_NUM` at 15 px land like **10.7 px**.

**Thresholds (guidelines, not law — cited so you can disagree with the line, not
the arithmetic):**

- **ISO 9241-303:2011** (and its predecessor ISO 9241-3) gives a *character height*
  (= cap height for Latin) design minimum of **16′**, with **20–22′ preferred**, and
  recommends the upper figure where viewing conditions are degraded — which is what
  "across the room, at a glance, off-axis" is.
- **Legge & Bigelow (2011)** put the *critical print size* for maximum reading speed
  at roughly **0.2° x-height ≈ 12′**; below that reading speed measurably falls.
- **Snellen 20/20** = a letter subtending **5′** (1′ stroke). This is a *threshold*,
  not a reading size. 20/30 vision — extremely common uncorrected — moves the
  threshold to 7.5′.

I use four verdicts:

| verdict | band | meaning |
|---|---|---|
| READS | ≥ 20′ | glanceable, unfamiliar content, no effort |
| MARGINAL | 12–20′ | fine for *familiar* strings (digits, your own team's abbr); effortful for prose |
| EFFORTFUL | 7–12′ | resolvable if you deliberately look; fails on a glance; fails outright for a 20/30 eye |
| FAILS | < 7′ | at or below acuity threshold — texture, not text |

---

## 1. Type census

Every face in the build, its measured cap/digit height taken from the glyph
descriptors in `firmware/ScoreDeck/src/assets/font_*.c` (`box_h` of `'0'`), not from
the point size.

| token | face | px | cap px | ≈CSS px | ′@610 mm | ′@2.5 m | desk | room |
|---|---|---|---|---|---|---|---|---|
| `F_MICRO` | `micro13` IBM Plex Mono Medium | 13 | **9** | 9.3 | **9.6′** | 2.34′ | EFFORTFUL | FAILS |
| `F_NUM` | `num15` Plex Mono Medium tnum | 15 | **11** | 10.7 | **11.7′** | 2.87′ | EFFORTFUL | FAILS |
| `F_BODY` | `body15` Plex Sans Regular | 15 | 11 (x-ht 8) | 10.7 | **11.7′** | 2.87′ | EFFORTFUL | FAILS |
| `F_ABBR` | `abbr17` Archivo Cond SemiBold | 17 | **11** | 12.2 | 11.7′ | 2.87′ | EFFORTFUL | FAILS |
| `F_TITLE` | `title20` Archivo Cond Bold | 20 | 14 | 14.3 | 14.9′ | 3.65′ | MARGINAL | FAILS |
| `F_DISPLAY` | `display30` Archivo Cond Bold | 30 | 20 | 21.5 | 21.4′ | 5.21′ | READS | FAILS |
| `F_SCORE` | `score38` Archivo Cond Bold tnum | 38 | 27 | 27.2 | 28.8′ | **7.03′** | READS | EFFORTFUL |
| `F_SCORE_BIG` | `score46` | 46 | 34 | 32.9 | 36.3′ | **8.86′** | READS | EFFORTFUL |
| `F_HERO` | `hero72` (2 bpp) | 72 | 52 | 51.6 | 55.5′ | **13.55′** | READS | MARGINAL |
| `F_CLOCK` | `clock96` (2 bpp) | 96 | 69 | 68.7 | 73.7′ | **17.98′** | READS | READS |

Note `F_ABBR` is a 17 px face with an **11 px cap** — Archivo Condensed's cap is only
0.65 of the em, so `F_ABBR` is angularly *identical* to the 15 px mono faces despite
looking two sizes larger in source.

Roles, by face (from a full grep of `src/ui/*.cpp`):

- **`F_MICRO`** — zone-A `LIVE` and `/ N`, the filter pill, all three nav pills, the
  tile **situation chip** (`POWER PLAY`, `RED ZONE`, `1 OUT`), field-variant headers,
  the hero's league label and **last-scoring-play line**, every rail row, every
  settings label, the alert hint, the idle column headers, `teamBadge()`'s abbr.
- **`F_NUM`** — tile status (`3rd 04:21`, `Final/OT`), **team records**, broadcast,
  hero status/foot/right, the idle today/latest rows, the news timestamp, the ledger,
  all stats and clocks.
- **`F_BODY`** — reader prose, news headline + description, player names, city names.
- **`F_TITLE`** — tile team abbreviation, and only that.
- **`F_DISPLAY`** — hero team name, zone-A live count, alert verb, idle AM/PM.
- **`F_SCORE`/`_BIG`** — tile score. `_BIG` at Roomy and on the lead tile.
- **`F_HERO`** — hero score, idle countdown. **`F_CLOCK`** — idle clock.

`font_micro11.c` is still in the tree and is referenced by nothing.

### 1.1 What this table says

**At 2.5 m the board screen has no legible element at all.** The largest thing on a
default (Standard) board is a 27 px score cap at **7.03′** — 1.4× the 20/20 acuity
threshold, i.e. resolvable if you stop and stare, invisible on a glance, and *below*
threshold for a 20/30 eye. Every other element on the board is 2.3–5.2′: texture.

The one screen that already works across the room is **idle** — the 96 px clock at
18.0′ and the countdown at 13.6′. The panel has already proved it can be glanceable;
the board simply does not apply the same type scale.

**At 610 mm the design also misses its own standard.** `F_MICRO` (9.6′) and
`F_NUM`/`F_BODY`/`F_ABBR` (11.7′) all sit below ISO 9241-303's **16′ minimum** at the
exact distance the panel was calibrated for. `theme.h` says "Labels stay small; data
moved up" when `F_MICRO` went 11 → 13 px and data moved to `F_NUM` 15. That was the
right *direction* and it stopped roughly half-way: 15 px still lands at 11.7′, which
is ~60% of the angular size of ordinary 11 pt body text on a laptop.

**Prose is below the critical print size.** `F_BODY`'s x-height is 8 px = **8.5′** at
610 mm, against Legge's ~12′ CPS. The reader screen (a 560 px column of ~70-character
lines) is therefore read measurably slower than it needs to be.

To hit each threshold you need this much **cap height**:

| target | @610 mm | @2.5 m |
|---|---|---|
| 12′ (familiar-digit floor) | 11.2 px | **46.1 px** |
| 16′ (ISO minimum) | 15.0 px | **61.4 px** |
| 20′ (ISO preferred) | 18.7 px | **76.8 px** |

---

## 2. Contrast census

### 2.1 As specified — the theme is in good shape

Every ink × surface pair, round-tripped through RGB565 the way the panel actually
stores it (`R>>3`, `G>>2`, `B>>3`, re-expanded with bit replication — this is what
`desktop/main.cpp:100` writes to the BMP, so the shots *are* panel values):

| ink | plate | final | pre | live | hero |
|---|---|---|---|---|---|
| `C_INK` #F3F7FB → **#F7F7FF** | 19.30 | 16.77 | 15.63 | 14.75 | 13.10 |
| `C_INK2` #ACBCCE → **#ADBECE** | 10.81 | 9.39 | 8.75 | 8.26 | 7.34 |
| `C_INK3` #8696A8 → **#8496AD** | 6.81 | 5.91 | 5.51 | 5.20 | **4.62** |
| `pre.ink` #D2DEEA | 15.34 | — | 12.42 | — | — |
| `final.ink` #B6C4D2 | — | 10.31 | — | — | — |
| `final.ink2` #95A5B7 | — | 7.13 | — | — | — |
| `C_LIVE` #3BE0C0 → **#39E3C6** | 12.71 | 11.05 | 10.29 | 9.72 | 8.63 |
| `C_LIVE_SD` #2A9E8C | 6.23 | 5.42 | 5.05 | 4.76 | **4.23** |
| `C_LIVE_TX` #30B89D | 8.46 | 7.35 | 6.85 | 6.46 | 5.74 |
| `C_WARN` #F2B441 → **#F7B642** | 11.48 | 9.98 | 9.30 | 8.78 | 7.80 |

RGB565 quantisation slightly *helps*: the plate rounds #04070E → **#000408**, i.e.
darker, so every ratio against it rises. Nothing regresses.

Sampled off the real render (`--scenario 0`, Standard board), every text-on-surface
pair that actually ships:

| element | measured ink | surface | ratio |
|---|---|---|---|
| team name (`F_TITLE`, live) | #F7F7FF | #182431 | 14.75 |
| team name (pre) | #D6DFEF | #102029 | 12.42 |
| team name / winner score (final) | #B5C7D6 | #101821 | 10.31 |
| **leading score, team ink** (TOR) | #7BAAFF | #182431 | **6.77** |
| leading score, team ink (KC) | #FF869C | #182431 | 6.85 |
| leading score, team ink (EDM) | #FF8A5A | #182431 | 6.77 |
| **trailing score** (live, `ink3`) | #8496AD | #182431 | 5.20 |
| loser score (final, `ink3`) | #8496AD | #101821 | 5.91 |
| **record** `12-4-2` | #8496AD | #182431 | 5.20 |
| status `3rd 04:21` | #ADBECE | #182431 | 8.26 |
| status (final) | #94A6B5 | #101821 | 7.13 |
| **situation** `POWER PLAY` (`C_WARN`) | #F7B642 | #182431 | 8.78 |
| broadcast `SN` | #8496AD | #182431 | 5.20 |
| reader body prose | #ADBECE | #000408 | 10.81 |
| idle clock | #F7F7FF | #101018 | 17.76 |
| idle countdown (`C_LIVE`) | #39E3C6 | #182431 | 9.72 |

**Nothing ships below 4.5:1.** The `kStateInk` solve holds. Two corrections to the
brief's premises:

1. **`teamInkOn()` does *not* target 3.5:1 for scores.** `ui_board.cpp:1353` and
   `ui_hero.cpp:405` both pass `6.5f`. Measured across sixteen real team colours the
   lifted score lands at **6.67–6.91:1** on live and hero surfaces (and higher for
   teams already bright: Seattle 10.18, Boston 9.20). The 3.5 default survives only
   on the *edge light* (`ui_board.cpp:1463`) and the *lead rule* (`:1467`) — 3 px and
   2 px strips, which WCAG 1.4.11 puts at a 3:1 floor, so they clear it too. **A
   3.5:1 score is not a live defect.**
2. The single worst *specified* pair on the panel is `C_LIVE_SD` on the **hero**
   surface at **4.23:1** — below AA. `theme.h` already documents this and mints
   `C_LIVE_TX` to fix it. **`C_LIVE_TX` is then never used anywhere** (`grep` over
   all of `src/`: zero references outside `theme.h`). In practice `C_LIVE_SD` never
   reaches a hero surface today, so the defect is latent, not live — but the
   defence against it is a dead token.

### 2.2 As rendered — where the contrast actually goes

WCAG is computed on the *specified* colour. A 13 px mono glyph does not put its
specified colour anywhere. Measuring the coverage of every glyph pixel in the render
and re-computing:

| element | face | median stem | mean coverage | nominal | **coverage-weighted** |
|---|---|---|---|---|---|
| trailing score | `score38` | 6.0 px | 0.86 | 5.20 | 4.61 |
| leading score | `score38` | 5.0 px | 0.84 | 6.85 | 5.89 |
| team name | `title20` | 3.0 px | 0.72 | 14.75 | 10.90 |
| status | `num15` | 2.0 px | 0.58 | 8.26 | 5.24 |
| situation chip | `micro13` | 2.0 px | 0.59 | 8.78 | 5.62 |
| **record** | `num15` | 2.0 px | 0.62 | 5.20 | **3.62** |
| **broadcast `SN`** | `num15` | 1.0 px | 0.60 | 5.20 | **3.51** |
| **zone-A `LIVE`** (`C_LIVE_SD`) | `micro13` | 1.0 px | — | 5.95 | **3.50** |
| **zone-A `/ 9`** (`C_INK3`) | `micro13` | 1.0 px | — | 4.82 | **3.02** |

The `/ 9` case is the clearest: a 36×18 px histogram of that glyph pair contains
**7 pixels at #738294 and 5 at #6B798C, and nothing brighter**. `C_INK3` is
#8496AD. The specified ink is never reached — not dimmed, *never rendered*. Every
1-px-stem string on the panel is in this condition.

### 2.3 As delivered at each distance

Convolving the render with a Gaussian at the foveal PSF for a 20/20 eye
(σ = 0.5′, i.e. 0.47 px at 610 mm and 1.92 px at 2.5 m) and re-measuring peak
contrast inside each element's box:

| element | as drawn | delivered @610 mm | **delivered @2.5 m** |
|---|---|---|---|
| logo / badge, 30 px | 17.04 | 13.95 | **10.12** |
| team name `MTL` (`title20`) | 14.75 | 14.75 | 8.60 |
| leading score digit (`score38`) | 6.77 | 6.77 | 5.17 |
| live dot, 6 px (`C_LIVE`) | 9.72 | 9.72 | **4.96** |
| situation `POWER PLAY` (`C_WARN`) | 8.78 | 8.18 | **3.75** |
| status `3rd 04:21` | 8.26 | 8.00 | **2.92** |
| edge light, 3×30 px (team, 3.65:1) | 4.77 | 4.60 | **2.28** |
| **record** `12-4-2` | 5.20 | 5.01 | **2.18** |
| broadcast `SN` | 5.20 | 4.81 | **2.36** |
| lead rule, 220×2 px (team ink) | 3.65 | 3.55 | **2.04** |

Two channels survive 2.5 m with room to spare: **the logo/badge (10.1:1) and the
team-coloured score (5.2:1)**. Everything the brief nominates as expendable —
records, broadcast, status — collapses under 3:1 and becomes an undifferentiated
grey haze. That is the correct outcome; it just means those elements are *already*
paying nothing at room distance while still consuming ink at desk distance.

The two team-colour *chrome* devices — the 3 px edge light and the 2 px lead rule —
are sub-resolution at 2.5 m (0.78′ and 0.52′ wide) and deliver 2.28:1 and 2.04:1.
They cannot promote anything across the room.

*(A simulated 2.5 m view of the Standard board is at `/tmp/glanceability-view25m.png`;
the true retinal footprint is `/tmp/glanceability-view25m-small.png` at 195×117. The
hero and idle equivalents are `/tmp/glanceability-s10-25m.png` and
`/tmp/glanceability-idle-25m.png`.)*

### 2.4 The 6.5:1 lift is quietly destroying the team-colour channel

This is a contrast finding with a palette consequence, and it belongs to the owner's
complaint. `lift()` preserves hue *exactly* (Δh ≤ 1.7° across all sixteen sampled
teams) and normalises saturation and lightness to hit a ratio. Team colours that
differ mainly in *lightness* — which is most navies and most reds — therefore
converge. Sixteen sampled colours from `desktop/scenarios.cpp`, lifted to 6.5:1 on
the live fill and round-tripped through RGB565:

```
TOR #00205B -> #7BAAFF     BUF #00338D -> #7BAAFF     ← byte-identical
KC  #E31837 -> #FF869C     CGY #C8102E -> #FF869C     ← byte-identical
EDM #FF4C00 -> #FF8A5A     SF  #FD5A1E -> #FF8A5A     ← byte-identical
DAL #041E42 -> #73AEFF     NYY #132448 -> #84AAFF     MTL #AF1E2D -> #FF8694
COL #6F263D -> #FF82AD     BOS #FFB81C -> #FFBA18     VGK #B4975A -> #C6A663
```

**11 collision pairs among 16 teams** (ΔH < 8°, ΔL < 8). The sixteen colours collapse
to about five perceptual clusters: periwinkle, salmon, peach, gold, cyan. At 2.5 m,
where the *score's colour* is one of only two surviving identity carriers, five
buckets is not an identity channel.

And it breaks an invariant `theme.h` states explicitly. That file argues C_LIVE stays
separable from lifted team colours because "teamInk() stops the moment it clears
3.5:1 … while this accent sits at 9.16:1 — **2.6× brighter** … If teamInk()'s target
ratio ever rises, or this colour is ever darkened, re-check it."

It rose. Philadelphia's #004C54 lifted to 6.5 renders **#00BED6 at 6.99:1**, hue
186.7° against C_LIVE's 168.4° — an 18° separation at a **1.39×** luminance margin,
not 2.6×. A Flyers score and a live dot are now the same colour family at similar
brightness. The file asked to be re-checked under exactly this condition.

---

## 3. Where the pixels actually go

Ink census of one live Standard tile (248×128 = 31,744 px, fill #182431), counting
every pixel that differs from the fill:

| region | ink px | share of tile ink |
|---|---|---|
| **frame** — border, specular pair, lead rule, edge light, live dot, corner AA | **1,999** | **33.3 %** |
| badges / logos (2) | 1,307 | 21.8 % |
| status row (clock + situation/broadcast) | 927 | 15.5 % |
| **scores (2)** | **742** | **12.4 %** |
| team names (2) | 590 | 9.8 % |
| records (2) | 431 | 7.2 % |
| **total** | 5,996 | 18.9 % of the tile has any ink at all |

Three things fall out:

1. **81 % of the tile is empty surface.** Density is not the constraint the current
   type sizes behave as if it is.
2. **The frame out-inks the scores 2.7 : 1.** 1,999 px of border/specular/rule/edge
   against 742 px of the number the product exists to show.
3. **The record is physically wider than the team it annotates.** `12-4-2` is six
   mono glyphs at 9.0 px advance = 54 px; `MTL` in `F_TITLE` is 34 px. The
   subordinate string is 1.6× the width of its own heading.

The score column compounds this: it is a fixed 74 px right-aligned box
(`ui_board.cpp:422`), but a one-digit hockey score fills 18 px of it. On a board of
NHL and soccer games, ~56 px × 2 rows × 12 tiles ≈ **1,340 px of tile width** is
reserved and unused.

---

## 4. What can actually fit — the density/legibility curve

Modelling the existing tile anatomy (`TILE_PAD_Y` 11 × 2, `STATUS_H` 21, two rows,
12 px gutters, 12 px margins) and Archivo Cond Bold tnum's measured advance/cap ratio
of 0.6526, for a 48 px bar:

| rows | tile H | row H | max score cap | ′@2.5 m | 3 cols | 4 cols |
|---|---|---|---|---|---|---|
| 4 | 93 | 25.0 | 20 px | 5.21′ | 12 games | 16 games |
| **3** | **128** | **42.5** | **37.5 px** | **9.77′** | **9 games** | **12 games** |
| **2** | **198** | **77.5** | **72.5 px** | **18.89′** | **6 games** | 8 games\* |
| 1 | 408 | 182.5 | 177 px | 46.2′ | 3 games | 4 games |

\* 4 columns gives a 185 px tile; a three-digit score at cap 72.5 needs 142 px of
score column, leaving 1 px for the logo. **Four columns works only when every visible
score is two digits.** Three columns leaves 64 px for a logo with three-digit scores,
112 px with two.

**There is no middle ground.** Three rows tops out at 9.8′ regardless of how the
tile is arranged — it is geometrically impossible to make a 3-row board glanceable
at 2.5 m. Two rows lands at 18.2–19.2′, inside the ISO preferred band. The cliff is
between two rows and three.

**The most important consequence: `DEN_ROOMY` already has the right geometry and
throws half of it away.** `kDensity[DEN_ROOMY]` is `{58, 248, 193, 12, 16, 70, 3, 2,
38, 0}` — 3 columns × 2 rows, tile height 193, row height **75 px** — and it draws
the score in `F_SCORE_BIG`, cap **34**. The row can hold a cap of **70**. Roomy is
sitting on a 2.06× score increase it is not spending, and `effectiveDensity()`
already selects Roomy automatically when ≤ 6 games are visible — which is exactly
the quiet night you are most likely to be across the room for.

---

## 5. Ranking: what to grow, what to demote, what to cut

**Must survive 2.5 m** (owner's stated requirement): the two scores, which team is
which, and the game state. Nothing else.

### Scale UP

| element | now | proposed | why |
|---|---|---|---|
| **tile score at Roomy/Feature** | cap 34 (8.9′) | **cap 70 (18.2′)** — new `font_score95`, Archivo Cond Bold tnum, 2 bpp, 13 glyphs | the only number that must cross the room; the row already holds it |
| **hero score** | `F_HERO` cap 52 (13.6′) | cap ~70 (18.2′) — regenerate `hero72` at 96 px, 2 bpp | `HERO_ROW_H` is 84; cap 70 + 10 leading fits. Footer at y=218 must move to ~236 |
| **logo / badge at Roomy** | 38 px | **56 px** | measured the *best*-surviving element at 2.5 m (10.12:1). This — not the team name — is the identity channel that works across the room |
| **live dot** | 6 px | 10 px | 4.96:1 delivered at 2.5 m is the state channel's whole margin |
| **situation chip** | `F_MICRO` cap 9 (9.6′) | `F_TITLE` cap 14 minimum | `POWER PLAY` / `RED ZONE` / `1 OUT` is the second most important live fact and is currently drawn in the **smallest face on the panel** |
| **`F_NUM` at Roomy/Feature** | 15 px (11.7′) | 20 px (16.0′) | clears the ISO desk floor. See the cost note below |
| **`F_BODY` on the reader** | 15 px, x-ht 8.5′ | 19 px, x-ht ~10.7′ | reaches Legge's critical print size; the reader is the one screen where reading *speed* is the metric |

### Demote

- **Records `12-4-2`** — 2.18:1 delivered at 2.5 m, wider than the team name they
  annotate. Drop from Roomy/Feature tiles entirely (keep at Standard/Dense, which are
  the desk layouts), or move to `F_MICRO`.
- **Broadcast (`SN`, `ESPN`, `MLB.TV`)** — 2.36:1 delivered, and it is *static*
  information about a game you are already watching. Keep on PRE tiles, where it is
  actionable; drop from LIVE and FINAL.
- **Hero team name** (`F_DISPLAY`, 5.2′ at 2.5 m, ~248 px of column) — it is a desk
  ornament. Fall back to the abbreviation and hand the width to the score.
- **Hero last-scoring-play line** (`F_MICRO`, 2.3′) — desk-only by construction.

### Cut

- **The lead rule** (`ui_board.cpp:325–332`, recoloured to team ink at `:1467`) —
  220 × 2 px, 0.52′ tall, **2.04:1 delivered at 2.5 m**, and 3.55:1 at 610 mm. Once
  the leading team's score is 70 px of that team's own colour, a 2 px rule adds
  nothing at either distance. Saves an object per tile and 440 px of invalidation.
- **The edge light** (`EDGE_W` 3, 3 × 30 px, team ink at 3.5:1) — 0.78′ wide,
  **2.28:1 delivered**. Same argument: it says "this side is ahead", which the
  coloured score now says at 30× the area. If it is kept for desk viewing it needs to
  be ≥ 8 px to survive at all.
- **`font_micro11.c`** — referenced by nothing. ~27 KB of source, ~1.3 KB of flash.
- **`C_LIVE_TX`** — solved, documented at length, used nowhere. Either apply it (the
  hero's live-state values are the case it was minted for) or delete it.

### The density cost, stated plainly

- Standard board: **9 games → 6** (3×2 instead of 3×3).
- Dense board: **12 games → 8**, and only when every score is two digits; otherwise 6.
- Feature (hero + 2) is unchanged in count; the hero score doubles.
- A pager already exists (`TILES_PER_PAGE`, scenario 4 covers 48 games), so overflow
  has somewhere to go.

**This should be automatic, not a setting.** `effectiveDensity()` already switches on
game count: ≤ 6 → Roomy, ≤ 9 → Standard, else Dense. Redefine that boundary as the
*room/desk* boundary — Roomy becomes the across-the-room layout, Standard and Dense
stay honest desk layouts and explicitly surrender cross-room legibility. A quiet
night is the night you are across the room; a twelve-game night is a night you are
sitting at the desk. The existing rule already encodes that; it just isn't spending
the pixels.

### Costs

- **Flash.** The entire font set is 30 KB of bitmap today (`font_score46` is 2,057 B).
  A cap-70 score face at 2 bpp ≈ **8–10 KB**; `hero96` at 2 bpp ≈ **+3 KB**;
  `F_NUM` at 20 px ≈ **+4.4 KB**; `F_BODY` at 19 px ≈ **+11 KB**. Total under 30 KB
  against ~1.4 MB free. Negligible.
- **Runtime.** A cap-70 two-digit score bbox is 91 × 70 = 6,370 px against today's
  44 × 34 = 1,496 px. Worst case (six tiles, all twelve scores change in one poll) is
  **76 k px** against the ~50 k/tick sustained budget — it spills into a second tick,
  which is fine and still far under a 230 ms full-screen repaint. Today's worst case
  at twelve tiles is 36 k px, so this is roughly a 2× increase in the rare case and
  no change in the common one (the change-cache means only *changed* scores
  invalidate).
- **`F_NUM` at 20 px has a real layout cost.** The advance goes 9.0 → 12.0 px, and
  `STATUS_W` (81 = "nine glyphs", `ui_board.cpp:100`) and `SIT_FULL_W` (108 = twelve
  glyphs, `:103`) are derived from it. They become 108 and 144, and 108 + 144 = 252 >
  the 216 px of usable width in a 248 px tile. **The status row can no longer hold
  both the clock and the full situation vocabulary at 20 px.** That is precisely the
  desk/room trade: at Roomy, drop the broadcast column and let the clock and situation
  split the row; at Standard/Dense keep `F_NUM` at 15.

---

## 6. Two defects found in passing

**6.1 The hero reveal is over-priced and observably lands wrong.**
`ui_hero.cpp:504–507` sets root opacity 85 → 170 → COVER over two 80 ms rungs. An
`opa < COVER` on a parent forces LVGL 8.3 to composite the whole subtree through a
transient layer: 508 × 268 = **136,144 px per rung, ×3 = ~408 k px** — more than a
full 800×480 screen — to fade in one card. The evidence PNG in the brief is itself
the proof that a *complete* refresh cycle can finish with the card at 85/255. The
lifetime is safe (`uiHeroInit` kills the timer at `:284`, so no dangling `s_root`),
so this is a cost and a visual-risk defect, not a crash. Given "nothing may delay
data", reveal the hero with something that does not composite — the edge light or
the lead rule wiping in — or with nothing at all.

**6.2 Neutral greys go magenta on this panel.** `lift()` returns `#AAAAAA` for a
black team (Pittsburgh); RGB565 renders it **#ADAAAD**. 5-bit red/blue round 0xAA to
0xAD while 6-bit green holds 0xAA exactly, so every *pure* grey acquires a +3 magenta
cast. Harmless here — the theme's greys are deliberately blue — but it means
`lift()`'s achromatic fallback is the one place the palette emits an unintended hue.

---

## 7. Acceptance rule

Two rules, one per distance, both mechanically checkable.

### The room rule (2.5 m)

> Stand 2.5 m from the panel and glance at it for one second. For **every game on
> screen** you must be able to read, without leaning in:
>
> 1. **both scores**;
> 2. **which team is which** — from the logo or the colour, *not* from any text;
> 3. **whether the game is live, finished or not started yet**.
>
> **Nothing else is required to be legible.** Records, broadcast, the game clock,
> period/inning, situation text, league names, timestamps and the top bar may all be
> illegible at 2.5 m. That is intended, not a bug — do not "fix" them by growing
> them, because every pixel they take comes off the score.

Measurable form, so it can be a `make` target rather than an opinion:

| # | rule | today |
|---|---|---|
| R1 | score cap height ≥ **61 px** (16′). Target **73 px** (19′). | 34 px (Roomy), 27 px (Standard) — **fails** |
| R2 | team identity = one contiguous region ≥ **40 × 40 px** (≥ 10′) at ≥ 3:1 against its tile | 38 px badge at 10.1:1 — **passes at Roomy, marginal at Dense (26 px)** |
| R3 | state channel: live/final/pre distinguishable at ≥ **3:1 delivered** after a σ = 1.92 px Gaussian | live dot 4.96:1 — **passes** |
| R4 | nothing may be the **sole** carrier of R1–R3 facts if its cap height is < 46 px | status row carries "Final" alone — **fails** |

Automatable: `./scoredeck-ui --shot x.bmp`, Gaussian-blur σ = 1.92 px, downsample to
195 × 117 (the true retinal footprint at 2.5 m relative to the desk view), and assert
that the scores are still readable in that 195 × 117 image. That is a repeatable
regression test, and the two scripts to do it already exist in this pass.

### The desk rule (610 mm)

> Everything in the room rule, plus: records, broadcast, clock and situation are
> legible without leaning in.

Measurable form:

| # | rule | today |
|---|---|---|
| D1 | anything a user **reads** has cap height ≥ **15 px** (16′, ISO 9241-303 minimum) | `F_NUM` 11 px, `F_MICRO` 9 px — **fails** |
| D2 | anything a user reads has **coverage-weighted** contrast ≥ 4.5:1, not just nominal | record 3.62, broadcast 3.51, `/ N` 3.02 — **fails** |
| D3 | prose (reader, news) has x-height ≥ **11 px** (12′, critical print size) | 8 px — **fails** |
| D4 | `F_MICRO` may be used **only** for strings that are recognised by position and never read (nav pills, column headers) | it currently carries the situation chip and the hero's scoring play — **fails** |

D2 is the one worth adopting even if nothing else here is: the theme did the hard
work of solving every tier against every surface, and then a 1-px stem throws a third
of it away before it reaches the eye. Measure the render, not the token.
