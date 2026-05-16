#pragma once
#include <Arduino.h>
#include <IPAddress.h>

namespace net_probe {

struct PingResult {
  bool     ok;
  uint16_t round_trip_ms;
  uint16_t attempts;
};

// Send up to `max_attempts` ICMP echos to `target`. Returns the first
// success; .ok = false if all attempts failed.
PingResult ping(IPAddress target, uint16_t max_attempts = 4);

}  // namespace net_probe
