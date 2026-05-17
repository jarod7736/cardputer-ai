#pragma once
#include <Arduino.h>

namespace keyboard_input {

// Special key codes returned in addition to printable ASCII.
// Named KB_* (not KEY_*) to avoid colliding with M5Cardputer's
// Keyboard_def.h macros (KEY_ENTER, KEY_BACKSPACE, etc.).
constexpr int KB_NONE      = -1;
constexpr int KB_ENTER     = '\r';
constexpr int KB_BACKSPACE = '\b';
constexpr int KB_ESCAPE    = 27;
constexpr int KB_TAB       = '\t';

// Arrows: values above ASCII range so callers can treat all chars
// uniformly. The library reports arrow keys via the hid_keys vector
// (standard USB HID scan codes 0x4F-0x52), not via state.word.
constexpr int KB_RIGHT     = 0x100 + 0x4F;
constexpr int KB_LEFT      = 0x100 + 0x50;
constexpr int KB_DOWN      = 0x100 + 0x51;
constexpr int KB_UP        = 0x100 + 0x52;

// Soft chord: Fn+S → open settings.
constexpr int KB_SETTINGS  = 0x200;

// Must be called after M5Cardputer.begin().
void begin();

// Non-blocking poll: returns the next pending key code, or KEY_NONE if
// nothing is queued. Call every frame from the main loop. Drains one
// printable character per call so a multi-character paste (or fast typing)
// flushes over several frames.
int poll();

}  // namespace keyboard_input
