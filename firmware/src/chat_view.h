#pragma once
#include <Arduino.h>
#include "scrollback.h"

namespace chat_view {

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
void set_status(Status s, const char* detail = nullptr);
void set_input(const char* buf, size_t cursor);
void render();   // call once per frame; no-ops if nothing changed

uint8_t scrollback_width_chars();
uint8_t scrollback_height_lines();

// Force a redraw on next render() even if no state changed (used when
// scrollback contents change — we don't observe scrollback directly).
void mark_dirty();

// Set the active profile label shown in the top status bar. Truncated
// to fit alongside the status text.
void set_active_profile_label(const char* label);

}  // namespace chat_view
