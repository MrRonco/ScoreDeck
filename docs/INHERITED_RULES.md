# Inherited rules — what AirRadar cost to learn

ScoreDeck is a clean-slate build, but it runs on the same board, the same
panel, the same LVGL version and the same PSRAM bandwidth ceiling. Every rule
below is transcribed from AirRadar's engineering contract, where each one
represents a real debugging session. Re-learning them would be the most
expensive thing this project could do.

Filtered to what applies here. Radar-specific rules (blip z-order, coasting
tracks, sprite `restore()` rectangles) are omitted.

---

## Boot and hardware

1. **PSRAM must be configured as OPI PSRAM.** Wrong or missing → large PSRAM
   allocations fail → black screen or boot loop. First thing to check on any
   "screen is black" report.

2. **`freq_write = 14 MHz`** in the panel config. 16 MHz produced pixel drift on
   this unit. Drop to 12 MHz if drift appears.

3. **The GT911 touch address is latched from the INT pin at reset.** Left
   floating it is a coin flip between 0x5D and 0x14 per power cycle. Perform a
   controlled reset: drive GPIO4 (INT) low as an output → TP_RST low via the
   CH422G → 12 ms → TP_RST high → 60 ms address-latch window with INT still low
   → release INT to input. That pins 0x5D every boot.

4. **`Wire.end()` after the CH422G writes.** LovyanGFX's own I2C driver owns the
   bus afterward for the GT911. Do not reintroduce `Wire` at runtime without
   rethinking bus ownership.

5. **`WiFi.setSleep(false)` after every connect.** Modem-sleep wake bursts
   contend with the RGB panel's continuous ~32 MB/s PSRAM DMA and cause visible
   screen wiggle.

---

## Rendering and LVGL

6. **The LVGL draw buffer is 800×30 (48 KB) in internal SRAM — not 60 lines.**
   96 KB starved everything else: internal heap settled around 17 KB and
   optional subsystems were shed permanently. It must stay in internal SRAM
   (PSRAM buffers contend with the panel DMA), just not that large.

7. **`lcd.setSwapBytes(false)` — byte-swap in `flush_cb` yourself.** With the
   flag on, LovyanGFX resolves `pushImage` through the `rgb565_t` pixelcopy
   specialisation, which skips `Panel_FrameBufferBase::writeImage`'s per-row
   `memcpy` and turns every flushed pixel into an individual convert-and-store
   into PSRAM. Swapping in internal SRAM first keeps the bulk `memcpy`.

8. **Every dynamic LVGL write must be change-cached — flags included.**
   `lv_obj_add_flag` / `lv_obj_clear_flag` repaint the object even when the flag
   already held that value. A `setHidden()` called every tick on a large image
   repainted it 4×/s and that *was* the mysterious "wiggle". Guard every write
   with a comparison against the last value.

9. **`lv_obj_move_foreground()` invalidates the WHOLE PARENT.** It calls
   `lv_obj_move_to_index`, whose last line is `lv_obj_invalidate(parent)`
   (LVGL 8.3, `lv_obj_tree.c:216`). Never call it inside per-item creation.
   Defer z-order changes, and only perform them when the object is visible.

10. **`LV_INV_BUF_SIZE` overflow repaints the entire screen.** Exceed it in one
    refresh period and LVGL discards every pending area and invalidates the whole
    display (`lv_refr.c:256`). The default is 32; **set it to 64 in `lv_conf.h`**.
    Symptom is a ~384,000 px flush with bounding box `0,0-799,479`.

11. **LVGL flex overflows; it does not shrink.** A child wider than its parent
    spills rather than compressing, and a `SIZE_CONTENT` parent can *under-measure*
    and clip a correctly-sized child. When a value looks truncated, call
    `lv_obj_update_layout()` and measure before blaming the font — twice the label
    was fine and the parent was the bug. Fixed-width keys plus `LV_LABEL_LONG_DOT`
    beat hoping.

12. **Rendering is event-driven — there is no refresh loop.** The panel DMA
    already consumes most PSRAM bandwidth. Never add per-frame animation without
    budgeting pixels against it. *(See [`UI.md`](UI.md) §8: this is why the alert
    card fades in 4 discrete steps and then holds static, and §1 for why no
    frosted panel is ever allowed to move.)*

---

## Memory and lifetime

13. **`vTaskDelete(NULL)` skips every C++ destructor in that scope.** It never
    returns, so a `DynamicJsonDocument` — or any RAII object — declared in a task
    function leaks its heap buffer on **every single run**. This was AirRadar's
    entire "mysterious 72 B/s drain": one leak per 15 s poll. Either scope the
    object in a nested block, or put the body in a helper function and call
    `vTaskDelete` in the wrapper. **This is the most expensive lesson in the
    file** and ScoreDeck will have more polling tasks than AirRadar did.

14. **Check the TLS gate in LOOP context *before* spawning a task.** Network
    tasks take a 12 KB internal stack; spawning one only to discover the gate is
    shut burns the very RAM the gate protects — AirRadar measured ~80 no-op
    create/destroy cycles per minute, which was the fragmentation engine pinning
    `heap_largest` at 10 KB. The gate must test free size **and** largest free
    block, because mbedTLS needs a ~16.4 KB contiguous buffer.

15. **One TLS connection at a time, with priority.** The main state poll is
    essential; game detail, roster, news and logo fetches are optional and get
    shed below the internal-heap floor. Concurrent TLS starves mbedTLS.

---

## Flash

16. **EVERY FATFS write starves the panel DMA — the whole screen shakes.** Flash
    and PSRAM share the MSPI bus and `CONFIG_SPI_FLASH_AUTO_SUSPEND` is **not
    set** in the prebuilt arduino-esp32 3.3.10 libraries, so a flash operation
    cannot be suspended to let cache traffic through. Measured: **150–220 ms of
    blocked bus per write, almost independent of size** — it is sector erase plus
    FAT metadata, not bytes. Consequences:
    - **Chunking a small write makes it worse.** Five 2 KB writes cost more than
      one 9 KB write. Only chunk when the payload is genuinely large (hundreds of KB).
    - **The only lever at small sizes is frequency.** Rate-limit logo cache
      persistence to one write per 45 s.

---

## Diagnosis

17. **Shake vs flicker is the triage.** A *whole-screen shake* is the RGB panel's
    DMA starved of PSRAM bandwidth — look for MSPI contention: flash writes, or
    rule 5's modem sleep. A *local flicker or tear* is an oversized LVGL repaint —
    look at invalidation. AirRadar spent six hypotheses chasing repaints before
    the owner said "the entire screen shakes", which eliminated rendering in one
    sentence. The measurable signature of the shake is **a long stall with ZERO
    pixels flushed**, which every repaint metric is structurally blind to.

18. **Instrument before hypothesising.** Three confident explanations for
    AirRadar's glitch were wrong and each cost a round trip. Build the equivalent
    of `/api/stalls` — per-stage loop timing plus flushed pixel count and its
    bounding box — **in Phase 0**, not when something breaks.

19. **Instrumentation that is never incremented reads as a measurement.** Two
    counters were declared, published to `/metrics` and never written, so
    "component X contributes exactly 0" was a hardcoded zero, and the wrong
    subsystem was blamed for three sessions. Also: a fixed-cadence subsystem makes
    any time-linear drain *look* per-poll.

---

## Web portal security

20. **A blank secret field must never overwrite the stored secret.** The password
    field renders blank *by design* so the secret is not served back — so an
    unconditional write means saving any unrelated setting silently erases it.
    Guard every secret write with `if (value.length())`.

21. **Validate `Host` against the names the device answers to, before anything
    else.** Comparing Origin to Host is not a guard: both are the requester's to
    choose, so the test only ever proves they agree — and under DNS rebinding they
    agree perfectly (`Host: evil.com` with `Origin: http://evil.com` passes, and
    with no panel password the request is then authorised). Check Host against the
    mDNS name, the bare label and `WiFi.localIP()` first; that is the anchor the
    origin test hangs off. An absent Host header is allowed on purpose — every
    browser sends one, so rejecting buys nothing, and a client that omits it
    cannot be rebound.

22. **Sanitize anything from the wire before it reaches a URL.** AirRadar
    sanitizes callsigns to `[A-Za-z0-9]` because they come straight off the ADS-B
    wire and would otherwise be injectable. ScoreDeck's equivalent: team ids,
    league slugs and event ids from the proxy, and favorites from the portal.

---

## Conventions worth copying

- All geometry, timings and NVS keys named in `config.h` — no magic numbers in
  modules.
- Palette and fonts as tokens in `ui/theme.h`.
- Freeze tabular figures (`pyftfeatfreeze -f tnum`) on any face that renders
  changing numbers, or digits visibly jitter. Scores change constantly.
- **Codepoint ranges are not uniform and dropping one breaks glyphs silently.**
  See [`UI.md`](UI.md) §9 — ScoreDeck needs Latin-1 Supplement and Latin Ext-A for
  athlete names, which AirRadar's 7-bit ASCII faces do not have.
- Generated `font_*.c` files are OFL-1.1. Keep a `THIRD-PARTY-NOTICES.md`.
