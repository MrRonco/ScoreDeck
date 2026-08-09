# Desktop harness

Runs ScoreDeck's UI in a window on a Mac, or renders it headless to a BMP.

```bash
brew install sdl2
make            # build
./scoredeck-ui  # interactive window
make shots      # render every scenario to shots/scn*.bmp
make screens    # render every screen   to shots/scr-*.bmp
make lint       # every label, every screen: can its font draw its text?
```

## Why this exists

Live sports only ever show you the ordinary case. The states that break things
are the ones you cannot summon: a 48-game November Saturday, a 12-inning
linescore, three-digit college basketball scores, a proxy that has gone away,
an athlete called Ødegaard. Every scenario in `scenarios.cpp` has caught at
least one real bug.

The headless mode is not an afterthought. `--shot` renders a frame with no
window and no display, so a UI change can be looked at in the same turn it is
written — and `make shots` produces a reviewable contact sheet.

## What is real and what is faked

The rule is **stub the plumbing, keep the logic**. Compiled from the actual
firmware source, unmodified:

- every `ui/*.cpp` screen
- `core/state.cpp` — settings, quiet hours, `boardFollows()`
- `assets/font_*.c` — the same generated faces with the same glyph ranges
- `firmware/lv_conf.h` **verbatim**, via `LV_CONF_PATH`, so the colour depth,
  `LV_INV_BUF_SIZE` and allocator match the panel exactly

`fakes.cpp` supplies only what touches a network, a flash chip or the panel.
Every `api*Start()` returns false: scenarios write `g_board` directly, so a
fetch would overwrite what the scenario is trying to show.

`logoGet()` always misses, so the colour-badge fallback is what you see. That
is the correct default — it is what every install renders before running the
asset build, and the harness has no proxy to fetch from.

## Reaching the secondary screens

Six of the nine screens clear their state inside `Open()` and then wait on a
fetch, which in the harness never lands — so they would only ever render
"loading". `scenarioReapply()` fills standings, news, lineups, player cards and
game detail *after* the screen has opened. The player card is not a screen at
all but an overlay on the lineup, so `--screen player` opens both.

```bash
./scoredeck-ui --screen standings          # any of the nine
./scoredeck-ui --shot out.bmp --scenario 8 # headless, tennis/golf/F1
```

## The font lint

`make lint` walks the live LVGL object tree across every screen on every
scenario, and for each visible label asks its assigned font whether it has a
glyph for every codepoint the label is holding. It exits non-zero on a miss.

This exists because the faces are generated with the narrowest glyph range each
job needs, so pointing a label at the wrong one renders hollow boxes and
reports nothing — and static analysis cannot catch it, because the face is
chosen where the object is built and the text arrives from upstream at runtime,
usually in another file. Six labels had shipped that way, including `GOAL` on
the alert card and the wordmark on the first-boot screen. See `theme.h` for
what each face actually covers.

## Shims

`shim/` provides Arduino, `Preferences`, `WiFi`, `esp_heap_caps`,
`esp32-hal-psram` and `freertos/`. Deliberately small: if a new firmware file
needs more of the platform than these provide, that is a signal worth reading —
the UI is supposed to talk to LVGL and to `g_*` state, not to the platform.

The heap shim reports a healthy heap so the TLS gate stays open. It cannot say
anything true about the real heap; that measurement only exists on the device.

## Scenarios

| # | Name | What it catches |
|---|---|---|
| 0 | typical mixed board | the ordinary case |
| 1 | empty — idle screen | the idle empty state, no stray badges |
| 2 | nine live games | every edge light at once |
| 3 | long names, three-digit scores | overflow and truncation |
| 4 | 48 games — cap and pager | the device cap and paging |
| 5 | no proxy configured | first-run state |
| 6 | stale upstream | must say so, not present stale as live |
| 7 | accented names | font range — Latin Extended-A |
| 8 | tennis, golf, F1 | the SET / LEADERBOARD / GRID tiles |

## Keys

```
1..9    scenario          SPACE  next scenario
b i g   board idle game   s n l  standings news lineup
p       write shot.bmp    q      quit
```

Mouse is the touch panel, so click and drag exercise the same handlers a finger
does — including the gestures that page the board.

## Bugs it found on day one

- The **edge light never appeared**: its change-cache was initialised to
  "visible" while the object was built hidden, so the first update matched and
  returned early. A cache that disagrees with reality suppresses exactly the
  update it was meant to make cheap.
- The **idle clock was blank on first paint**: `uiIdleTick()` early-returned
  while the screen was hidden, and `uiIdleRefresh()` runs before `uiShow()`.
- The **back chevron rendered as a box**: `<` is `0x3C`, outside every range in
  the condensed abbreviation face.
- **Every accented name rendered as boxes.** The font carried Latin Extended-A;
  the labels used `F_MICRO` and `F_ABBR`, which do not. Verifying the font file
  proved nothing about the screens.

## And on day two

Once the harness could reach all nine screens, it found six more of the same
kind — including `GOAL` on the alert card and `ScoreDeck` on the first-boot
screen, both set in a face with no letters in it at all. That is what turned
the pattern into `make lint`: the fix for a bug you have now made four times is
not another fix, it is a check.
