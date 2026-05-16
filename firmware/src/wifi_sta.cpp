#include "wifi_sta.h"

#include <WiFi.h>

namespace wifi_sta {

bool connect(const char* ssid, const char* password, Stream& out,
             uint32_t timeout_ms) {
  out.printf("wifi: connecting to %s\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  uint32_t deadline = millis() + timeout_ms;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
    out.print('.');
  }
  out.println();

  if (WiFi.status() != WL_CONNECTED) {
    out.printf("wifi: FAILED (status=%d) after %lu ms\n",
               WiFi.status(), (unsigned long) timeout_ms);
    return false;
  }

  out.println("wifi: connected");
  out.printf("  ip:      %s\n", WiFi.localIP().toString().c_str());
  out.printf("  gateway: %s\n", WiFi.gatewayIP().toString().c_str());
  out.printf("  dns:     %s\n", WiFi.dnsIP().toString().c_str());
  out.printf("  rssi:    %d dBm\n", WiFi.RSSI());
  return true;
}

bool is_up() {
  return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

IPAddress local_ip() {
  return WiFi.localIP();
}

}  // namespace wifi_sta
