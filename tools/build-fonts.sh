#!/usr/bin/env bash
# Regenerate the LVGL faces. See docs/UI.md §9 and THIRD-PARTY-NOTICES.md.
#
#   npm i -g lv_font_conv@1.5.2
#   pip3 install opentype-feature-freezer
#
# Two things here are load-bearing and neither is taste:
#   * tnum is FROZEN on every face rendering changing numbers, or score digits
#     visibly jitter as they tick over.
#   * body carries Latin-1 Supplement AND Latin Extended-A. Doncic, Odegaard,
#     Konate and Vlasic are boxes in a 7-bit ASCII face.
set -euo pipefail
OUT="$(cd "$(dirname "$0")/.." && pwd)/firmware/ScoreDeck/src/assets"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

curl -sL -o Archivo.ttf     "https://github.com/google/fonts/raw/main/ofl/archivo/Archivo%5Bwdth,wght%5D.ttf"
curl -sL -o PlexSans.ttf    "https://github.com/google/fonts/raw/main/ofl/ibmplexsans/IBMPlexSans%5Bwdth,wght%5D.ttf"
curl -sL -o PlexMono.ttf    "https://github.com/google/fonts/raw/main/ofl/ibmplexmono/IBMPlexMono-Medium.ttf"

# Both display faces are variable — pin the condensed instance before converting.
python3 - <<'PY'
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
for src, axes, out in [
    ("Archivo.ttf",  {"wght": 700, "wdth": 75},  "Archivo-CondBold.ttf"),
    ("Archivo.ttf",  {"wght": 600, "wdth": 75},  "Archivo-CondSemi.ttf"),
    ("PlexSans.ttf", {"wght": 400, "wdth": 100}, "PlexSans-Regular.ttf"),
]:
    f = TTFont(src)
    instancer.instantiateVariableFont(f, axes, inplace=True, updateFontNames=False)
    f.save(out)
PY

pyftfeatfreeze -f tnum Archivo-CondBold.ttf Archivo-CondBold-tnum.ttf
pyftfeatfreeze -f tnum PlexMono.ttf         PlexMono-tnum.ttf

gen() { lv_font_conv --font "$1" -r "$2" --size "$3" --bpp 4 --no-compress \
          --format lvgl -o "$OUT/$4.c" --force-fast-kern-format; }

gen Archivo-CondBold-tnum.ttf '0x30-0x3A,0x2D,0x20'                              38 font_score38
gen Archivo-CondBold-tnum.ttf '0x30-0x3A,0x2D,0x20'                              46 font_score46
gen Archivo-CondSemi.ttf      '0x20,0x23,0x25,0x2A-0x2F,0x30-0x39,0x3A,0x41-0x5A' 17 font_abbr17
gen PlexSans-Regular.ttf      '0x20-0x7E,0xB0,0xB7,0xC0-0xFF,0x100-0x17F'         15 font_body15
gen PlexMono-tnum.ttf         '0x20-0x7E,0xB0,0xB7'                               11 font_micro11

# lv_font_conv emits "lvgl/lvgl.h"; the Arduino library resolves as <lvgl.h>.
sed -i '' 's|#include "lvgl/lvgl.h"|#include <lvgl.h>|' "$OUT"/font_*.c 2>/dev/null || \
  sed -i    's|#include "lvgl/lvgl.h"|#include <lvgl.h>|' "$OUT"/font_*.c
echo "regenerated 5 faces into $OUT"
