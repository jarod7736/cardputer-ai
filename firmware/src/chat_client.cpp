#include "chat_client.h"

#include <cstdio>
#include <cstring>

#include "http_sse.h"

namespace chat_client {

static std::vector<Message> s_history;
static String   s_host;
static uint16_t s_port = 0;
static String   s_bearer;

void begin() {
  s_history.clear();
  s_history.reserve(16);
}

void configure(const String& host, uint16_t port, const String& bearer) {
  s_host   = host;
  s_port   = port;
  s_bearer = bearer;
}

const std::vector<Message>& history() { return s_history; }

void reset() { s_history.clear(); }

// JSON-escape into out. Handles the small subset of characters that
// break a JSON string in user-typed text + Claude-generated text.
static void json_escape_into(String& out, const String& src) {
  for (size_t i = 0; i < src.length(); ++i) {
    char c = src[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((uint8_t) c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned) (uint8_t) c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
}

static String build_body(const String& profile_id,
                          const std::vector<Message>& msgs) {
  String out;
  out.reserve(256 + msgs.size() * 64);
  out += "{\"profile_id\":\"";
  out += profile_id;
  out += "\",\"stream\":true,\"messages\":[";
  for (size_t i = 0; i < msgs.size(); ++i) {
    if (i) out += ',';
    out += "{\"role\":\"";
    out += msgs[i].role;
    out += "\",\"content\":\"";
    json_escape_into(out, msgs[i].content);
    out += "\"}";
  }
  out += "]}";
  return out;
}

// Parse the `content` string out of an OAI delta chunk and append to
// `delta_out`. Hand-rolled to avoid pulling ArduinoJson onto the path.
// We only look at the first occurrence — OAI puts one delta per chunk.
static void extract_content(const char* line, String& delta_out) {
  const char* p = std::strstr(line, "\"content\":");
  if (!p) return;
  p += 10;                       // "content":
  while (*p == ' ') ++p;
  if (*p != '"') return;
  ++p;                           // opening quote
  while (*p && *p != '"') {
    if (*p == '\\' && p[1]) {
      ++p;
      switch (*p) {
        case 'n':  delta_out += '\n'; break;
        case 'r':  delta_out += '\r'; break;
        case 't':  delta_out += '\t'; break;
        case '"':  delta_out += '"';  break;
        case '\\': delta_out += '\\'; break;
        case '/':  delta_out += '/';  break;
        case 'u': {
          // \uXXXX → reject anything non-ASCII for M3 simplicity.
          if (p[1] && p[2] && p[3] && p[4]) {
            char hex[5] = { p[1], p[2], p[3], p[4], 0 };
            int cp = (int) std::strtol(hex, nullptr, 16);
            p += 4;
            if (cp < 0x80) delta_out += (char) cp;
            else           delta_out += '?';
          }
          break;
        }
        default: delta_out += *p;
      }
      ++p;
    } else {
      delta_out += *p++;
    }
  }
}

SendResult send(const String& profile_id,
                const String& user_text,
                std::function<void(const String&)> on_delta,
                String& assistant_out) {
  SendResult r{false, ""};
  s_history.push_back({String("user"), user_text});

  const String body = build_body(profile_id, s_history);
  assistant_out = "";

  http_sse::Result hr = http_sse::post_sse(
      s_host.c_str(), s_port,
      "/v1/chat/completions",
      s_bearer.c_str(),
      body.c_str(),
      [&](const char* line) {
        String delta;
        delta.reserve(32);
        extract_content(line, delta);
        if (delta.length() > 0) {
          assistant_out += delta;
          on_delta(delta);
        }
      });

  if (!hr.ok) {
    std::snprintf(r.error, sizeof(r.error), "proxy: %s", hr.error);
    s_history.pop_back();   // don't keep a half-sent turn
    return r;
  }

  s_history.push_back({String("assistant"), assistant_out});
  r.ok = true;
  return r;
}

}  // namespace chat_client
