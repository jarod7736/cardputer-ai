#include "sd_bundle.h"

#include <SD.h>
#include <SPI.h>

namespace sd_bundle {

namespace {
constexpr int kSckPin  = 40;
constexpr int kMisoPin = 39;
constexpr int kMosiPin = 14;
constexpr int kCsPin   = 12;

bool s_mounted = false;

// Read a whole text file into a String. Returns false on missing or empty.
bool read_file(const char* path, String& out) {
  if (!SD.exists(path)) return false;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  out.reserve(f.size());
  while (f.available()) out += (char) f.read();
  f.close();
  return out.length() > 0;
}

// Trim ASCII whitespace from both ends in place.
void trim(String& s) {
  int start = 0, end = (int) s.length();
  while (start < end && isspace((unsigned char) s[start])) ++start;
  while (end > start && isspace((unsigned char) s[end - 1])) --end;
  if (start > 0 || end < (int) s.length()) s = s.substring(start, end);
}

void strip_inline_comment(String& s) {
  int hash = s.indexOf('#');
  if (hash >= 0) s = s.substring(0, hash);
}

// Parse wg.conf text into bundle. Returns error string (empty on success).
String parse_wg_conf(const String& text, Bundle& b) {
  // Walk line by line, track current section, key = value splits.
  String section;
  int line_start = 0;
  bool saw_priv = false, saw_addr = false, saw_pub = false, saw_endpoint = false;
  for (int i = 0; i <= (int) text.length(); ++i) {
    if (i < (int) text.length() && text[i] != '\n' && text[i] != '\r') continue;
    String line = text.substring(line_start, i);
    line_start = i + 1;
    if (i < (int) text.length() && text[i] == '\r'
        && i + 1 < (int) text.length() && text[i + 1] == '\n') {
      // Skip the LF after CR.
      ++line_start;
      ++i;
    }
    strip_inline_comment(line);
    trim(line);
    if (line.length() == 0) continue;
    if (line[0] == '[' && line[line.length() - 1] == ']') {
      section = line.substring(1, line.length() - 1);
      section.toLowerCase();
      continue;
    }
    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String key   = line.substring(0, eq);
    String value = line.substring(eq + 1);
    trim(key); trim(value);
    key.toLowerCase();

    if (section == "interface") {
      if (key == "privatekey") { b.wg_priv_key = value; saw_priv = true; }
      else if (key == "address") { b.wg_address = value; saw_addr = true; }
      // ignore MTU and DNS
    } else if (section == "peer") {
      if (key == "publickey") { b.wg_peer_pub = value; saw_pub = true; }
      else if (key == "endpoint") { b.wg_endpoint = value; saw_endpoint = true; }
      else if (key == "allowedips") { b.wg_allowed_ips = value; }
      else if (key == "persistentkeepalive") { b.wg_keepalive = (uint16_t) value.toInt(); }
    }
  }

  if (!saw_priv)     return "wg.conf: missing [Interface] PrivateKey";
  if (!saw_addr)     return "wg.conf: missing [Interface] Address";
  if (!saw_pub)      return "wg.conf: missing [Peer] PublicKey";
  if (!saw_endpoint) return "wg.conf: missing [Peer] Endpoint";
  return "";
}

// Tiny JSON walker. Finds "key" : "value" or "key" : <number>. Same
// approach as proxy_api/chat_client — handles our exact shape, no
// general parser.
bool find_str(const String& src, const char* key, String& out) {
  String pat = String("\"") + key + "\"";
  int k = src.indexOf(pat);
  if (k < 0) return false;
  int colon = src.indexOf(':', k + pat.length());
  if (colon < 0) return false;
  // skip whitespace
  int i = colon + 1;
  while (i < (int) src.length() && isspace((unsigned char) src[i])) ++i;
  if (i >= (int) src.length() || src[i] != '"') return false;
  ++i;  // opening quote
  String tmp;
  while (i < (int) src.length() && src[i] != '"') {
    if (src[i] == '\\' && i + 1 < (int) src.length()) {
      ++i;
      char c = src[i];
      switch (c) {
        case '"': tmp += '"';  break;
        case '\\': tmp += '\\'; break;
        case '/': tmp += '/';  break;
        case 'n': tmp += '\n'; break;
        case 't': tmp += '\t'; break;
        default:  tmp += c;
      }
    } else {
      tmp += src[i];
    }
    ++i;
  }
  if (i >= (int) src.length()) return false;
  out = tmp;
  return true;
}

bool find_int(const String& src, const char* key, long& out) {
  String pat = String("\"") + key + "\"";
  int k = src.indexOf(pat);
  if (k < 0) return false;
  int colon = src.indexOf(':', k + pat.length());
  if (colon < 0) return false;
  int i = colon + 1;
  while (i < (int) src.length() && isspace((unsigned char) src[i])) ++i;
  String num;
  while (i < (int) src.length() && (isdigit((unsigned char) src[i]) || src[i] == '-')) {
    num += src[i++];
  }
  if (num.length() == 0) return false;
  out = num.toInt();
  return true;
}

String parse_proxy_json(const String& text, Bundle& b) {
  if (!find_str(text, "host",   b.proxy_host))     return "proxy.json: missing host";
  if (!find_str(text, "bearer", b.proxy_bearer))   return "proxy.json: missing bearer";
  if (!find_str(text, "device_id", b.device_id))   return "proxy.json: missing device_id";
  long port = 0;
  if (!find_int(text, "port", port))               return "proxy.json: missing port";
  b.proxy_port = (uint16_t) port;
  // optional
  find_str(text, "default_profile_id", b.default_profile_id);
  if (b.default_profile_id.length() == 0) b.default_profile_id = "claude-opus";
  return "";
}

}  // namespace

bool mount() {
  if (s_mounted) return true;
  SPI.begin(kSckPin, kMisoPin, kMosiPin, kCsPin);
  if (!SD.begin(kCsPin, SPI, 25000000)) {
    SPI.end();
    return false;
  }
  s_mounted = true;
  return true;
}

void unmount() {
  if (!s_mounted) return;
  SD.end();
  SPI.end();
  s_mounted = false;
}

Bundle try_read() {
  Bundle b;
  if (!mount()) {
    b.status = Status::kAbsent;
    return b;
  }
  String wg_text, pj_text;
  bool wg_ok = read_file("/wg.conf", wg_text);
  bool pj_ok = read_file("/proxy.json", pj_text);
  if (!wg_ok && !pj_ok) {
    b.status = Status::kAbsent;
    return b;
  }
  if (!wg_ok) { b.status = Status::kInvalid; b.error = "wg.conf missing on SD"; return b; }
  if (!pj_ok) { b.status = Status::kInvalid; b.error = "proxy.json missing on SD"; return b; }
  String err = parse_wg_conf(wg_text, b);
  if (err.length() > 0) { b.status = Status::kInvalid; b.error = err; return b; }
  err = parse_proxy_json(pj_text, b);
  if (err.length() > 0) { b.status = Status::kInvalid; b.error = err; return b; }
  b.status = Status::kOk;
  return b;
}

bool wipe() {
  if (!mount()) return false;
  bool wg_ok = !SD.exists("/wg.conf") || SD.remove("/wg.conf");
  bool pj_ok = !SD.exists("/proxy.json") || SD.remove("/proxy.json");
  return wg_ok && pj_ok;
}

}  // namespace sd_bundle
