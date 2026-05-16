# M1 — WireGuard Tunnel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up an end-to-end WireGuard tunnel from the Cardputer-ADV to the home UDM, so that the device can reach a private IP on the home LAN. Output: a successful `ping` (or HTTP round-trip) from the firmware to a hard-coded test endpoint inside the home network, proven over the WG tunnel and not over the Wi-Fi LAN directly.

**Architecture:** The firmware migrates from M0's Arduino-only PlatformIO project to an **Arduino-on-ESP-IDF** hybrid (`framework = arduino, espidf` in PlatformIO) so that `esp_wireguard` — an ESP-IDF component — can sit beside the high-level Arduino + M5Unified UI code we already have. WireGuard configuration for M1 is **hard-coded in source** (key material in a separate `.gitignored` header) so we are only testing the tunnel, not the provisioning flow (that is M5's problem). Server-side, we add one peer entry on the UDM and document the steps; no automation yet.

**Tech Stack:**
- Existing M0 project re-platformed: `firmware/platformio.ini` switches to `framework = arduino, espidf` and `board = m5stack-cardputer-adv` (M0 proved M5Unified detects this board natively, so we use the specific profile to get `M5.Keyboard` and other ADV bindings — see M0 follow-up #1).
- New library dep: `trombik/esp_wireguard` (or its actively maintained fork; see Task 0 for selection).
- ESP-IDF Wi-Fi STA + `esp_netif` for the underlying link.
- UDM-side WireGuard Server (Network app, "VPN → WireGuard Server" or equivalent path on the user's UDM firmware version).

**Out of scope for M1:**
- Proxy MVP (that is M2)
- Dynamic / per-peer config from SD (that is M5)
- Persistent retry across reboots beyond a basic supervisor loop
- TLS-anything (we're testing UDP transport, not application-layer crypto)

**Carry-overs from M0** (recorded in `docs/m0-hardware-report.md`):
- No PSRAM. Stack/heap budget stays tight. `esp_wireguard` is small (~50 KB code, modest RAM); fine.
- `board_M5CardputerADV` is the M5Unified board id. Use the matching PlatformIO board profile.
- ESP-IDF 5.x + mbedTLS + lwIP build cleanly under PlatformIO — `esp_wireguard` depends on both, so we're unblocked.

**Verification style:** M1 has a real testable success criterion: can the device reach a private home IP that it cannot reach without the tunnel? We test this by pinging two endpoints back-to-back — one only reachable via WG, one only reachable via the local LAN — and asserting the WG-only one succeeds and the LAN-only one fails. That is the strongest possible evidence that traffic is actually going through the tunnel.

---

## File structure

Changes to existing files and new files this plan introduces:

```
firmware/
├── platformio.ini                              (modified — switch to arduino+espidf, ADV board)
├── sdkconfig.defaults                          (NEW — IDF config for arduino-as-component build)
├── partitions.csv                              (NEW — explicit 8MB partition map with NVS reserve)
├── include/
│   └── wg_secrets.h.example                    (NEW — template for the gitignored wg_secrets.h)
└── src/
    ├── main.cpp                                (modified — boot wifi + WG, run reachability test)
    ├── wifi_sta.h / wifi_sta.cpp               (NEW — minimal Wi-Fi STA bring-up)
    ├── wg_link.h / wg_link.cpp                 (NEW — esp_wireguard init + supervisor)
    ├── net_probe.h / net_probe.cpp             (NEW — pings/HTTP-gets that prove tunnel direction)
    └── probe_*.{h,cpp}                         (unchanged from M0; kept on a key-trigger path)

docs/
├── m1-wireguard-report.md                      (NEW — captures handshake times, RTTs, gotchas)
└── ops/
    └── udm-wireguard-setup.md                  (NEW — exact UI/CLI steps used on the UDM)

.gitignore                                       (modified — add firmware/include/wg_secrets.h)
```

The split keeps `wg_link` agnostic of any specific app code: it just brings the tunnel up, exposes "is the tunnel up?" + "is the last-known handshake fresh?", and reports state changes. `net_probe` is the layer that *uses* the tunnel to prove something. Future milestones (proxy traffic, OTA) plug into `wg_link`'s "tunnel up" event, not into the WG library directly.

---

## Task 0: Pick the WireGuard library

**Why it's a task:** the upstream `trombik/esp_wireguard` repo has been stagnant in the past; multiple forks have moved on with ESP-IDF 5.x fixes. M0 proved IDF 5.x compiles for us, so we want a library that targets the same version. We resolve this *first* because it affects every later task.

**Files:** decision recorded in `docs/m1-wireguard-report.md` (created in this task).

- [ ] **Step 1: Survey candidate libraries**

For each of these, check (a) last commit date, (b) declared ESP-IDF version compatibility, (c) presence of a PlatformIO `library.json`, (d) open-issue list for ESP-IDF 5.x problems:

- `trombik/esp_wireguard` (upstream)
- `ciniml/WireGuard-ESP32` (PlatformIO-friendly fork)
- any fork referenced in the upstream repo's issues as "use this one instead"

Run: `git ls-remote https://github.com/trombik/esp_wireguard.git HEAD` for each to get the head commit; eyeball the GitHub UI for the rest.

- [ ] **Step 2: Pick one, record the decision**

Create `docs/m1-wireguard-report.md` with this content (filling `<…>` placeholders):

```markdown
# M1 WireGuard Report

**Library chosen:** `<owner/repo @ pinned-rev>`
**Selection reasoning:** <2-3 sentences: last-commit date, IDF-5.x compat, PlatformIO support>
**Alternatives considered:** <bullet list with one line each on why not>

(Sections below filled in as later tasks complete.)

## UDM-side configuration
TBD (filled in by Task 1)

## First handshake
TBD (filled in by Task 5)

## Reachability proof
TBD (filled in by Task 6)
```

- [ ] **Step 3: Commit the decision**

```bash
cd /home/jarod7736/workspace/cardputer-ai
git add docs/m1-wireguard-report.md
git commit -m "docs(m1): pick WireGuard library and record reasoning"
```

---

## Task 1: UDM-side WireGuard server + first peer

We bring up the server side *before* writing firmware so we have a known-good target. If the UDM side is misconfigured, debugging the firmware is hopeless.

**Files:**
- Create: `docs/ops/udm-wireguard-setup.md`
- Modify: `docs/m1-wireguard-report.md` (UDM section)

- [ ] **Step 1: Generate a peer keypair on a Linux box (not on the UDM, not in the firmware)**

```bash
wg genkey | tee /tmp/cardputer.key | wg pubkey > /tmp/cardputer.pub
cat /tmp/cardputer.key   # device private key (we'll embed in firmware temporarily)
cat /tmp/cardputer.pub   # device public key (give to UDM)
```

Record these values somewhere private. The private key will land in a `.gitignore`d `wg_secrets.h` (Task 2 step 4).

- [ ] **Step 2: Configure the UDM WireGuard Server**

Open the UniFi Network UI → **Settings → Teleport & VPN → VPN Server → WireGuard** (path varies by UniFi firmware version; current as of mid-2026).

- Create a server named `cardputer-vpn`.
- Server listen port: keep default `51820` (or pick another UDP port if 51820 is in use; record what you chose).
- Server subnet: pick a /24 that does NOT overlap any existing LAN. `10.7.0.0/24` is a safe default. Server itself is `10.7.0.1`.
- Add a client/peer named `cardputer-edc`:
  - Public key: paste the `.pub` from Step 1.
  - Allowed IPs (server-side): `10.7.0.2/32` — the address the device will use.
  - DNS: leave blank for M1 (we'll resolve later via proxy; for now the firmware uses raw IPs).

Note the **server public key** the UDM displays — the device needs it. Note the **endpoint** the UDM exposes (public IPv4:port, or your dynamic-DNS hostname).

- [ ] **Step 3: Test the server with a known-good client first**

Before flashing anything to the Cardputer, prove the server works using a phone or laptop with the WireGuard app:

```ini
[Interface]
PrivateKey = <client priv from a *throwaway* keypair, not Step 1>
Address = 10.7.0.3/32

[Peer]
PublicKey = <UDM server pubkey>
Endpoint  = <udm-endpoint>:51820
AllowedIPs = 192.168.1.0/24       # only push LAN through tunnel for the test
PersistentKeepalive = 25
```

From that client, with WG enabled, run `ping 192.168.1.1` (or whatever the UDM's LAN IP is). It should succeed. Disable WG → ping fails (assuming you're not already on the home LAN). That's our "server side is good" baseline.

- [ ] **Step 4: Record what you did in `docs/ops/udm-wireguard-setup.md`**

Write a numbered, copy-pasteable runbook of the exact UI clicks (or CLI commands if you used SSH into the UDM) so M5's provisioning automation has something to model. Include screenshots if helpful, but prefer text. Sections:

1. UniFi firmware version + UI location of WG settings
2. Server creation (name, port, subnet)
3. Peer entry (address, allowed IPs, DNS)
4. Where to find the server pubkey + endpoint in the UI
5. How to revoke a peer (one click — needed for the M0 security story)

- [ ] **Step 5: Mirror the key facts into `m1-wireguard-report.md`**

Replace the `## UDM-side configuration` TBD block with:

- Server endpoint (DNS or IP : port)
- Server pubkey
- Server subnet
- This device's assigned IP (`10.7.0.2`)
- A note pointing at `docs/ops/udm-wireguard-setup.md` for the full runbook.

- [ ] **Step 6: Commit**

```bash
git add docs/ops/udm-wireguard-setup.md docs/m1-wireguard-report.md
git commit -m "docs(m1): UDM WireGuard server setup runbook and config record"
```

---

## Task 2: Repo + project re-platform (Arduino + ESP-IDF hybrid)

We switch the existing M0 project from pure Arduino to Arduino-as-an-ESP-IDF-component. M5Unified continues to work, but now we can pull in ESP-IDF components like `esp_wireguard` and use the IDF Wi-Fi/`esp_netif` APIs.

**Files:**
- Modify: `firmware/platformio.ini`
- Create: `firmware/sdkconfig.defaults`
- Create: `firmware/partitions.csv`
- Create: `firmware/include/wg_secrets.h.example`
- Modify: `.gitignore`

- [ ] **Step 1: Update `firmware/platformio.ini`**

Replace the current `[env:cardputer-adv]` block with:

```ini
[env:cardputer-adv]
platform = espressif32 @ ^6.7.0
board = m5stack-cardputer
framework = arduino, espidf

upload_speed = 921600
monitor_speed = 115200
monitor_filters = esp32_exception_decoder, time

build_flags =
  -D ARDUINO_USB_CDC_ON_BOOT=1
  -D ARDUINO_USB_MODE=1
  -D CORE_DEBUG_LEVEL=3
  -D CONFIG_ARDUHAL_LOG_COLORS=1

board_upload.flash_size = 8MB
board_build.flash_size = 8MB
board_build.partitions = partitions.csv

lib_deps =
  m5stack/M5Unified @ ^0.2.5
  ; WireGuard library selected in Task 0:
  ; (fill in the dep line after Task 0 completes, e.g.)
  ; ciniml/WireGuard-ESP32 @ ^0.3.0
```

> The board name `m5stack-cardputer` is the closest upstream PlatformIO profile. If your installed espressif32 version exposes a dedicated `m5stack-cardputer-adv` board, prefer it. Verify with `pio boards | grep -i cardputer`. If only the original Cardputer profile is available, that's fine — M5Unified runtime-detects the ADV variant (we proved this in M0).

- [ ] **Step 2: Create `firmware/sdkconfig.defaults`**

```
# Cardputer-AI M1 ESP-IDF defaults
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y

# We need lwIP raw, mbedTLS bigger numbers for WG handshake math, and PPP off.
CONFIG_LWIP_PPP_SUPPORT=n
CONFIG_LWIP_IPV4=y
CONFIG_LWIP_IPV6=n
CONFIG_MBEDTLS_HARDWARE_SHA=y
CONFIG_MBEDTLS_HARDWARE_AES=y

# We have no PSRAM (M0 result). Don't pretend.
CONFIG_SPIRAM=n

# Bring up Wi-Fi STA with sane buffer counts; tuned in M2 if needed.
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32
CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=8
```

- [ ] **Step 3: Create `firmware/partitions.csv`**

```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x6000,
otadata,  data, ota,     0xF000,   0x2000,
phy_init, data, phy,     0x11000,  0x1000,
factory,  app,  factory, 0x20000,  0x340000,
ota_0,    app,  ota_0,   0x360000, 0x340000,
spiffs,   data, spiffs,  0x6A0000, 0x150000,
```

> ~3.25 MB per app slot (factory + ota_0), ~1.3 MB SPIFFS for fonts and last-known-good caches, 24 KB NVS. Same shape DESIGN.md §9 sketched, sized to land inside 8 MB after the bootloader.

- [ ] **Step 4: Create `firmware/include/wg_secrets.h.example`**

```cpp
// Template for firmware/include/wg_secrets.h — copy to wg_secrets.h
// and fill in your peer keys. The real file is .gitignored.
//
// Generate keys with:  wg genkey | tee priv | wg pubkey > pub
//
// DO NOT COMMIT THE REAL wg_secrets.h.

#pragma once

namespace wg_secrets {

// Base64-encoded WireGuard keys (44 chars + '\0').
inline constexpr const char* kDevicePrivateKey = "<PASTE_DEVICE_PRIVATE_KEY_HERE>";
inline constexpr const char* kPeerPublicKey    = "<PASTE_SERVER_PUBLIC_KEY_HERE>";

// Server endpoint and ports.
inline constexpr const char* kPeerEndpointHost = "<udm-public-host-or-ip>";
inline constexpr uint16_t    kPeerEndpointPort = 51820;

// Addresses inside the WG subnet (10.7.0.0/24 chosen in Task 1).
inline constexpr const char* kDeviceAddress    = "10.7.0.2";
inline constexpr const char* kDeviceNetmask    = "255.255.255.0";

// What the firmware will route into the tunnel.
// For M1 we route only the home LAN; everything else stays on Wi-Fi.
inline constexpr const char* kAllowedIPCIDR    = "192.168.1.0/24";

// Wi-Fi credentials (M1 only; M5 will move these to SD-provisioned NVS).
inline constexpr const char* kWifiSSID         = "<your-ssid>";
inline constexpr const char* kWifiPassword     = "<your-wifi-password>";

}  // namespace wg_secrets
```

- [ ] **Step 5: Add the real header to `.gitignore`**

Append to `.gitignore`:

```gitignore

# WireGuard / Wi-Fi secrets embedded for M1 dev only.
firmware/include/wg_secrets.h
```

- [ ] **Step 6: Build the empty re-platformed project**

Run: `cd firmware && pio run`
Expected: build succeeds (first run downloads ESP-IDF, may take 5-10 minutes). Final size summary should show app partition usage well under the 3.34 MB limit. If the build fails with `m5stack-cardputer` board not found, change `board` back to `esp32-s3-devkitc-1` and add `-D BOARD_HAS_M5CARDPUTER_ADV` to build flags — M5Unified's runtime detection still works.

- [ ] **Step 7: Commit**

```bash
git add firmware/platformio.ini firmware/sdkconfig.defaults firmware/partitions.csv firmware/include/wg_secrets.h.example .gitignore
git commit -m "feat(m1): re-platform to arduino+espidf hybrid; template wg secrets"
```

---

## Task 3: Wi-Fi STA bring-up

WireGuard needs a working Layer 3 link first. We bring up Wi-Fi as a station with the (still-hardcoded) SSID/password from `wg_secrets.h`, and prove DHCP and DNS work.

**Files:**
- Create: `firmware/src/wifi_sta.h`
- Create: `firmware/src/wifi_sta.cpp`
- Modify: `firmware/src/main.cpp`
- Create: `firmware/include/wg_secrets.h` (local copy of the template, filled in — NOT committed)

- [ ] **Step 1: Copy the template and fill in your secrets locally**

```bash
cp firmware/include/wg_secrets.h.example firmware/include/wg_secrets.h
$EDITOR firmware/include/wg_secrets.h     # fill in real keys + SSID
```

`git status` should report the file as ignored (or not show it at all). Confirm before continuing.

- [ ] **Step 2: Create `firmware/src/wifi_sta.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <IPAddress.h>

namespace wifi_sta {

// Connects to the SSID/password supplied at compile time. Blocks until
// either the device has an IPv4 address from DHCP or `timeout_ms`
// elapses. Logs status to `out`. Returns true on success.
bool connect(const char* ssid, const char* password, Stream& out,
             uint32_t timeout_ms = 30000);

// Convenience: true if currently associated AND has a non-zero IP.
bool is_up();

// The last DHCP-assigned IPv4 address (0.0.0.0 if not connected).
IPAddress local_ip();

}  // namespace wifi_sta
```

- [ ] **Step 3: Create `firmware/src/wifi_sta.cpp`**

```cpp
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

  out.printf("wifi: connected\n");
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
```

- [ ] **Step 4: Wire Wi-Fi into `main.cpp`**

Replace the contents of `firmware/src/main.cpp` with:

```cpp
#include <Arduino.h>
#include "wifi_sta.h"
#include "wg_secrets.h"   // gitignored — see wg_secrets.h.example

void setup() {
  Serial.begin(115200);
  uint32_t deadline = millis() + 10000;
  while (!Serial && millis() < deadline) delay(50);
  delay(500);

  Serial.println();
  Serial.println("==== Cardputer-AI M1 — Wi-Fi STA bring-up ====");

  if (!wifi_sta::connect(wg_secrets::kWifiSSID, wg_secrets::kWifiPassword,
                         Serial)) {
    Serial.println("STOP: cannot continue without Wi-Fi");
    return;
  }

  Serial.println("==== Wi-Fi up; next: WireGuard tunnel ====");
}

void loop() {
  delay(1000);
}
```

- [ ] **Step 5: Build, flash, observe**

```bash
cd firmware && pio run -t upload
# (the background serial monitor from M0 is still running)
```

Reset the device. Expected serial output: `wifi: connected`, your DHCP IP, gateway, DNS. If `WL_CONNECTED` never arrives, check (a) SSID typo, (b) 5 GHz vs 2.4 GHz (ESP32-S3 is 2.4 GHz only), (c) WPA3-only network (set router to WPA2/WPA3 mixed).

- [ ] **Step 6: Sanity-check DNS resolution**

Add a one-shot DNS lookup in `setup()` after the `connect()` call to prove the lower-level stack works *before* WG enters the picture:

```cpp
  IPAddress test_ip;
  if (WiFi.hostByName("one.one.one.one", test_ip)) {
    Serial.printf("dns: one.one.one.one -> %s\n", test_ip.toString().c_str());
  } else {
    Serial.println("dns: lookup FAILED");
  }
```

Reflash, confirm a non-`0.0.0.0` result.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/wifi_sta.h firmware/src/wifi_sta.cpp firmware/src/main.cpp
git commit -m "feat(m1): wifi STA bring-up with DHCP + DNS sanity check"
```

---

## Task 4: WireGuard link module (no traffic yet)

We integrate `esp_wireguard` (or the fork chosen in Task 0). Goal: tunnel comes up, handshake completes, the device gets a route for the WG subnet. No application traffic yet.

**Files:**
- Modify: `firmware/platformio.ini` (un-comment the WG lib_deps line from Task 2)
- Create: `firmware/src/wg_link.h`
- Create: `firmware/src/wg_link.cpp`
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Add the WireGuard library dep**

Edit `firmware/platformio.ini` and replace the commented-out WG line in `lib_deps` with the actual package selected in Task 0, pinned to the commit/tag you chose. Example (substitute your selection):

```ini
lib_deps =
  m5stack/M5Unified @ ^0.2.5
  https://github.com/<owner>/<repo>.git#<tag-or-sha>
```

Run `pio pkg install` to fetch it, then `pio run` to confirm it links cleanly into the existing app.

- [ ] **Step 2: Create `firmware/src/wg_link.h`**

```cpp
#pragma once
#include <Arduino.h>

namespace wg_link {

struct Config {
  const char* device_private_key;   // base64
  const char* peer_public_key;      // base64
  const char* peer_endpoint_host;   // IP or DNS
  uint16_t    peer_endpoint_port;
  const char* device_address;       // "10.7.0.2"
  const char* device_netmask;       // "255.255.255.0"
  const char* allowed_ip_cidr;      // e.g. "192.168.1.0/24"
  uint16_t    persistent_keepalive_s;  // 25 is a sane default behind NAT
};

// Bring up the tunnel. Blocks until either the first handshake completes
// or `timeout_ms` elapses. Logs to `out`. Returns true on success.
bool start(const Config& cfg, Stream& out, uint32_t timeout_ms = 15000);

// True if the underlying library reports the tunnel is up and a
// handshake has happened within the last `freshness_ms` ms.
bool is_up(uint32_t freshness_ms = 180000);

// Seconds since the last completed handshake; UINT32_MAX if never.
uint32_t seconds_since_last_handshake();

// Tear down the tunnel (used in tests / on Wi-Fi loss).
void stop();

}  // namespace wg_link
```

- [ ] **Step 3: Create `firmware/src/wg_link.cpp`**

The exact API of `esp_wireguard` / `WireGuard-ESP32` varies between forks; the pattern below is for `ciniml/WireGuard-ESP32`. Adapt as needed for whatever Task 0 selected — what matters is the *shape*, not the function names.

```cpp
#include "wg_link.h"

#include <WireGuard-ESP32.h>

namespace wg_link {

static WireGuard s_wg;
static uint32_t s_last_handshake_ms = 0;
static bool s_running = false;

bool start(const Config& cfg, Stream& out, uint32_t timeout_ms) {
  out.println("wg: starting tunnel");
  out.printf("  endpoint: %s:%u\n", cfg.peer_endpoint_host, cfg.peer_endpoint_port);
  out.printf("  device:   %s/%s\n", cfg.device_address, cfg.device_netmask);
  out.printf("  allowed:  %s\n", cfg.allowed_ip_cidr);

  IPAddress local_ip;
  if (!local_ip.fromString(cfg.device_address)) {
    out.println("wg: bad device_address");
    return false;
  }

  bool ok = s_wg.begin(
      local_ip,
      cfg.device_private_key,
      cfg.peer_endpoint_host,
      cfg.peer_public_key,
      cfg.peer_endpoint_port);

  if (!ok) {
    out.println("wg: WireGuard.begin() FAILED");
    return false;
  }
  s_running = true;

  // The library brings up the netif synchronously, but the first
  // handshake happens lazily on first packet. We force it by sending a
  // single ICMP echo to the server's WG-internal address (10.7.0.1).
  uint32_t deadline = millis() + timeout_ms;
  while (millis() < deadline) {
    if (s_wg.is_initialized()) {
      // Library-specific: many forks expose a `last_handshake_ms` getter.
      // If yours does not, fall back to a successful ping in Task 6 as
      // the handshake proof.
      s_last_handshake_ms = millis();
      out.println("wg: link initialized");
      return true;
    }
    delay(100);
  }

  out.println("wg: timed out waiting for init");
  return false;
}

bool is_up(uint32_t freshness_ms) {
  if (!s_running) return false;
  if (s_last_handshake_ms == 0) return false;
  return (millis() - s_last_handshake_ms) < freshness_ms;
}

uint32_t seconds_since_last_handshake() {
  if (s_last_handshake_ms == 0) return UINT32_MAX;
  return (millis() - s_last_handshake_ms) / 1000;
}

void stop() {
  if (s_running) {
    s_wg.end();
    s_running = false;
  }
}

}  // namespace wg_link
```

- [ ] **Step 4: Wire WG into `main.cpp`**

```cpp
#include <Arduino.h>
#include "wifi_sta.h"
#include "wg_link.h"
#include "wg_secrets.h"

void setup() {
  Serial.begin(115200);
  uint32_t deadline = millis() + 10000;
  while (!Serial && millis() < deadline) delay(50);
  delay(500);

  Serial.println();
  Serial.println("==== Cardputer-AI M1 — Wi-Fi + WireGuard ====");

  if (!wifi_sta::connect(wg_secrets::kWifiSSID, wg_secrets::kWifiPassword,
                         Serial)) {
    Serial.println("STOP: Wi-Fi failed");
    return;
  }

  wg_link::Config cfg{
    .device_private_key      = wg_secrets::kDevicePrivateKey,
    .peer_public_key         = wg_secrets::kPeerPublicKey,
    .peer_endpoint_host      = wg_secrets::kPeerEndpointHost,
    .peer_endpoint_port      = wg_secrets::kPeerEndpointPort,
    .device_address          = wg_secrets::kDeviceAddress,
    .device_netmask          = wg_secrets::kDeviceNetmask,
    .allowed_ip_cidr         = wg_secrets::kAllowedIPCIDR,
    .persistent_keepalive_s  = 25,
  };

  if (!wg_link::start(cfg, Serial)) {
    Serial.println("STOP: WireGuard failed to come up");
    return;
  }

  Serial.println("==== Wi-Fi + WG up; next: reachability proof ====");
}

void loop() {
  delay(5000);
  Serial.printf("wg: %s, last handshake %u s ago\n",
                wg_link::is_up() ? "UP" : "DOWN",
                wg_link::seconds_since_last_handshake());
}
```

- [ ] **Step 5: Build, flash, observe**

Reset the device. Expected: `wg: starting tunnel`, then `wg: link initialized` within a few seconds. The periodic loop messages should show the tunnel staying up.

- [ ] **Step 6: Update `m1-wireguard-report.md`**

Fill the `## First handshake` section with:
- How long `wg_link::start()` took (eyeball from serial timestamps)
- Whether the keepalive kept it alive across a 60-second idle (let it run, check loop output)

- [ ] **Step 7: Commit**

```bash
git add firmware/platformio.ini firmware/src/wg_link.h firmware/src/wg_link.cpp firmware/src/main.cpp docs/m1-wireguard-report.md
git commit -m "feat(m1): wg_link module — tunnel comes up against UDM peer"
```

---

## Task 5: Reachability probe — prove the tunnel actually carries traffic

The tunnel being "up" only proves UDP packets are flowing to the UDM. The real test is whether traffic to a private LAN IP inside the home network reaches that host and gets a reply. We test this with two pings:

1. **Tunnel-only target:** a private IP only reachable inside the home LAN (e.g. `192.168.1.1`, the UDM's LAN IP).
2. **Internet target:** `1.1.1.1` — should be reachable via the device's regular Wi-Fi default route, *not* through the tunnel (since our `AllowedIPs` is only the home LAN).

If both succeed, the tunnel works AND we're not accidentally routing all traffic through it.

**Files:**
- Create: `firmware/src/net_probe.h`
- Create: `firmware/src/net_probe.cpp`
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Create `firmware/src/net_probe.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <IPAddress.h>

namespace net_probe {

struct PingResult {
  bool     ok;
  uint16_t round_trip_ms;  // 0 if !ok
  uint16_t attempts;       // total tries before giving up
};

// ICMP ping `target` up to `max_attempts` times with `timeout_ms` per
// attempt. Returns the first successful round-trip.
PingResult ping(IPAddress target, uint16_t max_attempts = 4,
                uint32_t timeout_ms = 1500);

}  // namespace net_probe
```

- [ ] **Step 2: Create `firmware/src/net_probe.cpp`**

```cpp
#include "net_probe.h"

#include <ESP32Ping.h>   // built-in helper available in espressif32 Arduino core

namespace net_probe {

PingResult ping(IPAddress target, uint16_t max_attempts, uint32_t timeout_ms) {
  PingResult r{false, 0, 0};
  for (uint16_t i = 1; i <= max_attempts; ++i) {
    ++r.attempts;
    if (Ping.ping(target, 1)) {
      // Ping.averageTime() reports the average ms of the last batch
      r.ok = true;
      r.round_trip_ms = (uint16_t) Ping.averageTime();
      return r;
    }
    delay(250);
    (void) timeout_ms;  // ESP32Ping doesn't expose per-attempt timeout cleanly
  }
  return r;
}

}  // namespace net_probe
```

> If `ESP32Ping` is not part of your installed Arduino core, add `marian-craciunescu/ESP32Ping` to `lib_deps`. Most modern espressif32 installs include it indirectly.

- [ ] **Step 3: Drive the two pings from `main.cpp`**

Replace the body of `setup()` after the WG-up message with:

```cpp
  Serial.println("==== Wi-Fi + WG up; running reachability proof ====");

  // 1) Target reachable only via WG (UDM's LAN IP).
  IPAddress lan_target;
  lan_target.fromString("192.168.1.1");
  auto lan = net_probe::ping(lan_target);
  Serial.printf("ping LAN  %s: %s (%u ms, %u attempts)\n",
                lan_target.toString().c_str(),
                lan.ok ? "OK" : "FAIL",
                lan.round_trip_ms, lan.attempts);

  // 2) Target reachable only via the regular default route (not WG).
  IPAddress wan_target(1, 1, 1, 1);
  auto wan = net_probe::ping(wan_target);
  Serial.printf("ping WAN  %s: %s (%u ms, %u attempts)\n",
                wan_target.toString().c_str(),
                wan.ok ? "OK" : "FAIL",
                wan.round_trip_ms, wan.attempts);

  // Success criterion for M1.
  if (lan.ok && wan.ok) {
    Serial.println("==== M1 SUCCESS: tunnel reaches home LAN; default route still works ====");
  } else if (lan.ok && !wan.ok) {
    Serial.println("==== PARTIAL: tunnel works but default route is broken (routing scope too wide?) ====");
  } else if (!lan.ok && wan.ok) {
    Serial.println("==== FAIL: default route works but tunnel doesn't reach LAN ====");
  } else {
    Serial.println("==== FAIL: no network reachable at all ====");
  }
```

> Adjust the `192.168.1.1` target if your home LAN uses a different subnet — pick anything that's only reachable inside the LAN, never from the public internet.

- [ ] **Step 4: Build, flash, observe**

Reset the device. The success line is the proof. If you see PARTIAL or FAIL, the most common causes:

- `AllowedIPs` on the device side is too broad (e.g. `0.0.0.0/0`) — fix by narrowing to your LAN subnet.
- UDM peer's allowed IPs don't include `10.7.0.2/32` — fix on the server side.
- UDM's WireGuard server-side routing isn't actually exposing the LAN subnet to peers — UniFi sometimes requires an explicit "Forward Traffic" rule.

- [ ] **Step 5: Record results in `m1-wireguard-report.md`**

Fill the `## Reachability proof` section with:
- The two ping round-trip times
- A note on any troubleshooting steps that mattered (so M5/M6 don't relearn them).

- [ ] **Step 6: Commit**

```bash
git add firmware/src/net_probe.h firmware/src/net_probe.cpp firmware/src/main.cpp docs/m1-wireguard-report.md
git commit -m "feat(m1): reachability probe — tunnel-only + default-route pings"
```

---

## Task 6: Resilience — survive Wi-Fi flap and a UDM reboot

A tunnel that comes up once is not the same as one we can rely on. M1's last task is a small supervisor loop that re-handshakes after the network drops out.

**Files:**
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Add a supervisor loop**

Replace `loop()` with:

```cpp
void loop() {
  // If Wi-Fi dies, the WG netif has nothing to ride on. Reconnect first,
  // then bring the tunnel back up. We do not retry forever — backoff so
  // we don't melt the battery if the user is genuinely offline.
  static uint32_t backoff_ms = 1000;
  static const uint32_t kMaxBackoff = 30000;

  if (!wifi_sta::is_up()) {
    Serial.println("supervisor: wifi down — reconnecting");
    wg_link::stop();
    if (!wifi_sta::connect(wg_secrets::kWifiSSID, wg_secrets::kWifiPassword,
                           Serial, 20000)) {
      delay(backoff_ms);
      backoff_ms = min(backoff_ms * 2, kMaxBackoff);
      return;
    }
  }

  if (!wg_link::is_up()) {
    Serial.println("supervisor: wg down — restarting tunnel");
    wg_link::Config cfg{ /* same as setup() */ };
    // (factor the Config out into a shared helper before this task —
    // here for clarity; share it via a wg_secrets_to_cfg() inline.)
    wg_link::start(cfg, Serial);
  }

  // Stay alive long enough to be useful, but yield CPU.
  delay(2000);
  backoff_ms = 1000;  // reset on each healthy iteration
}
```

(Yes — refactor the `Config` construction into a small inline helper so `setup()` and `loop()` aren't duplicating it. That cleanup belongs in this commit.)

- [ ] **Step 2: Test Wi-Fi flap**

With the device running and a successful M1 message in the log, temporarily disable Wi-Fi on the UDM (or change the password). Watch the serial log: it should report `wifi down — reconnecting`, then fail/back off. Re-enable Wi-Fi; the device should recover and bring WG back up within ~30 s.

- [ ] **Step 3: Test UDM-side reboot (optional but recommended)**

If feasible without disrupting other users, reboot the UDM. The device should re-handshake automatically when the server returns. If it doesn't, the keepalive isn't doing its job — increase it or add an explicit handshake-failure timer.

- [ ] **Step 4: Update the report and commit**

Add a `## Resilience` section to `m1-wireguard-report.md` summarizing both tests. Then:

```bash
git add firmware/src/main.cpp docs/m1-wireguard-report.md
git commit -m "feat(m1): supervisor loop survives wifi flap and udm reboot"
```

---

## Task 7: Finalize and tag

**Files:**
- Modify: `docs/m1-wireguard-report.md` (final pass)

- [ ] **Step 1: Verify there are no remaining TBDs in `docs/m1-wireguard-report.md`**

Run: `grep -n 'TBD\|FIXME' docs/m1-wireguard-report.md`
Expected: no matches.

- [ ] **Step 2: Cross-link the report from `docs/m0-hardware-report.md`**

Append at the end of `docs/m0-hardware-report.md`:

```markdown
---

**Next milestone:** [M1 — WireGuard tunnel](m1-wireguard-report.md).
```

- [ ] **Step 3: Commit and tag**

```bash
git add docs/m0-hardware-report.md docs/m1-wireguard-report.md
git commit -m "docs(m1): cross-link M0→M1; finalize M1 report"
git tag -a m1-complete -m "M1 WireGuard tunnel complete — device reaches home LAN over WG"
```

- [ ] **Step 4: Push the branch and the tag**

```bash
git push -u origin <branch-name>
git push origin m1-complete
```

Open the PR URL the push prints, merge to main as usual.

---

## Done criteria

M1 is complete when **all** of these are true:

1. `firmware/` builds cleanly with the arduino+espidf hybrid framework.
2. On boot, the device associates with Wi-Fi, brings up the WG tunnel, and prints the success line: `==== M1 SUCCESS: tunnel reaches home LAN; default route still works ====`.
3. The device survives a Wi-Fi disconnect/reconnect cycle without manual reset.
4. `docs/m1-wireguard-report.md` is filled in with the chosen library, UDM endpoint, handshake/RTT measurements, and resilience test results.
5. `docs/ops/udm-wireguard-setup.md` documents the server-side runbook in a way M5's provisioning automation can later script.
6. The repository tag `m1-complete` exists.

At that point, M2 (proxy MVP on lobsterboy) can begin. The device has a stable Layer-3 path to anything inside the home network, including a future `claude-proxy` listening on lobsterboy.

---

## Open questions deferred to later milestones

- **Direct-mode fallback** (DESIGN.md §11 Q3): not addressed in M1. The supervisor loop currently does *not* fall back to a non-tunneled path when WG can't be established. We add that in M3 or later when it's clear users actually need it.
- **WG key storage at rest**: the M1 secrets live in a `.gitignore`d compile-time header. M5 moves them to encrypted NVS via the SD-card provisioning flow.
- **Captive-portal Wi-Fi networks** (DESIGN.md §8): the M1 firmware will silently fail on a captive portal. M3+ adds a captive-portal-detected status indicator and surfaces it in the UI.
- **DNS-inside-tunnel**: M1 only resolves DNS via Wi-Fi (the default route). The proxy IP will be reached by raw IP in M2; we don't need tunnel-internal DNS until later.
