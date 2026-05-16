#include "wg_link.h"

#include <cstring>
#include <ctime>
#include <esp_err.h>
#include <esp_wireguard.h>

namespace wg_link {

// The droscy/esp_wireguard library documents that esp_wireguard_init MUST
// be called only once for a given peer. To recycle the tunnel, call
// esp_wireguard_disconnect() then esp_wireguard_connect() — without
// re-initing. We split our public start()/stop() to honor that, even
// though the supervisor calls them many times across reconnects.

static wireguard_config_t s_cfg{};
static wireguard_ctx_t    s_ctx = ESP_WIREGUARD_CONTEXT_DEFAULT();
static bool               s_initialized = false;   // esp_wireguard_init has been called
static bool               s_connected   = false;   // esp_wireguard_connect succeeded
static bool               s_default_set = false;   // route swap done

// Strings backing s_cfg need stable storage because the library reads
// them lazily (DNS callbacks, handshake builder).
static char s_priv[64];
static char s_pub[64];
static char s_endpoint[64];
static char s_addr[20];
static char s_mask[20];

// Parse "0.0.0.0/0" or "192.168.1.0/24" → ip+mask strings.
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

// Copy strings into module-owned storage and point s_cfg at them.
static bool capture_config(const Config& cfg, Stream& out) {
  auto safe_copy = [&](char* dst, size_t cap, const char* src, const char* name) -> bool {
    if (!src) { out.printf("wg: missing %s\n", name); return false; }
    size_t n = std::strlen(src);
    if (n >= cap) { out.printf("wg: %s too long\n", name); return false; }
    std::memcpy(dst, src, n + 1);
    return true;
  };
  if (!safe_copy(s_priv,     sizeof(s_priv),     cfg.device_private_key, "private_key")) return false;
  if (!safe_copy(s_pub,      sizeof(s_pub),      cfg.peer_public_key,    "peer_public_key")) return false;
  if (!safe_copy(s_endpoint, sizeof(s_endpoint), cfg.peer_endpoint_host, "endpoint")) return false;
  if (!safe_copy(s_addr,     sizeof(s_addr),     cfg.device_address,     "device_address")) return false;
  if (!safe_copy(s_mask,     sizeof(s_mask),     cfg.device_netmask,     "device_netmask")) return false;

  s_cfg = (wireguard_config_t) ESP_WIREGUARD_CONFIG_DEFAULT();
  s_cfg.private_key          = s_priv;
  s_cfg.public_key           = s_pub;
  s_cfg.endpoint             = s_endpoint;
  s_cfg.port                 = cfg.peer_endpoint_port;
  s_cfg.address              = s_addr;
  s_cfg.netmask              = s_mask;
  s_cfg.persistent_keepalive = cfg.persistent_keepalive_s;
  return true;
}

bool start(const Config& cfg, Stream& out, uint32_t timeout_ms) {
  // Phase 1: one-shot init. If already initialized for the same config,
  // skip — the library refuses repeat inits. We also assume the config
  // doesn't change between calls (device key / peer pubkey / endpoint
  // are compile-time constants from wg_secrets.h).
  if (!s_initialized) {
    if (!capture_config(cfg, out)) return false;
    out.println("wg: starting tunnel (first-time init)");
    out.printf("  endpoint: %s:%u\n", s_endpoint, cfg.peer_endpoint_port);
    out.printf("  device:   %s/%s\n", s_addr, s_mask);
    out.printf("  allowed:  %s\n", cfg.allowed_ip_cidr);
    out.printf("  full-tun: %s\n", cfg.set_as_default_route ? "yes" : "no");

    esp_err_t err = esp_wireguard_init(&s_cfg, &s_ctx);
    if (err != ESP_OK) {
      out.printf("wg: esp_wireguard_init failed: %d\n", err);
      return false;
    }
    s_initialized = true;
  } else {
    out.println("wg: re-arming tunnel (already initialized)");
  }

  // Phase 2: connect — idempotent. If we previously connected and then
  // got disconnected, the library wants us to call disconnect+connect.
  // Here we always call connect; if it returns "already connected" we
  // treat as success. ESP_ERR_RETRY means DNS still resolving.
  esp_err_t err = esp_wireguard_connect(&s_ctx);
  if (err != ESP_OK && err != ESP_ERR_RETRY) {
    out.printf("wg: esp_wireguard_connect failed: %d\n", err);
    return false;
  }
  s_connected = true;

  // Phase 3: add the routed CIDR. peer_add_ip is idempotent in the
  // library (it scans for a matching ip/mask first), so calling on
  // every (re)connect is safe.
  char ip[32];
  char mask[32];
  if (!parse_cidr(cfg.allowed_ip_cidr, ip, sizeof(ip), mask, sizeof(mask))) {
    out.printf("wg: bad allowed_ip_cidr '%s'\n", cfg.allowed_ip_cidr);
    return false;
  }
  err = esp_wireguard_add_allowed_ip(&s_ctx, ip, mask);
  if (err != ESP_OK) {
    out.printf("wg: add_allowed_ip(%s/%s) failed: %d\n", ip, mask, err);
    // Don't fail hard — the device's own /32 is already routed, and the
    // peer may still come up. Continue.
  } else {
    out.printf("wg: allowed_ip ok: %s/%s\n", ip, mask);
  }

  // Phase 4: wait for handshake. We do NOT swap the default route yet —
  // that happens only after peer_is_up confirms.
  uint32_t deadline = millis() + timeout_ms;
  while (millis() < deadline) {
    if (esp_wireguard_peer_is_up(&s_ctx) == ESP_OK) {
      time_t hs = 0;
      esp_wireguard_latest_handshake(&s_ctx, &hs);
      out.printf("wg: peer UP (last handshake epoch=%ld)\n", (long) hs);

      if (cfg.set_as_default_route && !s_default_set) {
        err = esp_wireguard_set_default(&s_ctx);
        if (err == ESP_OK) {
          s_default_set = true;
          out.println("wg: WG interface is now the default route");
        } else {
          out.printf("wg: set_default failed (peer was up): %d\n", err);
        }
      }
      return true;
    }
    delay(200);
  }

  out.printf("wg: peer did not come up within %lu ms\n",
             (unsigned long) timeout_ms);
  // Library keeps the handshake timer running internally — supervisor
  // can keep polling is_up(). We do NOT disconnect here; tearing the
  // tunnel down would lose the lwIP netif slot we already initialized.
  return false;
}

bool is_up() {
  if (!s_initialized) return false;
  return esp_wireguard_peer_is_up(&s_ctx) == ESP_OK;
}

uint32_t seconds_since_last_handshake() {
  if (!s_initialized) return UINT32_MAX;
  time_t hs = 0;
  if (esp_wireguard_latest_handshake(&s_ctx, &hs) != ESP_OK || hs == 0) {
    return UINT32_MAX;
  }
  time_t now = time(nullptr);
  if (now <= 0 || hs > now) return 0;
  return (uint32_t)(now - hs);
}

void stop() {
  // Disconnect but DO NOT teardown the netif (re-init is forbidden by
  // the library, so the netif must persist for the program's lifetime).
  if (s_connected) {
    if (s_default_set) {
      esp_wireguard_restore_default(&s_ctx);
      s_default_set = false;
    }
    esp_wireguard_disconnect(&s_ctx);
    s_connected = false;
  }
}

}  // namespace wg_link
