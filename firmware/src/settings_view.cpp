#include "settings_view.h"

#include <M5Cardputer.h>
#include "keyboard_input.h"

namespace settings_view {

namespace {

constexpr int kActionCount = 3;
const char* const kActionLabels[kActionCount] = {
  "reconnect wg",
  "factory reset...",
  "back",
};

void draw_status(const Status& s, int cursor) {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);

  // Header
  d.setTextSize(1);
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.setCursor(0, 0);
  d.println(" settings");

  // Status block
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(0, 12);
  d.printf(" id     %s\n", s.device_id.length() ? s.device_id.c_str() : "(unset)");
  d.printf(" wifi   %s  %ddBm\n",
           s.wifi_up ? s.wifi_ssid.c_str() : "down",
           s.wifi_up ? s.wifi_rssi : 0);
  d.printf(" wg     %s  %us\n",
           s.wg_up ? "up" : "down",
           (unsigned) s.wg_handshake_age_s);
  d.printf(" proxy  %s:%u\n", s.proxy_host.c_str(), (unsigned) s.proxy_port);
  d.printf(" model  %s\n", s.active_profile_label.c_str());

  // Action list
  int y = 80;
  d.setTextSize(2);
  for (int i = 0; i < kActionCount; ++i) {
    uint16_t fg = (i == cursor) ? TFT_BLACK : TFT_WHITE;
    uint16_t bg = (i == cursor) ? TFT_YELLOW : TFT_BLACK;
    d.setTextColor(fg, bg);
    d.setCursor(0, y);
    d.printf(" %s", kActionLabels[i]);
    y += 16;
  }

  // Hint
  d.setTextSize(1);
  d.setTextColor(TFT_DARKGREY, TFT_BLACK);
  d.setCursor(0, 135 - 8);
  d.print("up/dn=move  enter=pick  esc=back");
}

void draw_reset_prompt(const String& typed) {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.setTextSize(1);
  d.setTextColor(TFT_RED, TFT_BLACK);
  d.setCursor(0, 0);
  d.println(" factory reset");
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(0, 16);
  d.setTextSize(2);
  d.println(" wipes wg, proxy,");
  d.println(" device id from NVS.");
  d.println("");
  d.setTextSize(1);
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.setCursor(0, 80);
  d.println(" type RESET + enter");
  d.println(" esc cancels");
  d.setTextSize(2);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(0, 110);
  d.printf("> %s_", typed.c_str());
}

bool reset_confirm() {
  String typed;
  draw_reset_prompt(typed);
  while (true) {
    int k = keyboard_input::poll();
    if (k == keyboard_input::KB_NONE) { delay(15); continue; }
    if (k == keyboard_input::KB_ESCAPE) return false;
    if (k == keyboard_input::KB_ENTER) {
      return typed == "RESET";
    }
    if (k == keyboard_input::KB_BACKSPACE) {
      if (typed.length() > 0) typed.remove(typed.length() - 1);
      draw_reset_prompt(typed);
      continue;
    }
    if (k >= 0x20 && k < 0x7F && typed.length() < 10) {
      typed += (char) k;
      draw_reset_prompt(typed);
    }
  }
}

}  // namespace

Action run(const Status& s) {
  int cursor = 0;
  draw_status(s, cursor);

  while (true) {
    int k = keyboard_input::poll();
    if (k == keyboard_input::KB_NONE) { delay(15); continue; }
    if (k == keyboard_input::KB_ESCAPE) return Action::kClosed;
    if (k == 's' || k == 'S') {
      // Fn+S toggle was already swallowed by main.cpp before entering;
      // a bare 's' from the picker is unusual but harmless — treat as close.
      return Action::kClosed;
    }
    if (k == keyboard_input::KB_UP || k == 'k' || k == 'K') {
      if (cursor > 0) --cursor;
      draw_status(s, cursor);
    } else if (k == keyboard_input::KB_DOWN || k == 'j' || k == 'J') {
      if (cursor + 1 < kActionCount) ++cursor;
      draw_status(s, cursor);
    } else if (k == keyboard_input::KB_ENTER) {
      switch (cursor) {
        case 0: return Action::kReconnectWg;
        case 1:
          if (reset_confirm()) return Action::kFactoryReset;
          draw_status(s, cursor);  // returned from confirm w/o triggering
          break;
        case 2: return Action::kClosed;
      }
    }
  }
}

}  // namespace settings_view
