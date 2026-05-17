#include "http_sse.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <cstring>
#include <cstdio>

namespace http_sse {

// Read until '\n' (skips '\r'). Returns true if a line was assembled
// (possibly empty), false on timeout or hard disconnect.
static bool read_line(WiFiClient& cx, char* out, size_t cap, uint32_t timeout_ms) {
  size_t n = 0;
  uint32_t deadline = millis() + timeout_ms;
  while (n + 1 < cap) {
    if ((int32_t)(deadline - millis()) <= 0) return false;
    if (!cx.connected() && cx.available() == 0) {
      if (n == 0) return false;
      break;
    }
    if (cx.available() == 0) { delay(2); continue; }
    int c = cx.read();
    if (c < 0) continue;
    if (c == '\r') continue;
    if (c == '\n') { out[n] = '\0'; return true; }
    out[n++] = (char) c;
  }
  out[n] = '\0';
  return true;
}

Result post_sse(const char* host, uint16_t port, const char* path,
                const char* bearer_token, const char* body,
                OnEvent on_event, uint32_t overall_timeout_ms) {
  Result r{0, false, ""};

  WiFiClient cx;
  cx.setTimeout(10000);
  if (!cx.connect(host, port)) {
    std::snprintf(r.error, sizeof(r.error), "connect %s:%u failed", host, (unsigned) port);
    return r;
  }

  size_t blen = std::strlen(body);
  cx.printf(
      "POST %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Authorization: Bearer %s\r\n"
      "Content-Type: application/json\r\n"
      "Accept: text/event-stream\r\n"
      "Content-Length: %u\r\n"
      "Connection: close\r\n\r\n",
      path, host, bearer_token, (unsigned) blen);
  cx.write((const uint8_t*) body, blen);

  // Status line.
  char line[512];
  if (!read_line(cx, line, sizeof(line), 10000)) {
    std::snprintf(r.error, sizeof(r.error), "no status line");
    cx.stop();
    return r;
  }
  int sp = 0;
  while (line[sp] && line[sp] != ' ') ++sp;
  r.status_code = std::atoi(line + sp);

  // Drain headers.
  while (read_line(cx, line, sizeof(line), 10000)) {
    if (line[0] == '\0') break;
  }

  if (r.status_code < 200 || r.status_code >= 300) {
    std::snprintf(r.error, sizeof(r.error), "http %d", r.status_code);
    cx.stop();
    return r;
  }

  uint32_t deadline = millis() + overall_timeout_ms;
  while ((int32_t)(deadline - millis()) > 0) {
    if (!read_line(cx, line, sizeof(line), 5000)) break;
    if (line[0] == '\0') continue;       // event separator
    if (std::strncmp(line, "data: ", 6) != 0) continue;
    const char* payload = line + 6;
    if (std::strcmp(payload, "[DONE]") == 0) {
      r.ok = true;
      break;
    }
    on_event(payload);
  }

  cx.stop();
  // Some servers close without [DONE]; if status was 2xx, accept.
  if (!r.ok && r.status_code >= 200 && r.status_code < 300) r.ok = true;
  return r;
}

}  // namespace http_sse
