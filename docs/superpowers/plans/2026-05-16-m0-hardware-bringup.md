# M0 — Hardware Bring-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up a minimal PlatformIO firmware project for the M5Stack Cardputer-ADV that exercises every onboard peripheral the rest of the project depends on (display, keyboard, microSD, serial) and emits a one-page hardware report capturing the unknowns from DESIGN.md §11 — most importantly whether PSRAM is present.

**Architecture:** A single Arduino-framework PlatformIO project under `firmware/` running a stepwise hardware probe. Each peripheral gets its own probe module called from `setup()`, results stream to USB serial and to the LCD. The final task runs an additional ESP-IDF "hello world" project under `firmware-idf-probe/` to satisfy DESIGN.md M0's toolchain-verification checkbox without coupling early Arduino exploration to ESP-IDF migration timing.

**Tech Stack:**
- PlatformIO Core + VS Code (already installed locally, not yet configured for this repo)
- Arduino framework on `platform = espressif32` (primary probe project)
- ESP-IDF 5.x via PlatformIO (secondary toolchain-verification project only)
- `M5Unified` library (provides Cardputer-ADV display, keyboard, and IMU abstractions; verify version supports `-ADV` variant — fall back path documented in Task 3)
- `SD` (Arduino core) for microSD
- Espressif `esp_chip_info`, `esp_psram_get_size`, `heap_caps_get_total_size` for SoC introspection

**Out of scope for M0:** Wi-Fi, WireGuard, TLS, UI design, networking of any kind. M0 is only about confirming we can talk to the hardware and learning what we have to work with.

**Verification style:** Hardware bring-up is exploratory, not test-first. Each task ends with a manual verification step (read serial, read screen, observe LED/keypress). Where a task produces data we want to keep, the verification step writes that data into `docs/m0-hardware-report.md`.

---

## File structure

Files created by this plan:

```
cardputer-ai/
├── .gitignore                                    (modified — add PlatformIO artifacts)
├── firmware/
│   ├── platformio.ini                            (Arduino probe project config)
│   ├── README.md                                 (how to build/flash/monitor)
│   └── src/
│       ├── main.cpp                              (entry point — calls each probe in turn)
│       ├── probe_soc.h / probe_soc.cpp           (esp_chip_info + PSRAM + heap)
│       ├── probe_display.h / probe_display.cpp   (ST7789V2 init + test pattern)
│       ├── probe_keyboard.h / probe_keyboard.cpp (TCA8418 keypress echo)
│       └── probe_sd.h / probe_sd.cpp             (mount, list, write/read test)
├── firmware-idf-probe/
│   ├── platformio.ini                            (ESP-IDF 5.x toolchain verification)
│   ├── sdkconfig.defaults                        (minimal IDF config)
│   ├── CMakeLists.txt                            (project root)
│   └── src/
│       ├── CMakeLists.txt                        (component registration)
│       └── main.c                                (esp-idf hello world that boots)
└── docs/
    └── m0-hardware-report.md                     (captured findings — committed)
```

Each `probe_*` module is one responsibility, one .cpp/.h pair, no shared state beyond a `Stream& out` reference passed in. This sets the pattern for later milestones: small, focused files with explicit interfaces.

---

## Task 0: Repo prep

**Files:**
- Modify: `cardputer-ai/.gitignore`
- Create: `cardputer-ai/docs/m0-hardware-report.md`

- [ ] **Step 1: Verify current .gitignore contents**

Run: `cat /home/jarod7736/workspace/cardputer-ai/.gitignore`
Expected: existing entries (note them — we will *append*, not replace).

- [ ] **Step 2: Append PlatformIO ignore rules**

Add these lines to `.gitignore` (append at end; do not duplicate any line that's already present):

```gitignore

# PlatformIO
.pio/
.pioenvs/
.piolibdeps/
.vscode/.browse.c_cpp.db*
.vscode/c_cpp_properties.json
.vscode/launch.json
.vscode/ipch
firmware/.pio/
firmware-idf-probe/.pio/
```

- [ ] **Step 3: Create empty hardware report stub**

Create `docs/m0-hardware-report.md` with this content:

```markdown
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
```

- [ ] **Step 4: Commit**

```bash
cd /home/jarod7736/workspace/cardputer-ai
git add .gitignore docs/m0-hardware-report.md
git commit -m "chore(m0): gitignore platformio artifacts; stub hardware report"
```

---

## Task 1: PlatformIO project skeleton

**Files:**
- Create: `firmware/platformio.ini`
- Create: `firmware/src/main.cpp`
- Create: `firmware/README.md`

- [ ] **Step 1: Create `firmware/platformio.ini`**

```ini
; PlatformIO config for Cardputer-AI M0 hardware probe (Arduino framework)
;
; The Cardputer-ADV is ESP32-S3FN8 with 8 MB flash. PSRAM presence is what
; M0 is here to determine, so we DO NOT enable PSRAM-dependent build flags
; yet. We start from the generic ESP32-S3 DevKitC-1 board profile (closest
; match in upstream PlatformIO) and override what we need.

[env:cardputer-adv]
platform = espressif32 @ ^6.7.0
board = esp32-s3-devkitc-1
framework = arduino

; Flash & monitor
upload_speed = 921600
monitor_speed = 115200
monitor_filters = esp32_exception_decoder, time

; USB-CDC on boot so Serial works over the native USB port
build_flags =
  -D ARDUINO_USB_CDC_ON_BOOT=1
  -D ARDUINO_USB_MODE=1
  -D CORE_DEBUG_LEVEL=3

; 8 MB flash, no PSRAM assumed yet
board_upload.flash_size = 8MB
board_build.flash_size = 8MB
board_build.partitions = default_8MB.csv

lib_deps =
  m5stack/M5Unified @ ^0.2.5
```

- [ ] **Step 2: Create `firmware/src/main.cpp` (bare skeleton)**

```cpp
// Cardputer-AI M0 hardware probe — entry point.
//
// This file intentionally does very little; each probe lives in its own
// module so we can add/remove them independently. Probes are called in
// order in setup(), then loop() idles. We are not building an app yet.

#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  // Give USB-CDC time to enumerate on the host before we print.
  delay(1500);
  Serial.println();
  Serial.println("==== Cardputer-AI M0 hardware probe ====");
  Serial.println("(skeleton — no probes wired in yet)");
}

void loop() {
  delay(1000);
}
```

- [ ] **Step 3: Create `firmware/README.md`**

````markdown
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
````

- [ ] **Step 4: Build the skeleton (do not flash yet)**

Run: `cd /home/jarod7736/workspace/cardputer-ai/firmware && pio run`
Expected: build succeeds. First run will download the espressif32 platform + Arduino core + M5Unified — this can take several minutes. The final lines should resemble:

```
RAM:   [=         ]   X.X% (used XXXX bytes from 327680 bytes)
Flash: [          ]   X.X% (used XXXXXX bytes from 3342336 bytes)
========================= [SUCCESS] Took N seconds =========================
```

If the build fails on `board = esp32-s3-devkitc-1` resolution, your PlatformIO platform index is stale: `pio pkg update -g -p espressif32` and retry.

- [ ] **Step 5: Commit**

```bash
cd /home/jarod7736/workspace/cardputer-ai
git add firmware/platformio.ini firmware/src/main.cpp firmware/README.md
git commit -m "feat(m0): scaffold PlatformIO Arduino project for Cardputer-ADV"
```

---

## Task 2: SoC + memory probe

This is the most important probe — it answers the PSRAM question from DESIGN.md §11.

**Files:**
- Create: `firmware/src/probe_soc.h`
- Create: `firmware/src/probe_soc.cpp`
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Create `firmware/src/probe_soc.h`**

```cpp
#pragma once
#include <Arduino.h>

// Print SoC identity, flash size, internal SRAM stats, and PSRAM presence
// + size to the given Stream. Safe to call from setup() after Serial.begin.
void probe_soc(Stream& out);
```

- [ ] **Step 2: Create `firmware/src/probe_soc.cpp`**

```cpp
#include "probe_soc.h"

#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <esp_psram.h>
#include <esp_system.h>

static const char* chip_model_name(esp_chip_model_t m) {
  switch (m) {
    case CHIP_ESP32:    return "ESP32";
    case CHIP_ESP32S2:  return "ESP32-S2";
    case CHIP_ESP32S3:  return "ESP32-S3";
    case CHIP_ESP32C3:  return "ESP32-C3";
    case CHIP_ESP32H2:  return "ESP32-H2";
    case CHIP_ESP32C6:  return "ESP32-C6";
    default:            return "(unknown)";
  }
}

void probe_soc(Stream& out) {
  out.println();
  out.println("-- SoC --");

  esp_chip_info_t info{};
  esp_chip_info(&info);
  out.printf("model:        %s\n", chip_model_name(info.model));
  out.printf("revision:     %u\n", info.revision);
  out.printf("cores:        %u\n", info.cores);
  out.printf("features:     %s%s%s%s%s\n",
             (info.features & CHIP_FEATURE_WIFI_BGN)    ? "WIFI " : "",
             (info.features & CHIP_FEATURE_BT)          ? "BT "   : "",
             (info.features & CHIP_FEATURE_BLE)         ? "BLE "  : "",
             (info.features & CHIP_FEATURE_EMB_FLASH)   ? "EMB-FLASH " : "",
             (info.features & CHIP_FEATURE_EMB_PSRAM)   ? "EMB-PSRAM " : "");

  out.printf("flash size:   %u bytes\n", (unsigned) ESP.getFlashChipSize());
  out.printf("crystal:      %u MHz\n", (unsigned) (rtc_clk_xtal_freq_get()));

  out.println();
  out.println("-- Memory --");
  out.printf("internal SRAM total: %u bytes\n",
             (unsigned) heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
  out.printf("internal SRAM free:  %u bytes\n",
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  out.printf("largest internal blk: %u bytes\n",
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

#if CONFIG_SPIRAM
  size_t psram_size = esp_psram_get_size();
  if (psram_size > 0) {
    out.printf("PSRAM:         PRESENT, %u bytes total\n", (unsigned) psram_size);
    out.printf("PSRAM free:    %u bytes\n",
               (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  } else {
    out.println("PSRAM:         not detected (CONFIG_SPIRAM is on but esp_psram_get_size==0)");
  }
#else
  out.println("PSRAM:         build configured WITHOUT SPIRAM support");
  out.println("               (rebuild with -D BOARD_HAS_PSRAM if probe needed)");
#endif

  out.println();
}
```

- [ ] **Step 3: Wire probe into `main.cpp`**

Replace the contents of `firmware/src/main.cpp` with:

```cpp
#include <Arduino.h>
#include "probe_soc.h"

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("==== Cardputer-AI M0 hardware probe ====");

  probe_soc(Serial);

  Serial.println("==== probes complete (more to come) ====");
}

void loop() {
  delay(1000);
}
```

- [ ] **Step 4: Build, flash, and observe**

```bash
cd /home/jarod7736/workspace/cardputer-ai/firmware
pio run -t upload
pio device monitor
```

Expected: serial output beginning with `==== Cardputer-AI M0 hardware probe ====`, followed by an `-- SoC --` block with `model: ESP32-S3`, `cores: 2`, features including `WIFI BT BLE EMB-FLASH`, `flash size: 8388608 bytes`.

The PSRAM line is the critical one. There are three possible outcomes — record which you saw:

- `PSRAM: build configured WITHOUT SPIRAM support` → re-run **Step 5 below** to rebuild with PSRAM enabled and re-flash.
- `PSRAM: PRESENT, N bytes total` → write the size into the hardware report and we're done with PSRAM.
- `PSRAM: not detected` → the chip has no PSRAM. Record this in the report; later milestones must plan for SD-backed scrollback.

- [ ] **Step 5: Re-probe with PSRAM build flags ON**

This step exists because the espressif32 Arduino default for `esp32-s3-devkitc-1` does not enable PSRAM unless you tell it to. We have to try with-PSRAM in case the ADV has it.

Edit `firmware/platformio.ini`, add to `build_flags`:

```ini
  -D BOARD_HAS_PSRAM
  -mfix-esp32-psram-cache-issue
```

And add:

```ini
board_build.arduino.memory_type = qio_opi
```

Rebuild and re-flash:

```bash
pio run -t upload
pio device monitor
```

If the PSRAM line now reports a size — great, leave the flags in. If it still reports "not detected" or the device crashes at boot with PSRAM init errors, **revert this step** (remove the three lines you just added) and record "PSRAM not present" in the hardware report. Either outcome is fine; we just need to know.

- [ ] **Step 6: Update hardware report with SoC + memory findings**

Edit `docs/m0-hardware-report.md` and replace every `?` in the **SoC** and **Memory** sections with the values printed by the probe. Tick the `DESIGN.md §11 Q1` checkbox at the bottom.

- [ ] **Step 7: Commit**

```bash
cd /home/jarod7736/workspace/cardputer-ai
git add firmware/src/probe_soc.h firmware/src/probe_soc.cpp firmware/src/main.cpp firmware/platformio.ini docs/m0-hardware-report.md
git commit -m "feat(m0): SoC + memory probe; record PSRAM verdict in hw report"
```

---

## Task 3: Display probe (ST7789V2, 240×135)

**Files:**
- Create: `firmware/src/probe_display.h`
- Create: `firmware/src/probe_display.cpp`
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Create `firmware/src/probe_display.h`**

```cpp
#pragma once
#include <Arduino.h>

// Initialize the Cardputer-ADV LCD via M5Unified and draw a known test
// pattern: solid red/green/blue/white corners and the text "M0 DISPLAY OK"
// centered. Logs progress to `out`.
//
// Returns true if M5Unified reports the display is present and the test
// pattern was issued without error.
bool probe_display(Stream& out);
```

- [ ] **Step 2: Create `firmware/src/probe_display.cpp`**

```cpp
#include "probe_display.h"

#include <M5Unified.h>

bool probe_display(Stream& out) {
  out.println("-- Display --");

  auto cfg = M5.config();
  M5.begin(cfg);

  // M5Unified picks the board variant from build flags + runtime detection.
  // Print what it thinks we are running on so we can confirm ADV support.
  out.printf("M5Unified board id: %d\n", (int) M5.getBoard());

  if (M5.Display.width() == 0 || M5.Display.height() == 0) {
    out.println("display: NOT INITIALIZED (M5Unified reported 0x0)");
    out.println("display: M5Unified may not yet support Cardputer-ADV.");
    out.println("display: see fallback notes in plan Task 3 step 5");
    return false;
  }

  out.printf("display: %d x %d, color depth %d\n",
             M5.Display.width(), M5.Display.height(), M5.Display.getColorDepth());

  // Test pattern
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.fillRect(0, 0, 20, 20, TFT_RED);
  M5.Display.fillRect(M5.Display.width() - 20, 0, 20, 20, TFT_GREEN);
  M5.Display.fillRect(0, M5.Display.height() - 20, 20, 20, TFT_BLUE);
  M5.Display.fillRect(M5.Display.width() - 20, M5.Display.height() - 20,
                      20, 20, TFT_WHITE);

  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString("M0 DISPLAY OK",
                        M5.Display.width() / 2, M5.Display.height() / 2);

  out.println("display: test pattern drawn");
  out.println();
  return true;
}
```

- [ ] **Step 3: Wire probe into `main.cpp`**

Update `firmware/src/main.cpp` to call `probe_display` after `probe_soc`:

```cpp
#include <Arduino.h>
#include "probe_soc.h"
#include "probe_display.h"

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("==== Cardputer-AI M0 hardware probe ====");

  probe_soc(Serial);
  probe_display(Serial);

  Serial.println("==== probes complete (more to come) ====");
}

void loop() {
  delay(1000);
}
```

- [ ] **Step 4: Build, flash, observe**

```bash
cd /home/jarod7736/workspace/cardputer-ai/firmware
pio run -t upload
pio device monitor
```

Expected on serial: `display: 240 x 135, color depth N`, `display: test pattern drawn`.
Expected on the screen: four colored corner squares (red, green, blue, white) and the text `M0 DISPLAY OK` centered.

- [ ] **Step 5: If display did NOT come up, document the fallback**

If `M5.Display.width()` returned 0, M5Unified does not yet recognize the ADV variant. **Do not implement the fallback in this milestone** — instead, record in `docs/m0-hardware-report.md`:

```
- Display: M5Unified did not initialize (board id reported: <N>).
  Follow-up: implement raw ST7789V2 init via LovyanGFX or TFT_eSPI in M1,
  citing M5Stack's Cardputer-ADV schematic for the SPI pin map.
```

Then mark this task complete and proceed to Task 4 — we have enough to move on.

- [ ] **Step 6: Update hardware report**

Fill in the **Display** section of `docs/m0-hardware-report.md` with what you observed (yes/no for the test pattern, and the board id printed by M5Unified).

- [ ] **Step 7: Commit**

```bash
cd /home/jarod7736/workspace/cardputer-ai
git add firmware/src/probe_display.h firmware/src/probe_display.cpp firmware/src/main.cpp docs/m0-hardware-report.md
git commit -m "feat(m0): display probe via M5Unified; record outcome in hw report"
```

---

## Task 4: Keyboard probe (TCA8418)

**Files:**
- Create: `firmware/src/probe_keyboard.h`
- Create: `firmware/src/probe_keyboard.cpp`
- Modify: `firmware/src/main.cpp`

DESIGN.md §3 calls out the TCA8418 specifically. M5Unified's keyboard abstraction may or may not cover the ADV's TCA8418 directly. This probe takes a layered approach: try M5Unified's keyboard API first, and a one-shot I²C bus scan confirms the chip is at least on the bus.

- [ ] **Step 1: Create `firmware/src/probe_keyboard.h`**

```cpp
#pragma once
#include <Arduino.h>

// Initialize the Cardputer-ADV keyboard and run an interactive probe for
// `duration_ms` milliseconds. Prints each detected keypress to `out`. Also
// performs a one-shot I²C bus scan and logs every responding address.
//
// Returns the number of keypresses captured.
int probe_keyboard(Stream& out, uint32_t duration_ms);
```

- [ ] **Step 2: Create `firmware/src/probe_keyboard.cpp`**

```cpp
#include "probe_keyboard.h"

#include <M5Unified.h>
#include <Wire.h>

static void i2c_scan(Stream& out) {
  out.println("i2c scan:");
  // Cardputer-ADV uses the default Arduino Wire instance for its internal
  // I2C bus by default. If the TCA8418 sits on Wire1 we'll find that out
  // when nothing answers on Wire.
  Wire.begin();
  int found = 0;
  for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      out.printf("  device @ 0x%02X\n", addr);
      if (addr == 0x34) {
        out.println("    ^ TCA8418 default address — keyboard controller");
      }
      ++found;
    }
  }
  if (found == 0) {
    out.println("  (no devices responded on Wire)");
  }
}

int probe_keyboard(Stream& out, uint32_t duration_ms) {
  out.println("-- Keyboard --");
  i2c_scan(out);

  out.printf("Press keys for %lu ms — captured presses will print below:\n",
             (unsigned long) duration_ms);

  int captured = 0;
  uint32_t until = millis() + duration_ms;
  while (millis() < until) {
    M5.update();
    if (M5.Keyboard.isChange()) {
      auto status = M5.Keyboard.keysState();
      for (auto& w : status.word) {
        if (w >= 0x20 && w < 0x7F) {
          out.printf("  key: '%c' (0x%02X)\n", (char) w, (unsigned) w);
        } else {
          out.printf("  key: <0x%02X>\n", (unsigned) w);
        }
        ++captured;
      }
    }
    delay(10);
  }

  out.printf("keyboard: %d presses captured\n", captured);
  out.println();
  return captured;
}
```

- [ ] **Step 3: Wire probe into `main.cpp`**

```cpp
#include <Arduino.h>
#include "probe_soc.h"
#include "probe_display.h"
#include "probe_keyboard.h"

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("==== Cardputer-AI M0 hardware probe ====");

  probe_soc(Serial);
  probe_display(Serial);
  probe_keyboard(Serial, 15000);  // 15-second interactive window

  Serial.println("==== probes complete (more to come) ====");
}

void loop() {
  delay(1000);
}
```

- [ ] **Step 4: Build, flash, observe**

```bash
cd /home/jarod7736/workspace/cardputer-ai/firmware
pio run -t upload
pio device monitor
```

Expected: an `i2c scan:` block listing at least one device. The TCA8418 default address is `0x34`; M5Unified configurations vary, so any address that responds is a positive signal — record what you saw. During the 15-second window, type a few keys; you should see `key: '...'` lines on serial.

- [ ] **Step 5: Update hardware report**

Fill in the **Keyboard** section with:
- The I²C address(es) found
- Whether keypresses were captured by `M5.Keyboard`
- A sample of 4-5 keypresses

If no keys were captured but the I²C scan found a device at 0x34, record: "TCA8418 present at 0x34 but M5Unified keyboard binding inactive — needs direct driver in M1."

- [ ] **Step 6: Commit**

```bash
cd /home/jarod7736/workspace/cardputer-ai
git add firmware/src/probe_keyboard.h firmware/src/probe_keyboard.cpp firmware/src/main.cpp docs/m0-hardware-report.md
git commit -m "feat(m0): keyboard + i2c scan probe; record findings in hw report"
```

---

## Task 5: microSD probe

**Files:**
- Create: `firmware/src/probe_sd.h`
- Create: `firmware/src/probe_sd.cpp`
- Modify: `firmware/src/main.cpp`

DESIGN.md §7 (Provisioning) depends on SD working — this confirms it does and exercises a write/read round-trip.

- [ ] **Step 1: Create `firmware/src/probe_sd.h`**

```cpp
#pragma once
#include <Arduino.h>

// Mount the microSD card via M5Unified's SD pin map, list the root
// directory, then write and read back a small canary file. Prints
// everything to `out`. Returns true on full round-trip success.
bool probe_sd(Stream& out);
```

- [ ] **Step 2: Create `firmware/src/probe_sd.cpp`**

```cpp
#include "probe_sd.h"

#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

static constexpr const char* kCanaryPath = "/m0_canary.txt";
static constexpr const char* kCanaryBody = "cardputer-ai m0 probe canary";

bool probe_sd(Stream& out) {
  out.println("-- microSD --");

  // M5Unified exposes the SD SPI pins for each board. Use those rather
  // than guessing — if the API doesn't expose them, fall back to the
  // M5Stack-published pin map for Cardputer-ADV (confirm in M1).
  SPIClass spi(HSPI);
  int sck  = M5.getPin(m5::pin_name_t::sd_spi_sclk);
  int miso = M5.getPin(m5::pin_name_t::sd_spi_miso);
  int mosi = M5.getPin(m5::pin_name_t::sd_spi_mosi);
  int cs   = M5.getPin(m5::pin_name_t::sd_spi_ss);
  out.printf("sd pins: sck=%d miso=%d mosi=%d cs=%d\n", sck, miso, mosi, cs);

  if (sck < 0 || miso < 0 || mosi < 0 || cs < 0) {
    out.println("sd: M5Unified did not expose SD pins for this board.");
    out.println("sd: cannot mount in M0 — defer to M1 with explicit pin map.");
    return false;
  }

  spi.begin(sck, miso, mosi, cs);
  if (!SD.begin(cs, spi, 20000000)) {
    out.println("sd: mount FAILED");
    return false;
  }

  uint64_t size_mb = SD.cardSize() / (1024ULL * 1024ULL);
  out.printf("sd: mounted, card size = %llu MB\n", (unsigned long long) size_mb);

  out.println("sd: root listing:");
  File root = SD.open("/");
  if (root) {
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      out.printf("  %s  (%lu bytes)\n", f.name(), (unsigned long) f.size());
    }
    root.close();
  }

  // Write canary
  File w = SD.open(kCanaryPath, FILE_WRITE);
  if (!w) { out.println("sd: open for write FAILED"); SD.end(); return false; }
  w.print(kCanaryBody);
  w.close();

  // Read it back
  File r = SD.open(kCanaryPath, FILE_READ);
  if (!r) { out.println("sd: open for read FAILED"); SD.end(); return false; }
  String got = r.readString();
  r.close();

  bool ok = (got == kCanaryBody);
  out.printf("sd: canary round-trip %s (\"%s\")\n",
             ok ? "OK" : "MISMATCH", got.c_str());

  // Clean up canary so we don't litter the card
  SD.remove(kCanaryPath);

  SD.end();
  out.println();
  return ok;
}
```

- [ ] **Step 3: Wire probe into `main.cpp`**

```cpp
#include <Arduino.h>
#include "probe_soc.h"
#include "probe_display.h"
#include "probe_keyboard.h"
#include "probe_sd.h"

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("==== Cardputer-AI M0 hardware probe ====");

  probe_soc(Serial);
  probe_display(Serial);
  probe_keyboard(Serial, 15000);
  probe_sd(Serial);

  Serial.println("==== probes complete ====");
}

void loop() {
  delay(1000);
}
```

- [ ] **Step 4: Insert a FAT32-formatted microSD and re-flash**

```bash
cd /home/jarod7736/workspace/cardputer-ai/firmware
pio run -t upload
pio device monitor
```

Expected: `sd: mounted, card size = <N> MB`, a root listing (even if empty), and `sd: canary round-trip OK`.

If `M5.getPin(...)` returned `-1` for any SD pin, the SD section will skip — record "deferred to M1" in the report.

- [ ] **Step 5: Update hardware report**

Fill in the **microSD** section with mount result, card size, and round-trip outcome.

- [ ] **Step 6: Commit**

```bash
cd /home/jarod7736/workspace/cardputer-ai
git add firmware/src/probe_sd.h firmware/src/probe_sd.cpp firmware/src/main.cpp docs/m0-hardware-report.md
git commit -m "feat(m0): microSD mount + canary round-trip probe"
```

---

## Task 6: ESP-IDF toolchain verification (separate project)

DESIGN.md §10 M0 includes "Verify mbedTLS + lwIP compile clean in ESP-IDF 5.x." We satisfy this with a tiny separate project so it doesn't tangle with the Arduino probe.

**Files:**
- Create: `firmware-idf-probe/platformio.ini`
- Create: `firmware-idf-probe/sdkconfig.defaults`
- Create: `firmware-idf-probe/CMakeLists.txt`
- Create: `firmware-idf-probe/src/CMakeLists.txt`
- Create: `firmware-idf-probe/src/main.c`

- [ ] **Step 1: Create `firmware-idf-probe/platformio.ini`**

```ini
[env:cardputer-adv-idf]
platform = espressif32 @ ^6.7.0
board = esp32-s3-devkitc-1
framework = espidf
monitor_speed = 115200
build_flags =
  -D ARDUINO_USB_CDC_ON_BOOT=1
```

- [ ] **Step 2: Create `firmware-idf-probe/sdkconfig.defaults`**

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_MBEDTLS_HARDWARE_SHA=y
CONFIG_LWIP_TCP=y
```

- [ ] **Step 3: Create `firmware-idf-probe/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(idf_probe)
```

- [ ] **Step 4: Create `firmware-idf-probe/src/CMakeLists.txt`**

```cmake
idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS ""
  REQUIRES mbedtls lwip
)
```

- [ ] **Step 5: Create `firmware-idf-probe/src/main.c`**

```c
// ESP-IDF toolchain verification for Cardputer-AI.
//
// Goal: ensure ESP-IDF 5.x + mbedTLS + lwIP compile cleanly for the
// ESP32-S3 target so M1 can pull in esp_wireguard without surprise.

#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"
#include "mbedtls/version.h"
#include "lwip/init.h"

static const char* TAG = "idf-probe";

void app_main(void) {
    ESP_LOGI(TAG, "ESP-IDF probe booted on %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "mbedTLS version: %s", MBEDTLS_VERSION_STRING);
    ESP_LOGI(TAG, "lwIP version: %s", LWIP_VERSION_STRING);
}
```

- [ ] **Step 6: Build only (do NOT flash — keep the Arduino probe on the device)**

```bash
cd /home/jarod7736/workspace/cardputer-ai/firmware-idf-probe
pio run
```

Expected: build succeeds with output ending in `[SUCCESS]`. The first run downloads ESP-IDF 5.x — this can take 10+ minutes.

- [ ] **Step 7: Update hardware report**

In `docs/m0-hardware-report.md` under **Toolchain**, record:
- PlatformIO Core version: output of `pio --version`
- ESP-IDF 5.x hello-world build clean: yes/no

- [ ] **Step 8: Commit**

```bash
cd /home/jarod7736/workspace/cardputer-ai
git add firmware-idf-probe/ docs/m0-hardware-report.md
git commit -m "feat(m0): ESP-IDF 5.x toolchain verification project"
```

---

## Task 7: Finalize the M0 hardware report

**Files:**
- Modify: `docs/m0-hardware-report.md`

- [ ] **Step 1: Verify every `?` is filled in**

Run: `grep -n '?' /home/jarod7736/workspace/cardputer-ai/docs/m0-hardware-report.md`
Expected: no remaining bare `?` lines (the only `?` allowed is inside intentional text).

- [ ] **Step 2: Fill in date and commit hash**

Replace the `Date captured: TBD` line with today's date.
Replace the `Firmware commit: TBD` line with the output of `git rev-parse --short HEAD`.

- [ ] **Step 3: Confirm the §11 Q1 checkbox is ticked**

The line `- [ ] DESIGN.md §11 Q1 — PSRAM presence` should now read `- [x] DESIGN.md §11 Q1 — PSRAM presence`.

- [ ] **Step 4: Commit**

```bash
cd /home/jarod7736/workspace/cardputer-ai
git add docs/m0-hardware-report.md
git commit -m "docs(m0): finalize hardware report — M0 complete"
```

- [ ] **Step 5: Tag the milestone**

```bash
git tag -a m0-complete -m "M0 hardware bring-up complete"
```

---

## Done criteria

M0 is complete when **all** of these are true:

1. `firmware/` builds and flashes cleanly to the Cardputer-ADV.
2. Serial output during boot includes a clean SoC, display, keyboard, and SD probe block.
3. `firmware-idf-probe/` builds cleanly with ESP-IDF 5.x.
4. `docs/m0-hardware-report.md` has no `?` placeholders, and the PSRAM checkbox is ticked.
5. The repository tag `m0-complete` exists.

At that point DESIGN.md §11 Q1 is answered, and M1 (WireGuard) can begin with full knowledge of the memory budget.
