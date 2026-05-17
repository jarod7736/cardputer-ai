#pragma once
#include <Arduino.h>

namespace settings_view {

// Status snapshot the caller passes in (gathered from wifi_sta /
// wg_link / nvs_config / chat_view).
struct Status {
  String device_id;
  String wifi_ssid;
  int    wifi_rssi;          // dBm, 0 if not connected
  bool   wifi_up;
  bool   wg_up;
  uint32_t wg_handshake_age_s;
  String proxy_host;
  uint16_t proxy_port;
  String active_profile_label;
};

// Action the user selected before the screen closed. Caller acts on it.
enum class Action : uint8_t {
  kClosed,            // back / Esc / Fn+S again
  kReconnectWg,
  kFactoryReset,      // user confirmed RESET prompt; caller wipes NVS + restarts
};

Action run(const Status& s);

}  // namespace settings_view
