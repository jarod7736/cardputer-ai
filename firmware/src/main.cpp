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
#include "proxy_api.h"
#include "profile_store.h"
#include "picker_view.h"
#include "settings_view.h"
#include "nvs_config.h"
#include "provisioning.h"

// Header-secrets are a dev-only fallback for the maintainer's own
// Cardputer when they don't want to provision. Off by default; enable
// with -D DEV_USE_HEADER_SECRETS=1 in platformio.ini.
#if DEV_USE_HEADER_SECRETS
#include "wg_secrets.h"
#include "proxy_secrets.h"
#endif

static String s_input;
static size_t s_cursor = 0;
static String s_active_profile;
static std::vector<proxy_api::Profile> s_profiles;
static String s_device_id;

// Built lazily once NVS or headers have been consulted.
static nvs_config::Wifi  s_wifi;
static nvs_config::Wg    s_wg;
static nvs_config::Proxy s_proxy;

static wg_link::Config build_wg_cfg() {
  return wg_link::Config{
    .device_private_key      = s_wg.priv_key.c_str(),
    .peer_public_key         = s_wg.peer_pub.c_str(),
    .peer_endpoint_host      = s_wg.endpoint_host.c_str(),
    .peer_endpoint_port      = s_wg.endpoint_port,
    .device_address          = s_wg.addr.c_str(),
    .device_netmask          = s_wg.netmask.c_str(),
    .allowed_ip_cidr         = s_wg.allowed_ip_cidr.c_str(),
    .persistent_keepalive_s  = 25,
    .set_as_default_route    = true,
  };
}

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
  String prompt = String(label_for_id(s_active_profile)) + "> ";
  scrollback::push(scrollback::LineKind::kAssistant,
                   prompt.c_str(),
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
  chat_view::mark_dirty();
  redraw();
}

static void open_settings() {
  settings_view::Status st;
  st.device_id            = s_device_id;
  st.wifi_ssid            = s_wifi.ssid;
  st.wifi_rssi            = (int) WiFi.RSSI();
  st.wifi_up              = wifi_sta::is_up();
  st.wg_up                = wg_link::is_up();
  st.wg_handshake_age_s   = wg_link::seconds_since_last_handshake();
  st.proxy_host           = s_proxy.host;
  st.proxy_port           = s_proxy.port;
  st.active_profile_label = label_for_id(s_active_profile);

  auto action = settings_view::run(st);
  if (action == settings_view::Action::kReconnectWg) {
    wg_link::stop();
    auto cfg = build_wg_cfg();
    wg_link::start(cfg, Serial);
  } else if (action == settings_view::Action::kFactoryReset) {
    nvs_config::wipe_all();
    profile_store::set_active_profile_id("");  // also clear M4 picker memory
    delay(200);
    ESP.restart();
  }
  chat_view::mark_dirty();
  redraw();
}

// Halt with a full-screen message; never returns.
[[noreturn]] static void halt_with(const char* line1, const char* line2) {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.setTextColor(TFT_RED, TFT_BLACK);
  d.setTextSize(2);
  d.setCursor(0, 24);
  d.println(line1);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setTextSize(1);
  d.setCursor(0, 60);
  d.println(line2);
  while (true) delay(1000);
}

// Pull config out of NVS (preferred). Returns false if NVS isn't fully
// populated, in which case the caller checks the dev-header fallback.
static bool load_config_from_nvs() {
  if (!nvs_config::is_provisioned()) return false;
  s_wg     = nvs_config::read_wg();
  s_proxy  = nvs_config::read_proxy();
  s_wifi   = nvs_config::read_wifi();
  s_device_id = nvs_config::read_device().device_id;
  // wifi may be empty if M5 SD bundle didn't ship it; we'll cover with
  // the dev fallback below. But the other three must be present.
  return true;
}

#if DEV_USE_HEADER_SECRETS
static void load_config_from_headers() {
  s_wifi.ssid = wg_secrets::kWifiSSID;
  s_wifi.pass = wg_secrets::kWifiPassword;
  s_wg.priv_key       = wg_secrets::kDevicePrivateKey;
  s_wg.peer_pub       = wg_secrets::kPeerPublicKey;
  s_wg.endpoint_host  = wg_secrets::kPeerEndpointHost;
  s_wg.endpoint_port  = wg_secrets::kPeerEndpointPort;
  s_wg.addr           = wg_secrets::kDeviceAddress;
  s_wg.netmask        = wg_secrets::kDeviceNetmask;
  s_wg.allowed_ip_cidr= wg_secrets::kAllowedIPCIDR;
  s_proxy.host        = proxy_secrets::kHost;
  s_proxy.port        = proxy_secrets::kPort;
  s_proxy.bearer      = proxy_secrets::kBearerToken;
  s_proxy.default_profile_id = proxy_secrets::kProfileId;
  s_device_id         = "dev-headers";
}
#endif

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
  nvs_config::begin();

  // 1. First-boot SD provisioning fork.
  //    SD.begin() blocks for >5 s when no card is present, tripping the
  //    task watchdog. We only probe when:
  //      - production build (no header fallback) AND NVS is empty, OR
  //      - dev build with NVS empty AND no header fallback wouldn't help
  //        (we keep dev path SD-free; re-provision via factory reset
  //        flips DEV_USE_HEADER_SECRETS workflows back to provisioning).
  provisioning::Outcome outcome;
#if DEV_USE_HEADER_SECRETS
  // Dev unit always has a header fallback. Don't touch SD on every
  // boot; the user provisions via a separate build env without the
  // header flag when they're ready.
  outcome.result = provisioning::Result::kNoBundle;
#else
  if (!nvs_config::is_provisioned()) {
    outcome = provisioning::run(Serial);
  } else {
    outcome.result = provisioning::Result::kNoBundle;
  }
#endif
  if (outcome.result == provisioning::Result::kCommitted) {
    auto& d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_GREEN, TFT_BLACK);
    d.setTextSize(2);
    d.setCursor(0, 30);
    d.println(" provisioned.");
    d.println(" rebooting...");
    delay(1500);
    ESP.restart();
  }
  if (outcome.result == provisioning::Result::kFailed) {
    halt_with("provision fail", outcome.detail.c_str());
  }
  bool sd_ignored = (outcome.result == provisioning::Result::kAlreadyProvisioned);

  // 2. Load runtime config.
  bool from_nvs = load_config_from_nvs();
#if DEV_USE_HEADER_SECRETS
  if (!from_nvs) {
    load_config_from_headers();
  } else if (s_wifi.ssid.length() == 0) {
    // NVS populated but wifi not yet in the bundle scope — borrow
    // ssid/pass from the dev headers.
    s_wifi.ssid = wg_secrets::kWifiSSID;
    s_wifi.pass = wg_secrets::kWifiPassword;
  }
#else
  if (!from_nvs) {
    halt_with("no config", "insert SD bundle + reboot");
  }
#endif

  chat_client::configure(s_proxy.host, s_proxy.port, s_proxy.bearer);
  proxy_api::configure(s_proxy.host, s_proxy.port, s_proxy.bearer);

  // 3. Active profile.
  s_active_profile = profile_store::active_profile_id();
  if (s_active_profile.length() == 0) {
    s_active_profile = s_proxy.default_profile_id;
  }
  chat_view::set_active_profile_label(s_active_profile.c_str());

  // 4. Wi-Fi.
  chat_view::set_status(chat_view::Status::kWifiConnecting);
  redraw();
  if (s_wifi.ssid.length() == 0) {
    halt_with("no wifi", "settings: Fn+S (when boot completes)");
  }
  if (!wifi_sta::connect(s_wifi.ssid.c_str(), s_wifi.pass.c_str(), Serial)) {
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

  // 5. WireGuard.
  chat_view::set_status(chat_view::Status::kWgConnecting);
  redraw();
  auto cfg = build_wg_cfg();
  if (!wg_link::start(cfg, Serial)) {
    scrollback::push(scrollback::LineKind::kError, "wg failed",
                     chat_view::scrollback_width_chars());
    chat_view::mark_dirty();
    chat_view::set_status(chat_view::Status::kError, "wg");
    redraw();
    return;
  }

  // 6. Profile catalog over the tunnel.
  auto pr = proxy_api::fetch_profiles();
  if (pr.ok) {
    s_profiles = pr.profiles;
    chat_view::set_active_profile_label(label_for_id(s_active_profile));
  } else {
    Serial.printf("proxy_api: %s\n", pr.error);
  }

  if (sd_ignored) {
    scrollback::push(scrollback::LineKind::kSystem,
                     "SD bundle ignored: eject card to clear msg",
                     chat_view::scrollback_width_chars());
  }
  scrollback::push(scrollback::LineKind::kSystem,
                   "tunnel up. p=profiles  fn+s=settings  enter=send",
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
    wifi_sta::connect(s_wifi.ssid.c_str(), s_wifi.pass.c_str(), Serial, 20000);
  } else if (!wg_link::is_up()) {
    chat_view::set_status(chat_view::Status::kWgConnecting);
    redraw();
    auto cfg = build_wg_cfg();
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
  } else if (k == keyboard_input::KB_SETTINGS) {
    open_settings();
  } else if (k == 'p' && s_input.length() == 0) {
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
