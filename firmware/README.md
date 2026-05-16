# firmware/ — Cardputer-AI device firmware (M0 probe)

This directory currently holds the M0 hardware bring-up probe, not the
final firmware. It will be replaced/restructured in M1+.

## Build & flash

From the repo root, with PlatformIO Core on `PATH`:

```bash
cd firmware
pio run                       # build
pio run -t upload             # build + flash (Cardputer in download mode)
pio device monitor             # 115200 baud, USB-CDC
```

To enter download mode on Cardputer-ADV: hold `G0` (or follow M5's docs for
the ADV variant) while plugging in USB, then release after `pio` starts.

## Layout

- `platformio.ini` — build config
- `src/main.cpp` — calls each probe in turn
- `src/probe_*.{h,cpp}` — one module per peripheral
