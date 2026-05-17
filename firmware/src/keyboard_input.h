#pragma once
#include <Arduino.h>

namespace keyboard_input {

// Special key codes returned in addition to printable ASCII.
constexpr int KEY_NONE      = -1;
constexpr int KEY_ENTER     = '\r';
constexpr int KEY_BACKSPACE = '\b';
constexpr int KEY_ESCAPE    = 27;
constexpr int KEY_TAB       = '\t';

// Must be called after M5Cardputer.begin().
void begin();

// Non-blocking poll: returns the next pending key code, or KEY_NONE if
// nothing is queued. Call every frame from the main loop. Drains one
// printable character per call so a multi-character paste (or fast typing)
// flushes over several frames.
int poll();

}  // namespace keyboard_input
