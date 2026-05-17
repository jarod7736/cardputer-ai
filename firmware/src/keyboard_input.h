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

// Must be called after M5Cardputer.begin().
void begin();

// Non-blocking poll: returns the next pending key code, or KEY_NONE if
// nothing is queued. Call every frame from the main loop. Drains one
// printable character per call so a multi-character paste (or fast typing)
// flushes over several frames.
int poll();

}  // namespace keyboard_input
