#!/usr/bin/env python3
"""Every rule this codebase states in a comment, asserted against the source.

Written in Python rather than the C++ the plan named, because most of these
invariants are properties of the SOURCE TEXT — "no colour literal outside the
theme", "no chromatic token at partial opacity" — and a C++ binary that greps
its own repository is a worse tool for that than a script. Badge fit stays in
the C++ font lint, which already walks a live tree.

Run by `make lint`. Exit code is the number of violations.

Phase 20 left two rules as PENDING for phase 21 to implement. Phase 21 did, and
they are source rules after all — not the object-tree walk the docstring above
first assumed. The reason is worth writing down, because it is also the
measurement that made the phase bigger than filed:

  lv_obj.c:436 gives EVERY lv_obj_create LV_OBJ_FLAG_CLICKABLE, and
  lv_obj_pos.c:955 hit-tests on exactly that flag. So "is this object
  hit-testable" is not a runtime question at all — it is true by default, and
  the only thing a source file can do is take it away. Which means both halves
  of the press invariant are decidable from the text: a control is an object
  the source hands to uiPressable()/uiButton(), and everything else is a
  surface whether the source noticed or not.

PENDING is empty. Nothing here is aspirational; if a rule cannot be enforced it
does not belong in this file.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
UI = os.path.join(ROOT, '..', 'firmware', 'ScoreDeck', 'src', 'ui')

RADII = {2, 6, 10, 14, 18}
CHROMATIC = ('A_LIVE', 'C_LIVE', 'S_ALERT', 'C_WARN', 'C_LIVE_TX')

# Rules that are declared but not yet enforceable. Kept as a dict rather than
# deleted so that "we know and are not checking" stays visible; phase 21
# emptied it.
PENDING = {}

# The calls that make an object a control, and the two that deliberately make
# one an input surface WITHOUT the press outline (see uiTapZone in theme.h).
PRESS_CALLS = ('uiPressable(', 'uiPrimaryButton(', 'uiButton(', 'backChip(')
EXEMPT_CALLS = ('uiScrim(', 'uiTapZone(')
# A strictly smaller set, and the distinction is the whole point of the rule:
# uiPressable() ADOPTS our press, it does not remove lv_theme_default's. An
# lv_btn that only gets uiPressable() ends up wearing BOTH — the 2 px teal
# outline and the 20% darken underneath it. Only these drop the theme first.
RESET_CALLS = ('uiPrimaryButton(', 'uiButton(', 'backChip(', 'lv_obj_remove_style_all(')

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

# glassPanel()'s LAST ARGUMENT is a radius too, and it was the hole in the rule
# above: six cards shipped a literal 12 — a rung that does not exist — and the
# gate above never saw them because they are an argument, not a style write.
for name, lines in ui_sources():
    if name == 'theme.cpp':
        continue
    for n, line in enumerate(lines, 1):
        code = strip_comment(line)
        m = re.search(r'glassPanel\s*\(.*,\s*(\d+)\s*\)', code)
        if not m:
            continue
        r = int(m.group(1))
        if r == 0 or r in RADII:
            continue
        bad('radius-family', f'{name}:{n}',
            f'glassPanel radius {r} is not in the family (2/6/10/14/18)')

# ── 4b. one press, and it is attached to the things you can press ───────────
#
# Two halves, and the second is the one that bites.
#
# (a) glassPanel() must hand back an INERT surface. It cannot stop attaching
#     the press STYLE — the board tiles and the hero card are promoted to
#     controls in a different file and would silently lose their feedback —
#     so what it must drop is the FLAG, which is what LVGL hit-tests.
#
# (b) Nothing may be made hit-testable without saying which it is. Every
#     lv_obj_add_flag(x, CLICKABLE) and every lv_btn_create needs x to reach
#     one of PRESS_CALLS, or to be declared an input surface via EXEMPT_CALLS.
#     Objects are matched by NAME within a file, which is coarse and is the
#     right amount of coarse: this catches the shape of the defect (a handler
#     with nothing to show for it) without pretending to understand scope.
theme = open(os.path.join(UI, 'theme.cpp')).read()
gp = theme[theme.find('lv_obj_t* glassPanel('):]
gp = gp[:gp.find('\nlv_obj_t* teamBadge')]
if 'lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE)' not in gp:
    bad('inert-pressed', 'theme.cpp:glassPanel',
        'glassPanel() must clear LV_OBJ_FLAG_CLICKABLE — a panel is a surface')

def base_name(v):
    """s_card[i] and s_card[0] are one object here; t.root and t.edge are NOT.
    The first version stripped the member too, which made every write to any
    field of a tile look like a write to the tile itself — seven false
    positives on ui_board alone, and a rule that cries wolf gets deleted."""
    return re.sub(r'\[[^\]]*\]', '[]', v)

for name, lines in ui_sources():
    if name == 'theme.cpp':
        continue
    # Subscripts are normalised on BOTH sides or nothing matches: the source
    # writes uiPressable(s_rowBg[r]) and the declaration is s_rowBg[i].
    src = base_name('\n'.join(strip_comment(l) for l in lines))
    # A glassPanel() result already carries the press style — see glassPanel()
    # — so adding the flag to one IS the whole promotion. Only objects built
    # some other way have to ask for the treatment separately.
    glass = {base_name(m) for m in
             re.findall(r'([\w\[\]\.>-]+?)\s*=\s*glassPanel\(', src)}
    # A struct member names a ROLE, so tile.root built by glassPanel() in one
    # function is the same thing as s_tile[].root promoted in another. Without
    # this the board's twelve tiles read as untreated, which they are not.
    glass_roles = {g.rsplit('.', 1)[1] for g in glass if '.' in g}
    def treated(v, calls):
        return any(f'{c}{v}' in src for c in calls)
    def is_glass(v):
        return v in glass or ('.' in v and v.rsplit('.', 1)[1] in glass_roles)
    for n, line in enumerate(lines, 1):
        code = strip_comment(line)
        m = re.search(r'lv_obj_add_flag\(\s*([\w\[\]\.>-]+?)\s*,\s*LV_OBJ_FLAG_CLICKABLE', code)
        if m:
            v = base_name(m.group(1))
            if is_glass(v):
                continue
            if not treated(v, PRESS_CALLS) and not treated(v, EXEMPT_CALLS):
                bad('inert-pressed', f'{name}:{n}',
                    f'{v} is hit-testable with no press treatment — uiPressable() '
                    f'it, or declare it an input surface with uiTapZone()')
        m = re.search(r'([\w\[\]\.>-]+?)\s*=\s*lv_btn_create\(', code)
        if m:
            v = base_name(m.group(1))
            if not treated(v, RESET_CALLS):
                bad('btn-style-reset', f'{name}:{n}',
                    f'{v} is an lv_btn, so it carries lv_theme_default\'s OWN 20% '
                    f'darken press — uiButton() resets it and adopts ours')

# ── 4c. a glass panel's fill and its shade move together ────────────────────
#
# The contact shadow is solved per surface (StateInk.shade) and lives in a
# child, so a bare bg_color write repaints the card and leaves its own shadow
# behind. glassSetFill() writes both.
for name, lines in ui_sources():
    if name == 'theme.cpp':
        continue
    src = '\n'.join(strip_comment(l) for l in lines)
    src = base_name(src)
    panels = {base_name(m) for m in
              re.findall(r'([\w\[\]\.>-]+?)\s*=\s*glassPanel\(', src)}
    for n, line in enumerate(lines, 1):
        code = strip_comment(line)
        m = re.search(r'lv_obj_set_style_bg_color\(\s*([\w\[\]\.>-]+?)\s*,', code)
        if m and base_name(m.group(1)) in panels:
            bad('glass-fill', f'{name}:{n}',
                f'{base_name(m.group(1))} is a glassPanel — route the fill through '
                f'glassSetFill() so the solved shade follows it')

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
