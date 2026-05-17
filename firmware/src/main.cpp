#include <Arduino.h>
#include <WiFi.h>
#include <M5Cardputer.h>
#include <time.h>
#include <sys/time.h>
#include <vector>

#include "wifi_sta.h"
#include "wg_link.h"
#include "chat_view.h"
#include "chat_client.h"
#include "keyboard_input.h"
#include "scrollback.h"
#include "wg_secrets.h"
#include "proxy_secrets.h"
#include "proxy_api.h"
#include "profile_store.h"
#include "picker_view.h"

static wg_link::Config wg_cfg() {
  return wg_link::Config{
    .device_private_key      = wg_secrets::kDevicePrivateKey,
    .peer_public_key         = wg_secrets::kPeerPublicKey,
    .peer_endpoint_host      = wg_secrets::kPeerEndpointHost,
    .peer_endpoint_port      = wg_secrets::kPeerEndpointPort,
    .device_address          = wg_secrets::kDeviceAddress,
    .device_netmask          = wg_secrets::kDeviceNetmask,
    .allowed_ip_cidr         = wg_secrets::kAllowedIPCIDR,
    .persistent_keepalive_s  = 25,
    .set_as_default_route    = true,
  };
}

static String s_input;
static size_t s_cursor = 0;
static String s_active_profile;
static std::vector<proxy_api::Profile> s_profiles;

static void redraw() {
  chat_view::set_input(s_input.c_str(), s_cursor);
  chat_view::render();
}

static const char* label_for_id(const String& id) {
  for (auto& p : s_profiles) {
    if (p.id == id) return p.label.c_str();
  }
  return id.c_str();
}

static void send_current_input() {
  if (s_input.length() == 0) return;

  String prefixed = String("you> ") + s_input;
  scrollback::push(scrollback::LineKind::kUserTurn,
                   prefixed.c_str(),
                   chat_view::scrollback_width_chars());
  scrollback::push(scrollback::LineKind::kAssistant,
                   "claude> ",
                   chat_view::scrollback_width_chars());
  chat_view::mark_dirty();
  chat_view::set_status(chat_view::Status::kSending);
  redraw();

  String reply;
  auto result = chat_client::send(
      s_active_profile,
      s_input,
      [](const String& d) {
        scrollback::append_to_last(d.c_str(),
                                   chat_view::scrollback_width_chars());
        chat_view::mark_dirty();
        chat_view::render();
      },
      reply);

  if (!result.ok) {
    scrollback::push(scrollback::LineKind::kError, result.error,
                     chat_view::scrollback_width_chars());
    chat_view::mark_dirty();
    chat_view::set_status(chat_view::Status::kError, result.error);
  } else {
    chat_view::set_status(chat_view::Status::kReady);
  }

  s_input = "";
  s_cursor = 0;
  redraw();
}

static void open_picker() {
  // Re-fetch in case the catalog changed on the proxy.
  auto pr = proxy_api::fetch_profiles();
  if (pr.ok) s_profiles = pr.profiles;
  String chosen;
  if (picker_view::run(s_profiles, s_active_profile, chosen)) {
    s_active_profile = chosen;
    profile_store::set_active_profile_id(s_active_profile);
    chat_view::set_active_profile_label(label_for_id(s_active_profile));
    scrollback::push(scrollback::LineKind::kSystem,
                     (String("switched: ") + label_for_id(s_active_profile)).c_str(),
                     chat_view::scrollback_width_chars());
  }
  // Returning to chat — repaint everything regardless of pick/cancel.
  chat_view::mark_dirty();
  redraw();
}

void setup() {
  auto cfg5 = M5.config();
  M5Cardputer.begin(cfg5, /*enableKeyboard=*/true);

  Serial.begin(115200);
  uint32_t deadline = millis() + 5000;
  while (!Serial && millis() < deadline) delay(50);

  chat_view::begin();
  scrollback::begin();
  keyboard_input::begin();
  chat_client::begin();
  profile_store::begin();

  // Read the persisted active profile id; fall back to compile-time default.
  s_active_profile = profile_store::active_profile_id();
  if (s_active_profile.length() == 0) {
    s_active_profile = proxy_secrets::kProfileId;
  }
  chat_view::set_active_profile_label(s_active_profile.c_str());

  chat_view::set_status(chat_view::Status::kWifiConnecting);
  redraw();

  if (!wifi_sta::connect(wg_secrets::kWifiSSID, wg_secrets::kWifiPassword,
                         Serial)) {
    scrollback::push(scrollback::LineKind::kError, "wifi failed",
                     chat_view::scrollback_width_chars());
    chat_view::mark_dirty();
    chat_view::set_status(chat_view::Status::kError, "wifi");
    redraw();
    return;
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  uint32_t ntp_deadline = millis() + 15000;
  while (time(nullptr) < 1700000000 && millis() < ntp_deadline) delay(250);

  chat_view::set_status(chat_view::Status::kWgConnecting);
  redraw();

  auto cfg = wg_cfg();
  if (!wg_link::start(cfg, Serial)) {
    scrollback::push(scrollback::LineKind::kError, "wg failed",
                     chat_view::scrollback_width_chars());
    chat_view::mark_dirty();
    chat_view::set_status(chat_view::Status::kError, "wg");
    redraw();
    return;
  }

  // Tunnel is up — pull the catalog so we can show real labels.
  auto pr = proxy_api::fetch_profiles();
  if (pr.ok) {
    s_profiles = pr.profiles;
    chat_view::set_active_profile_label(label_for_id(s_active_profile));
  } else {
    Serial.printf("proxy_api: %s\n", pr.error);
  }

  scrollback::push(scrollback::LineKind::kSystem,
                   "tunnel up. p=profiles, type+enter=send",
                   chat_view::scrollback_width_chars());
  chat_view::mark_dirty();
  chat_view::set_status(chat_view::Status::kReady);
  redraw();
}

void loop() {
  if (!wifi_sta::is_up()) {
    chat_view::set_status(chat_view::Status::kWifiConnecting);
    redraw();
    wg_link::stop();
    wifi_sta::connect(wg_secrets::kWifiSSID, wg_secrets::kWifiPassword,
                      Serial, 20000);
  } else if (!wg_link::is_up()) {
    chat_view::set_status(chat_view::Status::kWgConnecting);
    redraw();
    auto cfg = wg_cfg();
    wg_link::start(cfg, Serial);
    chat_view::set_status(chat_view::Status::kReady);
    redraw();
  }

  static uint32_t last_tick = 0;
  if (millis() - last_tick > 5000) {
    last_tick = millis();
    char detail[40];
    std::snprintf(detail, sizeof(detail), "wg %us  rssi %d",
                  (unsigned) wg_link::seconds_since_last_handshake(),
                  (int) WiFi.RSSI());
    chat_view::set_status(chat_view::Status::kReady, detail);
    redraw();
  }

  int k = keyboard_input::poll();
  if (k == keyboard_input::KB_ENTER) {
    send_current_input();
  } else if (k == keyboard_input::KB_BACKSPACE) {
    if (s_input.length() > 0) {
      s_input.remove(s_input.length() - 1);
      s_cursor = s_input.length();
      redraw();
    }
  } else if (k == 'p' && s_input.length() == 0) {
    // Open profile picker only when input is empty (so 'p' is typeable
    // mid-sentence).
    open_picker();
  } else if (k >= 0x20 && k < 0x7F) {
    if (s_input.length() < 76) {
      s_input += (char) k;
      s_cursor = s_input.length();
      redraw();
    }
  }

  delay(15);
}
