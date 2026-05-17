#pragma once

#include <Arduino.h>

// First-boot SD-card provisioning bundle reader.
//
// Looks for /wg.conf and /proxy.json at the SD root. Returns a parsed
// Bundle on success. The caller commits the contents to NVS via
// nvs_config and then calls wipe() to atomically remove the files.

namespace sd_bundle {

enum class Status : uint8_t {
  kAbsent,    // SD not inserted, or neither file present
  kOk,        // both files present and parsed
  kInvalid,   // one or both files failed to parse (see .error)
};

struct Bundle {
  Status status = Status::kAbsent;
  String error;  // populated on kInvalid

  // wg.conf fields (raw values from [Interface] + [Peer])
  String wg_priv_key;
  String wg_address;          // e.g. "10.42.0.2/32"
  String wg_peer_pub;
  String wg_endpoint;         // host:port
  String wg_allowed_ips;      // e.g. "0.0.0.0/0"
  uint16_t wg_keepalive = 25;

  // proxy.json fields
  String proxy_host;
  uint16_t proxy_port = 8420;
  String proxy_bearer;
  String device_id;
  String default_profile_id;
};

// Initialize SPI for the SD slot and try to mount. Returns false if
// no card present. Safe to call multiple times.
bool mount();

// Unmount and release SPI.
void unmount();

// Read + parse both files if present. Mounts SD internally.
Bundle try_read();

// Delete /wg.conf and /proxy.json from SD. Returns true if both were
// removed (or didn't exist). Returns false on a real I/O failure.
bool wipe();

}  // namespace sd_bundle
