# ScoreDeck — one costed plan

*Synthesis of four area audits and the adversarial verification passes over them.
Every number below was either re-derived in this pass or is cited to the audit that
measured it. Read-only: no repo file was modified, nothing was built or flashed, the
panel was not contacted. Harness renders written this pass: `/tmp/synth-plan-s0.bmp`,
`/tmp/synth-plan-s3.bmp`.*

---

## 1. Verdict

**The palette is not incoherent because it has too many colours. It is incoherent
because the team-colour channel is drawn at two different contrast ratios, and the
higher of the two puts team colours *on top of* the accent's own lightness band.**

Measured, on the 35 real kit colours in `desktop/scenarios.cpp`, rendered through
RGB565 exactly as the panel stores them:

| channel | where | rendered L\* today |
|---|---|---|
| team colour as **structure** (edge light, lead rule, bloom, win-prob) — `teamInkOn(c, fill)` @ 3.5:1 | `ui_board.cpp:1463`, `ui_hero.cpp:417/447/467/468` | **50.5 – 83.4** (median 51.7 live) |
| team colour as **type** (the leading score) — `teamInkOn(c, fill, 6.5f)` | `ui_board.cpp:1353`, `ui_hero.cpp:405` | **69.2 – 83.4** (median 69.6 live) |
| `C_WARN` (the amber) | everywhere | **78.24** |
| `C_LIVE` (the accent) | everywhere | **81.78** |

The two signal colours sit *inside* the team channel's range on every surface. There
is no lightness moat at all. Lightness is the eye's strongest grouping cue, and the
system has surrendered it: on a single live tile, Toronto renders at L\* 51 as an
edge light and at L\* 70 as a score — two blues, one team, one tile — while the
accent it is supposed to be subordinate to sits at L\* 82, only 12 L\* above the
brighter of the two and *below* an already-bright kit like Boston's gold at L\* 79.9.

That is the mechanism behind all four things the owner named:

| owner's words | what it actually is | fixed by |
|---|---|---|
| "teal edge bar" | 92.7 % of all `C_LIVE` pixels on the board are the decorative rail sliver; only 1.8 % are the live dot | Phase 1 |
| "green live dots" | the pulse modulates `bg_opa` 150→255, so the dot travels 27 L\* and at trough renders **#299183** — a dimmer, greener teal than the rail's flat #39E3C6. They are literally different rendered colours from the same token | Phase 1 |
| "blue card accents" | the two-ratio problem above: the same team, twice, 16 L\* apart, on the same tile | Phase 2 |
| "orange base diamonds" | `C_WARN` does two incompatible jobs; the game-situation chip is 15.1 % of the board's salient chroma, so the interrupt colour has no interrupt value left | Phase 1 |

Independently re-measured on `--scenario 0` this pass: 8,502 salient-chroma pixels
(C ≥ 25, L\* ≥ 40) — teal family 3,711 (43.6 %), of which **3,268 are the 8 px rail
sliver**; amber 1,286 (15.1 %); team hues 3,149 (37.0 %). The audits' census
reproduces to the pixel.

**The fix is one ratio and one ceiling, not a new palette.** Unifying every
team-derived element at **5.5:1 with a rendered lightness ceiling of L\* 68**
produces one team colour per team, in one band, 8.4 L\* below the accent's dimmest
pulse rung — with every kit still above WCAG AA and the leading score still brighter
than the trailing score's neutral. Nothing in the existing solved-contrast system is
touched. Nothing in the team-identity channel is destroyed.

---

## 2. What will NOT change, and why

These are assets. The plan protects them explicitly.

1. **`kStateInk[4]` and the surface ladder.** Every tier is solved per surface to
   hold ≥10:1 / ≥7:1 / ≥4.5:1. Nothing in this plan raises an ink to chase a
   measurement. *(This is `glance:G3`'s constraint, promoted to a rule: any proposal
   whose fix is "brighten an ink" is rejected by default, because the tiers are
   solved per surface and a global brighten breaks the ladder on the surfaces that
   were not the problem. It is what disqualifies the reflex fix for the leader /
   trailer inversion and leaves 5.5:1 as the answer instead.)*
2. **Team colour is content, not chrome.** `teamInk()` / `teamFill()` /
   `badgeInk()` / `chipSolve()` stay. ~3,000 arbitrary hues keep arriving from the
   wire and keep being lifted to legibility. The plan *bounds* the channel; it does
   not narrow it, tint it, or replace it with the accent.
3. **One declared accent.** `C_LIVE` #3BE0C0 keeps its hue — measurement says it is
   near-optimal (5.9 % team collisions against 21.6 % for the neutral chrome family).
   It is renamed `A_LIVE`, its family is collapsed to one rung, and its pixels are
   moved from decoration to signal. It is not extended, diluted, or joined by a
   second chromatic accent.
4. **The pre-baked-and-dithered gradient technique in `ui/plate.cpp`.** It is the
   correct answer and Phase 6 applies it to the bloom rather than inventing a
   different one. `bg_grad_dir` stays banned.
5. **Radius family, spacing, glass anatomy, press-is-an-outline.** Untouched except
   for the outline's opacity (Phase 1), which is a token-hygiene fix, not a redesign.
6. **The 2 px lead rule and 3 px edge light are kept.** One audit proposed cutting
   them as sub-resolution at 2.5 m. They are, but they are part of the tile's
   identity language at the desk and they get *more* coherent under one ratio.
   Cutting them is a visual redesign the owner did not ask for. Flagged as an open
   question, not scheduled.

---

## 3. Root causes

**R1 — The team channel is drawn at two ratios, and the higher one invades the
accent's lightness band.** `teamInkOn(c, fill)` defaults to 3.5:1; the score passes
`6.5f`. The same team therefore renders 16.1 L\* apart (mean over 54 kits; max
20.4) on one tile, and at 6.5:1 the channel occupies L\* 69–83 — straddling `C_WARN`
(78.2) and `C_LIVE` (81.8). *Note: `ui_board.cpp:1342`'s own comment says the leader
"lifts its team colour to **5.5:1**". The code at `:1353` passes `6.5f`. The comment
is the correct design; the constant drifted.*

**R2 — The accent's pixel budget is inverted.** 92.7 % of `C_LIVE` on the board is
the rail sliver — pure decoration. 1.8 % is the live dot. On the idle screen, 94.4 %
of `C_LIVE` is a countdown to a game that has **not started**, i.e. the exact
opposite of the token's declared meaning. A colour whose pixels mostly mean nothing
cannot teach the eye what it means.

**R3 — Chromatic tokens are drawn at partial opacity, so the rendered colour is
never the declared one.** `lv_color_mix` blends toward the surface, losing lightness
*and* chroma, and it quantises the opacity to `(opa + 4) >> 3` before any channel
arithmetic — 26 levels, not 256. Four shipped sites do this: the pulse dot
(`bg_opa` 150→255), the press outline (`border_opa` 150), the hero footer
(`text_opa` 180), the heartbeat bar (`bg_opa` 90). All four render outside every
cell of any grammar written over token values. This is the same RGB565
step-quantisation failure the codebase already bans gradients over, applied to the
one thing on the panel that moves.

**R4 — `C_WARN` carries two incompatible jobs at two incompatible frequencies.**
"Something is at stake in this game" (constant presence: 15.1 % of the board's
salient chroma, in two of three live tiles) and "the system has failed" (rare).
Constant presence destroys interrupt value.

**R5 — Two roots are cleared of `LV_OBJ_FLAG_HIDDEN` unconditionally on every poll,
repainting 64.6 % of the FEATURE screen for zero visual change.** `lv_obj_clear_flag`
invalidates unconditionally after clearing (`lv_obj.c:281`). `ui_ledger.cpp:188`
does this to an 800×140 root (112,000 px); `ui_hero.cpp:502` does it to a 508×268
root (136,144 px). Together **248,144 px per poll**, ≈149 ms of flush against a
~230 ms full-screen figure — before any proposal in these audits adds a pixel. The
irony is exact: `ui_hero.cpp:330` defines a change-cached `setVis()` helper and uses
it on nine children, then writes the root's own flag raw. *Verified independently
this pass; no audit found it.*

---

## 4. The token system

Names change where the meaning changed. Hexes are unchanged unless marked.

### 4.1 Signal — "the system is talking"

| token | hex | 565 | L\* / C | meaning |
|---|---|---|---|---|
| `A_LIVE` | `#3BE0C0` | `#39E3C6` | 81.78 / 48.8 | happening now, or touch this. *(was `C_LIVE`)* |
| `A_LIVE_P0..P3` | see below | — | 76.42 → 80.36 | the four dimmer pulse rungs — **solved solids**, not opacities |
| `S_ALERT` | `#F2B441` | `#F7B642` | 78.24 / 66.5 | the system is not okay: no Wi-Fi, no proxy, stale, league cap. *(was `C_WARN`; hex unchanged)* |

Pulse rungs, solved on the `A_LIVE` ray at distinct RGB565 lattice points:

| rung | 565 | L\* | ΔL\* | live | hero | plate |
|---|---|---|---|---|---|---|
| `A_LIVE_P0` | `#31D3B5` | 76.42 | — | 8.32 | 7.39 | 10.89 |
| `A_LIVE_P1` | `#31D7B5` | 77.63 | 1.21 | 8.62 | 7.66 | 11.28 |
| `A_LIVE_P2` | `#39DBBD` | 79.16 | 1.53 | 9.02 | 8.01 | 11.80 |
| `A_LIVE_P3` | `#39DFBD` | 80.36 | 1.20 | 9.33 | 8.29 | 12.21 |
| `A_LIVE` | `#39E3C6` | 81.78 | 1.42 | 9.72 | 8.63 | 12.71 |

Five rungs, roughly even (1.2–1.5 L\*), every one a distinct 565 value, every one
≥ 7.39:1 on every surface, every one inside the signal band. Amplitude 5.36 L\*
against today's 27.04 — still ~17 % Weber, plainly a breath, but it never leaves the
band twice a second.

**Deleted:** `C_LIVE_SD` #2A9E8C (L\* 58.81, C 36.0 — mid lightness, high chroma:
reads as a team colour) and `C_LIVE_TX` #30B89D (L\* 68.15, C 43.4 — sits exactly in
the moat; and it has **zero** style writes in the entire codebase, only a comment at
`ui_board.cpp:1171`). Their 10 + 0 call sites are routed by meaning, not by hue —
see Phase 1.

### 4.2 Team — "this is who is playing"

```
T_INK(colour, surface) = clampL( lift(colour, surface, 5.5f), CEIL(surface) )
CEIL(surface)          = max(68.0, L*_required(5.5f, surface))
```

Implemented as a **wrapper declared in `theme.h`**, never inside `lift()`. `lift()`
is the shared primitive behind `teamInk()`, `teamInkOn()` **and `teamFill()`**; a
ceiling inside it would silently darken every bright badge fill and then
`badgeInk()` would re-solve the label ink on top of it.

`clampL` scales the RGB triple down multiplicatively. That is **hue-preserving
(≤ 0.63° over the sample) and chroma-reducing** — Boston's gold loses ~12 % of its
chroma. Say so; it is the visible change.

Measured, 35 kits, rendered after 565:

| surface | rendered min | rendered mean | L\* band | kits clamped | below AA | trailer (`ink3`) |
|---|---|---|---|---|---|---|
| live `#1B2636` | **5.60** | 5.98 | 63.7 – 67.7 | 2 / 35 | 0 | 5.20 |
| hero `#222E40` | **5.55** | 5.85 | 67.1 – 68.8 | 6 / 35 | 0 | 4.62 |

- Every kit clears WCAG AA (4.5:1) with margin, on every surface, **after**
  quantisation — the solve is checked in 565 space, not 24-bit.
- The leading score (≥ 5.55:1) stays above the trailing score's `ink3` (5.20 live /
  4.62 hero) on both surfaces the score is team-coloured on. Emphasis points the
  right way, which is the thing `ui_board.cpp:1440-1445` was written to fix.
- The team ceiling (68) sits **8.4 L\*** below the dimmest signal rung (76.42) and
  **13.8 L\*** below `A_LIVE`. That moat is the whole system.

Why not the other candidates:

| ratio | live min | hero min | leader < trailer? | ceiling feasible? |
|---|---|---|---|---|
| 3.5 (today's structure) | 3.57 | 3.51 | **yes, 31/35** | — |
| 4.5 (colour audit's C2) | 4.61 | 4.54 | **yes, 31/35 live** | — |
| 5.0 | 5.11 | 5.05 | yes, 18/35 live | hero needs L\* 65.9 |
| **5.5** | **5.60** | **5.55** | **no** | **hero needs L\* 65.9 < 68 ✓** |
| 6.0 | 6.12 | 6.05 | no | hero needs L\* **68.87 > 68** ✗ |
| 6.5 (today's score) | 6.68 | 6.60 | no | hero needs L\* **71.84** — no moat left ✗ |

5.5 is the only value that clears the trailer on every surface *and* leaves the
ceiling above the solver's floor. Six is where the ceiling starts fighting the floor;
6.5 — today's value — is why the team channel is inside the accent's band.

The guard `CEIL = max(68, L*_required(ratio, surface))` means raising the ratio moves
the ceiling instead of silently defeating the floor. A build-time assertion over the
shipped kit table must check both: every kit clears `minRatio` after clamping, **and**
`min(signal L*) − CEIL ≥ 8.0`. Without it the failure is invisible — at ceiling 62
(the audit's original proposal) **35/35 kits fall below 5.5** (min 5.23 live, 4.66
hero) and the clamp, not the solver, is quietly deciding the colour. At ceiling 66 the
hero alone fails 20/35. Only 68 clears both surfaces.

Concrete before / after on the live fill:

| team | today @6.5 | T_INK @5.5, ceil 68 |
|---|---|---|
| TOR / VAN `#00205B` | `#7BAAFF` L\* 69.6 · 6.77 | `#639AFF` L\* 64.1 · 5.69 |
| **BOS / NSH `#FFB81C`** | `#FFBA18` L\* 79.9 — **this is `S_ALERT`** (ΔL\* 1.7, Δhue 1.5°) | `#D69A10` L\* 67.6 · 6.36 — still gold, 10.6 L\* clear |
| MTL `#AF1E2D` | `#FF8694` L\* 69.8 · 6.81 | `#FF697B` L\* 64.0 · 5.66 |
| PHI `#004C54` | `#00BED6` L\* 70.7 — 18° from `A_LIVE` at 1.39× | `#00AEC6` L\* 65.2 — 16.6 L\* below `A_LIVE` |
| teal-ish kit `#99D9D9` | `#9CDBDE` L\* 83.4 — **brighter than the accent** | `#7BAEAD` L\* 67.7 · 6.36 |

### 4.3 Ink and structure — "this is the page"

Unchanged values; one addition and five retirements.

| token | hex | 565 | L\* / C |
|---|---|---|---|
| `INK_1` | `#F3F7FB` | `#F7F7FF` | 97.44 / 4.1 |
| `INK_2` | `#ACBCCE` | `#ADBECE` | 76.17 / 10.3 |
| `INK_3` | `#8696A8` | `#8496AD` | 61.43 / 14.2 |
| `EDGE_HI` | `#3A4759` | `#39455A` | 29.07 / 14.1 |
| `EDGE` | `#2E3A4C` | `#29384A` | 22.98 / 13.0 |
| **`T_NONE`** *(new)* | `#5D6D7E` | — | — | absorbs nine placeholder-badge literals |

Retired literals: `0x334455` (`ui_hero:218`) and the eight `0x5D6D7E` sites →
`T_NONE`. `0x04070C` (`ui_alert:86`, `ui_lineup:173`) → `BG` — it and `C_PLATE`
`#04070E` both quantise to `#000408`, the **only** 565 collision among all declared
tokens plus loose literals. `0x3A4757` → `EDGE_HI`. `0x1E2836` → `EDGE`. `0x0B111B`
→ `BG`. `0x2A3646` → `EDGE`.

**`0x4A5666` at `ui_rail.cpp:180` goes to `EDGE_HI`, not `INK_3`.** It is not a
border — it is the *empty* branch of a text-colour ternary
(`total ? C_INK3 : lv_color_hex(0x4A5666)`). Routing it to `INK_3` would make both
branches the same colour and delete the has-games / no-games distinction on every
rail row, while moving 25.4 L\*. `EDGE_HI` moves it 0.30 L\* and keeps two branches.

**Two accent literals hide inside `snprintf` format strings** and are invisible to
any grep for `lv_color_hex`: `ui_rail.cpp:175` (`"#3be0c0 %u#/%u"`) and
`ui_board.cpp:1675` (`"%s · #3be0c0 %u LIVE#"`). *The audits found one of the two;
the second is new in this pass.* Both must be generated from the token, or renaming
`C_LIVE` → `A_LIVE` silently desyncs the rail count and the league pill.

---

## 5. The grammar

Three cells. **Stated over RENDERED colour, after RGB565 and after any blend** —
not over token values, because that is where every violation lives.

| cell | rule | members |
|---|---|---|
| **SIGNAL** | L\* ≥ 76 **and** chroma ≥ 30 | `A_LIVE` + 4 pulse rungs (76.4–81.8, C 47–49); `S_ALERT` (78.2, C 66.5) |
| **TEAM** | L\* ∈ [floor(5.5, surface), **68**], chroma unconstrained | ~3,000 kit colours via `T_INK()` |
| **INK / STRUCTURE** | chroma ≤ 16, any L\* | `INK_1` (C 4.1) … `EDGE` (C 13.0), `T_NONE`, surfaces, line hues |

No overlap: chroma 30 vs 16 separates the two chromatic cells from ink; L\* 76 vs 68
separates signal from team. **8.4 L\* of moat, 14 of chroma.** Every boundary is a
number a lint can assert.

Three supporting rules, each of which fixes several findings at once:

> **Rule A — No chromatic token may be drawn at partial opacity, anywhere.**
> If a dimmer rung of a chromatic token is needed, declare it as a solved solid.
> This is the discipline `plate.cpp` already establishes for anything blended in
> RGB565, applied to style writes. It fixes the pulse dot, the press outline, the
> hero footer and the heartbeat bar with one rule instead of four patches.

> **Rule B — The team band is per-surface at the bottom and fixed at the top.**
> "The L\* that clears 5.5:1 on *this* fill, capped at 68." The same team is
> 63.7–67.7 on a live tile and 67.1–68.8 on the hero. That 3.4 L\* cross-surface
> spread is real and is stated, not hidden inside a band width.

> **Rule C — Selection is a treatment, not a hue.**
> See §6.

Violations found and their disposition:

| site | rendered today | cell | fix |
|---|---|---|---|
| press outline, `theme.cpp:295-297` (`C_LIVE` @ opa 150) | L\* 49.9–55.9, C 32–34 | **TEAM** | draw solid `A_LIVE` at `LV_OPA_COVER` |
| hero footer, `ui_hero.cpp:274` (`C_LIVE` @ text_opa 180) | L\* 64.4, C 37.8 | none | → `si.ink3` (it is a *fact*, like the clock) |
| heartbeat bar, `ui_board.cpp:784/790` (`C_LIVE_SD` @ bg_opa 90) | L\* 20.4, C 17.5 | none | → `EDGE_HI` nominal / `S_ALERT` overdue |
| pulse dot trough, `pulse.cpp:46` | L\* 54.4, C 32.5, **3.79:1 on hero — below AA** | **TEAM** | solid rungs, §4.1 |

---

## 6. Selection

The colour audit proposed `A_SELECT = #F3F7FB`. **Rejected.** It is bit-identical to
`C_INK` (both quantise to 565 `0xF7BF`), and `theme.cpp:290` already makes `C_INK`
every glass panel's *inherited* text colour — so a "selected" label would be
indistinguishable from an unselected one that merely inherited the style. It also
fails this plan's own SIGNAL rule (chroma 4.1, not ≥ 30). And measurement says every
available *hue* is a worse trade: the emptiest bands against 102 real team-hue
samples are 90–135° (reads as "another teal" at 2.5 m) and 315–345° (reads as an
error state); either takes the board from two chromatic channels to three.

**Selection becomes an inverted chip:** `INK_1` fill with a `badgeInk()`-solved plate
label. It is achromatic, adds no hue family, is structurally impossible to confuse
with inherited body text, and is a stronger, more learnable form than a colour.
Unify the two sites that already gesture at this — `ui_board.cpp:1527` (`C_INK` page
dot) and `ui_rail.cpp:96` (`lv_color_white()` filter tab), which today are **two
different values** — onto `INK_1` in the same change.

`A_LIVE` keeps the press outline: a press genuinely *is* "now", and the argument in
`theme.h` for the 2 px outline is sound. Persistent selection and the instant of
touch stay distinct — one is a form, the other a colour.

---

## 7. Desk (610 mm) vs room (2.5 m): where they genuinely conflict

Both goals are real and they are not always compatible. Stated plainly:

**They do not conflict on the glow.** The 8 px terminal band of the bloom is 3.5
c/deg at the desk; the 28 px band is 4.0 c/deg at 2.5 m. Both sit on the peak of the
contrast-sensitivity function. The rings are visible at *both* distances, and no
change to sprite size or curve exponent escapes it — only sub-level accuracy does.

**They conflict on the rail.** The rail sliver is 0.14° at 610 mm and 0.035° at
2.5 m — at the acuity limit as a *structure* even today. A liveness cap on it is
desk-only either way. Resolution: the rail is a **desk instrument**; the
room-glance liveness signal is the top-bar LIVE count and the dots, and starving the
rail of accent makes both louder. Stated so it is not re-litigated.

**They conflict on tile density, and only a 2-row grid resolves it.** Modelled from
the existing tile anatomy: 4 rows → cap 20 px (5.2′ at 2.5 m); **3 rows → cap 37.5 px
(9.77′) — geometrically impossible to make glanceable at any arrangement**; 2 rows →
cap 72.5 px (18.9′, inside ISO 9241-303's preferred band). The cliff is between two
rows and three. `DEN_ROOMY` is already 3 × 2 with a 75 px row and draws a cap-34
score in it — it owns the geometry and spends half of it.
**Resolution: `effectiveDensity()`'s existing count rule is already the right rule,
reframed** — ≤ 6 games → Roomy = *the across-the-room layout*; > 6 → Standard/Dense =
*desk layouts that explicitly surrender 2.5 m legibility*. A quiet night is the night
you are across the room; a twelve-game night is a night you are at the desk.

**They conflict on team identity, and the honest answer is the badge, not the score.**
Even at 5.5:1 the lift still produces byte-identical pairs after 565 (11 excess
values among 35 kits, against 14 at today's 6.5 — a ~20 % improvement, not a
solution). KC `#E31837` and CGY `#C8102E` still land on the same value on a live
tile. This is not fixable: on a fill at L\* 13.7 any ink clearing 4.5:1 must sit at
L\* ≥ 57.5 and any clearing 6.5:1 at L\* ≥ 68.5, so *source lightness order is
destroyed by the contrast requirement itself*, not by the implementation. Measured at
2.5 m, the logo/badge delivers **10.12:1** and the team-coloured score **5.17:1**.
**The badge is the identity channel that survives the room; the score carries the
number.** That is why Phase 5 grows the Roomy badge 38 → 56 px and not only the digits.

**Two audits disagreed and are reconciled here.** One asserted the hero's staged
reveal composites through a transient layer and could leave the card dim. It does
not: `lv_obj_set_style_opa()` writes `LV_STYLE_OPA`, which
`lv_obj_get_style_opa_recursive()` multiplies into each descendant's own draw
opacity; the layer path reads `LV_STYLE_OPA_LAYERED`, which nothing sets. Proven by
solving 125,000 card pixels for a unique blend factor (mix = 10/32) and reproducing
the render 6,462/6,462 exact. **The `KNOWN ARTEFACT` note in the brief is wrong about
the failure mode — there is no dim-card leak path.** Both audits nonetheless reach
the same conclusion on *cost*: three full-card invalidations = 408,432 px = ~245 ms
of flush inside a 160 ms window, and `bg_opa < COVER` also fails
`LV_EVENT_COVER_CHECK` so the plate under all 136,144 px is re-blitted each rung.
Cut it to one rung.

---

## 8. The plan

Ordered by owner-visible impact per unit risk. Every phase is independently
shippable and independently verifiable on the harness before anything is flashed.

---

### Phase 0 — Make it measurable, and stop the bleeding
**Risk: near-zero. Cost: trivial (≈6 lines of firmware, ≈10 of harness).
Visual change: none.**

Nothing else in this plan can be honestly verified until this lands.

1. **`ui_ledger.cpp:188` and `ui_hero.cpp:502`** — gate the unconditional
   `lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN)` behind a cached bool.
   `ui_hero.cpp:330` already defines exactly the right helper (`setVis`) and uses it
   on nine children; the root was missed. **248,144 px per poll → 0 in steady
   state** on the FEATURE screen (64.6 % of the screen, ≈149 ms of flush, for zero
   visual change). *Sequencing note: the hero's `appearing` test reads the flag
   before the clear, so the gate must preserve that read.*
2. **`desktop/main.cpp` — add `--settle <ms>`** that pumps `lv_timer_handler()`
   against an advanced tick before writing the BMP. Today `--shot` runs two refresh
   iterations, which complete far inside the reveal timer's 80 ms period, so **every
   FEATURE screenshot in the repo — `docs/img/panel-feature.png`, `feature.png`,
   `feature-rail.png` — is a rung-0 frame with the card at 33 % opacity.** Nobody
   has ever seen the hero as it ships. That is why the glow banding survived a
   fit-and-finish pass: at 33 % the plate's Bayer dither leaks through and
   *accidentally* dithers the glow (86 distinct values along a clean ray, mean run
   1.05 px — no bands at all). Settled, the card is opaque, there is no noise source
   left, and the bands go flat (21 values, mean run 5.37 px).
3. **Three `make lint` checks in `desktop/`**, alongside the existing missing-glyph
   guard:
   - **salient-chroma census** — hue-family histogram over C ≥ 25, L\* ≥ 40, plus
     the accent's absolute pixel count, with a **floor** on the accent;
   - **coverage-weighted contrast** — measure the render, not the token (≈40 lines
     of PIL; the probe scripts already exist);
   - **565 collision check** over all declared tokens.

**How we prove it:** add a pixel counter to the desktop shim's flush; assert
steady-state poll invalidation on `--scenario 3` is 0 px / 0 regions. Regenerate
`docs/img/panel-feature.png` with `--settle 400` — the difference between that and
today's file *is* the proof for item 2.

---

### Phase 1 — One accent, one meaning
**Answers owner ask #1 (the loudest half). Risk: low. Cost: small — ~20 call sites,
zero flash, zero new geometry, strictly fewer pixels drawn.**

| # | change | site | measured effect |
|---|---|---|---|
| 1.1 | rail segment body `C_LIVE`/`C_EDGE_HI` → **`INK_3`** always | `ui_rail.cpp:89` | 3,268 accent px → 0. `EDGE_HI` on the plate measures **2.13:1** — below WCAG 1.4.11's 3:1 for a meaningful graphic, and the segment is being *promoted* to carrying structure. `INK_3` gives **6.81:1** and chroma 14.2 (INK cell). |
| 1.2 | liveness → an 8 × 3 `A_LIVE` cap at the segment's **bottom** edge | `ui_rail.cpp:89` | ~72 px total. **Bottom, not top**: the white filter tab is drawn at `(0, y)` size 3 × min(h,12) (`ui_rail.cpp:93-99`), so a top cap would collide with it on the one league that is both live and filtered — the common case — and the two indicators would destroy each other. |
| 1.3 | idle countdown `C_LIVE` → **`kStateInk[GS_PRE].ink`** `#D2DEEA` | `ui_idle.cpp:242` | 3,810 px of accent that means "not started" → 0. **Not `INK_1`**: `ui_idle.cpp:163` already draws the clock in `C_INK` at 96 px on the same screen, and two large numerics in identical ink separated only by size is a new problem. `#D2DEEA` is 11.62:1, L\* 88.59, 8.9 L\* below the clock, and its declared meaning is literally "scheduled". |
| 1.4 | pulse: `bg_opa` 150..255 → **5 solid `bg_color` rungs** (§4.1) | `pulse.cpp:46` | trough L\* 54.4 → 76.4; hero trough 3.79:1 (below AA) → 7.39:1. Same one style write per tick, same 324 px/frame, no blend, no quantiser artefact. **Not opa 225**: `(opa+4)>>3` gives only 5 unevenly-spaced rendered levels there (1.45 / 2.42 / 1.40 / 2.72 L\*), so the dot would hold a value for 3–4 ticks then jump — reintroducing the exact banding this codebase bans gradients over, in the one thing that moves. |
| 1.5 | live dot 6 px → **10 px** | `ui_board.cpp` | this is the compensation for 1.1–1.3, and it is the room-glance signal: 4.96:1 delivered at 2.5 m is the state channel's whole margin. |
| 1.6 | situation chip ink → **`si.ink3`**; `S_ALERT` only when `isTense()` | `ui_board.cpp:555` | **`si.ink3`, not `si.ink2`**: `ui_board.cpp:1174` already sets the game clock to `si.ink2`, both are `F_NUM`, both on the same row 8 px apart — `POWER PLAY` would render in identical colour, face and size to `3rd 04:21`. `ink3` (5.20:1 live) is the tier of the broadcast whose slot the chip literally shares. |
| 1.7 | `C_WARN` → **`S_ALERT`**, meaning only "the system is not okay" | rename | hex unchanged. |
| 1.8 | delete `C_LIVE_TX` (zero style writes); delete `C_LIVE_SD`, route its 10 sites by meaning | `theme.h` | heartbeat (`ui_board:784/790/1706`, `ui_idle:200/206`) → `EDGE_HI` nominal / `S_ALERT` overdue — a healthy heartbeat is not "live", it is "nothing wrong". LIVE caps label (`ui_board:695/1639`) → `A_LIVE`. Settings pills/meter (`ui_settings:468/532/538`) → the selection treatment (§6). |
| 1.9 | hero footer situation line → **`si.ink3`** (was `C_LIVE` @ text_opa 180) | `ui_hero.cpp:274` | removes a blended chromatic (Rule A) and matches 1.6. |
| 1.10 | press outline → solid `A_LIVE` at `LV_OPA_COVER` | `theme.cpp:295-297` | removes the last blended chromatic. |
| 1.11 | **both** `"#3be0c0"` recolour literals generated from the token | `ui_rail.cpp:175`, `ui_board.cpp:1675` | otherwise the rename silently desyncs two live counts. |

**Honest note on 1.6.** "Split by frequency" is *not* what the gate does.
`ui_focus.cpp:33-46` returns true for `SM_CLOCK` when the power-play bit is set or
`sitRedZone()` — which is exactly when the chip's text *is* `POWER PLAY` or
`RED ZONE`. So for hockey and football the chip stays `S_ALERT` whenever it says
anything at all; **only baseball's diamonds and outs are demoted**. That happens to
be precisely the "orange base diamonds" the owner named, so the direction is right —
but the rationale must be stated correctly or the next reader will expect an amber
reduction on NHL tiles that never arrives. Also: `isTense()` is `static` in
`ui_focus.cpp` and must be de-staticked and declared in a header. That is the real
cost of this line.

**How we prove it:** re-run the salient-chroma census on `--scenario 0`. Today:
8,502 salient px — teal 3,711 (43.6 %, of which 3,268 is the rail), amber 1,286
(15.1 %), team 3,149 (37.0 %). Target after Phase 1: accent ≈ 1,150 px (≈72 rail caps
+ 192 top bar + ~900 dots at 10 px) with **≥ 70 % of it on live dots**, amber reduced
to power-play / red-zone tiles only, and the board down from 8 salient hue families
to 6 — both removed families being chrome. Assert the accent floor in `make lint`.

---

### Phase 2 — One team colour per team
**Answers owner ask #1 (the other half) and owner ask "blue card accents" directly.
Risk: low-medium (it changes every team colour on the panel, but every change is
measured). Cost: trivial in code — one wrapper plus 12 call sites — medium in
verification.**

1. **Declare `T_INK(colour, surface)` in `theme.h`** per §4.2, as a wrapper. Do not
   touch `lift()`, `teamFill()` or `badgeInk()`.
2. **Route all 12 team-colour sites through it**, with their *real* surface:

   | site | today | note |
   |---|---|---|
   | `ui_board.cpp:1353` | `teamInkOn(c, si.fill, 6.5f)` | leading score |
   | `ui_board.cpp:1463` | `teamInkOn(c, si.fill)` → 3.5 | edge light + lead rule |
   | `ui_hero.cpp:405` | `6.5f` | hero leading score |
   | `ui_hero.cpp:417` | default 3.5 | bloom recolour |
   | `ui_hero.cpp:447` | default 3.5 | hero edge |
   | `ui_hero.cpp:467/468` | default 3.5 | win-prob halves |
   | **`ui_game.cpp:202`** | default 3.5 | **the audits missed this one.** The game sheet you open *from* a tile is a third rendering of the same team; leaving it out preserves the exact defect this phase exists to remove |
   | **`ui_alert.cpp:205`** | **raw wire colour, no lift at all** | a 200×6 px bar at `LV_OPA_COVER`. Measured over 34 kits against the alert card's `C_SURF_2`: **24/34 fall below 3.0:1**; TOR/VAN `#00205B` = **1.01:1**, NYY 1.03, DAL 1.07, PIT 1.34. On the product's flagship moment — a goal alert for the owner's own team — the bar is invisible |
   | `ui_alert.cpp:163/203`, `ui_idle.cpp:443`, `ui_settings.cpp:994` | `teamInk()` | all hard-code the live-tile fill `0x1B2636` regardless of the surface they actually draw on |
3. **Build-time assertion** over the shipped kit table: every kit clears `minRatio`
   *after* clamping, in 565; and `min(signal L*) − CEIL ≥ 8.0`.
4. **Win-probability join** — `ui_hero.cpp:467-468`. Both halves land at the same
   lightness by construction, so the division between the two teams' shares is
   carried by hue alone (measured MTL/TOR: ΔL\* 1.61, contrast between halves
   1.06:1). **Phase 2 makes this worse, not neutral** — under one ratio the halves
   land 0.15 L\* apart. Fix with a **2 px `INK_1` tick at the boundary** (13.10:1 on
   the hero fill). Both originally-proposed remedies fail on measurement: a
   `BG`-coloured 1 px separator is 1.47:1 against the hero fill (not a luminance cue
   at either distance), and running the trailing half at 60 % opacity renders
   2.45:1 — below WCAG 1.4.11's 3:1 non-text floor, and it violates Rule A.
5. **Selection treatment** per §6; unify `ui_rail.cpp:96` and `ui_board.cpp:1527`.
6. **Literal cleanup** per §4.3 — 16 mechanical edits.

**How we prove it:** three measurements, all scripted this pass.
(a) Per-tile *same-team* ΔL\*: today mean 16.13 / max 20.36 across 54 kits → must be
**0** (one ratio, one colour). (b) Band assertion: every rendered team pixel in
`--scenario 0` and `--scenario 10` falls in L\* [floor, 68]; every accent pixel in
L\* ≥ 76. (c) Byte-identical-pair count after 565: 14 excess values → 11. Report
(c) honestly — it improves, it does not go to zero, and §7 explains why that is not
fixable and why the badge is the answer.

---

### Phase 3 — Take the rings out of the glow, cheaply
**Answers owner ask #2 (interim). Risk: very low. Cost: trivial — 4 lines in
`bloomInit`, boot-time only, 0 flash, 0 PSRAM, 0 runtime, 0 change to invalidation.**

The glow is a runtime gradient with **no dithering**, and `lv_color_mix()` discards
3 bits of alpha before any channel arithmetic (`mix = (opa+4)>>3`, 26 levels not 256
— `LV_COLOR_MIX_ROUND_OFS` is unset in `firmware/lv_conf.h`, so
`lv_conf_internal.h:99` defaults it to 0 and selects the packed 5/6/5 fast path).
Result: **21–26 flat annuli**, 2 px wide at the centre growing to 28 px at the rim,
separated by 1.8–2.2 L\* steps = **10–15 % Weber**, against a contour-detection
threshold of ~0.5–1 % at 3–5 c/deg. The bloom is 12 files away from the correct fix
(`plate.cpp`'s Bayer + grain), and never used it.

1. **4 × 4 Bayer dither on the runtime alpha buffer.** Amplitude is exactly one
   `lv_color_mix` step = 8 units of `opa_tmp` = 10.24 alpha units at `img_opa` 200.
   Measured (TOR): mean run 5.37 px → **2.56 px**, max run 27 → ~12.
   **Be honest about what this does not do**: mean 4×4-cell error stays flat at
   1.49 L\* at every amplitude. Alpha is one variable shared by three channels with
   different deltas (TOR is R+2 G+19 B+23) and the output is a floor, so for TOR's
   blue only 18 of the 25 steps change the value at all and the dither's energy is
   thrown away on the other 7. **This halves the ring visibility; it does not remove
   the rim.** Phase 6 removes it.
2. **Cut the staged reveal to one rung** (or drop it). One rung is 82 ms of flush in
   an 80 ms window — marginal but honest — and it halves the window in which the
   glow is at 9 levels instead of 26. Keep the two things it gets right: it fires
   only when the root was hidden (`ui_hero.cpp:338`), so the per-poll
   `uiBoardRefresh → uiHeroShow` path does not re-trigger it, and it is mutually
   exclusive with the odometer by construction. *Note: the dither amplitude is
   derived from `img_opa`, so the reveal detunes it 3× while it runs — another
   reason to cut it.*
3. **Fix three wrong comments.** `bloom.cpp:51-65` computes the blue delta as
   `(255 - 66) * alpha / 255`, using 255 as the glow's blue — that is the *white*
   sprite, but `lv_img_set_recolor` replaces it with the team colour before the
   blend, so for MTL `#FA2A3F` the blue delta is −1 of 31 and the whole argument is
   void; the signal is entirely in red for roughly half the league. The same comment
   reasons in 8-bit alpha throughout and never mentions `(opa+4)>>3`.
   `ui_hero.cpp:234-241` documents a *measured* sprite centre that is 1 px off in
   both axes, because `lv_obj_set_pos` is relative to the parent's **content** area
   and `glassPanel` carries a 1 px border (proven: the bit-exact model matched
   94.32 % of glow pixels at the documented origin and **100.00 %** at +1,+1). A
   comment whose entire point is "this was measured, not reasoned" must not carry a
   known-wrong measurement.

**How we prove it:** `--shot --settle 400 --scenario 10`, then measure run-length
along a clean radial ray and per-4×4-cell L\* error against the continuous ideal.
Accept at mean run ≤ 2.6 px. The comparison images (`/tmp/heroglow-bloom-compare3.png`)
already exist. **This is only checkable at all once Phase 0's `--settle` lands.**

---

### Phase 4 — The bottom row becomes results
**Answers owner ask #3. Risk: medium (it replaces a file). Cost: medium.**

Two facts frame it. First, **the FEATURE layout already *is* the standard 3×3 grid** —
hero = cells (0,0)–(1,1), tile strip = column 2 — and the ledger is row 2 of that
same grid drawn on a foreign 2 × 372 column grid. Measured seam offsets: UPCOMING's
right edge is 136 px off the hero's; FINAL's left edge is 124 px off the tile strip's.
Only the outer margins are shared. Second, **the band is 75.4 % void** on the owner's
own frame: 107,520 px carrying 1,323 ink px (1.23 %), with the entire UPCOMING column
(48.4 % of the band) at **exactly zero ink**.

```
TODAY (feature, rail closed)                    PROPOSED
┌────────────────────────────────────────┐      ┌────────────────────────────────────────┐
│ top bar                                │      │ top bar                                │
├───────────────────────────┬────────────┤      ├───────────────────────────┬────────────┤
│                           │  tile      │      │                           │  tile      │
│   HERO   x16 w508 y60     │  x536      │      │   HERO   x16 w508 y60     │  x536      │
│          h268             │  w248      │      │          h268             │  w248      │
│                           ├────────────┤      │                           ├────────────┤
│                           │  tile      │      │                           │  tile      │
│                           │  (empty)   │      │                           │  (empty)   │
├─────────────┬─────────────┴────────────┤      ├──────────┬──────────┬──────────────────┤
│ UPCOMING    │ FINAL      x412 w372     │      │ result   │ result   │ result           │
│ x16 w372    │ ─────────────────────    │ y340 │ x16 w248 │ x276     │ x536   h100      │ y340
│ (0 ink,     │ VAN 4 | SEA 5 | Final    │      │ badge/ab/│ w248     │ w248             │
│  48% of     │ (rows 2-3: 0 ink)        │      │ score ×2 │          │                  │
│  the band)  │                          │      ├──────────┴──────────┴──────────────────┤
└─────────────┴──────────────────────────┘      │ 40 px of visible plate — the "ground"  │
   ✗ misaligns with everything above it         └────────────────────────────────────────┘
   ✗ 75.4 % void                                   ✓ grid row 2: x 16 / 276 / 536, w 248
   ✗ 1.87× louder than kStateInk allows              (rail open: x 156 / 376 / 596)
                                                   ✓ k = min(count,3), re-centred at k<3
```

Result card anatomy, 248 × 100:

```
┌ x=14 ────────────────────────────────── w−14 ┐
│  ▣ 24×24  VAN                        4       │  row A, y 9–41   winner: kStateInk[FINAL].ink
│  ▣ 24×24  SEA                        5       │  row B, y 42–74  loser:  .ink3   tie: .ink2 both
│  Final/OT                                    │  y 78–94, F_NUM
└──────────────────────────────────────────────┘
   badge x14 · abbr F_ABBR x44 · score F_SCORE38 right-aligned w62 at x=w−76
```

| # | change | why |
|---|---|---|
| 4.1 | **Finals selected by `startUtc` DESC** | `proxy/src/espn.ts`'s `sortAndCap()` ends `return a.t - b.t` — start time *ascending* — and `ui_ledger.cpp:153` takes the first three finals it meets. On any night with >3 finals the column locks onto the games that started **earliest**; a game that just ended is 4th+ and is **never shown**. `Game::startUtc` is already parsed and already used by `ui_idle.cpp`. **Ship this even if nothing else in Phase 4 ships** — the card treatment would otherwise promote stale results into larger, brighter type. ~15 lines, a 3-slot max-scan, no sort. |
| 4.2 | **Draw the band as grid row 2** | y=340, h=100. Rail closed: x 16/276/536 w 248 (k=3), x 16/406 w 378 (k=2), x 276 centred (k=1), hidden (k=0). Rail open: x 156/376/596 w 210/210/192 — derived from `HERO_W` 430 and the existing 192 strip width so they cannot drift. |
| 4.3 | **One content-driven stream, not two fixed bins** | candidates = passing games not on hero/strip; finals (DESC) get first claim, upcoming (ASC) fills the tail, k = min(count,3), left-to-right, row re-centred at k<3. A lower-left hole becomes structurally impossible because there is no lower-left bin to be empty. The two bins are *anti-correlated* with the promotion rule: FEATURE is chosen when live ∈ [1,3], which is exactly before the slate ramps (many PRE, no FINAL) and after it winds down (no PRE, many FINAL). Three of the four canonical scenarios that resolve to FEATURE ship with the void. |
| 4.4 | **Card treatment, sized 100 px not 128** | The ledger renders score, abbr *and* status all in `font_num15` at 11 px — zero size hierarchy — and uses the **global** `C_INK` (18.72:1) where a FINAL card uses `kStateInk[2].ink` (10.02:1): finals are drawn **1.87× louder than the state ladder says a final should be**. 100 px leaves a 40 px reserve of visible plate along the bottom edge — that reserve is the "ground" the original card-ledger rejection (`ui_ledger.cpp:5-17`) was actually about. My coverage model reproduces that rejection's figures exactly (FEATURE today 62.0 %, 3×3 grid 84.4 %, full 128 px tiles **86.8 % — the rejected number**); at h=100 with k=3 it lands at **81.4 %**. **Explicitly reject** the cheap variant of raising FEATURE's `per` from 2 to 5 to reuse `buildTile`: `DensitySpec` carries one `tileH`, so those cards come out 128 tall and land back on 86.8 %. |
| 4.5 | **Status in `F_NUM`, not `F_MICRO`** | *(correction to the source audit.)* `Final/OT` is read, not recognised by position. `F_MICRO` is reserved for strings that are never read. Zero flash cost — `font_num15` is already in use. |
| 4.6 | **Tappable** | `ui_ledger.cpp` has **zero** event callbacks and never sets `LV_OBJ_FLAG_CLICKABLE`: a settled game on the board is the only game on the panel you cannot tap. `ui_idle.cpp:82/308` already solved this for its identical rows. Reuse the tile's three callbacks; `s_glassPressed` already supplies the press feedback, so nothing lights up at rest. |
| 4.7 | **Change-cache every per-poll write** | The ledger is the only uncached per-poll writer on the FEATURE screen: 18 `lv_label_set_text`, 4 opacity writes, up to 6 colour writes, unconditionally. `lv_label_set_text` invalidates *before* comparing (`lv_label.c:90`); `lv_obj_set_local_style_prop` does not compare either. **Promote `setTextCached`/`setNumCached`/`setHiddenCached` from `static` in `ui_board.cpp` to `ui.h`** rather than copying them — the ledger's whole defect class is that they were not reachable from there. *Correction to the source audit: the ~22 label areas do not consume 22 of the 64 `LV_INV_BUF_SIZE` slots — `lv_refr_join_area()` merges them, since every label area is strictly inside the root's. The 64-slot cliff is not the risk; the flush is, and Phase 0 already removed the dominant term.* |
| 4.8 | **Centre the hero when the row and the strip are both empty** | `--scenario 7` (1 live, 1 total): measured ink in x524-799 y52-479 is **exactly 0**, hero anchored top-left in a ~216,000 px void, 25 % coverage. x = (800−508)/2 = 146, y = 48 + (432−268)/2 = 130. Two cached position writes on one object. Must be sequenced **before** `uiHeroShow()` so it never lands inside the reveal. |

Cost: replaces `ui_ledger.cpp` with a results-row file. Net +16 LVGL objects (−23
ledger, +39 cards) ≈ 3.2 KB — and `lv_conf.h:28-32` sets `LV_MEM_CUSTOM_ALLOC` to
`ps_malloc`, so it comes from the 8 MB PSRAM and does **not** threaten the 16.4 KB
contiguous block mbedTLS needs. No flash cost (reuses `font_score38` / `font_abbr17`
/ `font_num15`).

**How we prove it:** per-scenario ink-coverage and void measurement on scenarios 3,
6, 7 and 10 (today: 1.23 % ink, 75.4 % void, 0 ink in 48.4 % of the band → target
≥ 68.4 % whole-panel coverage at k=1 and 81.4 % at k=3, with **no** empty bin at any
k). Seam check: assert every card's x and right edge equals a grid column edge, both
rail states. Steady-state poll invalidation 0 px (Phase 0's counter).

---

### Phase 5 — Across the room
**Answers the second viewing distance. Risk: medium. Cost: medium. Gated on a
re-measurement (below).**

**This phase is gated.** Phases 1 and 2 shrink the system's saturated ink ~3× while
this phase grows the team channel; nobody has measured the combined budget, and a
board where the overwhelming majority of saturated ink is arbitrary team hue is not
self-evidently the cure for "it doesn't feel like one palette". **Land Phases 1–2,
re-run the salient-chroma census, and only then commit to the score size.**

1. **`font_score95`** (Archivo Cond Bold tnum, `'0x30-0x3A,0x2D,0x20'`, 2 bpp) →
   cap ≈70 px = **18.2′ at 2.5 m**, inside ISO 9241-303's preferred band.
   **Scoped to `DEN_ROOMY` only.** *Correction to the source audit: it is
   geometrically impossible on the FEATURE side tiles* — `kDensity[LAY_FEATURE]`
   carries `tileH` 128, and `buildTile` lays team rows of
   `(128 − 2·TILE_PAD_Y − STATUS_H)/2 = 42 px`. A cap-70 face has line_height 70.
   FEATURE side tiles keep `F_SCORE` unless `DensitySpec` gains a second `tileH` —
   the same missing field Phase 4.4 identifies. **Record the invariant the Roomy fit
   depends on entirely:** these score faces are digit-only crops whose line_height
   *equals* cap height (`score46` 34/34, `hero72` 52/52, `clock96` 69/69), so
   LVGL's label box equals the ink box. 70 fits the 75 px Roomy row with 5 px spare;
   it would not if the face were ever regenerated with a wider glyph range.
2. **Roomy badge/logo 38 → 56 px.** Measured the *best*-surviving element at 2.5 m
   (10.12:1). Per §7 this — not the score's hue — is the identity channel that
   crosses the room.
3. **`font_hero72` regenerated at 96 px** (cap ≈70). `HERO_ROW_H` is 84, so cap 70 +
   10 leading fits; the hero footer moves y=218 → ~236.
4. **Score box width follows the sport's digit count** instead of a fixed 74 px
   (`ui_board.cpp:422`): 140 px for 3-digit, 95 px for 2-digit. At `tileW` 248 that
   leaves 64 px of identity budget for 3-digit sports and 112 px for 2-digit.
   Today's fixed box reserves ~1,340 px of unused tile width on a 12-tile NHL board.
5. **`F_NUM` 15 → 20 px at Roomy only** (cap 15 = 16.0′, clears the ISO desk floor).
   The advance goes 9.0 → 12.0 px, so `STATUS_W` (81) and `SIT_FULL_W` (108) become
   108 and 144, whose sum 252 exceeds the 216 px of usable width in a 248 px tile —
   **at 20 px the status row must drop the broadcast column** and let the clock and
   situation split it. That is the desk/room trade, made explicit. Standard and Dense
   keep 15 px.
6. **Demote at Roomy only:** records (2.18:1 delivered at 2.5 m, and physically 1.6×
   wider than the team abbreviation they annotate) and broadcast on LIVE/FINAL tiles
   (2.36:1 delivered, and static information about a game you are already watching —
   keep it on PRE, where it is actionable).
7. **`F_BODY` 15 → 19 px on the reader and news screens only** (x-height ~10.7′,
   reaching Legge & Bigelow's critical print size). This is the one screen where
   reading *speed* is the metric. ≈ +11 KB flash.
8. **Delete `font_micro11.c`** — referenced by nothing.

**Density cost, stated plainly:** Standard 9 → 6 games, Dense 12 → 8 (and 8 only when
every score is two digits). The pager (`TILES_PER_PAGE`) already handles overflow.
`effectiveDensity()`'s existing count rule is reframed, not rewritten.

**Flash:** ~8–10 KB (`score95`) + ~3 KB (`hero96`) + ~4.4 KB (`F_NUM` 20) + ~11 KB
(`F_BODY` 19) ≈ 28 KB against ~1.4 MB free. The entire font set is 30 KB today.

**Runtime:** a cap-70 two-digit score bbox is 91 × 70 = 6,370 px against today's
44 × 34 = 1,496 px. Worst case (six Roomy tiles, all twelve scores change in one
poll) is 76 k px against the ~50 k/tick sustained budget — it spills to a second
tick, still far under a 230 ms full-screen repaint. The change-cache means only
*changed* scores invalidate, so the common case is unchanged.

**How we prove it:** the room rule, mechanised. `--shot`, Gaussian blur σ = 1.92 px
(the foveal PSF at 2.5 m), downsample to 195 × 117 (the true retinal footprint), and
assert both scores are still readable. Four checkable rules:

| # | rule | today |
|---|---|---|
| R1 | score cap ≥ 61 px (16′); target 73 px | 34 px Roomy / 27 px Standard — **fails** |
| R2 | team identity = one contiguous region ≥ 40 × 40 px at ≥ 3:1 | 38 px badge at 10.1:1 — passes at Roomy |
| R3 | live/final/pre distinguishable at ≥ 3:1 delivered after σ = 1.92 px | live dot 4.96:1 — passes |
| R4 | nothing < cap 46 px may be the **sole** carrier of R1–R3 | status row carries "Final" alone — **fails** |

Plus the gate: re-run the salient-chroma census **after** this phase and assert the
accent floor still holds.

---

### Phase 6 — The glow, properly
**Answers owner ask #2 (end state). Risk: medium-high (it is the first planar asset
in the codebase and it moves z-order). Cost: small-to-medium.**

Pre-composite the glow per team in 8-bit, Bayer-dither the **increment**, quantise to
RGB565, blit through a hard stencil. This is `plate.cpp`'s technique, applied where
it belongs. Measured against the continuous ideal (TOR): mean run 5.37 → **1.37 px**,
mean 4×4-cell error 1.50 → **0.56 L\*** — below the ~1 L\* JND. Other teams follow
(MTL 2.08 → 0.53; EDM 1.55 → 0.63; the teal 1.18 → 0.55).

Dithering the **increment** rather than the absolute value is load-bearing: the outer
corner then contains exactly one value, bit-identical to the card, because `#222E40`
is not exactly representable in RGB565 and dithering the absolute value leaves a
visible 220 × 220 textured patch.

**The z-order restructure is in scope and is why this is not Phase 3.** Child order in
`ui_hero.cpp` is creation order, and the `k=1` iteration creates `s_bloom[1]`
*after* the `k=0` iteration created `s_score[0]`. Geometry (verified this pass with
`HERO_ROW_H` 84, `HERO_W` 508): `s_bloom[1]` spans child x 356–575, y 66–285;
`s_score[0]` sits at x 340 w 144, line_height 52 → x ~356–483, y 66–118. They overlap
across 127 × 52 px, and the glow is **not** faint there. `glassPanel`'s bottom shade
child also falls inside the footprint. Today all of this is invisible because the
alpha is a soft ramp; a hard 0/255 stencil with pre-composited RGB would **erase the
trailing team's score**. Required:

1. `lv_obj_move_to_index(s_bloom[k], 2 + k)` so both blooms sit directly above
   `glassPanel`'s specular pair and below all content. *(This preserves
   `theme.cpp:365`'s invariant — the pair stays at children 0 and 1, and
   `glassRelayout()` keeps indexing them positionally. Nothing is inserted ahead of
   them.)*
2. Stencil alpha to 0 over the bottom shade strip's rows and outside the card's
   rounded rect — which also solves the corner overspill an opaque rect would cause
   and lets you drop the **38 % of the sprite the card clips anyway** (only 150 of
   220 columns are ever visible).
3. **Add an explicit `lv_obj_invalidate(o)` in `bloomSet()`.** Removing
   `lv_img_set_recolor` removes the *only* style write in that function; with a
   pre-composited sprite there is nothing left to dirty the object and the new sprite
   would never be drawn.
4. Write it as `LV_IMG_CF_RGB565A8`, which is **planar** (`w·h·2` bytes of RGB565
   then `w·h` bytes of A8), not interleaved. Every image in this codebase today is
   interleaved `TRUE_COLOR_ALPHA`; this is the project's first planar asset. The
   built-in decoder accepts it verbatim from a variable source.

**Cost, corrected.** Flash 0. PSRAM 145,200 B unchanged with the stencil (96,800 B
if fully opaque — a 48,400 B *saving*). CPU **~45 ms** once per lead change at
`plate.cpp`'s own 3.3 MB/s figure — ~18 ms if only the 150 × 200 visible window is
generated — and it lands **synchronously inside the hero's own paint**:
`uiHeroShow` starts writing pixels at `ui_hero.cpp:344` and does not reach
`bloomSet` until `:420`, on the per-poll path. That is the honest number; "off the
data path" was wrong.

**Per-draw cost strictly falls.** `LV_IMG_CACHE_DEF_SIZE` is 0, so today every
invalidation re-runs `convert_cb` + the recolour loop + a masked `lv_color_mix` over
~30,000 px, uncached — exactly as `bloom.cpp:97` warns. With a hard mask it becomes
`MAP_NORMAL_MASK_PX`, a 4-px-at-a-time compare-and-copy loop (*not* `lv_memcpy` —
that branch is the mask==NULL case), and two scratch allocations disappear. This
matters most for the animation the product exists for: **the bloom sits directly
behind the hero score label** (11,739 px of overlap), and `odoTick` fires **six
times over 200 ms on every same-game score increase**, re-running the whole uncached
path each time.

**Two options are ruled out on measurement, permanently — record them so they are
not re-proposed:**

- **A flash-resident RGB-dithered gradient asset is destroyed before it is blended.**
  `img_recolor` at `LV_OPA_COVER` overwrites the sprite's RGB verbatim
  (`lv_color_mix_premult(premult_v, rgb_buf[i], 0)` returns the recolour colour
  exactly). Every RGB pixel is discarded; only the A8 plane survives. It would also
  cost 96.8–145.2 KB of flash and fix the artwork at build time — the argument
  `plate.cpp:13-27` already lost.
- **Narrowing the gradient's contrast range does not shrink the step, it widens the
  band.** Sweep on the exact pipeline: `img_opa` 200 → 41 runs / 5.4 px mean / peak
  edge 2.07 L\*; 120 → 8.8 px / 2.07; 80 → 12.9 px / 2.07; 24 → 44.0 px / 1.77. The
  peak step is essentially constant because it is floored at **one RGB565 level**.
  A 44 px band is 0.78° at the desk but 0.19° = 3.2 c/deg at 2.5 m — the exact peak
  of the CSF. Range reduction converts high-frequency banding into low-frequency
  banding, which is *more* visible across the room, and destroys the signature at
  the same time.

**How we prove it:** `--shot --settle 400 --scenario 10` for each of TOR / MTL / EDM
/ teal; run-length and per-cell L\* error along a clean radial ray; accept at mean
run ≤ 1.5 px and mean cell error ≤ 0.7 L\*. Plus a regression check that
`s_score[0]`'s digits and the card's bottom shade strip are still drawn (a pixel diff
against the Phase 5 baseline in those two boxes).

---

## 9. Disposition — nothing survives silently

### Verification problems marked **fatal**

| id | problem | disposition |
|---|---|---|
| `colour:C2` — 4.5:1 renders the leading score below `ink3` and drops kits below AA after 565 | **APPLIED, and corrected further.** Unified at **5.5:1 + ceiling L\* 68**, re-derived independently this pass over 35 kits. Adds the missing 8th site (`ui_game.cpp:202`) plus 4 `teamInk()` sites the audit did not count. *Also corrects the verification itself:* its claim that 5.5 keeps the leader above `ink3` "on all four" surfaces is false on the FINAL fill (5.48 vs `ink3` 5.91) — harmless, because FINAL scores use neutral ink and never team ink (`ui_board.cpp:1348`), and the edge light is live-only. |
| `colour:L2` — `A_SELECT #F3F7FB` is bit-identical to `C_INK`, already the inherited text colour | **APPLIED.** Selection becomes an inverted-chip *treatment* (§6). `ui_rail.cpp:96` and `ui_board.cpp:1527` unified onto `INK_1` in the same change. |
| `glance:G6` — the chip is `F_NUM`, not `F_MICRO`; every angular figure is invalid; conclusion contradicts `colour:B2` | **APPLIED.** Verified myself at `ui_board.cpp:552`. Size claim withdrawn; resolved in B2's favour with B2's own correction (`si.ink3`). Stated explicitly: the chip is not readable at 2.5 m at any size this tile can afford, and it is not defended on that basis. |
| `bloom:G4` — a hard 0/255 stencil with pre-composited RGB erases `s_score[0]` and the card's bottom shade | **APPLIED.** Verified myself (creation order + geometry). The z-order move and stencil are folded into Phase 6's scope and cost, and Phase 3 (G5) is scheduled first as the low-risk interim. |
| `glance:G1` — `font_score95` is geometrically impossible on FEATURE side tiles (42 px rows) | **APPLIED.** Scoped to `DEN_ROOMY` only. The `line_height == cap` invariant the Roomy fit depends on is recorded beside the font-generation command. |
| `glance:G5` vs `colour:C2/C3/C4` — opposite, mutually exclusive surgery on `lift()` | **APPLIED by arbitration.** One contract: `T_INK` = `clampL(lift(c, s, 5.5f), 68)`, as a **wrapper**, not inside the primitive. Both collision counts re-run at the chosen value this pass. `glance:G5` option (a) is **rejected** — see §10. |

### Verification problems marked **needs-change**

| id | disposition |
|---|---|
| `colour:C3` — grammar written over token values; press outline lands in its own TEAM cell; band is per-surface | **APPLIED.** §5 states all three cells over *rendered* colour and adds **Rule A** (no chromatic token at partial opacity), which fixes the press outline, hero footer, heartbeat bar and pulse dot with one rule. **Rule B** states the band per-surface. The "TEAM lives at L\* 46-62" numbers are **dropped** — false at 5.5:1 — and replaced with per-surface floors to a fixed ceiling of 68. |
| `colour:C4` — ceiling and floor are the same constraint with 1.1 L\* slack; "hue exact" is only half true | **APPLIED, with a stronger formula.** `CEIL = max(68, L*_required(ratio, surface))` so raising the ratio moves the ceiling instead of defeating the floor, plus a build-time assertion on both the floor and an 8.0 L\* moat. The audit's L\* 62 is **rejected on measurement**: at 5.5 it puts **35/35 kits below target** (min 4.66) and the clamp silently becomes the decision-maker. Mechanism reworded to "hue-preserving, chroma-reducing" with Boston's −12 % chroma stated. Implemented as a wrapper (engine's correction) so `teamFill()` and `badgeInk()` are untouched. |
| `colour:B1` — opa 225 gives only 5 unevenly-spaced rendered levels | **APPLIED.** Five pre-solved solid `bg_color` rungs (§4.1), even to 1.2–1.5 L\*, no blend, no quantiser. The audit's minor evidence error (trough ≠ `C_LIVE_SD` "to within 1 LSB"; it is 3 green + 1 blue LSBs, ΔL\* 4.05) is noted; the substance — the trough is a dimmer, less chromatic teal than the flat rail bar, below AA on the hero — is confirmed and is the owner's "green live dots". |
| `colour:B2` — `si.ink2` merges the chip into the clock; the `isTense()` rationale is not what the code does; `isTense()` is `static` | **APPLIED.** Routed to `si.ink3`. Rationale rewritten honestly (only baseball is demoted; hockey and football keep amber whenever the chip says anything). De-static cost added. |
| `colour:C1` — `EDGE_HI` is sub-3:1; the cap collides with the filter tab; the countdown must not go to `INK_1`; the dot budget is 324 px not 64; a missed literal | **APPLIED, all five**, and a **sixth found**: `ui_board.cpp:1675` carries a second `"#3be0c0"` inside a format string. Segment body → `INK_3` (6.81:1, chroma 14.2). *Note: I measure `EDGE_HI` on the plate at **2.13:1**, not 2.69 — the correction is stronger than stated.* Cap moved to the segment's bottom edge. Countdown → `kStateInk[GS_PRE].ink`. Dot budget restated as 324 px (→ ~900 at 10 px). |
| `colour:L3` — `0x4A5666` is a text ternary, not a border; the ΔL\* claim is wrong by 4× | **APPLIED.** Routed to `EDGE_HI` (ΔL\* 0.30, both branches stay distinguishable). Max rendered change restated as 17.16 ΔL\* (`0x334455 → T_NONE`, on a placeholder badge fill whose label ink is unaffected). |
| `colour:G1` — both win-prob remedies fail (2.45:1 and 1.47:1); C2 makes it worse | **APPLIED.** 2 px `INK_1` tick at the join (13.10:1). Sequenced with Phase 2 and stated: one ratio takes the halves from 1.61 to 0.15 ΔL\*. |
| `glance:G5` — option (a) infeasible; option (b) is monotone improvement, not a compromise | **APPLIED.** Option (a) rejected with the L\* 57.5 / 68.5 floors. Option (b) adopted at 5.5, converging with the C2 correction from the opposite direction. Lint expressed on rendered 565 values. |
| `colour:B3` — sequence after the ratio decision; fold in `ui_alert.cpp:86`'s scrim | **APPLIED.** `ui_alert.cpp:205` is in Phase 2's routing table with the rest; the scrim is in §4.3's literal list. |
| `glance:G3` — promote the acceptance criterion to a constraint on all other findings | **APPLIED** as §2 item 1: any fix that raises an ink to chase a rendered-contrast measurement is rejected by default. |
| `cross-audit:accent-vs-team-budget` — nobody computed the combined budget | **APPLIED.** Census re-derived independently this pass. Phase 5 is **gated** on re-running it after Phases 1–2; the accent floor becomes a tracked `make lint` invariant; the compensation is the 10 px dot and the top-bar count, not letting C1's reduction stand unexamined. §7 states the badge-not-the-score conclusion this analysis forces. |
| `engine:colour:C2` — split the ratio: 4.5 for structure, higher for the score | **REJECTED, with reason.** The split exists to keep the leader above the trailer; **5.5 achieves that on both surfaces the score is team-coloured on** (5.60 / 5.55 against `ink3`'s 5.20 / 4.62) *without* splitting. A split would reintroduce the exact two-ratios-per-team defect that is this plan's root cause R1. |
| `engine:colour:C4` — wrapper, not inside `lift()` | **APPLIED.** Verified myself: `lift()` is the shared primitive behind `teamInk`, `teamInkOn` **and** `teamFill`, and `teamFill`'s early return is what keeps bright badges bright. |
| `engine:bloom:G4` — ~45 ms not ~30, synchronous mid-paint; missing `lv_obj_invalidate`; planar not interleaved; mask-px not memcpy | **APPLIED**, all four, in Phase 6's cost line. |
| `engine:ledger:L7` — understates cost 3×; the real term is the unconditional `clear_flag`, present on the hero too | **APPLIED and PROMOTED to Phase 0.** Verified independently: `lv_obj.c:281` invalidates unconditionally; ledger root 800×140 = 112,000 px, hero root 508×268 = 136,144 px, **248,144 px per poll**, ≈149 ms of flush, zero visual change. Two lines. This is the single largest measurable win in the plan and it ships first. |

---

## 10. Dropped

| proposal | why |
|---|---|
| `colour:C2`'s unified **4.5:1** | Renders the leading score at mean 5.08:1 on a live tile — below `kStateInk[GS_LIVE].ink3`'s 5.20:1, so the biggest number on the tile would be dimmer than the record beneath it (31 of 35 kits) — and after 565 two kits fall below AA on the FINAL fill (`#002868` 4.49, `#041E42` 4.44) with a third sitting exactly on the line on PRE, because `lift()` solves against the 24-bit fill while the panel shows the 565 surface. |
| `colour:C4`'s **L\* 62** ceiling | At the chosen 5.5:1 it clamps **35/35** kits below target (min 5.23 live, **4.66 hero**). The clamp, not the solver, would decide every team colour, and no assertion would fire. Raised to 68 with an auto-raising guard. |
| `colour:C3`'s "**TEAM lives at L\* 46-62**" | True only at 4.5:1, which is rejected. Restated as per-surface floors (63.7 live / 67.1 hero at 5.5) to a fixed ceiling of 68, with the 3.4 L\* cross-surface spread stated rather than hidden. |
| `colour:L2`'s **`A_SELECT = #F3F7FB`** | Bit-identical to `C_INK` (565 `0xF7BF`), which `theme.cpp:290` already makes every glass panel's inherited text colour — the token could carry no state. Also fails the plan's own chroma ≥ 30 signal rule (it measures 4.1). Replaced by a treatment. |
| `colour:B1`'s **opa 225** floor | `(opa+4)>>3` leaves only 5 rendered levels across 225..255, unevenly spaced (1.45 / 2.42 / 1.40 / 2.72 L\*), so the dot holds one value for 3–4 ticks then jumps. Replaced by solid rungs. |
| `colour:C1`'s **`EDGE_HI`** rail body | 2.13:1 on the plate — below WCAG 1.4.11's 3:1 for a graphic that is simultaneously being promoted to carry the rail's entire information content. Replaced by `INK_3` at 6.81:1. |
| `colour:C1`'s **top-anchored** liveness cap | Occupies the same pixels as the white filter tab (`ui_rail.cpp:93-99`, `(0,y)` 3×min(h,12)) on the one league that is both live and filtered. Moved to the segment's bottom edge. |
| `colour:C1`'s **`INK_1`** idle countdown | `ui_idle.cpp:163` already draws the clock in `C_INK` at 96 px on the same screen; the "largest type on screen" rationale is false. Replaced by `kStateInk[GS_PRE].ink`. |
| `colour:L3`'s **`0x4A5666 → INK_3`** | It is the empty branch of a text-colour ternary; the swap would delete the has-games / no-games distinction on every rail row while moving 25.4 L\*. Routed to `EDGE_HI` (0.30 L\*). |
| `colour:G1`'s two win-prob remedies | 60 % opacity on the trailing half renders 2.45:1 (below the 3:1 non-text floor, and violates Rule A); a `BG`-coloured 1 px separator is 1.47:1 against the hero fill — not a luminance cue at either distance, which was the whole point. Replaced by a 2 px `INK_1` tick. |
| `glance:G6`'s **size claim** | `ui_board.cpp:552` sets `F_NUM`, not `F_MICRO`. Verified. Every derived angular figure and the "smallest face on the panel" premise collapse. |
| `glance:G5`'s **option (a)** (preserve source lightness order in `lift()`) | Infeasible against the AA floor: on the 565 live fill at L\* 13.70, any ink clearing 4.5:1 must sit at L\* ≥ 57.5 and 6.5:1 at ≥ 68.5. The ordering information is destroyed by the contrast *requirement*, not the implementation. |
| `glance:G1` for the **FEATURE side tiles** | `kDensity[LAY_FEATURE].tileH` is 128 → 42 px team rows; a cap-70 face has line_height 70. Needs a second `tileH` field or it does not happen. |
| `glance:G2`'s **global** `F_NUM` 20 / `F_BODY` 19 | `STATUS_W` + `SIT_FULL_W` become 252 > 216 px of usable tile width. Scoped: `F_NUM` 20 at Roomy only (dropping the broadcast column), `F_BODY` 19 on reader/news only. |
| `glance:G4`'s **cut the lead rule and edge light** | Correct that they deliver 2.04:1 and 2.28:1 at 2.5 m, but they are part of the tile's identity language at the desk and they become *more* coherent under one ratio. Cutting the product's signature elements is a redesign the owner did not ask for. → Open question. |
| `bloom:G7` (narrow the contrast range) and `bloom:G6` (flash-resident RGB gradient) | Both already rejected on measurement by their own author; recorded in Phase 6 so they are not re-proposed. |
| The brief's **`KNOWN ARTEFACT`** premise (reveal can leave the card dim) | Disproven: `LV_STYLE_OPA` is multiplied into each descendant's own draw opacity, not composited through a layer (`LV_STYLE_OPA_LAYERED` is never set), and every `s_rev` deletion is paired with an opacity reset or a root replacement. The *cost* conclusion survives; the failure-mode conclusion does not. |
| `ledger:L10` (align idle's LATEST column, adopt the winner/loser ink rule) | Correct and cheap, but strictly optional and touches a screen with none of the owner's three asks. Keep as a follow-up. |

---

## 11. Open questions — decisions only the owner can make

1. **Density.** Phase 5 costs Standard 9 → 6 games and Dense 12 → 8. Is
   across-the-room legibility worth a third of the board on a busy night? The plan
   proposes making it automatic via the *existing* `effectiveDensity()` count rule
   (≤ 6 → Roomy = the room layout), so a busy night stays a desk layout — but that
   is a taste call, not a measurement.
2. **The lead rule and the edge light.** Measured at 2.04:1 and 2.28:1 delivered at
   2.5 m — they pay nothing across the room. They are 33 % of the tile's ink budget
   as part of the frame, against 12 % for the scores. Keep them as desk-only
   signature, or spend the pixels? The plan keeps them.
3. **How much of Phase 5 to run at all.** Phases 1–4 answer all three of the stated
   asks. Phase 5 answers a fourth thing ("glanceable from across the room") that
   competes with desk density and consumes the budget Phase 1 just freed. The plan
   gates it on a re-measurement; the owner decides whether the gate opens.
4. **The staged hero reveal.** Cut to one rung, or drop entirely? It costs ~245 ms of
   flush across three rungs on a device whose rule is "nothing may delay data", and
   it is safe but not free. The plan recommends one rung; dropping it is also
   defensible.
5. **Team-colour collisions.** Even at 5.5:1 the lift produces 11 excess values among
   35 kits after 565 (KC and CGY land byte-identical on a live tile). §7 argues this
   is unfixable at RGB565 with a contrast floor and that the badge must carry
   identity. If that is not acceptable, the alternative is a curated per-league
   colour table on the proxy — a wire-contract change nobody has costed.
6. **Provenance gap.** The material handed to this synthesis contained **two** of the
   three stated adversarial reviews, and the engine-feasibility verdict was truncated
   mid-sentence in `ledger:L7`'s correction. Everything load-bearing in §1, §4 and
   Phase 0 was re-derived from source in this pass, but if a third review exists it
   has not been reconciled here.

---

## 12. Sequencing summary

```
Phase 0  measurable + stop the bleeding    ── prerequisite for verifying 1,3,4,6
   │
   ├── Phase 1  one accent, one meaning     ── owner ask #1a   (rail, amber, pulse, dots)
   │      │
   │      └── Phase 2  one team colour      ── owner ask #1b   (T_INK, 12 sites)
   │             │
   │             └── [GATE: re-run census] ── Phase 5  across the room
   │
   ├── Phase 3  glow dither (interim)       ── owner ask #2a
   │      │
   │      └── Phase 6  glow pre-composite   ── owner ask #2b   (needs z-order move)
   │
   └── Phase 4  results row                 ── owner ask #3
          └── 4.1 (finals by startUtc DESC) ships alone if nothing else does
```

Phases 1+2, 3, and 4 are mutually independent after Phase 0 and can be flashed in any
order. Phase 5 is gated on Phase 2's census. Phase 6 is gated on Phase 3 shipping and
on accepting the z-order restructure.
