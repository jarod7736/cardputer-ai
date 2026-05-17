#pragma once
#include <Arduino.h>
#include <functional>

namespace http_sse {

using OnEvent = std::function<void(const char* data_line)>;

struct Result {
  int  status_code;
  bool ok;
  char error[64];
};

// POST `body` to `host:port`+`path` with the given bearer token. Reads
// the response as a Server-Sent Events stream and calls `on_event` for
// each "data: ..." line (the leading "data: " is stripped). Returns
// when the connection closes, an event of "[DONE]" arrives, or no bytes
// have moved for `overall_timeout_ms`.
Result post_sse(
    const char* host,
    uint16_t    port,
    const char* path,
    const char* bearer_token,
    const char* body,
    OnEvent     on_event,
    uint32_t    overall_timeout_ms = 60000);

}  // namespace http_sse
