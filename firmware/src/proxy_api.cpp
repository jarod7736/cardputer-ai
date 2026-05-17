#include "proxy_api.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <cstdio>
#include <cstring>

namespace proxy_api {

static String   s_host;
static uint16_t s_port = 0;
static String   s_bearer;

void configure(const String& host, uint16_t port, const String& bearer) {
  s_host   = host;
  s_port   = port;
  s_bearer = bearer;
}


static const char* skip_ws(const char* p) {
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
  return p;
}

static const char* read_string(const char* p, String& out) {
  out = "";
  if (*p != '"') return nullptr;
  ++p;
  while (*p && *p != '"') {
    if (*p == '\\' && p[1]) {
      char c = p[1];
      switch (c) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case '"': out += '"';  break;
        case '\\': out += '\\'; break;
        case '/':  out += '/';  break;
        default: out += c;
      }
      p += 2;
    } else {
      out += *p++;
    }
  }
  if (*p != '"') return nullptr;
  return p + 1;
}

static const char* find_field(const char* obj_start, const char* obj_end,
                              const char* key) {
  // Naive: search "key": within the object range.
  String pat = String("\"") + key + "\":";
  const char* hit = std::strstr(obj_start, pat.c_str());
  if (!hit || hit >= obj_end) return nullptr;
  return skip_ws(hit + pat.length());
}

static const char* find_obj_end(const char* obj_start) {
  if (*obj_start != '{') return nullptr;
  int depth = 0;
  bool in_str = false;
  bool esc    = false;
  for (const char* p = obj_start; *p; ++p) {
    if (in_str) {
      if (esc) { esc = false; continue; }
      if (*p == '\\') { esc = true; continue; }
      if (*p == '"')  { in_str = false; continue; }
    } else {
      if (*p == '"') { in_str = true; continue; }
      if (*p == '{') ++depth;
      else if (*p == '}') { --depth; if (depth == 0) return p + 1; }
    }
  }
  return nullptr;
}

FetchResult fetch_profiles() {
  FetchResult r{false, "", {}};

  WiFiClient cx;
  cx.setTimeout(10000);
  if (!cx.connect(s_host.c_str(), s_port)) {
    std::snprintf(r.error, sizeof(r.error), "connect failed");
    return r;
  }

  cx.printf(
      "GET /v1/profiles HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Authorization: Bearer %s\r\n"
      "Accept: application/json\r\n"
      "Connection: close\r\n\r\n",
      s_host.c_str(), s_bearer.c_str());

  // Drain headers, accumulate body.
  String body;
  body.reserve(2048);
  bool in_body = false;
  String line;
  line.reserve(128);
  uint32_t deadline = millis() + 15000;
  while ((int32_t)(deadline - millis()) > 0 && (cx.connected() || cx.available())) {
    while (cx.available()) {
      int c = cx.read();
      if (c < 0) break;
      if (!in_body) {
        if (c == '\r') continue;
        if (c == '\n') {
          if (line.length() == 0) in_body = true;
          else                    line = "";
          continue;
        }
        line += (char) c;
      } else {
        body += (char) c;
      }
    }
    if (!cx.connected() && !cx.available()) break;
    delay(2);
  }
  cx.stop();

  // Walk "profiles":[ ... ]
  const char* p = std::strstr(body.c_str(), "\"profiles\":");
  if (!p) {
    std::snprintf(r.error, sizeof(r.error), "no profiles field");
    return r;
  }
  p = skip_ws(p + 11);
  if (*p != '[') {
    std::snprintf(r.error, sizeof(r.error), "bad json");
    return r;
  }
  ++p;
  while (true) {
    p = skip_ws(p);
    if (*p == ']') break;
    if (*p != '{') {
      std::snprintf(r.error, sizeof(r.error), "bad json");
      return r;
    }
    const char* obj_end = find_obj_end(p);
    if (!obj_end) {
      std::snprintf(r.error, sizeof(r.error), "unterminated obj");
      return r;
    }

    Profile prof;
    if (const char* v = find_field(p, obj_end, "id"))       read_string(v, prof.id);
    if (const char* v = find_field(p, obj_end, "label"))    read_string(v, prof.label);
    if (const char* v = find_field(p, obj_end, "provider")) read_string(v, prof.provider);
    if (const char* v = find_field(p, obj_end, "model"))    read_string(v, prof.model);
    if (prof.id.length() > 0) r.profiles.push_back(std::move(prof));

    p = obj_end;
    p = skip_ws(p);
    if (*p == ',') ++p;
  }
  r.ok = true;
  return r;
}

}  // namespace proxy_api
