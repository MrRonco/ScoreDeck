# Building & flashing ScoreDeck (macOS)

## 0. One-time

```bash
brew install --cask wch-ch34x-usb-serial-driver   # CH343 UART on the board
brew install arduino-cli
arduino-cli config init --overwrite
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.10
arduino-cli lib install "lvgl@8.3.11" "LovyanGFX@1.2.25" "ArduinoJson@6.21.6"
```

## 1. Place lv_conf.h — the step everyone gets wrong

LVGL looks for it **next to**, not inside, its library folder:

```bash
cp firmware/lv_conf.h ~/Documents/Arduino/libraries/lv_conf.h
```

## 2. Build and flash

```bash
FQBN='esp32:esp32:esp32s3:CDCOnBoot=default,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,UploadSpeed=921600'

arduino-cli compile --fqbn "$FQBN" firmware/ScoreDeck
arduino-cli upload  --fqbn "$FQBN" -p /dev/cu.wchusbserial* firmware/ScoreDeck
arduino-cli monitor -p /dev/cu.wchusbserial* -c baudrate=115200
```

Two settings bite:

- **`PSRAM=opi`** — miss it and the panel is black. First thing to check.
- **`CDCOnBoot=default`** routes `Serial` to UART0, which is the CH343 port you
  flash over. With `CDCOnBoot=cdc` the console moves to the board's *other*
  USB-C connector and the flashing port goes silent.

## 3. Serial console

The USB port already allows reflashing, so the console adds no trust boundary —
it just removes a two-minute build cycle from every config change.

```
proxy <url>      point at your proxy          poll     force a poll now
token <t>        bearer token                 show     current config
favs nhl:21,...  followed teams               games    dump the board
region ca        broadcast region             shot     100x30 screen dump
lgs nhl,mlb      leagues to show              reboot
```

`shot` prints a luminance map of the live framebuffer — enough to verify the
grid renders without a camera pointed at the glass.

## 4. Going back to AirRadar

The board is unchanged; reflash the other sketch, or use AirRadar's web flasher
with `flasher/airradar-merged.bin`. ScoreDeck's NVS lives in its own namespace
(`sdeck`) and does not disturb AirRadar's (`radar`) — in fact ScoreDeck reads
AirRadar's Wi-Fi credentials on first boot so you do not retype them.

## 5. Development defaults (optional)

`firmware/ScoreDeck/src/dev_defaults.h` is gitignored. If present it seeds an
empty proxy URL on boot:

```c
#define DEV_PROXY_URL "http://192.168.10.100:8787"
```
