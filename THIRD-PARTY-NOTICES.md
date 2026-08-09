# Third-party notices

ScoreDeck's firmware is **GPL-3.0-or-later** ([`LICENSE`](LICENSE)); the proxy is
**AGPL-3.0-or-later** ([`proxy/LICENSE`](proxy/LICENSE)).

This file lists everything else that ships with it. It matters most for the
pre-built flasher images, because those **statically link** the libraries below —
distributing a binary carries the same notice obligations as distributing source.

## Compiled into the firmware

| Component | Licence |
|---|---|
| [LVGL](https://lvgl.io) 8.3.11 | MIT |
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) 1.2.25 | BSD-2-Clause |
| [ArduinoJson](https://arduinojson.org) 6.21.6 | MIT |
| [arduino-esp32](https://github.com/espressif/arduino-esp32) core 3.3.10 | LGPL-2.1-or-later |
| ESP-IDF, mbedTLS and related Espressif components | Apache-2.0 |

All are compatible with GPL-3.0-or-later.

## Fonts — NOT GPL

The generated `firmware/ScoreDeck/src/assets/font_*.c` files embed glyph outlines
and are licensed under the **SIL Open Font License 1.1**, not the GPL. Full text
in [`LICENSES/OFL-1.1.txt`](LICENSES/OFL-1.1.txt).

| Face | Source | Copyright |
|---|---|---|
| `font_score38`, `font_score46` | Archivo (Condensed Bold, `tnum` frozen) | Omnibus-Type |
| `font_abbr17` | Archivo (Condensed SemiBold) | Omnibus-Type |
| `font_body15` | IBM Plex Sans Regular | IBM Corp. |
| `font_micro11` | IBM Plex Mono Medium (`tnum` frozen) | IBM Corp. |

Regenerate with [`tools/build-fonts.sh`](tools/build-fonts.sh).

## Team names, logos and player likenesses

Team names, marks and logos are the property of their respective owners. Player
headshots are personal likenesses licensed through the players' associations.

**No logo or headshot assets are included in this repository or in any release
binary.** The build tools fetch them onto your own machine, into your own proxy,
for personal use. See [`docs/OPEN_SOURCE.md`](docs/OPEN_SOURCE.md) §1.

## Sports data

Retrieved from ESPN's publicly reachable but **undocumented and unofficial**
endpoints. This project is not affiliated with or endorsed by ESPN or any league,
and provides no warranty that those endpoints will continue to work.
