#!/usr/bin/env bash
# Put ScoreDeck back on the panel after it has been used for something else.
#
#   tools/restore-panel.sh                 # auto-detects the port
#   tools/restore-panel.sh /dev/cu.xyz     # or name it
#
# Flashes flasher/scoredeck-ota.bin, the exact image built from the commit this
# script sits in — no recompile, no toolchain, nothing to get wrong at 1am.
# If that file is missing or you want to rebuild from source instead, see the
# arduino-cli line at the bottom.
set -euo pipefail
cd "$(dirname "$0")/.."

PORT="${1:-$(ls /dev/cu.wchusbserial* 2>/dev/null | head -1)}"
[ -n "$PORT" ] || { echo "no panel found — plug it in, or pass the port"; exit 1; }

CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc"

echo "flashing ScoreDeck to $PORT"
"$CLI" upload -p "$PORT" --fqbn "$FQBN" --input-file flasher/scoredeck-ota.bin firmware/ScoreDeck

cat <<'NOTE'

Done. Settings survive a reflash — they live in NVS, not in the image — so
Wi-Fi, the proxy URL, the token and your favourites all come back with it.

To rebuild from source instead:
  arduino-cli compile --fqbn <FQBN> --upload -p <PORT> firmware/ScoreDeck
NOTE
