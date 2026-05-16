# M0 Hardware Report — Cardputer-ADV

**Date captured:** 2026-05-16
**Firmware commit:** d6c1b9b (firmware that produced this report)

This document captures findings from the M0 hardware bring-up probe.

## SoC

- Chip model: ESP32-S3
- Revision: 0
- CPU cores: 2
- Features: WIFI, BLE (no Classic BT advertised; no `EMB-FLASH` / `EMB-PSRAM` flags in `esp_chip_info` — flash is external on the FN8 variant)
- Flash size (from `ESP.getFlashChipSize()`): 8 388 608 bytes (8 MB) — vendor XMC per esptool
- Crystal frequency: 40 MHz

## Memory

- Internal SRAM total: 394 524 bytes (~385 KB usable via `MALLOC_CAP_INTERNAL`)
- Internal SRAM free at boot: 364 608 bytes (~356 KB) after Arduino + M5Unified init
- **PSRAM present: NO** (`ESP.getPsramSize()` returned 0; tried with and without `BOARD_HAS_PSRAM` build flags — same result). **Resolves DESIGN.md §11 Q1.**
- PSRAM total: n/a
- PSRAM free at boot: n/a
- Largest contiguous internal heap block: 327 668 bytes (~320 KB)

**Implication for later milestones:** scrollback, history, and any
sizable buffers must live in internal SRAM or spill to microSD. We have
no PSRAM headroom. Design budgets accordingly.

## Display (ST7789V2, 240×135)

- M5Unified detected board variant: **24 = `board_M5CardputerADV`** — Cardputer-ADV is supported out of the box, no raw driver work needed.
- Resolution: 240 × 135, color depth 16 bpp
- Test pattern (four colored corner squares + "M0 DISPLAY OK") rendered cleanly: yes
- M5Unified initialized an I²C bus during display init at SDA=GPIO8, SCL=GPIO9 — distinct from the Arduino default `Wire` instance (see Keyboard).

## Keyboard (TCA8418)

- I²C scan on default `Wire` (SDA=GPIO2, SCL=GPIO1): **0 devices found**.
- This is **not** "no keyboard" — M5Unified initialized a separate I²C bus on SDA=8/SCL=9 for internal peripherals; the TCA8418 almost certainly lives on that bus, not the default Arduino `Wire`.
- M0 scope did not extend to driving the second bus; raw key capture is deferred to M1 along with selecting between (a) M5Unified's built-in keyboard binding for `board_M5CardputerADV` and (b) a direct TCA8418 driver.

## microSD

- SD pins (from M5Unified): SCK=40, MISO=39, MOSI=14, CS=12
- Mount succeeded: yes
- Card size: 29 820 MB (32 GB nominal card)
- Root listing captured (existing Bruce firmware artifacts present; canary file `m0_canary.txt` written and removed cleanly).
- Read/write round-trip: **OK** (canary contents matched bit-for-bit).
- Minor: `addApbChangeCallback()` duplicate-registration warning during `SPI.begin` reuse — non-fatal, log-level only. Document and ignore in M1.

## Toolchain

- PlatformIO Core version: 6.1.19
- ESP-IDF 5.x hello-world build clean: yes (`firmware-idf-probe/` builds in ~4.4 min cold; mbedTLS + lwIP link without errors; produces a valid ESP32-S3 image).
- Arduino-on-`esp32-s3-devkitc-1` build clean: yes (`firmware/` builds in ~30 s warm; final firmware uses 15.0% of 3.34 MB app partition and 6.7% of 320 KB internal SRAM with all probes wired in).
- Host environment notes (recorded so M1 doesn't re-discover them):
  - WSL2 + usbipd-win for device passthrough.
  - Required enabling systemd in `/etc/wsl.conf` so udev would run and apply a rule giving `dialout` group access to the ESP32-S3 USB JTAG VID/PID (`303a:1001`).
  - PowerShell `usbipd attach --wsl --busid <id> --auto-attach` is needed for stable monitoring across resets, because ESP32-S3 native USB re-enumerates on every reset.

## Follow-ups feeding M1

1. **Switch to a Cardputer-specific board profile** so `M5.Keyboard` is bound and we can capture typed keys without writing a raw driver. M5Unified already auto-detects the board (`board_M5CardputerADV`), so this is mostly a `platformio.ini` change.
2. **Confirm the TCA8418's I²C bus** is the SDA=8/SCL=9 instance and scan there in M1 to verify the chip address.
3. **PSRAM-free buffer budget:** scrollback should be a fixed-size ring in internal SRAM with overflow spilling to a microSD ring file.
4. **`addApbChangeCallback()` duplicate warning** during SD bring-up — harmless, but worth a one-line note in the M1 SD module so a future reader doesn't chase it.

## Open questions resolved

- [x] DESIGN.md §11 Q1 — PSRAM presence: **absent**.
