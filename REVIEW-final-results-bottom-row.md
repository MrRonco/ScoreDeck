# ScoreDeck review — FINAL results treatment and bottom-row balance

Area: the owner's ask #3 — *"Give the FINAL results the same card treatment as the
live games and rebalance the bottom row — the bare table leaves dead space on the
lower left."*

Read-only audit. Sources read: `firmware/ScoreDeck/src/ui/ui_ledger.cpp` (full),
`ui_board.cpp` (layout/promotion/tile paths), `ui_hero.cpp` (geometry macros),
`ui_idle.cpp` (the second results table), `config.h` (`kDensity`), `theme.cpp/h`,
`proxy/src/app.ts` + `espn.ts` (`sortAndCap`), LVGL 8.3 `lv_label.c` / `lv_refr.c`
/ `lv_conf.h`. Renders produced with the prebuilt harness into `/tmp/ledger-area-*`.
Nothing in the repo was modified; the panel was not contacted.

---

## 0. Headline

**The FEATURE layout is already the standard 3×3 grid.** The hero occupies cells
(0,0)–(1,1), the tile strip occupies (0,2) and (1,2), and the bottom band is
row 2 of that same grid — but it is the only row not drawn on it. The ledger sits
at exactly the grid's row-2 `y` (340) and then lays out on a *different* horizontal
grid (2 × 372 at x 16/412) that shares no vertical with anything above it.

Verified against pixels, not comments:

| element | measured | grid cell it should occupy |
|---|---|---|
| standard grid row 2 (scenario 1, 3×3) | cards at **x 16–263, 276–523, 536–783**, **y 340–467** | — |
| hero (`HERO_X/W/Y/H`) | x 16–523, y 60–327 | cols 0+1 (248+12+248 = **508** = `HERO_W`), rows 0+1 (128+12+128 = **268** = `HERO_H`) |
| FEATURE tile strip (`kDensity[3]`) | x 536–783, y 60–187 / 200–327 | col 2, rows 0+1 |
| **ledger root** | **y 340**, cols at x 16–387 / 412–783 | **row 2 — but on a 372 px grid, not 248** |

So the owner's "dead space on the lower left" is not a sizing accident and not a
missing card. It is one row of the layout's own grid that was replaced with a
table drawn on a foreign grid, and then filled by two content streams that are
empty precisely when this layout is chosen.

---

## 1. The dead space, measured

### 1.1 The canonical case

`docs/img/panel-feature.png` (the real panel capture) is pixel-identical in layout
to harness `--scenario 3`. I measured both; numbers below are from the harness
render `/tmp/ledger-area-s3.png`, cross-checked against the panel PNG.

Content threshold: any pixel with `max(r,g,b) > 32`. The plate `#04070E` dithers
into the range (0–16, 4–16, 8–24), so this threshold separates plate from ink and
from card fill cleanly.

Ledger band = the ledger root's own rectangle, usable width x 16–783, y 340–479
→ **107,520 px**.

| region | rect | area | ink | verdict |
|---|---|---|---|---|
| whole band | x16–783, y340–479 | 107,520 px | **1,323 px = 1.23%** | — |
| UPCOMING column | x16–387, y340–479 | 52,080 px (48.4% of band) | **0 px** | 100% empty |
| FINAL rows 2–3 | x412–783, y402–479 | 29,016 px (27.0% of band) | **0 px** | 100% empty |
| **total void** | | **81,096 px** | | **75.4% of the band** |

Everything actually drawn in the lower third of the panel:

```
"FINAL" header   bbox (413,350)–(454,358)    42 × 9 px
hairline rule    bbox (412,368)–(783,368)   372 × 1 px
one result row   bbox (413,386)–(783,399)   371 × 14 px
```

Three elements. 1,323 lit pixels in 107,520. The 372 px rule is a long line
pointing into nothing.

Also empty on this frame: FEATURE tile slot 2 (x 536–783, y 200–327, 31,744 px,
**0 ink**). Whole-panel card coverage on this frame is **53.7%**.

### 1.2 The worst case

Harness `--scenario 7` (one live game, one game total): the ledger hides itself
entirely (`ui_ledger.cpp:187`), the tile strip is empty, and the measured ink in
**x 524–799, y 52–479 (118,128 px) is exactly 0**, as is the whole band
y 332–479 apart from the 8 px rail sliver. One 508×268 card on a 384,000 px
panel = **25% ink coverage**. The card sits in the top-left corner with an
L-shaped void of ~216,000 px around it.

### 1.3 Why it is empty — the structural cause

Not "the UPCOMING column happens to have no content in this scenario". The
promotion rule and the ledger's content model are **anti-correlated by
construction**:

`ui_board.cpp:211` — FEATURE is chosen when `live ∈ [1,3] && liveHeroable() ≥ 1`.

`ui_ledger.cpp:152` — column 0 takes `GS_PRE`, column 1 takes `GS_FINAL`, capped
at 3 rows each.

A board has few live games at exactly two moments: **before** the slate ramps
(→ many PRE, zero FINAL, right column empty) and **after** it winds down
(→ zero PRE, many FINAL, **left column empty — the owner's complaint**). In the
middle, when both columns would be full, there are usually ≥4 live games and
FEATURE is not chosen at all. For both columns to fill you need ≥3 PRE **and**
≥3 FINAL **and** only 1–3 live — i.e. ≥7 visible games with almost none of them
in progress. That is a narrow, transient window.

The harness's own canonical scenario set agrees. Of the four scenarios that
resolve to FEATURE:

| scenario | UPCOMING ink | FINAL ink | state |
|---|---|---|---|
| `--scenario 10` | 3,425 | 2,660 | both full (the only one) |
| `--scenario 3` | **0** | 1,323 | left column empty |
| `--scenario 6` | **0** | 1,186 | left column empty |
| `--scenario 7` | **0** | **0** | ledger hidden entirely |

**Three of four ship with the lower-left void.** The one that fills both columns
is the least representative board in the set (9 games, 3/3/3).

### 1.4 The alignment failure, independent of emptiness

Even with both columns full (`--scenario 10`), the band does not belong to the
composition above it:

* UPCOMING right edge **387** vs hero right edge **523** → 136 px off.
* FINAL left edge **412** vs tile-strip left edge **536** → 124 px off.
* Only the outer margins (16, 783) are shared.

The eye tracks the hero/strip seam at x≈530 down the panel and it dies at y=340;
a new seam appears 120 px to the left. That is the "balance" half of the
complaint, and it is present even on the one frame where nothing is empty.

(Rail-open is internally consistent: ledger columns 156/488 × 300 end at x 787,
which matches the rail-open tile strip's right edge 787 exactly. No defect there.)

---

## 2. What else is wrong with the ledger (found while auditing)

These matter because any card treatment inherits them unless fixed.

### 2.1 The FINAL column shows the *oldest* results of the night

`proxy/src/espn.ts sortAndCap()` sorts favourite → state → **`a.t - b.t`
(start time ASCENDING)**. `ui_board.cpp rankOf()` ranks on state and `isFav` only,
and `buildOrder()` is a stable insertion sort — so proxy order survives within a
rank. `uiLedgerRender()` then walks that order and takes the **first three**
finals it meets (`ui_ledger.cpp:153`).

Result: on a night with more than three finals, the column locks onto the three
games that **started earliest** and never shows the game that just ended. The
single most valuable settled result on the panel — the one that finished sixty
seconds ago — is the one guaranteed to be missing.

`Game.startUtc` is already parsed (`net/api.cpp:185`) and already used by
`ui_idle.cpp`. The fix is a selection pass, not a wire change.

### 2.2 The finals are the smallest *and* the loudest thing on the panel

Measured ink heights from the renders:

| element | face | ink height | at 610 mm (56.2 px/°) | at 2.5 m (230 px/°) |
|---|---|---|---|---|
| result **card** score (grid row 2) | `font_score38` | **27 px** | 28.8′ | **7.0′** |
| result card team abbr | `font_abbr17` | 14 px | 14.9′ | 3.6′ |
| **ledger** score, abbr *and* status | `font_num15` | **11 px** | 11.7′ | **2.9′** |

The ledger renders the score — the highest-value datum in the row — at the same
11 px as the broadcast callsign. Zero size hierarchy inside the row; the only
hierarchy is ink lightness.

**Where the two viewing distances conflict, say so:** at 2.5 m the 5′ acuity
limit means the ledger is not "small", it is *below the resolution limit* — no
character in it can be identified. A card does not fix that for team names
(14 px → 3.6′, still unreadable); it fixes it for the **score** (27 px → 7.0′,
the only element on a settled game that clears the limit). So the card treatment
buys glanceability for exactly one field, and that is the right one. Do not
oversell it: nobody reads "Final/OT" from the sofa at any size this panel can
afford.

Contrast, computed WCAG:

| datum | ledger (on plate `#04070E`) | on a FINAL card (`#101825`) |
|---|---|---|
| winning side | `C_INK` `#F3F7FB` → **18.72:1** | `kStateInk[2].ink` `#B6C4D2` → **10.02:1** |
| losing side | `C_INK3` `#8696A8` → 6.66:1 | `kStateInk[2].ink3` `#8696A8` → 5.89:1 |

`ui_ledger.cpp:171–172` uses the **global** `C_INK`, not `kStateInk[GS_FINAL]`.
So a settled result on the bare plate is rendered at the brightest ink in the
system on the darkest ground in the system — **1.87× the contrast the same datum
gets on a card**, in direct contradiction of the state ladder's rule that a final
is the quiet tier. That is a genuine, measurable palette incoherence in this area
(relevant to ask #1): the finals are not a different hue, they are a different
*tier discipline*.

### 2.3 A final on the board screen is the only game you cannot tap

`ui_ledger.cpp` contains **zero** `lv_obj_add_event_cb` and never sets
`LV_OBJ_FLAG_CLICKABLE` (grep count: 0). Meanwhile `ui_idle.cpp:82,308` gives the
idle screen's identical FINAL rows an invisible hit target with a pressed-only
`C_LINE @ 24` fill. The same result is tappable on one screen and inert on the
other. Idle already solved the "don't make inert things look interactive"
objection; the board ledger simply never adopted the solution.

(Minor, same family: idle's left table column is at x 24, the board ledger's at
x 16 — two tables of the same kind, 8 px apart. Idle's finals also use a
different format, `"VAN @ SEA   4 - 5"`, against the ledger's
`"VAN  4 | SEA  5 | Final/OT"`.)

### 2.4 The ledger is the only uncached per-poll writer on the screen

`ui_board.cpp` has `setTextCached` / `setNumCached` / `setHiddenCached`
(lines 265–300) and the tiles use them rigorously. `ui_ledger.cpp` uses none.
Every `uiLedgerRender()` — i.e. every poll — unconditionally writes 18
`lv_label_set_text`, 4 `lv_obj_set_style_opa`, and up to 6
`lv_obj_set_style_text_color`, whether or not anything changed.

LVGL 8.3 `lv_label_set_text` calls `lv_obj_invalidate()` at entry
(`lv_label.c:90`) *before* it compares anything, then reallocates and calls
`lv_label_refr_text()`.

Cost per poll, for zero visual change:

* **~34,752 px** of label area (6 rows × 362 px of fields × 16 px line height),
  plus 1,848 px of headers and 744 px of rules → **≈ 37,344 px**, about a third of
  a screen, ≈ 75 ms of redraw against the ~230 ms full-screen figure.
* **≈ 22 of the 64** `LV_INV_BUF_SIZE` region slots (`lv_refr.c:247–249` drops an
  area only when it is fully inside one already queued). Overflowing 64 does not
  degrade gracefully — `lv_refr.c:255–258` resets `inv_p` and invalidates the
  **whole screen**. The ledger is not routinely the cause, but it is the single
  largest uncached consumer on the screen the owner is complaining about.
* 18 `lv_mem` free/alloc pairs.

**Correction to a plausible worry:** `lv_conf.h:28–32` sets
`LV_MEM_CUSTOM_ALLOC = ps_malloc`, so LVGL objects and label text come from the
**8 MB PSRAM**, not the tight internal heap. These reallocs do *not* threaten the
16.4 KB contiguous block mbedTLS needs. The cost is invalidation and PSRAM churn,
not the TLS floor — and by the same token, **object count is close to a
non-constraint here**: 40 extra objects is ~8 KB of PSRAM.

---

## 3. What "the same card treatment" must mean here

`ui_ledger.cpp:5–17` documents a measured rejection of the obvious answer:

> "the first draft of the featured layout put the hero in a 508×268 cell and the
> ledger in a 768×124 one, and the result covered MORE of the panel than the
> nine-up grid it replaced: 86.8% against 84.4% … no figure, no ground."

That number reproduces exactly under my model, which is how I know we are
measuring the same thing:

| composition | card coverage |
|---|---|
| FEATURE today, both tiles filled (bar + hero + 2 tiles) | **62.0%** ← the file's own figure |
| FEATURE today, owner's actual frame (1 tile) | 53.7% |
| 3×3 grid (bar + 9 tiles) | **84.4%** ← the file's own figure |
| **naïve fix: reuse full 128 px tiles for row 2** | **86.8%** ← **exactly the rejected number** |

So "just put the finals in three grid tiles" walks straight back into the
rejected design. Any proposal has to clear 84.4%-with-margin, not just look like
a card.

Two levers make that possible without abandoning the card:

1. **Height.** Grid tiles already vary by density (193 roomy / 128 standard /
   128 dense). A settled result needs two abbrs, two scores and one state word —
   not records, situation lines, broadcast and win-probability. **100 px** carries
   it and leaves a 40 px reserve of visible plate along the bottom edge, which is
   the "ground" the file's objection was actually about.
2. **Count.** The rejected draft was one 768 px slab — wall-to-wall. Three cards
   with 12 px gutters put plate *between* the figures, which is why the 3×3 grid
   reads at 84.4% without the same complaint.

---

## 4. Proposal A — "row 2" (recommended)

Draw the bottom band as **the standard grid's row 2**, on the layout's own
columns, filled by content rather than by fixed bins.

### 4.1 Geometry

Rail closed — cells on the standard grid columns, `y = 340`, `h = 100`
(y 340–439), 40 px plate reserve below:

| k (cards) | widths | x positions | rationale |
|---|---|---|---|
| 3 | 248, 248, 248 | 16, 276, 536 | exactly the grid's row-2 cells |
| 2 | 378, 378 | 16, 406 | band split in half (12 px gut); no void |
| 1 | 248 | **276** | centred — and x 276 *is* grid column 1 |
| 0 | — | band hidden | |

Rail open — mirrors the rail-open FEATURE frame exactly (hero 156–585, strip
596–787), `y = 340`, `h = 100`, gutter 10:

| k | widths | x positions | check |
|---|---|---|---|
| 3 | 210, 210, 192 | 156, 376, 596 | 156+210+10+210 = **586** = hero right edge +1; 596+192 = **788** = strip right edge +1 |
| 2 | 311, 311 | 156, 477 | 156…787 filled |
| 1 | 210 | 367 | centred in 156…787 |

The k=1 → x 276 centring is not invented: `ui_board.cpp:1478–1487` already argues
this exact principle for the vertical axis — *"Top-aligning it leaves the whole
lower half as one void, which reads as content that failed to arrive; splitting
the slack above and below reads as a composition."* Proposal A applies the
codebase's own rule to the horizontal axis.

*Judgement call, flagged:* widening to 378 at k=2 rather than centring two 248s
at x 146/406. Widening fills the band completely; centring keeps the grid. I
prefer widening because five data items in 378 px still read as generous, but if
it tests loose, the fallback (2 × 248 at 146/406) is a two-constant change.

### 4.2 Card anatomy — 248 × 100, FINAL variant

`glassPanel(parent, x, 340, w, 100, R_LG)`; fill `kStateInk[GS_FINAL].plate`
`#101825`; 1 px `C_LINE @ OPA_EDGE` border and the specular pair, both inherited
from `s_glass`. **No shadow** (`LV_SHADOW_CACHE_SIZE` is 0 and the theme defines
none — do not introduce one here). No gradient.

```
pad x 14, pad y 9
row A (away)  y  9 – 41   badge 24×24 @ x14,y+3 · abbr F_ABBR @ x44 · score F_SCORE38 right-aligned, w 62 @ x=w-76
row B (home)  y 42 – 74   same
status        y 79 – 90   F_MICRO @ x14, kStateInk[2].ink3, "Final" | "F/OT"
bottom pad    10
```

Ink: winner = `kStateInk[GS_FINAL].ink` `#B6C4D2` (10.02:1), loser =
`.ink3` `#8696A8` (5.89:1), tie = `.ink2` `#95A5B7` both. This is the fix for
§2.2 — the finals stop being rendered at `C_INK` 18.72:1 and rejoin the state
ladder.

PRE variant for leftover cells (identical object set, `kStateInk[GS_PRE]`
`#16202E`): scores hidden, status line carries `"7:00 PM"` left and the broadcast
right. This is precisely what a grid tile already does for a scheduled game.

### 4.3 Fill rule — why the void cannot come back

```
candidates = games passing the filter, not on the hero and not on the tile strip
finals   = candidates where state == GS_FINAL, sorted by startUtc DESCENDING   # §2.1 fix
upcoming = candidates where state == GS_PRE,   sorted by startUtc ASCENDING
row = (finals ++ upcoming)[0:3]          # finals get first claim — the owner's ask
k   = len(row);  if k == 0: hide the band
place row left-to-right in the k-column geometry above
```

One stream, not two bins. The lower-left cannot be empty while the lower-right
is full, because there is no lower-left bin to be empty. On the owner's actual
frame (1 final, 0 upcoming) the result is a single result card centred at x 276,
and the band's coverage goes from 1.23% ink to a figure on a ground.

### 4.4 Wireframes at true 800 × 480 proportions

1 column = 8 px, 1 row = 16 px.

**NOW — `--scenario 3` / `docs/img/panel-feature.png` (1 final, 0 upcoming):**

```
   x=0        16                                    523 536         783 800
    +-----------------------------------------------------------------------+
 0  | (~) LIVE 2/3 | < ALL LEAGUES · 2 LIVE >        | TABLE  NEWS  SETUP    |  bar y0-47
 3  +-----------------------------------------------------------------------+
 4  | |+---------------------------------------------+ +-------------------+ |
 5  | ||  NCAAM                          2nd 00:04.7 | | (o) GONZ    12-4-2 | |
 7  | ||  (o) UCONN   12-4-2                     118 | | (o) SDSU    21-6-4 | |
10  | ||  (o) TENN    21-6-4                     109 | | · OT2 03:11  CBSSN | |
11  | ||                                       ESPN2 | +-------------------+ |  y60-187
12  | ||                    HERO 508x268             |                       |
14  | ||                    x16..523  y60..327       |   ############### <-- tile slot 2
17  | ||                                             |   ## 248x128 EMPTY ## |
20  | |+---------------------------------------------+   ############### 31,744 px
21  |                                                                        |
22  |  ##################################  FINAL                             |  <- header 42x9
23  |  ##  UPCOMING COLUMN  372x140  ##  ------------------------------------|  <- rule 372x1
24  |  ##       0 ink px  100% EMPTY ##  JAX  3   WSH  0          Final/OT    |  <- 371x14
25  |  ##      52,080 px = 48.4%     ##  ####################################|
26  |  ##      of the band           ##  ##  FINAL rows 2-3  372x78        ##|
27  |  ##                            ##  ##  0 ink   29,016 px = 27.0%     ##|
29  |  ##################################  ####################################
    +-----------------------------------------------------------------------+
       band ink 1,323 px of 107,520 = 1.23%   ·   void 81,096 px = 75.4%
       band columns break at x 387/412 — 136 px and 124 px off the seams above
```

**PROPOSAL A, same data (k = 1):**

```
   x=0        16                                    523 536         783 800
    +-----------------------------------------------------------------------+
 0  | (~) LIVE 2/3 | < ALL LEAGUES · 2 LIVE >        | TABLE  NEWS  SETUP    |
 3  +-----------------------------------------------------------------------+
 4  | |+---------------------------------------------+ +-------------------+ |
 5  | ||  NCAAM                          2nd 00:04.7 | | (o) GONZ    12-4-2 | |
 7  | ||  (o) UCONN   12-4-2                     118 | | (o) SDSU    21-6-4 | |
10  | ||  (o) TENN    21-6-4                     109 | | · OT2 03:11  CBSSN | |
11  | ||                                       ESPN2 | +-------------------+ |
12  | ||               HERO  unchanged               |                       |
14  | ||               x16..523  y60..327            |    (strip slot 2      |
17  | ||                                             |     stays empty —     |
20  | |+---------------------------------------------+     live-only)        |
21  |                                    +---------------+                   |
22  |         plate                      | (o) JAX     3 |      plate        |  card x276..523
23  |                                    | (o) WSH     0 |                   |  y340..439
25  |                                    | Final/OT      |                   |  248 x 100
27  |                                    +---------------+                   |
28  |            <-- 40 px plate reserve along the bottom edge -->            |
29  |                                                                        |
    +-----------------------------------------------------------------------+
       x276 = grid column 1.  score 27 px ink (7.0' at 2.5 m, was 2.9').
       coverage 68.4% (was 53.7%).   band ink: a figure, not 1.23%.
```

**PROPOSAL A, k = 3 (`--scenario 10`: 3 finals, or 2 finals + 1 upcoming):**

```
   x=0        16          263 276         523 536         783 800
    +-----------------------------------------------------------------------+
 0  | (~) LIVE 3/9 | < ALL LEAGUES · 3 LIVE >        | TABLE  NEWS  SETUP    |
 3  +-----------------------------------------------------------------------+
 4  | |+---------------------------------------------+ +-------------------+ |
 5  | ||  NHL                                3rd 04:21| | (o) BUF      14   | |
 8  | ||  (o) CANADIENS  12-4-2                    2 | | (o) KC       21   | |
11  | ||  (o) MAPLE LEAFS 21-6-4                   3 | | · Q2 11:03  RED ZN | |
12  | ||  · POWER PLAY                    Sportsnet  | +-------------------+ |
13  | ||    Matthews (24) PP, from Marner            | +-------------------+ |
15  | ||                                             | | (o) EDM       1   | |
17  | ||                                             | | (o) CGY       0   | |
20  | |+---------------------------------------------+ | · 1st 18:44    SN | |
21  | +---------------+ +---------------+ +---------------+                  |
22  | | (o) VAN     4 | | (o) NYY     6 | | (o) DAL    17 |                  |  y340..439
24  | | (o) SEA     5 | | (o) BOS     2 | | (o) PHI    27 |                  |  h=100
26  | | Final/OT      | | Final         | | Final         |                  |
27  | +---------------+ +---------------+ +---------------+                  |
28  |          <-- 40 px plate reserve, the composition's floor -->           |
29  |                                                                        |
    +-----------------------------------------------------------------------+
       x 16/276/536, w 248 — the same columns as the hero (0+1) and strip (2).
       coverage 81.4%, below the 3x3 grid's 84.4% and the rejected 86.8%.
```

Note what the k=3 wireframe is: **it is the bottom row of `--scenario 1`, the
3×3 grid.** The owner has already seen this working — it is the row that reads
`VAN 4 / SEA 5 / Final/OT`, `NYY 6 / BOS 2 / Final`, `DAL 17 / PHI 27 / Final` in
the standard grid render. This proposal invents no new object; it stops the
FEATURE layout from being the one place those cards are withheld.

---

## 5. Proposal B — "one settled card, two panes" (the alternative)

A single `glassPanel` spanning the band on the grid: **x 16, y 340, w 768,
h 100**, `R_LG`, fill `kStateInk[GS_FINAL].plate`. Inside, a 1 px `C_LINE @
OPA_HAIR` vertical seam at **x 524** — continuing the hero/strip seam down
through the band, which is the alignment fix stated as a line. Left pane
(x 16–523, 508 wide) = FINAL results, 3 rows at 30 px pitch. Right pane
(x 536–783, 248 wide) = UPCOMING, 3 compact rows.

```
   x=0        16                                   523|536         783 800
21  | +----------------------------------------------+---------------------+ |
22  | | FINAL                                        |  UPCOMING           | |
23  | | (o) VAN  4   SEA  5              Final/OT    |  7:00 PM  BOS @ NYR | |  y340
25  | | (o) NYY  6   BOS  2              Final       |  8:10 PM  LAD @ SF  | |  h=100
26  | | (o) DAL 17   PHI 27              Final       |  9:30 PM  VGK @ COL | |
27  | +----------------------------------------------+---------------------+ |
28  |            <-- 40 px plate reserve -->                                 |
    +-----------------------------------------------------------------------+
    when one pane is empty the other's rows span the full 768 — the RECTANGLE
    never changes, so there is never a hole, only a card with content on one side
```

Its one real advantage: because the card rectangle is constant, the empty-column
case degrades to "a card whose content is on one side", never to a void, and
never to the empty-bordered-box "failed to load" signal that `ui_ledger.cpp:179`
warns about. Coverage **82.0%**.

Why I do not pick it:

* It keeps the fixed 3-row-per-bin capacity, so §2.1 (oldest results) and the
  emptiness asymmetry survive in content terms even though they stop showing
  geometrically.
* A 508 px row cannot afford `font_score38`; the scores stay at 11 px, so §2.2
  is unfixed — the finals remain the smallest thing on the panel and still fail
  at 2.5 m.
* Per-row tap targets need the `ui_idle.cpp` invisible-hit-target trick rebuilt
  here; in A the card *is* the target, with the theme's existing 2 px `C_LIVE`
  pressed outline.
* It is one slab, which is closer in kind to the 768×124 draft the file already
  rejected on figure/ground grounds.

**Pick: Proposal A.**

---

## 6. Cost of Proposal A, honestly

### 6.1 Objects and memory

| | objects |
|---|---|
| delete `ui_ledger.cpp`'s tree (1 root + 2 hdr + 2 rule + 18 cells) | **−23** |
| 3 result cards × (glassPanel 3 + 2 badges×2 + 2 abbr + 2 score + status + bcast = 13) | **+39** |
| **net** | **+16 ≈ 3.2 KB** |

`lv_conf.h:28–32` puts all of this in **PSRAM via `ps_malloc`**, not the tight
internal heap. 3.2 KB of 8 MB. Object count is not the binding constraint on this
change and I will not pretend it is.

Cheaper variant if you prefer zero new code: raise FEATURE's `per` from 2 to 5 and
let `spec()` place slots 2–4 on row 2, reusing `buildTile` wholesale (+63 objects,
~12.6 KB, and every change-cache already written). **But** `DensitySpec` carries
one `tileH`, so those cards come out 128 px tall and coverage lands on **86.8%** —
the rejected number. Take this variant only with a second `tileH` field, or not
at all.

### 6.2 Invalidation

| event | today | Proposal A |
|---|---|---|
| poll, nothing changed | **~37,344 px, ~22 regions, 18 reallocs** | **0 px, 0 regions** |
| one final's score/state changes | ~37,344 px | 248×100 = **24,800 px**, ~6 regions |
| k changes (row re-centres) | n/a | ≤ 768×100 = **76,800 px**, ~18 regions, one tick |

The 76,800 px re-centre exceeds the ~50,000 px/tick sustained budget for a single
tick (~77 ms). That is acceptable — it is a discrete event a handful of times an
evening, not sustained motion — but it must not land in the same tick as the
hero's staged reveal (`ui_hero.cpp` opacity 85→170→COVER over 2 × 80 ms). Sequence
the row's geometry write **before** `uiHeroShow()`, or fold it into the
`uiInit()` rebuild path that already runs on layout/rail flips.

Region-count headroom improves either way: the ledger's ~22 uncached slots per
poll disappear, moving the screen further from the `LV_INV_BUF_SIZE` 64 cliff
where `lv_refr.c:255–258` silently promotes the frame to a full-screen ~230 ms
repaint.

### 6.3 Mandatory change-caching

Every one of these is on the per-poll path. `lv_obj_add_flag`/`clear_flag` and
`lv_obj_set_local_style_prop` (`lv_obj_style.c:270–276`) both invalidate
unconditionally; `lv_label_set_text` invalidates before it looks at the string
(`lv_label.c:90`).

| write | gate |
|---|---|
| card visibility | `setHiddenCached(c.root, &c.cUsed, hide)` |
| card x / width (k changed, or rail flipped) | cache `c.cX`, `c.cW`; write only on change |
| card fill + all six ink colours | one gate on `c.cState != g.state`, exactly `ui_board.cpp:1155` |
| 5–6 text labels per card | `setTextCached(o, cache, cap, v)` |
| badge fill / logo swap | `c.cChip[k]` packed sentinel, as the tile does |
| score labels hidden in the PRE variant | `setHiddenCached` |
| tapped game index | `c.gameIdx`, as `TileUI` does |

`setTextCached` / `setNumCached` / `setHiddenCached` are currently `static` in
`ui_board.cpp` (lines 265–300). Promote them to `ui.h` rather than copying them —
the ledger's whole defect class is that they were not reachable from here.

### 6.4 Rail-open survival

Two width tables, exactly the shape `uiLedgerLayout()` already uses. No runtime
resize is needed: `uiBoardRefresh()` calls `uiInit()` whenever
`s_builtRail != uiRailOpen()` (`ui_board.cpp:1102`), so the cards are rebuilt at
the correct width on the flip. The 210/210/192 split is derived from constants
that already exist (`HERO_W` 430 = 210+10+210; strip `tileW` 192), so it cannot
drift from the frame above it.

Verified against pixels: rail-open FEATURE currently runs hero 156–585, strip
596–787, ledger 156–455 / 488–787. The proposed row lands on 156–365 / 376–585 /
596–787 — the first two exactly halve the hero, the third exactly matches the
strip.

### 6.5 Density

Row 2 exists only in `LAY_FEATURE`. `ROOMY`/`STANDARD`/`DENSE` already draw
finals as tiles, and `uiBoardRefresh()`'s non-feature branch already calls
`uiLedgerHide()` — that call becomes `uiResultsHide()`. Nothing else changes.

### 6.6 Empty and partially-full

| board | k | result |
|---|---|---|
| 3+ finals | 3 | three 248 cards on the grid |
| 2 finals | 2 | two 378 cards, band full |
| 1 final (the owner's frame) | 1 | one 248 card centred at x 276 |
| 1 final + 2 upcoming | 3 | finals first, then PRE-variant cards |
| 0 settled, 0 scheduled (`--scenario 7`) | 0 | band hidden — see below |

For k=0 the panel is one live game and nothing else. The current answer leaves a
508×268 card in the top-left with ~216,000 px of void around it. Cheapest honest
fix, and I recommend it as a separate one-liner: when the row is hidden **and**
the tile strip is empty, centre the hero at **x = 146, y = 130**
(`(800−508)/2`, `48 + (432−268)/2`). Two cached position writes on one object,
zero new objects, no reflow of the hero's internals.

---

## 7. Ordered recommendation

| # | change | value | cost |
|---|---|---|---|
| 1 | Select finals by `startUtc` DESC instead of taking the first three in `s_order` | stops showing the night's *oldest* results | ~15 lines, no wire change |
| 2 | Replace the two-bin ledger with the content-driven row-2 card set (§4) | kills the 75.4% void structurally; gives finals the card treatment; makes the score glanceable at 2.5 m | +16 objects, one file replaced |
| 3 | Change-cache every per-poll write in the new file (§6.3) | −37,344 px and −22 invalidation regions per poll | free, promote 3 existing helpers to `ui.h` |
| 4 | Make each result card tappable → `uiGameOpen` | closes the gap with `ui_idle.cpp:308` | 3 event callbacks, feedback already in `s_glassPressed` |
| 5 | Centre the hero when row 2 and the tile strip are both empty | fixes the 1-live composition | 2 cached position writes |

Do **not** do: three full 128 px tiles (86.8%, the rejected number), a 768 px
slab, any shadow (`LV_SHADOW_CACHE_SIZE` is 0), any `bg_grad_dir`.

---

## Appendix — reproduction

```
cd desktop
./scoredeck-ui --shot /tmp/ledger-area-s3.bmp  --scenario 3      # owner's case, 1 final 0 upcoming
./scoredeck-ui --shot /tmp/ledger-area-s10.bmp --scenario 10     # both columns full
./scoredeck-ui --shot /tmp/ledger-area-s10-rail.bmp --scenario 10 --rail
./scoredeck-ui --shot /tmp/ledger-area-s7.bmp  --scenario 7      # ledger hidden, worst void
./scoredeck-ui --shot /tmp/ledger-area-s1.bmp  --scenario 1      # 3x3 grid — row 2 is the target
```

Content mask used throughout: `max(r,g,b) > 32` (plate `#04070E` dithers to
≤ (16,16,24); card fill `#101825` and all ink clear it). Card-coverage model:
`bar 800×48 + hero 508×268 + n×(tileW×tileH)`; it reproduces the file's own
62.0% / 84.4% / 86.8% figures exactly, which is why the new numbers are
comparable to them.
