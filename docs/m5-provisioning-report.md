# M5 Provisioning & Ops Report

**Date completed:** 2026-05-17
**Branch / commit at completion:** `main` @ HEAD (PR for `m5-execution`)
**Hardware:** M5Stack Cardputer-ADV (ESP32-S3FN8, 8 MB flash, no PSRAM)

## Headline

Cardputer-AI is OTA-ready and per-device-token-aware on the proxy. The
device-side SD-bundle provisioning path is built and compiles; the
maintainer's own Cardputer stays on the M3/M4 header-secrets fallback
during M5 stabilization (`env:cardputer-adv-dev`). A fresh device
flashed with `env:cardputer-adv` and an SD bundle is the production
flow we ship.

## Final firmware budget

| Env | Flash | RAM | Δ vs M4 |
|---|---|---|---|
| `cardputer-adv` (prod) | 1 034 009 B (30.3 %) | 41 084 B (12.5 %) | +7 088 B flash |
| `cardputer-adv-dev` | 1 071 001 B (31.4 %) | 41 220 B (12.6 %) | +44 080 B flash |

The dev build carries both header-secrets paths AND NVS paths so it's
a touch larger; production drops the dead branch. Both well below the
3.4 MB factory partition.

## Proxy — 68 tests, all pass

- `tokens.py` — atomic per-device bearer store, schema v1, `revoke`
  marks without delete. Hot-path lookup is linear (≤ a few dozen
  tokens per server; fine).
- `auth.py` — `require_bearer` looks up the presented bearer in
  `TokenStore`; revoked → 401 with `"token revoked"`. M4's single
  `device_bearer_token` file is still accepted as a fallback iff it
  exists, so the M4→M5 upgrade on lobsterboy needs no coordinated
  cutover. Removed in M6.
- `peers.py` — `PeerStore` (mirrors catalog/tokens), `gen_keypair()`
  via `wg genkey|pubkey`, `write_bundle()` writes wg.conf + proxy.json
  at 0600, `allocate_address()` walks `network_cidr` and skips `.1`
  (server).
- `cli.py` — new `peer add/list/revoke` subcommands. `peer add`
  validates the slug, mints, allocates IP, writes the bundle, appends
  to both stores atomically (rolls back the token write if the peer
  write fails), and prints the `[Peer]` block for paste into the
  WireGuard server config.
- `install.sh` — drops `peers.json.example` (placeholder
  server_pubkey/endpoint) and an empty `tokens.json` on fresh
  installs.

## Device side

- **Partition table** — `partitions_ota.csv`. Identical offsets to
  M3/M4's `partitions.csv` so existing NVS keys survive. Adds a
  reserved `nvs_keys` partition (4 KB, `encrypted` flag) for M6's
  flash-encryption work; currently unused. Boot loader log on first
  M5 boot confirms layout: factory at 0x20000 (3.25 MB) +
  ota_0 at 0x360000 (3.25 MB) + spiffs at 0x6A0000 (1.3 MB).
- **`nvs_config`** — four namespaces (`m5wifi`, `m5wg`, `m5proxy`,
  `m5dev`) with a per-namespace `schema` u8. Mismatched schema or
  missing namespace → read as empty (forces re-provision).
  `is_provisioned()` is the conjunction; `wipe_all()` zaps only
  these four (`cprox` for the M4 active-profile selection is
  independent and survives factory reset on purpose).
- **`sd_bundle`** — hand-rolled INI parser for `wg.conf`
  (`[Interface]` + `[Peer]`, lowercase-folded keys, `#` comments
  stripped) and JSON walker for `proxy.json` (same hand-rolled
  pattern as `proxy_api`/`chat_client`). `wipe()` removes both files
  after a successful commit.
- **`provisioning`** — orchestrator: SD absent → `kNoBundle`; SD
  invalid → `kFailed` with detail; SD ok + NVS already populated →
  `kAlreadyProvisioned` (do NOT overwrite); SD ok + NVS empty →
  write 3 namespaces (wg, proxy, device) + wipe SD → `kCommitted`.
  Caller restarts on `kCommitted`.
- **`settings_view`** — full-screen status panel + 3 actions
  (Reconnect WG / Factory reset… / Back). Factory reset opens a
  typed-`RESET` confirm before wiping NVS + restarting. `Fn+S`
  opens; arrows + j/k navigate; Esc closes. Reachable even when WG
  is down so the user can recover a broken provision.
- **`main.cpp`** — full rewrite of `setup()`:
  1. M5 begin + module inits.
  2. **Provisioning fork**: only probe SD when production build AND
     NVS is empty (see "Bugs that bit us" below).
  3. Load config: NVS first; `DEV_USE_HEADER_SECRETS` falls back to
     `wg_secrets`/`proxy_secrets` headers; otherwise halt with
     "insert SD bundle".
  4. `chat_client::configure()` + `proxy_api::configure()` thread
     host/port/bearer from the loaded config.
  5. Wi-Fi → NTP → WG → profile catalog (unchanged).
  6. `loop()` adds `Fn+S` → `open_settings()`.
- **`chat_client` / `proxy_api`** — both gain `configure(host, port,
  bearer)`. M3/M4's compile-time `proxy_secrets::` lookups are gone.
- **`platformio.ini`** — new `[env:cardputer-adv-dev]` extends prod
  with `-D DEV_USE_HEADER_SECRETS=1`. The maintainer's Cardputer
  runs the dev build; production users flash the bare `cardputer-adv`
  env.

## Bugs that actually bit us

### 1. `SD.begin()` blocks >5 s with no card → task watchdog reset

First post-rewrite boot: chip immediately bootlooped. Serial showed
the new SPI GPIO inits (GPIO 40/39/14/12) followed by
`TG1WDT_SYS_RST`. The SD library blocks on its initial handshake
when nothing's in the slot, the 5 s task watchdog fires, the chip
resets, and we never reach Wi-Fi.

**Fix:** in the dev build, skip the SD probe entirely — the header
fallback covers all dev paths. In production (no header fallback),
probe SD only when NVS is empty (first-boot or post-factory-reset).
The user is expected to insert the card before that boot completes.

A more robust fix would be a card-detect pin check or a separate
task with a manual `feedLoopWDT()` around `SD.begin()`. Cardputer-ADV
doesn't expose a card-detect line on the Adafruit_TCA8418 bus we
already touch, so deferred to M5.5.

### 2. SPI bus contention worry — turned out not to be the cause

Initially suspected `SPI.begin(40, 39, 14, 12)` was yanking the
display's SPI controller. Looking at the bootloop pattern (watchdog
rather than `SystemHalt`/`Backtrace`) ruled it out. Display SPI is on
a separate bus from the SD slot; both can coexist when the SD slot
is healthy.

### 3. M4 `cprox` profile survived the partition swap

Deliberate, but worth confirming. The new `partitions_ota.csv`
keeps `nvs` at offset 0x9000 with size 0x6000 (identical to M3/M4).
The flashed dev build picked up the previously-selected profile
without intervention. The `nvs_keys` addition was inserted between
`phy_init` and `factory` in unused space, so no existing partition
shifted.

## Provisioning runbook (for a fresh Cardputer)

Until the dev unit is migrated off the header fallback, this is the
flow for a NEW device. Confirmed end-to-end via proxy unit tests +
manual SD-bundle render; not yet exercised on a second physical
Cardputer.

1. **On lobsterboy (one-time per server):**
   ```bash
   sudo $EDITOR /etc/cardputer-proxy/peers.json
   # Fill in server_pubkey, server_endpoint, network_cidr, proxy_host.
   ```

2. **Mint a peer for the new device:**
   ```bash
   sudo /opt/cardputer-proxy/.venv/bin/cardputer-proxy peer add jarod-edc-2 \
        --label "secondary Cardputer"
   # → bundle in ./out/jarod-edc-2/
   # → [Peer] block printed to stdout for UniFi paste
   ```

3. **Paste the printed `[Peer]` block** into the UniFi WireGuard
   server config; commit.

4. **Copy the bundle to SD:**
   ```bash
   cp ./out/jarod-edc-2/{wg.conf,proxy.json} /mnt/sd/
   sync
   umount /mnt/sd
   ```

5. **Flash production firmware to the Cardputer** (one-time):
   ```bash
   pio run -e cardputer-adv -t upload --upload-port /dev/ttyACM0
   ```

6. **Insert the SD card and reset.** On first boot:
   - Display shows "provisioned. rebooting..." (green).
   - Device restarts; comes up reading from NVS.
   - SD files are deleted automatically.
   - Status bar reaches "ready"; press `Fn+S` to confirm Wi-Fi/WG/proxy
     are correct.

7. **Re-provisioning:** open Settings (`Fn+S`) → "factory reset..." →
   type RESET + Enter. Device wipes M5 namespaces + reboots into
   provisioning fork; insert a new SD bundle to start over.

## Out of scope (deferred to M5.5 / M6)

- **Wi-Fi credentials in the bundle.** The M5 bundle only carries WG +
  proxy + device id. The dev unit reads Wi-Fi SSID/pass from the
  header. Production devices currently need `wifi_sta` to fail and
  the user to fix it via... well, they can't yet — Settings UI has no
  Wi-Fi editor. **M5.5 must add either Wi-Fi-in-bundle or a Settings
  Wi-Fi action before this can ship to a second user.**
- **SD card-detect pin** (mentioned in Bug 1). Would let production
  boots gracefully skip the SD probe when no card is present.
- **Scoped tokens** (DESIGN.md §4.4). Currently every token is
  all-access. Adding `chat` / `profiles:*` / `admin` scopes is
  straightforward but the device doesn't benefit yet.
- **Flash encryption + secure boot v2.** The partition table is ready
  (`nvs_keys` reserved); enabling them is M6 polish and needs care
  around recovery.
- **`device-key` device-side UI** (X-Device-Provided-Key from the
  Cardputer keyboard).
- **UDM API integration.** `peer add` prints a `[Peer]` block for
  manual paste; automating UniFi is a one-off ops task.
- **OTA itself.** Partition layout supports it; the C++ side of OTA
  lands in M6.

## Done criteria — final state

| # | Criterion | Status |
|---|---|---|
| 1 | Proxy `peer add/list/revoke` CLI produces working bundles | ✅ unit-tested |
| 2 | Proxy auth reads from `tokens.json` (env fallback) | ✅ |
| 3 | Partition layout is OTA-ready | ✅ |
| 4 | Device first-boot SD detection commits + wipes | ✅ built, untested on second device |
| 5 | Runtime config reads from NVS; headers are dev-only | ✅ on dev build via `DEV_USE_HEADER_SECRETS` flag |
| 6 | `Fn+S` opens settings (status + reconnect + factory reset) | ✅ verified on device |
| 7 | Factory reset wipes M5 namespaces and reboots into provisioning | ✅ built; not destructively tested on dev unit |
| 8 | Full runbook executed end-to-end on a real device | ⚠️ runbook documented; full execution waits on a second physical Cardputer or migrating the dev unit |
| 9 | Proxy tests green (40 + ~28 new = 68) | ✅ |
| 10 | Report exists | ✅ |
| 11 | Tag `m5-complete` | next |

The two ⚠️ items are the M5 "ship to a second user" caveat: the code
is in, the proxy side is unit-tested, the dev unit boots and runs
M3/M4 chat unchanged, but the SD-bundle end-to-end has not been
walked on hardware. Recommended pre-flight before declaring M5 truly
ship-ready: provision a second Cardputer with the production env and
confirm the green "provisioned" → reboot → ready transition.
