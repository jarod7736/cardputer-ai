# M3 Device Chat MVP Report

**Date completed:** 2026-05-16
**Branch / commit at completion:** `m3-execution` @ HEAD
**Hardware:** M5Stack Cardputer-ADV (ESP32-S3FN8, 8 MB flash, no PSRAM)

## Headline

The Cardputer is now a pocket Claude. Type → Enter → response streams onto
the LCD over WireGuard to the lobsterboy proxy. **First user-visible build.**

## Final firmware budget

| Resource | Used | Available | % |
|---|---|---|---|
| Flash (app slot) | 1 020 496 B | 3 407 872 B | 29.9 % |
| RAM (internal SRAM) | ~40.7 KB | 320 KB | 12.4 % |

Plenty of headroom in both. The chat history is the only growing buffer;
when it gets large we add token-aware truncation, not RAM.

## Keyboard path

**Used:** `m5stack/M5Cardputer @ ^1.1.1` library (bundled Adafruit_TCA8418
+ key matrix lookup).

Plan called for a Path A vs B decision: no upstream PlatformIO board
profile for the Cardputer-ADV exists, so the plan defaulted to Path B
(direct TCA8418 driver). Discovered during execution that M5Stack ships
a `M5Cardputer` library that explicitly supports the ADV variant and
includes a TCA8418 keymap reader. Used that instead — much less code,
correctly handles the modifier keys.

API shape: `M5Cardputer.Keyboard.keysState()` returns a `KeysState` with
bool flags for special keys (`enter`, `del`, `tab`, …) and a
`vector<char> word` of pressed printable characters. Wrapped in
`keyboard_input::poll()` with a small FIFO so the caller paints one
character per frame instead of getting a whole word at once.

**Gotcha worth recording:** M5Cardputer's `Keyboard_def.h` defines
`KEY_ENTER`, `KEY_BACKSPACE`, `KEY_FN`, etc. as **macros**. Our public
constants in `keyboard_input.h` originally had the same names —
preprocessor wins, build failed with cryptic "expected ')' before
numeric constant" inside `main.cpp`. Renamed to `KB_ENTER`,
`KB_BACKSPACE`, … to dodge the collision. Anything we add later that
exports key codes should use a non-`KEY_` prefix for the same reason.

## Display

Mixed font sizes for readability on the 240×135 LCD:
- **Status bar**: text size 1 (6×8 px) — keeps `wg 12s  rssi -58 dBm`
  compact at the top
- **Chat body + input row**: text size 2 (12×16 px) — readable from
  arm's length; ~20 chars per line × ~6 visible lines of chat

First flash tried size 1 throughout. User feedback was "the text is VERY
small. Claude responds" — pulled the chat body to size 2 in a follow-up
commit. Status bar stayed at size 1 because the debug info is more
useful than its legibility.

## Module boundaries (all built clean, all wired)

| Module | Responsibility | Knows nothing about |
|---|---|---|
| `keyboard_input` | TCA8418 read + KB_* code dispatch | LCD, network |
| `scrollback` | Fixed-capacity text ring with word wrap | Pixels, fonts |
| `chat_view` | LCD layout (status bar, scrollback area, input row) | Where text comes from |
| `http_sse` | HTTP/1.1 + SSE line reader over WiFiClient | Chat shape |
| `chat_client` | OAI request building + delta dispatch | UI, transport |
| `main.cpp` | Orchestration + supervisor + keyboard pump | None of the others' internals |

No new lib_deps beyond M5Cardputer. HTTP, SSE, and JSON parsing are all
hand-rolled.

## End-to-end behavior captured

1. Boot: status progresses `boot` → `wifi...` → `wg...` → `ready`. NTP
   syncs before WG (M1 lesson learned).
2. LCD layout appears correctly: navy status bar at top, dark chat area,
   `> _` prompt at bottom with cursor.
3. User typed a short message + Enter. Reply streamed in green as
   tokens arrived from the proxy. (Confirmed by user: "yes. it works.
   Claude responds.")
4. Multi-turn works — second message includes the first turn's context
   as a regular OAI message array.
5. Supervisor loop maintains WG: status bar shows `wg Ns` rolling
   between 0-25 s thanks to the persistent keepalive.

## Open items for M4

- **Esc to cancel in-flight requests** — straightforward; needs the
  HTTP client to support cancellation from a separate code path.
- **Scrollback PgUp/PgDn** — requires a scroll offset state in `chat_view`.
- **Multi-line input + cursor movement** — would need the input area
  to grow vertically.
- **Profile picker UI** — the proxy already supports profile CRUD as a
  future addition (M4); device UI to switch profiles waits on that.
- **Save bearer / Wi-Fi to NVS** — currently hard-coded in
  `proxy_secrets.h` / `wg_secrets.h`. M5 provisioning moves these.

## Done criteria — all met

1. ✅ Firmware builds cleanly (1.02 MB / 3.4 MB flash; 40.7 KB / 320 KB RAM)
2. ✅ On boot: status bar + scrollback + `> _` input on the LCD
3. ✅ Wi-Fi + WG come up automatically; status bar reflects state
4. ✅ Type + Enter → Claude reply streams onto the LCD
5. ✅ Second turn works with prior context
6. ✅ Supervisor pattern from M1 keeps Wi-Fi/WG alive in the background
7. ✅ This report exists
8. (Tag `m3-complete` to be created next)
