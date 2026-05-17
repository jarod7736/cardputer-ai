#pragma once

#include <Arduino.h>
#include <cstdint>

// Typed accessors for the four NVS namespaces M5 introduces. Schema
// version is stored per-namespace as a u8; if read != written the
// namespace is treated as empty (forces re-provision).
//
// Each namespace's writer commits a key "schema" alongside the data
// keys. is_*() checks both that the schema is current AND that the
// minimum required keys are present.

namespace nvs_config {

constexpr uint8_t kSchemaVersion = 1;

void begin();        // open/close as needed; safe to call once at boot

// --- wifi namespace ---------------------------------------------------------
struct Wifi {
  String ssid;
  String pass;
};
bool  has_wifi();
Wifi  read_wifi();
void  write_wifi(const Wifi& w);

// --- wg namespace -----------------------------------------------------------
struct Wg {
  String priv_key;          // base64
  String peer_pub;          // base64
  String endpoint_host;     // ip or fqdn
  uint16_t endpoint_port;   // udp
  String addr;              // device addr (no mask)
  String netmask;           // e.g. "255.255.255.255"
  String allowed_ip_cidr;   // e.g. "0.0.0.0/0"
};
bool  has_wg();
Wg    read_wg();
void  write_wg(const Wg& w);

// --- proxy namespace --------------------------------------------------------
struct Proxy {
  String host;
  uint16_t port;
  String bearer;
  String default_profile_id;
};
bool   has_proxy();
Proxy  read_proxy();
void   write_proxy(const Proxy& p);

// --- device namespace -------------------------------------------------------
struct Device {
  String device_id;
  String provisioned_at;    // ISO-8601 from proxy
};
bool    has_device();
Device  read_device();
void    write_device(const Device& d);

// Whole-device "is provisioned" → all four namespaces populated.
bool is_provisioned();

// Atomic-ish wipe of all four M5-owned namespaces. Does NOT touch
// `cprox` (M4 active profile) or any other namespace.
void wipe_all();

}  // namespace nvs_config
