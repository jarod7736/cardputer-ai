#include "scrollback.h"

#include <cstring>

namespace scrollback {

constexpr size_t kCapacity = 32;

static Line   s_lines[kCapacity];
static size_t s_head = 0;   // index of next slot to write
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
  if (width == 0) width = 1;
  size_t added = 0;
  const char* p = text;
  bool first = true;
  while (*p) {
    size_t remaining = std::strlen(p);
    size_t take = remaining > width ? width : remaining;
    // Prefer breaking on whitespace when we'd otherwise cut a word.
    if (take < remaining) {
      size_t b = take;
      while (b > 1 && p[b] != ' ' && p[b-1] != ' ') --b;
      if (b > (size_t)(width / 2)) take = b;
    }
    push_one(first ? kind : LineKind::kSystem, p, (uint8_t) take);
    ++added;
    first = false;
    p += take;
    while (*p == ' ') ++p;
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
  if (width == 0) width = 1;
  Line& last = slot(s_size - 1);
  size_t take = std::strlen(text);
  size_t room = (size_t)(width - last.len);
  if (take <= room) {
    std::memcpy(last.text + last.len, text, take);
    last.len += (uint8_t) take;
    last.text[last.len] = '\0';
    return 0;
  }
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
