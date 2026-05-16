#include "wg_link.h"

#include <cstring>
#include <ctime>
#include <esp_err.h>
#include <esp_wireguard.h>

namespace wg_link {

static wireguard_config_t s_cfg{};
static wireguard_ctx_t    s_ctx = ESP_WIREGUARD_CONTEXT_DEFAULT();
static bool               s_running = false;

// Parse "0.0.0.0/0" or "192.168.1.0/24" into ("0.0.0.0", "0.0.0.0") or
// ("192.168.1.0", "255.255.255.0"). Returns false if input is malformed.
static bool parse_cidr(const char* cidr, char* ip_out, size_t ip_len,
                       char* mask_out, size_t mask_len) {
  const char* slash = std::strchr(cidr, '/');
  if (!slash) return false;
  size_t ip_chars = (size_t)(slash - cidr);
  if (ip_chars >= ip_len) return false;
  std::memcpy(ip_out, cidr, ip_chars);
  ip_out[ip_chars] = '\0';

  int prefix = std::atoi(slash + 1);
  if (prefix < 0 || prefix > 32) return false;
  uint32_t m = prefix == 0 ? 0u : (uint32_t)(0xFFFFFFFFu << (32 - prefix));
  unsigned a = (m >> 24) & 0xFF, b = (m >> 16) & 0xFF,
           c = (m >>  8) & 0xFF, d = m & 0xFF;
  int n = std::snprintf(mask_out, mask_len, "%u.%u.%u.%u", a, b, c, d);
  return n > 0 && (size_t) n < mask_len;
}

bool start(const Config& cfg, Stream& out, uint32_t timeout_ms) {
  if (s_running) {
    out.println("wg: already running; stop() before restarting");
    return false;
  }

  out.println("wg: starting tunnel");
  out.printf("  endpoint: %s:%u\n", cfg.peer_endpoint_host, cfg.peer_endpoint_port);
  out.printf("  device:   %s/%s\n", cfg.device_address, cfg.device_netmask);
  out.printf("  allowed:  %s\n", cfg.allowed_ip_cidr);
  out.printf("  full-tun: %s\n", cfg.set_as_default_route ? "yes" : "no");

  s_cfg = (wireguard_config_t) ESP_WIREGUARD_CONFIG_DEFAULT();
  s_cfg.private_key          = cfg.device_private_key;
  s_cfg.public_key           = cfg.peer_public_key;
  s_cfg.endpoint             = cfg.peer_endpoint_host;
  s_cfg.port                 = cfg.peer_endpoint_port;
  s_cfg.address              = cfg.device_address;
  s_cfg.netmask              = cfg.device_netmask;
  s_cfg.persistent_keepalive = cfg.persistent_keepalive_s;

  esp_err_t err = esp_wireguard_init(&s_cfg, &s_ctx);
  if (err != ESP_OK) {
    out.printf("wg: esp_wireguard_init failed: %d\n", err);
    return false;
  }

  err = esp_wireguard_connect(&s_ctx);
  if (err != ESP_OK && err != ESP_ERR_RETRY) {
    out.printf("wg: esp_wireguard_connect failed: %d\n", err);
    return false;
  }
  // ESP_ERR_RETRY just means DNS is still resolving the endpoint —
  // the connect will be retried internally. Treat as not-yet-up below.

  s_running = true;

  // Add the routed CIDR to the allowed-ip list. For full-tunnel this is
  // 0.0.0.0/0; for split-tunnel it's the home LAN.
  char ip[32];
  char mask[32];
  if (!parse_cidr(cfg.allowed_ip_cidr, ip, sizeof(ip), mask, sizeof(mask))) {
    out.printf("wg: bad allowed_ip_cidr '%s'\n", cfg.allowed_ip_cidr);
    return false;
  }
  err = esp_wireguard_add_allowed_ip(&s_ctx, ip, mask);
  if (err != ESP_OK) {
    out.printf("wg: add_allowed_ip(%s/%s) failed: %d\n", ip, mask, err);
    return false;
  }
  out.printf("wg: allowed_ip added: %s mask %s\n", ip, mask);

  if (cfg.set_as_default_route) {
    err = esp_wireguard_set_default(&s_ctx);
    if (err != ESP_OK) {
      out.printf("wg: set_default failed: %d\n", err);
      return false;
    }
    out.println("wg: WG interface is now the default route");
  }

  // Poll for peer-up. The first handshake happens after connect kicks
  // off; the library reports up when the handshake completes.
  uint32_t deadline = millis() + timeout_ms;
  while (millis() < deadline) {
    if (esp_wireguard_peer_is_up(&s_ctx) == ESP_OK) {
      time_t hs = 0;
      esp_wireguard_latest_handshake(&s_ctx, &hs);
      out.printf("wg: peer UP (last handshake epoch=%ld)\n", (long) hs);
      return true;
    }
    delay(200);
  }

  out.printf("wg: peer did not come up within %lu ms\n",
             (unsigned long) timeout_ms);
  return false;
}

bool is_up() {
  if (!s_running) return false;
  return esp_wireguard_peer_is_up(&s_ctx) == ESP_OK;
}

uint32_t seconds_since_last_handshake() {
  if (!s_running) return UINT32_MAX;
  time_t hs = 0;
  if (esp_wireguard_latest_handshake(&s_ctx, &hs) != ESP_OK || hs == 0) {
    return UINT32_MAX;
  }
  time_t now = time(nullptr);
  if (now <= 0 || hs > now) return 0;
  return (uint32_t)(now - hs);
}

void stop() {
  if (s_running) {
    esp_wireguard_disconnect(&s_ctx);
    s_running = false;
  }
}

}  // namespace wg_link
