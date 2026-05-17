#include "nvs_config.h"

#include <Preferences.h>

namespace nvs_config {

namespace {
constexpr const char* kNsWifi   = "m5wifi";
constexpr const char* kNsWg     = "m5wg";
constexpr const char* kNsProxy  = "m5proxy";
constexpr const char* kNsDevice = "m5dev";

constexpr const char* kSchemaKey = "schema";

// Open a namespace read-only; returns true if it opens AND schema matches.
bool open_readable(Preferences& p, const char* ns) {
  if (!p.begin(ns, /*readOnly=*/true)) return false;
  uint8_t v = p.getUChar(kSchemaKey, 0);
  if (v != kSchemaVersion) {
    p.end();
    return false;
  }
  return true;
}

// Open a namespace writable. Always sets the schema key on first touch.
void open_writable(Preferences& p, const char* ns) {
  p.begin(ns, /*readOnly=*/false);
  p.putUChar(kSchemaKey, kSchemaVersion);
}

}  // namespace

void begin() {
  // No global state — each accessor opens its own scope. This keeps
  // failure of one namespace from poisoning the others.
}

// --- wifi -------------------------------------------------------------------

bool has_wifi() {
  Preferences p;
  if (!open_readable(p, kNsWifi)) return false;
  bool ok = p.isKey("ssid") && p.getString("ssid", "").length() > 0;
  p.end();
  return ok;
}

Wifi read_wifi() {
  Wifi w;
  Preferences p;
  if (!open_readable(p, kNsWifi)) return w;
  w.ssid = p.getString("ssid", "");
  w.pass = p.getString("pass", "");
  p.end();
  return w;
}

void write_wifi(const Wifi& w) {
  Preferences p;
  open_writable(p, kNsWifi);
  p.putString("ssid", w.ssid);
  p.putString("pass", w.pass);
  p.end();
}

// --- wg ---------------------------------------------------------------------

bool has_wg() {
  Preferences p;
  if (!open_readable(p, kNsWg)) return false;
  bool ok = p.getString("priv", "").length() > 0
         && p.getString("peer_pub", "").length() > 0
         && p.getString("addr", "").length() > 0;
  p.end();
  return ok;
}

Wg read_wg() {
  Wg w;
  Preferences p;
  if (!open_readable(p, kNsWg)) return w;
  w.priv_key        = p.getString("priv", "");
  w.peer_pub        = p.getString("peer_pub", "");
  w.endpoint_host   = p.getString("ep_host", "");
  w.endpoint_port   = p.getUShort("ep_port", 51820);
  w.addr            = p.getString("addr", "");
  w.netmask         = p.getString("netmask", "255.255.255.255");
  w.allowed_ip_cidr = p.getString("allowed", "0.0.0.0/0");
  p.end();
  return w;
}

void write_wg(const Wg& w) {
  Preferences p;
  open_writable(p, kNsWg);
  p.putString("priv",     w.priv_key);
  p.putString("peer_pub", w.peer_pub);
  p.putString("ep_host",  w.endpoint_host);
  p.putUShort("ep_port",  w.endpoint_port);
  p.putString("addr",     w.addr);
  p.putString("netmask",  w.netmask);
  p.putString("allowed",  w.allowed_ip_cidr);
  p.end();
}

// --- proxy ------------------------------------------------------------------

bool has_proxy() {
  Preferences p;
  if (!open_readable(p, kNsProxy)) return false;
  bool ok = p.getString("host", "").length() > 0
         && p.getString("bearer", "").length() > 0;
  p.end();
  return ok;
}

Proxy read_proxy() {
  Proxy r;
  Preferences p;
  if (!open_readable(p, kNsProxy)) return r;
  r.host               = p.getString("host", "");
  r.port               = p.getUShort("port", 8420);
  r.bearer             = p.getString("bearer", "");
  r.default_profile_id = p.getString("def_prof", "claude-opus");
  p.end();
  return r;
}

void write_proxy(const Proxy& pr) {
  Preferences p;
  open_writable(p, kNsProxy);
  p.putString("host",     pr.host);
  p.putUShort("port",     pr.port);
  p.putString("bearer",   pr.bearer);
  p.putString("def_prof", pr.default_profile_id);
  p.end();
}

// --- device -----------------------------------------------------------------

bool has_device() {
  Preferences p;
  if (!open_readable(p, kNsDevice)) return false;
  bool ok = p.getString("dev_id", "").length() > 0;
  p.end();
  return ok;
}

Device read_device() {
  Device d;
  Preferences p;
  if (!open_readable(p, kNsDevice)) return d;
  d.device_id       = p.getString("dev_id", "");
  d.provisioned_at  = p.getString("prov_at", "");
  p.end();
  return d;
}

void write_device(const Device& d) {
  Preferences p;
  open_writable(p, kNsDevice);
  p.putString("dev_id",  d.device_id);
  p.putString("prov_at", d.provisioned_at);
  p.end();
}

// --- whole-device -----------------------------------------------------------

bool is_provisioned() {
  return has_wifi() && has_wg() && has_proxy() && has_device();
}

void wipe_all() {
  for (const char* ns : {kNsWifi, kNsWg, kNsProxy, kNsDevice}) {
    Preferences p;
    p.begin(ns, /*readOnly=*/false);
    p.clear();
    p.end();
  }
}

}  // namespace nvs_config
