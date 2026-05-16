#include "net_probe.h"

#include <ESP32Ping.h>

namespace net_probe {

PingResult ping(IPAddress target, uint16_t max_attempts) {
  PingResult r{false, 0, 0};
  for (uint16_t i = 1; i <= max_attempts; ++i) {
    ++r.attempts;
    if (Ping.ping(target, 1)) {
      r.ok = true;
      r.round_trip_ms = (uint16_t) Ping.averageTime();
      return r;
    }
    delay(250);
  }
  return r;
}

}  // namespace net_probe
