# M1 WireGuard Report

**Library chosen:** `droscy/esp_wireguard @ v0.4.5` (commit `b7372757`)

**Selection reasoning:** Only candidate with a 2026 commit (HEAD 2026-04-17), declares both `espidf` and `arduino` frameworks in its `library.json` — matching our `framework = arduino, espidf` hybrid in `platformio.ini` — and is the WireGuard implementation ESPHome ships in production against ESP-IDF 5.x, which is the same major we verified compiles cleanly in M0.

**Alternatives considered:**

- `trombik/esp_wireguard` (canonical upstream) — ~19 months stale; **its own maintainer filed an open issue ("crash with esp-idf 5.x", Aug 2022) that remains unresolved**; no `library.json`; no Arduino wrapper.
- `ciniml/WireGuard-ESP32-Arduino` — last touched May 2022 (~4 years stale); Arduino-1.x / IDF 4.x era; disqualified by the 18-month freshness rule.

**platformio.ini dep line (for Task 4):**

```ini
lib_deps =
  m5stack/M5Unified @ ^0.2.5
  https://github.com/droscy/esp_wireguard.git#v0.4.5
```

(Sections below filled in as later M1 tasks complete.)

## UDM-side configuration

TBD (filled in by Task 1)

## First handshake

TBD (filled in by Task 4)

## Reachability proof

TBD (filled in by Task 5)

## Resilience

TBD (filled in by Task 6)
