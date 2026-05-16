#include <Arduino.h>
#include <IPAddress.h>
#include <WiFi.h>
#include <M5Unified.h>

#include "wifi_sta.h"
#include "wg_link.h"
#include "net_probe.h"
#include "wg_secrets.h"   // gitignored — see wg_secrets.h.example

// Minimal LCD status line — not the real UI (that's M3+). Just so the
// device isn't a black brick during M1 testing.
static void lcd_status(const char* line, uint16_t color = TFT_WHITE) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(4, 4);
  M5.Display.println("Cardputer-AI M1");
  M5.Display.setTextSize(1);
  M5.Display.setCursor(4, 32);
  M5.Display.println(line);
}

// One source of truth for the WG config, used by both setup() and the
// supervisor loop. The Wi-Fi creds and WG secrets live in wg_secrets.h.
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

// Run the M1 success-criteria checks once and print a verdict.
static void run_reachability_proof() {
  IPAddress in_tunnel;
  in_tunnel.fromString(wg_secrets::kInTunnelTestIP);
  IPAddress outbound;
  outbound.fromString(wg_secrets::kOutboundTestIP);

  auto a = net_probe::ping(in_tunnel);
  Serial.printf("ping in-tunnel %s: %s (%u ms, %u attempts)\n",
                in_tunnel.toString().c_str(),
                a.ok ? "OK" : "FAIL", a.round_trip_ms, a.attempts);

  auto b = net_probe::ping(outbound);
  Serial.printf("ping outbound %s: %s (%u ms, %u attempts)\n",
                outbound.toString().c_str(),
                b.ok ? "OK" : "FAIL", b.round_trip_ms, b.attempts);

  if (a.ok && b.ok) {
    Serial.println("==== M1 SUCCESS: tunnel up + outbound through tunnel works ====");
  } else if (a.ok && !b.ok) {
    Serial.println("==== PARTIAL: tunnel reaches the WG peer, but no outbound through it ====");
  } else if (!a.ok && b.ok) {
    Serial.println("==== FAIL: outbound works but tunnel peer is unreachable ====");
  } else {
    Serial.println("==== FAIL: no network reachable ====");
  }
}

void setup() {
  auto cfg5 = M5.config();
  M5.begin(cfg5);
  lcd_status("booting...");

  Serial.begin(115200);
  uint32_t deadline = millis() + 10000;
  while (!Serial && millis() < deadline) delay(50);
  delay(500);

  Serial.println();
  Serial.println("==== Cardputer-AI M1 — Wi-Fi + WireGuard ====");

  lcd_status("wifi: connecting...");
  if (!wifi_sta::connect(wg_secrets::kWifiSSID, wg_secrets::kWifiPassword,
                         Serial)) {
    Serial.println("STOP: Wi-Fi failed — supervisor will retry");
    lcd_status("wifi: FAILED", TFT_RED);
    return;
  }
  lcd_status("wifi: OK");

  // Quick DNS sanity before we ask WG to resolve its endpoint.
  IPAddress test_ip;
  if (WiFi.hostByName("one.one.one.one", test_ip)) {
    Serial.printf("dns: one.one.one.one -> %s\n", test_ip.toString().c_str());
  } else {
    Serial.println("dns: lookup FAILED (continuing — WG endpoint is an IP)");
  }

  lcd_status("wg: connecting...");
  auto cfg = wg_cfg();
  if (!wg_link::start(cfg, Serial)) {
    Serial.println("STOP: WireGuard failed — supervisor will retry");
    lcd_status("wg: FAILED", TFT_RED);
    return;
  }
  lcd_status("wg: UP, testing...");

  // Give the kernel a moment after route swap before we hit the wire.
  delay(500);
  run_reachability_proof();

  // The reachability_proof printed its own SUCCESS/FAIL line; reflect
  // that on the LCD using whether both pings worked. Cheap: read the
  // last two ping results via a re-ping (fast since the tunnel is up).
  IPAddress in_tunnel; in_tunnel.fromString(wg_secrets::kInTunnelTestIP);
  IPAddress outbound;  outbound.fromString(wg_secrets::kOutboundTestIP);
  bool a = net_probe::ping(in_tunnel, 1).ok;
  bool b = net_probe::ping(outbound, 1).ok;
  if (a && b) lcd_status("M1 SUCCESS: tunnel works", TFT_GREEN);
  else        lcd_status("M1: partial / fail", TFT_YELLOW);
}

void loop() {
  // Supervisor: if Wi-Fi or WG drops, reconnect with simple backoff.
  static uint32_t backoff_ms = 1000;
  static const uint32_t kMaxBackoff = 30000;

  if (!wifi_sta::is_up()) {
    Serial.println("supervisor: wifi down — reconnecting");
    wg_link::stop();
    if (!wifi_sta::connect(wg_secrets::kWifiSSID, wg_secrets::kWifiPassword,
                           Serial, 20000)) {
      delay(backoff_ms);
      backoff_ms = min(backoff_ms * 2, kMaxBackoff);
      return;
    }
    backoff_ms = 1000;
  }

  if (!wg_link::is_up()) {
    // Backoff so a persistently-failing peer doesn't spam logs.
    static uint32_t wg_backoff = 2000;
    static uint32_t next_attempt = 0;
    if (millis() >= next_attempt) {
      Serial.println("supervisor: wg down — restarting tunnel");
      auto cfg = wg_cfg();
      bool ok = wg_link::start(cfg, Serial);
      next_attempt = millis() + wg_backoff;
      wg_backoff = ok ? 2000 : min<uint32_t>(wg_backoff * 2, 30000);
    }
  } else {
    // Healthy path. Heartbeat once a minute so the log isn't silent.
    static uint32_t last_hb = 0;
    if (millis() - last_hb > 60000) {
      last_hb = millis();
      Serial.printf("supervisor: wg UP, %u s since last handshake\n",
                    wg_link::seconds_since_last_handshake());
    }
  }

  delay(2000);
}
