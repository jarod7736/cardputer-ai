#include "provisioning.h"

#include "nvs_config.h"
#include "sd_bundle.h"

namespace provisioning {

namespace {

// Split "host:port" → (host, port). Falls back to (input, 0) on no colon.
void split_endpoint(const String& ep, String& host, uint16_t& port) {
  int colon = ep.lastIndexOf(':');
  if (colon < 0) { host = ep; port = 0; return; }
  host = ep.substring(0, colon);
  port = (uint16_t) ep.substring(colon + 1).toInt();
}

// Split "10.42.0.2/32" → (addr, "255.255.255.255"). The device only
// uses /32 on the WG tunnel; the mask is derived for esp_wireguard.
void split_addr_with_mask(const String& cidr, String& addr, String& mask) {
  int slash = cidr.indexOf('/');
  if (slash < 0) { addr = cidr; mask = "255.255.255.255"; return; }
  addr = cidr.substring(0, slash);
  int bits = cidr.substring(slash + 1).toInt();
  // Build dotted-quad netmask from prefix bits (IPv4 only — IPv6 not in scope).
  uint32_t m = (bits == 0) ? 0 : (0xFFFFFFFFu << (32 - bits));
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                (unsigned)((m >> 24) & 0xFF),
                (unsigned)((m >> 16) & 0xFF),
                (unsigned)((m >> 8)  & 0xFF),
                (unsigned)( m        & 0xFF));
  mask = buf;
}

}  // namespace

Outcome run(Stream& log) {
  Outcome out;

  auto bundle = sd_bundle::try_read();
  switch (bundle.status) {
    case sd_bundle::Status::kAbsent:
      sd_bundle::unmount();
      out.result = Result::kNoBundle;
      return out;
    case sd_bundle::Status::kInvalid:
      sd_bundle::unmount();
      out.result = Result::kFailed;
      out.detail = bundle.error;
      log.printf("[provisioning] invalid bundle: %s\n", out.detail.c_str());
      return out;
    case sd_bundle::Status::kOk:
      break;
  }

  if (nvs_config::is_provisioned()) {
    sd_bundle::unmount();
    out.result = Result::kAlreadyProvisioned;
    out.detail = "remove SD bundle, or factory-reset to re-provision";
    log.println("[provisioning] SD bundle ignored — NVS already provisioned");
    return out;
  }

  log.println("[provisioning] committing SD bundle to NVS...");

  // Build NVS structs from the bundle.
  // Wi-Fi credentials don't ride in the bundle yet (M5 scope: WG +
  // proxy + device id). The user supplies Wi-Fi via a separate path
  // before re-flashing — for now, fall back to the proxy contact if
  // wifi NVS is already populated (it is, on the dev unit from M4).
  // TODO(M5.5): include wifi in the bundle as well.

  nvs_config::Wg wg;
  wg.priv_key       = bundle.wg_priv_key;
  wg.peer_pub       = bundle.wg_peer_pub;
  split_endpoint(bundle.wg_endpoint, wg.endpoint_host, wg.endpoint_port);
  split_addr_with_mask(bundle.wg_address, wg.addr, wg.netmask);
  wg.allowed_ip_cidr = bundle.wg_allowed_ips.length() > 0
                       ? bundle.wg_allowed_ips
                       : String("0.0.0.0/0");
  nvs_config::write_wg(wg);

  nvs_config::Proxy proxy;
  proxy.host               = bundle.proxy_host;
  proxy.port               = bundle.proxy_port;
  proxy.bearer             = bundle.proxy_bearer;
  proxy.default_profile_id = bundle.default_profile_id;
  nvs_config::write_proxy(proxy);

  nvs_config::Device dev;
  dev.device_id      = bundle.device_id;
  // No RTC on this boot — use the ESP boot epoch as a tag.
  dev.provisioned_at = String((uint32_t) (millis() / 1000));
  nvs_config::write_device(dev);

  // Wi-Fi: if header secrets are still compiled in AND no wifi NVS
  // yet, the M3 wg_secrets.h symbols will be used at runtime by
  // wifi_sta::connect() — no NVS entry needed here. If neither, the
  // boot path will surface "wifi missing" and the user can hit Fn+S
  // to set it (Settings UI will gain a wifi-set action in M5.5).

  if (!sd_bundle::wipe()) {
    log.println("[provisioning] WARN: SD wipe failed; bundle remains on card");
    // Don't fail — NVS already has the data; the user can pull the card.
  }
  sd_bundle::unmount();

  out.result = Result::kCommitted;
  out.detail = "provisioned " + bundle.device_id;
  log.printf("[provisioning] committed: %s\n", bundle.device_id.c_str());
  return out;
}

}  // namespace provisioning
