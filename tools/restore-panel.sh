#!/usr/bin/env bash
# Put ScoreDeck back on the panel after it has been used for something else.
#
#   tools/restore-panel.sh                 # auto-detects the port
#   tools/restore-panel.sh /dev/cu.xyz     # or name it
#   tools/restore-panel.sh --build         # recompile from source instead
#
# Flashes flasher/image/, the exact artifacts built from the commit this script
# sits in — no recompile, no toolchain warm-up.
#
# NOTE the whole DIRECTORY is what gets flashed, not one file: arduino-cli
# derives the bootloader and partition-table names from the sketch name beside
# the app image, so handing it a lone renamed .bin fails looking for
# "<name>.bootloader.bin". That is how the first version of this script broke.
set -euo pipefail
cd "$(dirname "$0")/.."

CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc"

BUILD=0
ARGS=()
for a in "$@"; do
  if [ "$a" = "--build" ]; then BUILD=1; else ARGS+=("$a"); fi
done

PORT="${ARGS[0]:-$(ls /dev/cu.wchusbserial* 2>/dev/null | head -1)}"
[ -n "$PORT" ] || { echo "no panel found — plug it in, or pass the port"; exit 1; }

if [ "$BUILD" = "1" ] || [ ! -d flasher/image ]; then
  echo "building from source and flashing to $PORT"
  "$CLI" compile --fqbn "$FQBN" --upload -p "$PORT" firmware/ScoreDeck
else
  echo "flashing the stored image to $PORT"
  "$CLI" upload -p "$PORT" --fqbn "$FQBN" --input-dir flasher/image firmware/ScoreDeck
fi

cat <<'NOTE'

Done. Settings survive a reflash — they live in NVS, not in the image — so
Wi-Fi, the proxy URL, the token and your favourites all come back with it.
NOTE
