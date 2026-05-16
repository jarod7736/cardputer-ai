# M0 Hardware Report — Cardputer-ADV

**Date captured:** TBD (fill in when probe runs)
**Firmware commit:** TBD (fill in `git rev-parse --short HEAD` after final commit)

This document captures findings from the M0 hardware bring-up probe. Values
marked `?` were not yet captured at the time of this commit.

## SoC

- Chip model: ?
- Revision: ?
- CPU cores: ?
- Features (Wi-Fi/BT/BLE/embedded flash): ?
- Flash size (from `ESP.getFlashChipSize()`): ?
- Crystal frequency: ?

## Memory

- Internal SRAM total: ?
- Internal SRAM free at boot: ?
- **PSRAM present:** ? (yes/no — **this answers DESIGN.md §11 Q1**)
- PSRAM total (if present): ?
- PSRAM free at boot (if present): ?
- Largest contiguous heap block: ?

## Display (ST7789V2, 240×135)

- M5Unified detected board variant: ?
- Test pattern rendered cleanly: ?

## Keyboard (TCA8418)

- I²C scan found TCA8418 at address: ?
- Sample keypresses captured: ?

## microSD

- Card mount succeeded: ?
- Card size: ?
- Read/write round-trip succeeded: ?

## Toolchain

- PlatformIO Core version: ?
- ESP-IDF 5.x hello-world build clean: ?

## Open questions resolved

- [ ] DESIGN.md §11 Q1 — PSRAM presence
