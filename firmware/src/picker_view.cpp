#include "picker_view.h"

#include <M5Cardputer.h>
#include "keyboard_input.h"

namespace picker_view {

static void draw(const std::vector<proxy_api::Profile>& profiles,
                 size_t cursor, const String& current_id) {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.setTextSize(2);
  d.setCursor(0, 0);
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.println(" select profile");
  for (size_t i = 0; i < profiles.size(); ++i) {
    uint16_t fg = (i == cursor) ? TFT_BLACK : TFT_WHITE;
    uint16_t bg = (i == cursor) ? TFT_YELLOW : TFT_BLACK;
    d.setTextColor(fg, bg);
    d.setCursor(0, 20 + (int)i * 16);
    const char* mark = (profiles[i].id == current_id) ? "*" : " ";
    String row = String(mark) + " " + profiles[i].label;
    if (row.length() > 19) row = row.substring(0, 19);
    d.print(row);
  }
  // Hint at the bottom.
  d.setTextSize(1);
  d.setTextColor(TFT_DARKGREY, TFT_BLACK);
  d.setCursor(0, 135 - 8);
  d.print("up/dn=move  enter=pick  esc=cancel");
}

bool run(const std::vector<proxy_api::Profile>& profiles,
         const String& current_id,
         String& selected_out) {
  if (profiles.empty()) return false;
  size_t cursor = 0;
  for (size_t i = 0; i < profiles.size(); ++i) {
    if (profiles[i].id == current_id) { cursor = i; break; }
  }
  draw(profiles, cursor, current_id);

  uint32_t deadline = millis() + 30000;
  while ((int32_t)(deadline - millis()) > 0) {
    int k = keyboard_input::poll();
    if (k == keyboard_input::KB_NONE) { delay(15); continue; }
    deadline = millis() + 30000;
    if (k == keyboard_input::KB_ESCAPE) return false;
    if (k == keyboard_input::KB_ENTER) {
      selected_out = profiles[cursor].id;
      return true;
    }
    if (k == keyboard_input::KB_DOWN || k == 'j' || k == 'J') {
      if (cursor + 1 < profiles.size()) ++cursor;
    } else if (k == keyboard_input::KB_UP || k == 'k' || k == 'K') {
      if (cursor > 0) --cursor;
    }
    draw(profiles, cursor, current_id);
  }
  return false;
}

}  // namespace picker_view
