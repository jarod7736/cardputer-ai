# M3 — Device Chat MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the Cardputer into a pocket Claude. On boot it shows a chat screen; the user types a message, hits Enter, and a Claude response streams onto the LCD over the WG tunnel to lobsterboy's claude-proxy (M2). This is **the first user-visible build** — the moment the project stops being a stack of infrastructure and starts being a device.

**Architecture:** The existing M1 firmware already has Wi-Fi + WG + display init + an LCD status helper. M3 adds:

1. A **TCA8418 keyboard reader** — M0 located the chip on M5Unified's second I²C bus (SDA=8/SCL=9); we need either a Cardputer-specific PlatformIO board profile (which makes `M5.Keyboard` work out of the box) or a small direct driver. Plan-default: board profile. If that's not available in our PlatformIO version, fall back to a tiny TCA8418 driver — Task 1 decides.
2. A **text display layer**: a fixed-size scrollback ring of `Line` rows, word-wrap on push, redraw on change. Lives entirely in internal SRAM since M0 confirmed no PSRAM.
3. An **input editor**: a single-line buffer at the bottom, with cursor, backspace, enter-to-send. Multi-line and editing flourishes are M4+.
4. An **SSE-streaming HTTP client**: POSTs to `http://<proxy>:8420/v1/chat/completions` with the device bearer token, parses OAI-shaped `data: {...}` chunks as they arrive, appends each `delta.content` into the scrollback in real time. Uses Arduino's `WiFiClient` (TCP) and a hand-rolled HTTP/1.1 + SSE reader — `HTTPClient` from arduino-esp32 doesn't stream cleanly.
5. A **conversation state** in memory: a `std::vector<Message>` that grows with each user turn + assistant reply. Bounded by available RAM, not by tokens — we'll add token-aware truncation in a later milestone.
6. A minimal **status bar**: top row of the LCD showing profile label + Wi-Fi/WG state + the last-handshake age. The full status bar in DESIGN.md §6 (battery, time, icons) waits for M4+.

**Tech Stack:**
- Reuses M1's stack: arduino+espidf hybrid, M5Unified, droscy/esp_wireguard, `ESP32Ping`
- Adds nothing new on the dependency side — HTTP + SSE are written by hand against `WiFiClient` so we keep build size tight and parsing surface minimal.

**Out of scope for M3:**
- Profile picker / editor / test affordances (M4)
- Settings screen, Wi-Fi picker (M4 + M5)
- First-boot SD-import provisioning (M5)
- History persistence beyond power cycle (v2)
- Cancel-in-flight (Esc) — nice-to-have but not gating; keep stretch
- Token / cost meter (M6)
- Cursor blink, theming, icons in status bar (M4+)

**Carry-overs from M0/M1/M2:**
- M0 hardware report: **no PSRAM**, all buffers in 320 KB internal SRAM. Keyboard chip lives on the second I²C bus.
- M1: Wi-Fi STA, NTP, droscy/esp_wireguard with init-once contract, full-tunnel routing, supervisor loop with backoff.
- M2: proxy live on lobsterboy with single hard-coded profile `claude-opus`, bearer auth; needs `CARDPUTER_PROXY_LISTEN_HOST=0.0.0.0` set + service restart before the device can reach it from inside the WG subnet.

**Verification style:** Manual on-device verification (this is firmware doing UI). Where we can write a unit test that runs on the host (e.g. for the SSE parser as a pure function over byte slices), we do. Most tasks ship + flash + observe.

---

## Decisions to confirm before starting

Defaults below; flag if any are wrong.

1. **Proxy URL the device uses**: an IP address, not a hostname (the device has no DNS-inside-tunnel yet). Default: lobsterboy's LAN IP — `192.168.1.<TBD>:8420`. Need to fill in. With M1's full-tunnel WG routing, the device sends 0.0.0.0/0 through the UDM; UDM forwards the LAN subnet directly. So lobsterboy's LAN IP on `192.168.1.0/24` is reachable from the Cardputer.
2. **Bearer token**: the random base64 generated during M2 install (`/etc/cardputer-proxy/secrets/device_bearer_token` on lobsterboy). Hard-code into `proxy_secrets.h` (gitignored, like `wg_secrets.h`).
3. **Default profile**: `claude-opus` (the only one M2 ships).
4. **Scrollback size**: 32 lines of up to 40 chars each = ~1.3 KB. Easy fit in SRAM with lots of room to spare. Bigger is fine if we want more history.
5. **System prompt for M3**: none. The device sends raw user/assistant turns. System prompts are a Settings-screen feature (M4).

---

## File structure

Changes layered onto the existing M1 firmware:

```
firmware/
├── platformio.ini                              (maybe modified — board profile, see Task 1)
├── include/
│   ├── wg_secrets.h.example                    (unchanged from M1)
│   └── proxy_secrets.h.example                 (NEW — template for proxy URL + bearer)
└── src/
    ├── main.cpp                                (modified — replaces M1 reachability-proof setup
    │                                            with a chat loop; supervisor still maintains WG)
    ├── wifi_sta.h / wifi_sta.cpp               (unchanged from M1)
    ├── wg_link.h / wg_link.cpp                 (unchanged from M1)
    ├── net_probe.h / net_probe.cpp             (kept, used only on boot for sanity)
    ├── keyboard_input.h / keyboard_input.cpp   (NEW — TCA8418 read; M5.Keyboard if available)
    ├── scrollback.h / scrollback.cpp           (NEW — fixed-size ring of wrapped lines)
    ├── chat_view.h / chat_view.cpp             (NEW — owns LCD layout: status bar + scrollback + input)
    ├── http_sse.h / http_sse.cpp               (NEW — streaming POST + line-by-line SSE reader)
    ├── chat_client.h / chat_client.cpp         (NEW — builds OAI request from convo state,
    │                                             calls http_sse, dispatches text deltas)
    └── proxy_secrets.h                          (NEW — gitignored, hand-edited)

docs/
└── m3-device-chat-report.md                    (NEW — captured RAM/flash numbers, gotchas, screenshots if any)

.gitignore                                       (modified — add proxy_secrets.h)
```

**Boundaries between modules:**
- `scrollback` owns text only — knows nothing about pixels.
- `chat_view` owns the LCD — knows how scrollback is drawn but doesn't store text itself.
- `http_sse` is transport-only — generic enough to POST anything; doesn't know about chat shapes.
- `chat_client` is glue: it knows the OAI request body shape, calls `http_sse`, and gives the caller a callback per text delta.
- `keyboard_input` returns characters + special keys (Enter, Backspace, escape codes for arrows/PgUp/PgDn); the caller decides what they mean.

---

## Task 0: Repo prep — proxy_secrets template, gitignore

**Files:**
- Create: `firmware/include/proxy_secrets.h.example`
- Modify: `.gitignore`

- [ ] **Step 1: Create `firmware/include/proxy_secrets.h.example`**

```cpp
// Template for firmware/include/proxy_secrets.h — copy and fill in. The
// real file is .gitignore'd. M5 provisioning moves these into NVS.

#pragma once

#include <cstdint>

namespace proxy_secrets {

// Where the cardputer-proxy lives. Reachable from the device via the
// WG tunnel (full-tunnel routes 0.0.0.0/0 through the UDM, which
// forwards 192.168.1.0/24 directly).
inline constexpr const char* kHost = "192.168.1.<TBD>";
inline constexpr uint16_t    kPort = 8420;

// Bearer token from /etc/cardputer-proxy/secrets/device_bearer_token
// on lobsterboy (generated during M2 install).
inline constexpr const char* kBearerToken = "<PASTE_BEARER_TOKEN_HERE>";

// Profile id known to the proxy. M2 ships exactly one.
inline constexpr const char* kProfileId = "claude-opus";

}  // namespace proxy_secrets
```

- [ ] **Step 2: Append `proxy_secrets.h` to `.gitignore`**

After the existing `firmware/include/wg_secrets.h` line, add:

```gitignore
firmware/include/proxy_secrets.h
```

- [ ] **Step 3: Commit**

```bash
git add firmware/include/proxy_secrets.h.example .gitignore
git commit -m "feat(m3): proxy_secrets.h template + gitignore for the real file"
```

---

## Task 1: Board profile + keyboard wiring

M0 noted that M5Unified runtime-detects `board_M5CardputerADV` (board id 24), but when we compile against `esp32-s3-devkitc-1` the `M5.Keyboard` binding stays inactive. We have two paths; this task picks one based on what the installed PlatformIO actually supports.

**Files:**
- Possibly modified: `firmware/platformio.ini`

- [ ] **Step 1: Probe for a Cardputer board profile**

```bash
cd firmware
pio boards 2>/dev/null | grep -i cardputer
```

- If output shows `m5stack-cardputer` (or `…adv`), switch `platformio.ini`:
  ```ini
  board = m5stack-cardputer        # or the ADV-specific id if present
  ```
- If nothing shows: keep `esp32-s3-devkitc-1` and we'll write a direct TCA8418 driver in Task 2.

- [ ] **Step 2: Rebuild with the new board profile (if changed)**

```bash
rm -f sdkconfig.cardputer-adv
pio run
```
Expected: build succeeds. Note: M5Unified will runtime-detect the ADV variant regardless of the upstream profile name — the profile affects which compile-time bindings (`M5.Keyboard`) are enabled, not which hardware we talk to.

- [ ] **Step 3: Smoke-test that `M5.Keyboard` exists in this build**

Add a one-line probe to the top of `setup()` temporarily:

```cpp
Serial.printf("keyboard binding present: %d\n",
              (int) (sizeof(decltype(&M5.Keyboard)) > 0));
```

(This is a compile-time check — if the field doesn't exist, the build fails. Pure runtime detection comes in Task 2.)

If the build fails because `M5.Keyboard` isn't defined: revert the board change, keep `esp32-s3-devkitc-1`, and proceed to Task 2's "Path B" branch.

- [ ] **Step 4: Commit the choice**

```bash
git add firmware/platformio.ini
git commit -m "feat(m3): switch to Cardputer board profile (enables M5.Keyboard)"
```

Or, if we stayed on the dev-kit profile:

```bash
git commit -m "chore(m3): document board profile decision in M3 report"
```
(commit is a no-op; instead note "no upstream Cardputer profile available; proceeding with direct TCA8418 driver" in `docs/m3-device-chat-report.md`).

---

## Task 2: keyboard_input module

Two implementation paths depending on Task 1's outcome. The header is the same either way; only the .cpp differs.

**Files:**
- Create: `firmware/src/keyboard_input.h`
- Create: `firmware/src/keyboard_input.cpp`

- [ ] **Step 1: Create `firmware/src/keyboard_input.h`**

```cpp
#pragma once
#include <Arduino.h>

namespace keyboard_input {

// Special key codes — returned in addition to printable ASCII.
// Anything >= 256 is a special; anything < 256 is a Unicode codepoint
// (we only care about ASCII for M3).
constexpr int KEY_NONE      = -1;
constexpr int KEY_ENTER     = '\r';
constexpr int KEY_BACKSPACE = '\b';
constexpr int KEY_ESCAPE    = 27;
constexpr int KEY_TAB       = '\t';

// Initialize the keyboard driver. Must be called after M5.begin().
void begin();

// Non-blocking poll: returns the next pending key code, or KEY_NONE if
// nothing is queued. Call this every frame from the main loop.
int poll();

}  // namespace keyboard_input
```

- [ ] **Step 2a (PATH A — M5.Keyboard available): implement via M5Unified**

If Task 1 left us with a Cardputer board profile that exposes `M5.Keyboard`:

```cpp
#include "keyboard_input.h"

#include <M5Unified.h>

namespace keyboard_input {

void begin() {
  // M5Unified initializes the keyboard in M5.begin(); nothing to do.
}

int poll() {
  M5.update();
  if (!M5.Keyboard.isChange()) return KEY_NONE;
  auto state = M5.Keyboard.keysState();
  for (auto& w : state.word) {
    // Only return the first new keypress this poll; queue the rest by
    // returning early — the caller polls every frame so they all get out.
    if (w == 0) continue;
    switch (w) {
      case 0x08: return KEY_BACKSPACE;
      case 0x0A:
      case 0x0D: return KEY_ENTER;
      case 0x1B: return KEY_ESCAPE;
      case 0x09: return KEY_TAB;
    }
    if (w >= 0x20 && w < 0x7F) return (int) w;
  }
  return KEY_NONE;
}

}  // namespace keyboard_input
```

- [ ] **Step 2b (PATH B — direct TCA8418 driver): implement against the second I²C bus**

If we stayed on the dev-kit profile, write a minimal TCA8418 driver. M5Stack's Cardputer-ADV schematic documents:
- I²C: SDA=GPIO8, SCL=GPIO9 (the second bus M5Unified initializes — see M0 report)
- TCA8418 address: `0x34`
- INT pin: typically GPIO13 (verify against schematic before relying on it)

```cpp
#include "keyboard_input.h"

#include <Wire.h>

// TCA8418 datasheet register map (subset).
constexpr uint8_t TCA8418_ADDR             = 0x34;
constexpr uint8_t REG_CFG                  = 0x01;
constexpr uint8_t REG_INT_STAT             = 0x02;
constexpr uint8_t REG_KEY_LCK_EC           = 0x03;  // keylock + event count
constexpr uint8_t REG_KEY_EVENT_A          = 0x04;
constexpr uint8_t REG_KP_GPIO_1            = 0x1D;
constexpr uint8_t REG_KP_GPIO_2            = 0x1E;
constexpr uint8_t REG_KP_GPIO_3            = 0x1F;

static TwoWire s_wire(1);  // second I2C controller; M5Unified uses #0 internally

static void reg_write(uint8_t reg, uint8_t val) {
  s_wire.beginTransmission(TCA8418_ADDR);
  s_wire.write(reg);
  s_wire.write(val);
  s_wire.endTransmission();
}

static uint8_t reg_read(uint8_t reg) {
  s_wire.beginTransmission(TCA8418_ADDR);
  s_wire.write(reg);
  s_wire.endTransmission(false);
  s_wire.requestFrom((uint8_t) TCA8418_ADDR, (uint8_t) 1);
  return s_wire.available() ? s_wire.read() : 0;
}

// Cardputer keymap: TCA8418 reports a "key code" 1-80 for the matrix
// position. We translate to ASCII via a lookup table that mirrors the
// QWERTY layout printed on the keycaps. Fill this in from the schematic
// / from observed press codes during bring-up (see Step 3 below).
static const uint8_t kKeymap[81] = {
  /* index 0 = "no event"; codes 1-80 map to matrix positions. */
  0,  // 0
  // ... fill in by observation (see Step 3)
};

namespace keyboard_input {

void begin() {
  s_wire.begin(8 /*SDA*/, 9 /*SCL*/, 100000);
  reg_write(REG_CFG, 0x01);                 // KE_IEN: enable key-event interrupts (we poll, but it sets the matrix to scan)
  reg_write(REG_KP_GPIO_1, 0xFF);           // rows 0-7 in keypad scan mode
  reg_write(REG_KP_GPIO_2, 0xFF);           // rows 8-15 (TCA8418 has 10 rows; spec covers up to 8 here)
  reg_write(REG_KP_GPIO_3, 0xFF);           // cols 0-9
}

int poll() {
  if ((reg_read(REG_KEY_LCK_EC) & 0x0F) == 0) return KEY_NONE;
  uint8_t evt = reg_read(REG_KEY_EVENT_A);
  bool pressed = (evt & 0x80) != 0;
  uint8_t code = evt & 0x7F;
  if (!pressed || code == 0 || code >= sizeof(kKeymap)) return KEY_NONE;
  uint8_t ch = kKeymap[code];
  switch (ch) {
    case 0x08: return KEY_BACKSPACE;
    case 0x0A: case 0x0D: return KEY_ENTER;
    case 0x1B: return KEY_ESCAPE;
    case 0x09: return KEY_TAB;
    default:
      return (ch >= 0x20 && ch < 0x7F) ? (int) ch : KEY_NONE;
  }
}

}  // namespace keyboard_input
```

- [ ] **Step 3: One-shot keymap discovery**

Whichever path we're on, before the keymap table is reliable we need to know what code each physical key emits. Add a temporary debug branch in `setup()` that prints raw codes for 30 seconds:

```cpp
Serial.println("keyboard discovery — press each key once, in row order:");
uint32_t deadline = millis() + 30000;
while (millis() < deadline) {
  int k = keyboard_input::poll();
  if (k != keyboard_input::KEY_NONE) {
    Serial.printf("  raw=%d ('%c')\n", k, (k >= 0x20 && k < 0x7F) ? (char) k : '?');
  }
  delay(20);
}
```

Press each key once, top-left to bottom-right. Use the printed values to fill in `kKeymap` (Path B only — Path A returns ASCII directly).

- [ ] **Step 4: Wire `keyboard_input::begin()` into `setup()`**

Add after `M5.begin()`:
```cpp
keyboard_input::begin();
```

- [ ] **Step 5: Flash and verify**

Type a sentence in serial monitor — every printable character should appear in the discovery output. Backspace, Enter should map to the special codes.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/keyboard_input.h firmware/src/keyboard_input.cpp
git commit -m "feat(m3): keyboard input module (path A or B per Task 1)"
```

Remove the discovery block from `setup()` before committing — keep the file clean.

---

## Task 3: scrollback ring

A ring buffer of wrapped text lines. No pixels, no fonts — just text.

**Files:**
- Create: `firmware/src/scrollback.h`
- Create: `firmware/src/scrollback.cpp`

- [ ] **Step 1: Create `firmware/src/scrollback.h`**

```cpp
#pragma once
#include <Arduino.h>

namespace scrollback {

// One on-screen line. Color decides how chat_view paints it.
enum class LineKind : uint8_t {
  kSystem,    // muted, e.g. "wifi connected"
  kUserTurn,  // "you> ..."
  kAssistant, // "claude> ..."
  kError,     // red
};

struct Line {
  LineKind kind;
  // Heap-free fixed-size buffer keeps allocation off the path. 64 is
  // enough for our 240-px-wide screen at the smallest legible font.
  char     text[64];
  uint8_t  len;
};

// Initialize the ring. Capacity is fixed at compile time (see kCapacity).
void begin();

// Number of currently-stored lines (≤ capacity).
size_t size();

// 0 = oldest still-present line, size()-1 = newest. Bounds-checked.
const Line& at(size_t idx);

// Push raw text wrapped to `width` chars. Long lines are split; the
// first chunk inherits `kind`, continuation chunks become kSystem-style
// continuations (no role prefix). Returns the number of lines added.
size_t push(LineKind kind, const char* text, uint8_t width);

// Append text to the most recent line (used for streaming token deltas).
// If appending would exceed `width`, wraps to a new continuation line.
// Returns the number of lines added (0 or 1).
size_t append_to_last(const char* text, uint8_t width);

// Wipe everything. New session.
void clear();

}  // namespace scrollback
```

- [ ] **Step 2: Create `firmware/src/scrollback.cpp`**

```cpp
#include "scrollback.h"

#include <cstring>

namespace scrollback {

constexpr size_t kCapacity = 32;

static Line   s_lines[kCapacity];
static size_t s_head = 0;  // index of next slot to write
static size_t s_size = 0;

static Line& slot(size_t i) {
  return s_lines[(s_head + kCapacity - s_size + i) % kCapacity];
}

static void push_one(LineKind kind, const char* s, uint8_t len) {
  Line& dst = s_lines[s_head];
  dst.kind = kind;
  dst.len  = len > (uint8_t)(sizeof(dst.text) - 1) ? (uint8_t)(sizeof(dst.text) - 1) : len;
  std::memcpy(dst.text, s, dst.len);
  dst.text[dst.len] = '\0';
  s_head = (s_head + 1) % kCapacity;
  if (s_size < kCapacity) ++s_size;
}

void begin() {
  s_head = 0;
  s_size = 0;
  std::memset(s_lines, 0, sizeof(s_lines));
}

size_t size() { return s_size; }

const Line& at(size_t idx) { return slot(idx); }

size_t push(LineKind kind, const char* text, uint8_t width) {
  if (width >= sizeof(Line::text)) width = sizeof(Line::text) - 1;
  size_t added = 0;
  const char* p = text;
  bool first = true;
  while (*p) {
    size_t remaining = std::strlen(p);
    size_t take = remaining > width ? width : remaining;
    // Try to break on whitespace if we'd otherwise cut a word in two.
    if (take < remaining) {
      size_t b = take;
      while (b > 1 && p[b] != ' ' && p[b-1] != ' ') --b;
      if (b > width / 2) take = b;
    }
    push_one(first ? kind : LineKind::kSystem, p, (uint8_t) take);
    ++added;
    first = false;
    p += take;
    while (*p == ' ') ++p;  // eat leading spaces on continuation lines
  }
  if (added == 0) {
    push_one(kind, "", 0);
    ++added;
  }
  return added;
}

size_t append_to_last(const char* text, uint8_t width) {
  if (s_size == 0) {
    return push(LineKind::kSystem, text, width);
  }
  if (width >= sizeof(Line::text)) width = sizeof(Line::text) - 1;
  // Walk index of last line.
  Line& last = slot(s_size - 1);
  size_t room = width - last.len;
  size_t take = std::strlen(text);
  if (take <= room) {
    std::memcpy(last.text + last.len, text, take);
    last.len += (uint8_t) take;
    last.text[last.len] = '\0';
    return 0;
  }
  // Doesn't fit — fill the rest of the current line, then push continuation(s).
  if (room > 0) {
    std::memcpy(last.text + last.len, text, room);
    last.len = width;
    last.text[width] = '\0';
  }
  return push(LineKind::kSystem, text + room, width);
}

void clear() {
  begin();
}

}  // namespace scrollback
```

- [ ] **Step 3: Build to verify it compiles**

```bash
pio run
```
Expected: build succeeds (this module has no dependencies on Wi-Fi/WG).

- [ ] **Step 4: Commit**

```bash
git add firmware/src/scrollback.h firmware/src/scrollback.cpp
git commit -m "feat(m3): fixed-capacity scrollback ring with word-wrap"
```

---

## Task 4: chat_view — LCD layout

Now we paint. Status bar at the top, scrollback in the middle, input line at the bottom. Cursor blinks in the input area. All redraw goes through this module.

**Files:**
- Create: `firmware/src/chat_view.h`
- Create: `firmware/src/chat_view.cpp`

- [ ] **Step 1: Create `firmware/src/chat_view.h`**

```cpp
#pragma once
#include <Arduino.h>
#include "scrollback.h"

namespace chat_view {

// Status the top bar shows.
enum class Status : uint8_t {
  kBoot,
  kWifiConnecting,
  kWifiUp,
  kWgConnecting,
  kReady,
  kSending,
  kError,
};

void begin();

// Replace the status text shown in the top bar.
void set_status(Status s, const char* detail = nullptr);

// Replace the input line text and cursor position.
void set_input(const char* buf, size_t cursor);

// Redraw whatever changed. Call once per frame.
void render();

// Pixel/char dimensions exposed so other modules size buffers consistently.
uint8_t scrollback_width_chars();   // how wide a text line is (chars)
uint8_t scrollback_height_lines();  // how many lines fit on screen
}
```

- [ ] **Step 2: Create `firmware/src/chat_view.cpp`**

```cpp
#include "chat_view.h"

#include <M5Unified.h>

namespace chat_view {

static Status     s_status = Status::kBoot;
static char       s_status_detail[40] = "";
static char       s_input_buf[80] = "";
static size_t     s_cursor = 0;
static bool       s_dirty  = true;

// Font: text size 1 (6×8 px) on a 240×135 LCD = 40 cols × 16 rows. The
// top row is status; the bottom row is input; the middle 14 are scroll.
static constexpr uint8_t kCharW = 6;
static constexpr uint8_t kCharH = 8;
static constexpr uint8_t kCols  = 40;
static constexpr uint8_t kRowsTotal = 16;
static constexpr uint8_t kStatusRow = 0;
static constexpr uint8_t kInputRow  = kRowsTotal - 1;
static constexpr uint8_t kScrollbackRows = kRowsTotal - 2;

uint8_t scrollback_width_chars()  { return kCols; }
uint8_t scrollback_height_lines() { return kScrollbackRows; }

static uint16_t color_for_kind(scrollback::LineKind k) {
  switch (k) {
    case scrollback::LineKind::kAssistant: return TFT_GREEN;
    case scrollback::LineKind::kUserTurn:  return TFT_CYAN;
    case scrollback::LineKind::kError:     return TFT_RED;
    case scrollback::LineKind::kSystem:
    default:                                return TFT_LIGHTGREY;
  }
}

static const char* label_for_status(Status s) {
  switch (s) {
    case Status::kBoot:              return "boot";
    case Status::kWifiConnecting:    return "wifi...";
    case Status::kWifiUp:            return "wifi";
    case Status::kWgConnecting:      return "wg...";
    case Status::kReady:             return "ready";
    case Status::kSending:           return "sending";
    case Status::kError:             return "ERROR";
  }
  return "?";
}

void begin() {
  M5.Display.setTextSize(1);
  M5.Display.fillScreen(TFT_BLACK);
  s_dirty = true;
  render();
}

void set_status(Status s, const char* detail) {
  s_status = s;
  if (detail) {
    std::strncpy(s_status_detail, detail, sizeof(s_status_detail) - 1);
    s_status_detail[sizeof(s_status_detail) - 1] = '\0';
  } else {
    s_status_detail[0] = '\0';
  }
  s_dirty = true;
}

void set_input(const char* buf, size_t cursor) {
  std::strncpy(s_input_buf, buf, sizeof(s_input_buf) - 1);
  s_input_buf[sizeof(s_input_buf) - 1] = '\0';
  s_cursor = cursor;
  s_dirty = true;
}

static void draw_status_row() {
  M5.Display.fillRect(0, kStatusRow * kCharH, M5.Display.width(), kCharH, TFT_NAVY);
  M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Display.setCursor(0, kStatusRow * kCharH);
  M5.Display.printf(" %s %s", label_for_status(s_status), s_status_detail);
}

static void draw_scrollback() {
  uint16_t y = kCharH;  // start after status bar
  M5.Display.fillRect(0, y, M5.Display.width(), kScrollbackRows * kCharH, TFT_BLACK);
  size_t n = scrollback::size();
  size_t start = n > kScrollbackRows ? n - kScrollbackRows : 0;
  for (size_t i = start; i < n; ++i, y += kCharH) {
    const auto& L = scrollback::at(i);
    M5.Display.setTextColor(color_for_kind(L.kind), TFT_BLACK);
    M5.Display.setCursor(0, y);
    M5.Display.print(L.text);
  }
}

static void draw_input_row() {
  uint16_t y = kInputRow * kCharH;
  M5.Display.fillRect(0, y, M5.Display.width(), kCharH, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(0, y);
  M5.Display.print("> ");
  M5.Display.print(s_input_buf);
  // Cursor as an underline at the current column (cursor in chars including the "> " prompt).
  uint16_t cur_x = (uint16_t)(s_cursor + 2) * kCharW;
  if (cur_x < M5.Display.width()) {
    M5.Display.fillRect(cur_x, y + kCharH - 1, kCharW - 1, 1, TFT_WHITE);
  }
}

void render() {
  if (!s_dirty) return;
  draw_status_row();
  draw_scrollback();
  draw_input_row();
  s_dirty = false;
}

}  // namespace chat_view
```

- [ ] **Step 3: Build**

```bash
pio run
```

- [ ] **Step 4: Commit**

```bash
git add firmware/src/chat_view.h firmware/src/chat_view.cpp
git commit -m "feat(m3): chat_view module — status bar + scrollback + input on the LCD"
```

---

## Task 5: http_sse — streaming POST + SSE reader

A small async-ish HTTP/1.1 + SSE client over `WiFiClient`. Synchronous, but it calls a chunk callback as bytes arrive so the UI can paint tokens in near-real-time. Lives on top of the WG tunnel (which is the default route).

**Files:**
- Create: `firmware/src/http_sse.h`
- Create: `firmware/src/http_sse.cpp`

- [ ] **Step 1: Create `firmware/src/http_sse.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <functional>

namespace http_sse {

// Called once per "data: ..." line. Payload is null-terminated and points
// into a buffer owned by the reader — copy if you need to keep it.
using OnEvent = std::function<void(const char* data_line)>;

struct Result {
  int  status_code;          // 0 on transport error
  bool ok;
  char error[64];
};

// POST `body` to `host:port` `path` with the given bearer token. Reads
// the response as a Server-Sent Events stream and calls `on_event` for
// each "data: ..." line. Returns when the connection closes, an event
// of "[DONE]" arrives, or `timeout_ms` elapses with no progress.
Result post_sse(
    const char* host,
    uint16_t    port,
    const char* path,
    const char* bearer_token,
    const char* body,
    OnEvent     on_event,
    uint32_t    overall_timeout_ms = 60000);

}  // namespace http_sse
```

- [ ] **Step 2: Create `firmware/src/http_sse.cpp`**

```cpp
#include "http_sse.h"

#include <WiFi.h>
#include <WiFiClient.h>

namespace http_sse {

static bool read_line(WiFiClient& cx, char* out, size_t cap, uint32_t timeout_ms) {
  size_t n = 0;
  uint32_t deadline = millis() + timeout_ms;
  while (n + 1 < cap) {
    if (millis() > deadline) return false;
    if (!cx.connected() && cx.available() == 0) return n > 0;
    if (cx.available() == 0) { delay(2); continue; }
    int c = cx.read();
    if (c < 0) continue;
    if (c == '\r') continue;
    if (c == '\n') { out[n] = '\0'; return true; }
    out[n++] = (char) c;
  }
  out[n] = '\0';
  return true;
}

Result post_sse(const char* host, uint16_t port, const char* path,
                const char* bearer_token, const char* body,
                OnEvent on_event, uint32_t overall_timeout_ms) {
  Result r{0, false, ""};

  WiFiClient cx;
  cx.setTimeout(10000);
  if (!cx.connect(host, port)) {
    std::snprintf(r.error, sizeof(r.error), "connect %s:%u failed", host, (unsigned) port);
    return r;
  }

  size_t blen = std::strlen(body);
  cx.printf(
      "POST %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Authorization: Bearer %s\r\n"
      "Content-Type: application/json\r\n"
      "Accept: text/event-stream\r\n"
      "Content-Length: %u\r\n"
      "Connection: close\r\n\r\n",
      path, host, bearer_token, (unsigned) blen);
  cx.write((const uint8_t*) body, blen);

  // Parse the status line.
  char line[512];
  if (!read_line(cx, line, sizeof(line), 10000)) {
    std::snprintf(r.error, sizeof(r.error), "no status line");
    cx.stop();
    return r;
  }
  // "HTTP/1.1 200 OK"
  int sp = 0;
  for (; line[sp] && line[sp] != ' '; ++sp) {}
  r.status_code = std::atoi(line + sp);

  // Drain headers until blank line.
  while (read_line(cx, line, sizeof(line), 10000)) {
    if (line[0] == '\0') break;
  }

  if (r.status_code < 200 || r.status_code >= 300) {
    std::snprintf(r.error, sizeof(r.error), "http %d", r.status_code);
    cx.stop();
    return r;
  }

  // Read SSE lines until [DONE] or connection close.
  uint32_t deadline = millis() + overall_timeout_ms;
  while (millis() < deadline) {
    if (!read_line(cx, line, sizeof(line), 5000)) break;
    if (line[0] == '\0') continue;       // event separator
    if (std::strncmp(line, "data: ", 6) != 0) continue;
    const char* payload = line + 6;
    if (std::strcmp(payload, "[DONE]") == 0) {
      r.ok = true;
      break;
    }
    on_event(payload);
  }

  cx.stop();
  if (!r.ok && r.status_code == 200) {
    // Stream ended without [DONE] but with a successful status —
    // treat as ok (some servers don't send [DONE]). Don't fail hard.
    r.ok = true;
  }
  return r;
}

}  // namespace http_sse
```

- [ ] **Step 3: Build**

```bash
pio run
```

- [ ] **Step 4: Commit**

```bash
git add firmware/src/http_sse.h firmware/src/http_sse.cpp
git commit -m "feat(m3): http_sse — streaming POST + line-by-line SSE reader"
```

---

## Task 6: chat_client — turn the SSE deltas into screen updates

**Files:**
- Create: `firmware/src/chat_client.h`
- Create: `firmware/src/chat_client.cpp`

- [ ] **Step 1: Create `firmware/src/chat_client.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <vector>

namespace chat_client {

struct Message {
  String role;     // "user" or "assistant"
  String content;
};

struct SendResult {
  bool ok;
  char error[80];
};

// Initialize from proxy_secrets.h values.
void begin();

// Append a user message to the conversation, send the full conversation
// to the proxy, accumulate the assistant response into `out`. The
// per-delta `on_delta` callback fires for each chunk of new text so the
// UI can paint as it arrives.
SendResult send(const String& user_text,
                std::function<void(const String&)> on_delta,
                String& assistant_out);

// Read-only view of the conversation (for redraw after scroll, future).
const std::vector<Message>& history();

// Wipe the conversation (new chat).
void reset();

}  // namespace chat_client
```

- [ ] **Step 2: Create `firmware/src/chat_client.cpp`**

```cpp
#include "chat_client.h"

#include <ArduinoJson.h>
#include "http_sse.h"
#include "proxy_secrets.h"

namespace chat_client {

static std::vector<Message> s_history;

void begin() {
  s_history.clear();
  s_history.reserve(16);
}

const std::vector<Message>& history() { return s_history; }

void reset() { s_history.clear(); }

static String build_body(const std::vector<Message>& msgs) {
  // Hand-build the JSON to keep the dep light if ArduinoJson isn't in
  // lib_deps yet. If it is, this would be cleaner with the library API.
  String out;
  out.reserve(256 + msgs.size() * 64);
  out += "{\"profile_id\":\"";
  out += proxy_secrets::kProfileId;
  out += "\",\"stream\":true,\"messages\":[";
  for (size_t i = 0; i < msgs.size(); ++i) {
    if (i) out += ',';
    out += "{\"role\":\"";
    out += msgs[i].role;
    out += "\",\"content\":";
    // Naive JSON-escape: the device only sends what the user typed
    // plus assistant text from a previous turn. Escape just the
    // characters that break a JSON string.
    out += '"';
    for (size_t j = 0; j < msgs[i].content.length(); ++j) {
      char c = msgs[i].content[j];
      switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
          if ((uint8_t) c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned) c);
            out += buf;
          } else {
            out += c;
          }
      }
    }
    out += "\"}";
  }
  out += "]}";
  return out;
}

SendResult send(const String& user_text,
                std::function<void(const String&)> on_delta,
                String& assistant_out) {
  SendResult r{false, ""};
  s_history.push_back({String("user"), user_text});

  const String body = build_body(s_history);
  assistant_out = "";

  http_sse::Result hr = http_sse::post_sse(
      proxy_secrets::kHost, proxy_secrets::kPort,
      "/v1/chat/completions",
      proxy_secrets::kBearerToken,
      body.c_str(),
      [&](const char* line) {
        // Parse the OAI chunk JSON. We only care about
        // choices[0].delta.content (text). Hand-parse to avoid pulling
        // ArduinoJson onto a stream path; the field is shallow.
        const char* p = std::strstr(line, "\"content\":");
        if (!p) return;
        p += 10;                       // length of "\"content\":"
        if (*p != '"') return;
        ++p;                           // skip opening quote
        String delta;
        delta.reserve(32);
        while (*p && *p != '"') {
          if (*p == '\\' && p[1]) {
            ++p;
            switch (*p) {
              case 'n':  delta += '\n'; break;
              case 'r':  delta += '\r'; break;
              case 't':  delta += '\t'; break;
              case '"':  delta += '"';  break;
              case '\\': delta += '\\'; break;
              case 'u': {
                // \uXXXX → reject anything non-ASCII for M3 simplicity.
                p += 4;
                delta += '?';
                break;
              }
              default: delta += *p;
            }
            ++p;
          } else {
            delta += *p++;
          }
        }
        if (delta.length() > 0) {
          assistant_out += delta;
          on_delta(delta);
        }
      });

  if (!hr.ok) {
    std::snprintf(r.error, sizeof(r.error), "proxy: %s", hr.error);
    // Don't keep a half-sent user turn in history if it failed.
    s_history.pop_back();
    return r;
  }

  s_history.push_back({String("assistant"), assistant_out});
  r.ok = true;
  return r;
}

}  // namespace chat_client
```

- [ ] **Step 3: Build**

```bash
pio run
```
Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/chat_client.h firmware/src/chat_client.cpp
git commit -m "feat(m3): chat_client — builds OAI request, streams deltas back to the UI"
```

---

## Task 7: wire it all together in main.cpp

The existing `main.cpp` (from M1) does: Wi-Fi → NTP → WG → reachability proof → supervisor. For M3 we replace the reachability proof + supervisor's "M1 SUCCESS" path with a chat loop.

**Files:**
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Replace `main.cpp`**

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <M5Unified.h>
#include <time.h>
#include <sys/time.h>

#include "wifi_sta.h"
#include "wg_link.h"
#include "chat_view.h"
#include "chat_client.h"
#include "keyboard_input.h"
#include "scrollback.h"
#include "wg_secrets.h"
#include "proxy_secrets.h"

static wg_link::Config wg_cfg() {
  return wg_link::Config{
    .device_private_key      = wg_secrets::kDevicePrivateKey,
    .peer_public_key         = wg_secrets::kPeerPublicKey,
    .peer_endpoint_host      = wg_secrets::kPeerEndpointHost,
    .peer_endpoint_port      = wg_secrets::kPeerEndpointPort,
    .device_address          = wg_secrets::kDeviceAddress,
    .device_netmask          = wg_secrets::kDeviceNetmask,
    .allowed_ip_cidr         = wg_secrets::kAllowedIPCIDR,
    .persistent_keepalive_s  = 25,
    .set_as_default_route    = true,
  };
}

static String s_input;
static size_t s_cursor = 0;

static void redraw() {
  chat_view::set_input(s_input.c_str(), s_cursor);
  chat_view::render();
}

static void send_current_input() {
  if (s_input.length() == 0) return;

  // Echo the user turn into scrollback first, in their color.
  String prefixed = String("you> ") + s_input;
  scrollback::push(scrollback::LineKind::kUserTurn,
                   prefixed.c_str(),
                   chat_view::scrollback_width_chars());
  scrollback::push(scrollback::LineKind::kAssistant,
                   "claude> ",
                   chat_view::scrollback_width_chars());

  chat_view::set_status(chat_view::Status::kSending);
  redraw();

  String reply;
  auto result = chat_client::send(
      s_input,
      /*on_delta=*/[](const String& d) {
        scrollback::append_to_last(d.c_str(),
                                   chat_view::scrollback_width_chars());
        chat_view::render();
      },
      reply);

  if (!result.ok) {
    scrollback::push(scrollback::LineKind::kError, result.error,
                     chat_view::scrollback_width_chars());
    chat_view::set_status(chat_view::Status::kError, result.error);
  } else {
    chat_view::set_status(chat_view::Status::kReady);
  }

  s_input = "";
  s_cursor = 0;
  redraw();
}

void setup() {
  auto cfg5 = M5.config();
  M5.begin(cfg5);

  Serial.begin(115200);
  uint32_t deadline = millis() + 5000;
  while (!Serial && millis() < deadline) delay(50);

  chat_view::begin();
  scrollback::begin();
  keyboard_input::begin();
  chat_client::begin();

  chat_view::set_status(chat_view::Status::kWifiConnecting);
  redraw();

  if (!wifi_sta::connect(wg_secrets::kWifiSSID, wg_secrets::kWifiPassword,
                         Serial)) {
    chat_view::set_status(chat_view::Status::kError, "wifi");
    redraw();
    return;
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  uint32_t ntp_deadline = millis() + 15000;
  while (time(nullptr) < 1700000000 && millis() < ntp_deadline) delay(250);

  chat_view::set_status(chat_view::Status::kWgConnecting);
  redraw();

  auto cfg = wg_cfg();
  if (!wg_link::start(cfg, Serial)) {
    chat_view::set_status(chat_view::Status::kError, "wg");
    redraw();
    return;
  }

  scrollback::push(scrollback::LineKind::kSystem,
                   "tunnel up. type and hit enter.",
                   chat_view::scrollback_width_chars());
  chat_view::set_status(chat_view::Status::kReady);
  redraw();
}

void loop() {
  // Supervisor: keep Wi-Fi + WG alive (re-using M1 semantics).
  if (!wifi_sta::is_up()) {
    chat_view::set_status(chat_view::Status::kWifiConnecting);
    redraw();
    wg_link::stop();
    wifi_sta::connect(wg_secrets::kWifiSSID, wg_secrets::kWifiPassword,
                      Serial, 20000);
  } else if (!wg_link::is_up()) {
    chat_view::set_status(chat_view::Status::kWgConnecting);
    redraw();
    auto cfg = wg_cfg();
    wg_link::start(cfg, Serial);
    chat_view::set_status(chat_view::Status::kReady);
    redraw();
  }

  // Keyboard input.
  int k = keyboard_input::poll();
  if (k == keyboard_input::KEY_ENTER) {
    send_current_input();
  } else if (k == keyboard_input::KEY_BACKSPACE) {
    if (s_input.length() > 0) {
      s_input.remove(s_input.length() - 1);
      s_cursor = s_input.length();
      redraw();
    }
  } else if (k >= 0x20 && k < 0x7F) {
    if (s_input.length() < 64) {
      s_input += (char) k;
      s_cursor = s_input.length();
      redraw();
    }
  }

  delay(15);
}
```

- [ ] **Step 2: Build, flash, observe**

```bash
pkill -9 -f 'monitor.sh' 2>/dev/null
pio run -t upload
~/.claude/jobs/$JOB/monitor.sh &
```

Then type into the Cardputer's keyboard:

```
hello?
```

(then Enter)

Expected behavior:
1. LCD shows `you> hello?` in cyan.
2. `claude> ` line appears below, in green.
3. Tokens stream in after `claude>` as Claude responds.
4. Status bar shows `sending` then `ready`.

If the proxy isn't reachable: scrollback gets a red error line. Check that lobsterboy was reconfigured for `CARDPUTER_PROXY_LISTEN_HOST=0.0.0.0` and firewall opened.

- [ ] **Step 3: Verify multi-turn**

Type a second message. It should be appended to the conversation and Claude should respond aware of the first turn (context is sent each request).

- [ ] **Step 4: Commit**

```bash
git add firmware/src/main.cpp
git commit -m "feat(m3): main.cpp orchestrates Wi-Fi + WG + chat loop on the LCD"
```

---

## Task 8: status bar polish

After the chat works, give the status bar enough info to debug from across the room.

**Files:**
- Modify: `firmware/src/main.cpp` (call `set_status` with detail strings periodically)
- Optionally modify: `firmware/src/chat_view.cpp` (richer rendering)

- [ ] **Step 1: Tick a heartbeat that updates status detail**

In `loop()`, every 5 seconds when idle, refresh the status string with WG handshake age:

```cpp
  static uint32_t last_tick = 0;
  if (chat_view::Status::kReady == chat_view::Status::kReady /* placeholder */ &&
      millis() - last_tick > 5000) {
    last_tick = millis();
    char detail[40];
    std::snprintf(detail, sizeof(detail), "wg %us  rssi %d",
                  wg_link::seconds_since_last_handshake(),
                  WiFi.RSSI());
    chat_view::set_status(chat_view::Status::kReady, detail);
    redraw();
  }
```

(Replace the `placeholder` condition with whatever state machine you carry — for M3, "ready when nothing else is in flight" is fine.)

- [ ] **Step 2: Build, flash, verify the status bar updates**

You should see the wg-handshake-age countdown rolling, and RSSI nearby.

- [ ] **Step 3: Commit**

```bash
git add firmware/src/main.cpp
git commit -m "feat(m3): status bar heartbeat with wg handshake age + RSSI"
```

---

## Task 9: Finalize the M3 report and tag

**Files:**
- Create: `docs/m3-device-chat-report.md`

- [ ] **Step 1: Capture findings**

In `docs/m3-device-chat-report.md`, record:
- Final flash + RAM usage (last `pio run` summary)
- Path A or B chosen for the keyboard
- Any keymap quirks (e.g. modifier keys that did weird things)
- Photos of the screen during a chat (optional but nice)
- Any retries / timeouts you noticed during real use
- A short script of an actual chat session (paste from serial)

- [ ] **Step 2: Tag**

```bash
git add docs/m3-device-chat-report.md
git commit -m "docs(m3): finalize M3 device chat MVP report"
git tag -a m3-complete -m "M3 device chat MVP complete — first demo-able pocket Claude"
git push origin <branch>
git push origin m3-complete
```

Open the PR, merge as usual.

---

## Done criteria

M3 is complete when **all** of these are true:

1. `firmware/` builds cleanly with the arduino+espidf hybrid framework and the chosen board profile.
2. On boot, the LCD shows the M3 chat layout (status bar at top, scrollback in middle, `> _` input at bottom).
3. Wi-Fi + WG come up automatically; status bar reflects state transitions.
4. Typing a sentence + Enter sends it to the proxy; Claude's reply streams onto the LCD as tokens arrive.
5. A second turn works and Claude sees the prior context.
6. The supervisor recovers from a Wi-Fi or WG drop without manual intervention.
7. `docs/m3-device-chat-report.md` captures the build, the keyboard choice, and a sample session.
8. Repository tag `m3-complete` exists on origin/main.

At that point the Cardputer-AI is **demo-able**. M4 starts adding the profile picker / editor / settings screens on top of this baseline.

---

## Open questions deferred to later milestones

- **Esc to cancel in-flight requests** — straightforward but needs `WiFiClient.stop()` from a separate code path; defer to M4 alongside the input editor's other affordances.
- **Scrollback PgUp/PgDn** — requires a scroll offset state in `chat_view`. Add when more than one screenful of history is common.
- **Multi-line input** — Shift+Enter inserts `\n`. Requires the input area to be 2-3 rows tall, not 1. M4.
- **Per-profile picker UI** — M4.
- **History persistence across power cycles** — DESIGN.md §11 Q5; depends on whether we want SRAM-only ring or SD-backed log. Revisit in v2.
- **Captive-portal Wi-Fi handling** — silently fails today; M3+ should surface "wifi connected but no internet" as its own status bar state.
