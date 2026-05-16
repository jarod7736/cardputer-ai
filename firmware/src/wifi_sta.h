#pragma once
#include <Arduino.h>
#include <IPAddress.h>

namespace wifi_sta {

// Connects to the SSID/password supplied at compile time. Blocks until
// either the device has an IPv4 address from DHCP or `timeout_ms`
// elapses. Logs status to `out`. Returns true on success.
bool connect(const char* ssid, const char* password, Stream& out,
             uint32_t timeout_ms = 30000);

// Convenience: true if currently associated AND has a non-zero IP.
bool is_up();

// The last DHCP-assigned IPv4 address (0.0.0.0 if not connected).
IPAddress local_ip();

}  // namespace wifi_sta
