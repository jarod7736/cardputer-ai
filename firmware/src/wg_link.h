#pragma once
#include <Arduino.h>

namespace wg_link {

struct Config {
  const char* device_private_key;   // base64
  const char* peer_public_key;      // base64
  const char* peer_endpoint_host;   // IP or DNS
  uint16_t    peer_endpoint_port;
  const char* device_address;       // e.g. "192.168.14.3"
  const char* device_netmask;       // e.g. "255.255.255.0"
  const char* allowed_ip_cidr;      // e.g. "0.0.0.0/0" for full-tunnel
  uint16_t    persistent_keepalive_s;  // 25 is sane behind NAT; 0 disables
  bool        set_as_default_route; // true = full-tunnel (everything via WG)
};

// Bring up the tunnel. Initializes WireGuard, kicks off the first
// handshake, optionally makes WG the default route. Logs to `out`.
// Returns true once the peer reports up, or false on timeout.
bool start(const Config& cfg, Stream& out, uint32_t timeout_ms = 15000);

// True if the underlying library reports the peer is up right now.
bool is_up();

// Seconds since the last completed handshake; UINT32_MAX if never.
uint32_t seconds_since_last_handshake();

// Tear down the tunnel (used on Wi-Fi loss / reconnect).
void stop();

}  // namespace wg_link
