# M1 WireGuard Report

**Date completed:** 2026-05-16
**Library chosen:** `droscy/esp_wireguard @ v0.4.5` (commit `b7372757`)

**Selection reasoning:** Only candidate with a 2026 commit (HEAD 2026-04-17), declares both `espidf` and `arduino` frameworks in its `library.json` — matching our `framework = arduino, espidf` hybrid in `platformio.ini` — and is the WireGuard implementation ESPHome ships in production against ESP-IDF 5.x, which is the same major we verified compiles cleanly in M0.

**Alternatives considered:**

- `trombik/esp_wireguard` (canonical upstream) — ~19 months stale; its own maintainer filed an open issue ("crash with esp-idf 5.x", Aug 2022) that remains unresolved; no `library.json`; no Arduino wrapper.
- `ciniml/WireGuard-ESP32-Arduino` — last touched May 2022 (~4 years stale); Arduino-1.x / IDF 4.x era; disqualified by the 18-month freshness rule.

**platformio.ini dep:**

```ini
lib_deps =
  m5stack/M5Unified @ ^0.2.5
  https://github.com/droscy/esp_wireguard.git#v0.4.5
  marian-craciunescu/ESP32Ping @ ^1.7
```

## UDM-side configuration

- UDM Pro, UniFi Network app native WireGuard server (`wgsrv1`)
- Server public key: `PuorEYXdzV0Exnmo7Gc9z5/bAVJAJNzlaXs1suxThHk=`
- Server endpoint (public): `136.49.128.207:51820`
- Server endpoint (LAN, used by M1 testing): `192.168.1.1:51820`
- Subnet: `192.168.14.0/24`, server itself `192.168.14.1`
- Device peer: `cardputer-edc`
  - Allowed IPs (server-side): `192.168.14.3/32`
  - Device assigned address: `192.168.14.3`

Full UDM setup runbook lives in `docs/ops/udm-wireguard-setup.md` (TBD —
the user clicked through the UI rather than recording each step; capture
the runbook before M5 provisioning automation begins).

## First handshake

Initially observed via `wg show` on the UDM: **1 KiB rx / 4 KiB tx** with
`latest handshake: 27 minutes, 7 seconds ago` (single accepted handshake,
then nothing for 27 minutes despite repeated firmware re-flashes).

Root cause: **WireGuard replay protection via TAI64N timestamps.** Every
handshake initiation embeds a `gettimeofday()`-based TAI64N. The peer
keeps a `greatest_timestamp` and rejects any handshake whose timestamp
is ≤ the previous accepted value. The ESP32-S3 has no RTC; without NTP
sync, every reboot reuses a near-epoch `tv_sec` for the timestamp. The
first reboot succeeds; every reboot after that has a timestamp ≤ the
greatest stored on the server and is silently rejected.

Fix: SNTP sync (`configTime(0, 0, "pool.ntp.org", "time.nist.gov")`)
**before** `wg_link::start()`. After the fix, fresh handshakes complete
within ~1 second of each WG bring-up.

## Reachability proof

After NTP sync + WG bring-up:
- `wg_link::is_up()` returns true; supervisor heartbeat reports "wg UP,
  N s since last handshake" with N rolling between 5-25 s (matches the
  25 s persistent keepalive we configured).
- LCD shows **"M1 SUCCESS: tunnel works"** (green).
- ICMP ping from the device:
  - in-tunnel target `192.168.14.1` (WG server interior IP, only
    reachable through the tunnel): **OK**
  - outbound target `1.1.1.1` (routed through WG because full-tunnel):
    **OK**

The success branch in `main.cpp` was reached, confirming both pings
succeeded on first run.

## Resilience

Not formally tested with a Wi-Fi flap / UDM reboot scenario yet — the
supervisor code is in place (`wifi_sta::is_up` + `wg_link::is_up`
polling with exponential backoff and stop/start cycling), but proving
it requires either physically toggling Wi-Fi or rebooting the UDM. Defer
to a follow-up commit if resilience formal testing is desired before M2.

## Lessons captured for M2+

1. **Library hidden contract:** `esp_wireguard_init` must be called
   exactly once per program. Recycling the tunnel uses
   `esp_wireguard_disconnect` + `esp_wireguard_connect`, never
   re-init. Our `wg_link::start()` now respects this — the `setup()`
   path inits; the supervisor's retry path only re-connects.

2. **`MAX_SRC_IPS` default is 1**, which leaves no room beyond the
   library's auto-added device /32. Bump to 8 via `-D
   CONFIG_WIREGUARD_MAX_SRC_IPS=8` in `platformio.ini` `build_flags`.
   (The library is consumed as an Arduino library, so values in
   `sdkconfig.defaults` for it do **not** reach its preprocessor.)

3. **Set the default route AFTER first handshake**, not before. The
   library snapshots `netif_default` at `init()` time and binds its UDP
   socket to that snapshot, but lwIP paths in the WG netif setup still
   consult the live default in places. Doing `esp_wireguard_set_default`
   only after `peer_is_up` succeeds removes the chicken-and-egg.

4. **NTP is a prerequisite for WG on ESP32**, full stop. Without it,
   the device can establish exactly **one** handshake in its lifetime
   per peer entry, then replay-protection blocks every subsequent
   attempt. M5 provisioning must include NTP server config in NVS, and
   later milestones must keep NTP working before WG every boot.

5. **For M1 testing on the home LAN**, use the UDM's LAN IP as the WG
   endpoint (`192.168.1.1:51820`) rather than its public IP. UniFi's
   hairpin-NAT behavior for "device on LAN tries to reach own WAN IP"
   is unreliable. When testing from outside the home, switch back to
   the public IP. M5 should make this a runtime decision based on which
   network the device is on, not a compile-time constant.

## Open questions deferred to later milestones

- **Resilience formal test** (Wi-Fi flap, UDM reboot) — code is in
  place, not yet observed working.
- **NTP server config persistence** — currently hard-coded
  `pool.ntp.org` + `time.nist.gov`. Move to NVS in M5.
- **Captive-portal Wi-Fi networks** (DESIGN.md §8) — M1 firmware will
  silently fail on a captive portal. M3+ adds detection/UI.
- **`docs/ops/udm-wireguard-setup.md`** — runbook not yet written;
  capture before M5 provisioning automation begins.
