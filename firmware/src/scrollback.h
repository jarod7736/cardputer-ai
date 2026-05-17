#pragma once
#include <Arduino.h>

namespace scrollback {

enum class LineKind : uint8_t {
  kSystem,    // muted, e.g. "wifi connected"
  kUserTurn,  // "you> ..."
  kAssistant, // "claude> ..."
  kError,     // red
};

struct Line {
  LineKind kind;
  // Fixed-size buffer keeps allocation off the path. 64 chars covers
  // our 240-px-wide screen at the smallest legible font.
  char     text[64];
  uint8_t  len;
};

void begin();
size_t size();
const Line& at(size_t idx);  // 0 = oldest still-present line

// Push raw text wrapped to `width` chars. Long lines split; the first
// chunk inherits `kind`, continuation chunks are kSystem-style. Returns
// the number of lines added.
size_t push(LineKind kind, const char* text, uint8_t width);

// Append text to the most recent line (streaming token deltas). Wraps
// to a new continuation line if needed. Returns lines added (0 or 1).
size_t append_to_last(const char* text, uint8_t width);

void clear();

}  // namespace scrollback
