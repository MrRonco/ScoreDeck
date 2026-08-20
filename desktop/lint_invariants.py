#!/usr/bin/env python3
"""Every rule this codebase states in a comment, asserted against the source.

Written in Python rather than the C++ the plan named, because most of these
invariants are properties of the SOURCE TEXT — "no colour literal outside the
theme", "no chromatic token at partial opacity" — and a C++ binary that greps
its own repository is a worse tool for that than a script. The two rules that
genuinely need a live object tree (badge fit, and pressed-but-inert objects)
stay in the C++ font lint, which already walks it.

Run by `make lint`. Exit code is the number of violations.

Some rules are scheduled to pass only once phase 21 lands. Those are listed in
PENDING with the phase that clears them, so the gate is honest about what it is
not yet enforcing rather than silently omitting it.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
UI = os.path.join(ROOT, '..', 'firmware', 'ScoreDeck', 'src', 'ui')

RADII = {2, 6, 10, 14, 18}
CHROMATIC = ('A_LIVE', 'C_LIVE', 'S_ALERT', 'C_WARN', 'C_LIVE_TX')

# Rules phase 21 is scheduled to clear. Listed, not hidden.
PENDING = {
    'btn-style-reset': 'phase 21 — every lv_btn needs lv_obj_remove_style_all() first',
    'inert-pressed':   'phase 21 — glassPanel() must not attach the press style by default',
}

violations = []
def bad(rule, where, msg):
    violations.append((rule, where, msg))

def ui_sources():
    for f in sorted(os.listdir(UI)):
        if f.endswith('.cpp'):
            yield f, open(os.path.join(UI, f)).read().split('\n')

def strip_comment(line):
    """Rules are about CODE. A line that only mentions a hex in prose is fine —
    several of these comments exist precisely to record what was removed."""
    i = line.find('//')
    return line[:i] if i >= 0 else line

# ── 1. colour literals live in the theme ────────────────────────────────────
for name, lines in ui_sources():
    if name in ('theme.cpp',):
        continue
    for n, line in enumerate(lines, 1):
        code = strip_comment(line)
        if 'lv_color_hex(0x' in code:
            bad('colour-literal', f'{name}:{n}',
                'lv_color_hex() outside theme.cpp — route it to a token')
        if 'lv_color_white()' in code or 'lv_color_black()' in code:
            bad('colour-literal', f'{name}:{n}',
                'lv_color_white()/black() outside theme.cpp — use C_INK / C_PLATE')

# ── 2. no recolour literal hidden in a format string ────────────────────────
# "#rrggbb text#" is LVGL's recolour syntax; a hex in there is invisible to
# every grep for lv_color_hex, which is exactly how one desynced for a release.
for name, lines in ui_sources():
    for n, line in enumerate(lines, 1):
        code = strip_comment(line)
        if re.search(r'"[^"]*#[0-9a-fA-F]{6}\s', code):
            bad('recolour-literal', f'{name}:{n}',
                'hex inside a recolour format string — generate it from the token')

# ── 3. no chromatic token drawn at partial opacity ──────────────────────────
# lv_color_mix quantises opa to (opa+4)>>3 and blends toward the surface, so a
# blended accent is not the accent: it loses lightness AND chroma and lands in
# the team band.
for name, lines in ui_sources():
    for n, line in enumerate(lines, 1):
        code = strip_comment(line)
        m = re.search(r'set_style_(?:bg|border|text|img_recolor)_opa\s*\([^;]*?,\s*(\d+)\s*,', code)
        if not m:
            continue
        opa = int(m.group(1))
        if opa >= 255:
            continue
        if any(t in code for t in CHROMATIC):
            bad('blended-chromatic', f'{name}:{n}',
                f'chromatic token at opa {opa} — declare a solved solid instead')

# ── 4. one radius family ────────────────────────────────────────────────────
for name, lines in ui_sources():
    for n, line in enumerate(lines, 1):
        code = strip_comment(line)
        m = re.search(r'set_style_radius\s*\([^,]+,\s*(\d+)\s*,', code)
        if not m:
            continue
        r = int(m.group(1))
        # 0 is an explicit reset; LV_RADIUS_CIRCLE and half-height pills are
        # circles, not members of the family.
        if r == 0 or r in RADII:
            continue
        bad('radius-family', f'{name}:{n}',
            f'radius {r} is not in the R_XS/SM/MD/LG/XL family (2/6/10/14/18)')

# ── 5. the accent has one meaning ───────────────────────────────────────────
# Settings is a persistent-choice surface: nothing on it is "happening now".
settings = open(os.path.join(UI, 'ui_settings.cpp')).read()
for n, line in enumerate(settings.split('\n'), 1):
    code = strip_comment(line)
    if re.search(r'\bC_LIVE\b|\bA_LIVE\b', code):
        bad('accent-meaning', f'ui_settings.cpp:{n}',
            'the accent means "happening now"; a settings choice is not that')

# ── 6. deleted tokens stay deleted ──────────────────────────────────────────
for name, lines in ui_sources():
    for n, line in enumerate(lines, 1):
        if 'C_LIVE_SD' in strip_comment(line):
            bad('dead-token', f'{name}:{n}', 'C_LIVE_SD was deleted in phase 12')

# ── 7. a finished score does not outrank a live one ─────────────────────────
#
# Phase 23's decision, pinned so it cannot silently revert. The FINAL winner's
# SCORE resolves to si.ink2, not si.ink. Measured on --scenario 0 with an
# alpha-weighted salience census (ink px summed as contrast - 1 against each
# tile's own measured fill): at si.ink the three brightest NUMBERS on a nine-up
# board were 3526 / 2130 / 2321 and all three were games that had ended,
# against live leaders at 1837 / 1484 / 981 / 816. At si.ink2 they are
# 2332 / 1409 / 1536.
#
# The lever is this SENTINEL and not the token. Darkening
# kStateInk[GS_FINAL].ink instead is a 45% ratio regression that takes every
# FINAL team NAME down with it — which is why the third check exists: si.ink2
# is already the status ink, so an abbreviation that followed the score down
# would render in the identical grey as the game clock on the same tile.
#
# This is the source-level form of the no-regression gate. The RENDERED form —
# no ink tier darker than it was — needs a render census and lives with the
# other two object-tree rules, not here.
board_src = open(os.path.join(UI, 'ui_board.cpp')).read()
if not re.search(r'want\s*==\s*0xFFFFFFFEu\s*\?\s*si\.ink2', board_src):
    bad('final-score-rung', 'ui_board.cpp',
        'the FINAL-winner sentinel 0xFFFFFFFE must resolve to si.ink2 (phase 23)')
ledger_src = open(os.path.join(UI, 'ui_ledger.cpp')).read()
if not re.search(r's_score\[slot\]\[k\],\s*won\s*\?\s*si\.ink2', ledger_src):
    bad('final-score-rung', 'ui_ledger.cpp',
        "the ledger's winning score must mirror the tile at si.ink2 (phase 23)")
if not re.search(r's_abbr\[slot\]\[k\],\s*won\s*\?\s*si\.ink\s*:', ledger_src):
    bad('final-score-rung', 'ui_ledger.cpp',
        'the team abbreviation must NOT follow the score down to si.ink2 — '
        'that is already the status ink on the same card')

# ── the baseline ────────────────────────────────────────────────────────────
#
# Phase 20 ships BEFORE phase 21, and phase 21 is what clears the radius family
# and the last colour literals. Turning the gate red for every commit in
# between would just train people to ignore it, and deleting the rules until
# then would hide exactly what phase 21 is for.
#
# So: the known set is written down, the gate fails on anything NEW, and it
# ALSO fails if a baseline entry has been fixed and not removed. The list can
# only ever shrink. Phase 21's job is to empty it.
BASE = os.path.join(ROOT, 'lint_baseline.txt')
known = set()
if os.path.exists(BASE):
    known = {l.split('#')[0].strip() for l in open(BASE) if l.split('#')[0].strip()}

now = {f'{rule} {where}' for rule, where, _ in violations}
new = sorted(now - known)
fixed = sorted(known - now)

print('source invariants')
for rule, where, msg in violations:
    tag = 'FAIL' if f'{rule} {where}' in new else 'base'
    print(f'  {tag}  {rule:<20} {where:<28} {msg}')
for rule, why in sorted(PENDING.items()):
    print(f'  todo  {rule:<20} {"":<28} {why}')

if new:
    print(f'\n{len(new)} NEW violation(s) — not in the baseline:')
    for k in new:
        print(f'    {k}')
if fixed:
    print(f'\n{len(fixed)} baseline entr(y/ies) no longer violated — remove from '
          f'desktop/lint_baseline.txt so the list keeps shrinking:')
    for k in fixed:
        print(f'    {k}')

print(f'\n{len(violations)} violation(s): {len(violations) - len(new)} baselined, '
      f'{len(new)} new. {len(PENDING)} rule(s) not yet enforced.')
sys.exit(1 if (new or fixed) else 0)
