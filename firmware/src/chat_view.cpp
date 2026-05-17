#include "chat_view.h"

#include <M5Cardputer.h>
#include <cstring>

namespace chat_view {

static Status s_status = Status::kBoot;
static char   s_status_detail[40] = "";
static char   s_input_buf[80] = "";
static size_t s_cursor = 0;
static bool   s_dirty  = true;

// Mixed font sizes so the chat is readable from arm's length while the
// status bar stays compact enough to show useful debug info.
//   Status bar: text size 1 (6×8 px)  — 40 cols × 1 row at top
//   Chat body:  text size 2 (12×16 px) — 20 cols × ~7 rows
//   Input:      text size 2            — 20 cols × 1 row at bottom
static constexpr uint8_t kStatusFontSize = 1;
static constexpr uint8_t kStatusCharH    = 8;   // 6×8 at size 1
static constexpr uint8_t kStatusCols     = 40;
static constexpr uint8_t kChatFontSize   = 2;
static constexpr uint8_t kChatCharW      = 12;  // 6×8 × 2
static constexpr uint8_t kChatCharH      = 16;
static constexpr uint8_t kChatCols       = 20;  // 240 / 12

// LCD is 240×135.
// Status: rows 0..7  (8 px)
// Chat:   rows 8..(135-16-1) (~111 px = 6 lines of 16 px + 15 leftover)
// Input:  bottom 16 px
static constexpr uint16_t kInputTopY     = 135 - kChatCharH;
static constexpr uint16_t kChatTopY      = kStatusCharH;
static constexpr uint16_t kChatHeight    = kInputTopY - kChatTopY;
static constexpr uint8_t  kChatRows      = (uint8_t)(kChatHeight / kChatCharH);

uint8_t scrollback_width_chars()  { return kChatCols; }
uint8_t scrollback_height_lines() { return kChatRows; }

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
    case Status::kBoot:           return "boot";
    case Status::kWifiConnecting: return "wifi...";
    case Status::kWifiUp:         return "wifi";
    case Status::kWgConnecting:   return "wg...";
    case Status::kReady:          return "ready";
    case Status::kSending:        return "sending";
    case Status::kError:          return "ERROR";
  }
  return "?";
}

void begin() {
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  s_dirty = true;
  render();
}

void mark_dirty() { s_dirty = true; }

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
  M5Cardputer.Display.fillRect(0, 0, M5Cardputer.Display.width(),
                                kStatusCharH, TFT_NAVY);
  M5Cardputer.Display.setTextSize(kStatusFontSize);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_NAVY);
  M5Cardputer.Display.setCursor(0, 0);
  M5Cardputer.Display.printf(" %s %s",
                              label_for_status(s_status), s_status_detail);
}

static void draw_scrollback() {
  M5Cardputer.Display.fillRect(0, kChatTopY, M5Cardputer.Display.width(),
                                kChatHeight, TFT_BLACK);
  M5Cardputer.Display.setTextSize(kChatFontSize);
  size_t n = scrollback::size();
  size_t start = n > kChatRows ? n - kChatRows : 0;
  uint16_t y = kChatTopY;
  for (size_t i = start; i < n; ++i, y += kChatCharH) {
    const auto& L = scrollback::at(i);
    M5Cardputer.Display.setTextColor(color_for_kind(L.kind), TFT_BLACK);
    M5Cardputer.Display.setCursor(0, y);
    M5Cardputer.Display.print(L.text);
  }
}

static void draw_input_row() {
  M5Cardputer.Display.fillRect(0, kInputTopY, M5Cardputer.Display.width(),
                                kChatCharH, TFT_BLACK);
  M5Cardputer.Display.setTextSize(kChatFontSize);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.setCursor(0, kInputTopY);
  M5Cardputer.Display.print("> ");
  M5Cardputer.Display.print(s_input_buf);
  // Cursor as a 2-px underline at the current column.
  uint16_t cur_x = (uint16_t)(s_cursor + 2) * kChatCharW;
  if (cur_x < M5Cardputer.Display.width()) {
    M5Cardputer.Display.fillRect(cur_x, kInputTopY + kChatCharH - 2,
                                  kChatCharW - 1, 2, TFT_WHITE);
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
