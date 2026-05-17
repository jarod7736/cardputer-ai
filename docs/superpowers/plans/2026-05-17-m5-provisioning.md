# M5 — Provisioning & Ops Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the compile-time `wg_secrets.h` / `proxy_secrets.h` headers with a runtime SD-card provisioning flow, so a fresh Cardputer can be brought onto the network without a developer's laptop. The proxy gains a `peer add` CLI that mints a WireGuard keypair + a per-device bearer token and writes a two-file bundle. The device's first boot reads the bundle from microSD, commits it to NVS, wipes the SD files, and reboots into normal mode. After M5, any flashed Cardputer + one SD card = a working pocket Claude. We also flip to an OTA-ready partition layout now so M6 doesn't have to migrate provisioned devices.

**Architecture:** Provisioning is **server-issued, device-consumed**. The proxy is the only thing that mints WG keys and tokens; the device never generates either. The bundle on the SD card is two plain JSON-ish files (`wg.conf` is the standard WireGuard text format, `proxy.json` is our own). NVS becomes the single source of truth for everything that was previously baked into the firmware. The supervisor loop and chat client read from NVS instead of `*_secrets.h`.

**Tech Stack:**
- Proxy: same FastAPI + httpx stack. New `peer.py` module owns peer CRUD and bundle generation. Reuses `catalog.py`'s atomic-write helper. WG keypair generation via the `wireguard-tools` package (already shipped on lobsterboy) or stdlib `subprocess` to `wg genkey` — no Python WG library needed.
- Device: ESP32 SD card via M5Cardputer's `SD` library (already a transitive dep). `Preferences` for NVS. No new lib_deps.

**Out of scope for M5** (explicitly deferred):
- **Scoped tokens** (DESIGN.md §4.4 — `chat`, `profiles:read`, etc.) — M5 ships one all-access token per device. Scopes land in M5.5 / M6 when there's a second device type that needs different access. Adds real complexity (token table with scope column, route-level middleware) for zero user-visible benefit today.
- **Flash encryption + secure boot v2** — DESIGN.md §8 / §9. These are one-way operations (can't disable without erasing the device) and require careful per-device key management. Provisioning works fine without them; the threat model just stays at "thief with desoldering iron can read NVS." Land in M6 polish after we know the flow is stable.
- **`device-key` auth wire path on device** (`X-Device-Provided-Key` header) — proxy already accepts the auth.kind (returns 501 if hit); device-side keyboard entry of API keys is a real UI design and lands in M5.5.
- **UDM API integration** — the `peer add` CLI prints the WG peer block for manual paste-in to UniFi for now. Automating UDM is a one-off ops task that doesn't gate M5.
- **OTA itself** — code lands in M6. M5 only changes the **partition layout** so M6 doesn't have to migrate already-provisioned devices.
- **Captive-portal escape hatches, multi-network Wi-Fi list** — single SSID per provision; user re-provisions if they move.
- **Token revocation broadcast** — proxy honors revocation immediately (next request fails 401); device discovers revocation by getting 401 and showing a clear error. No push channel.

**Carry-overs from earlier milestones:**
- M2 proxy has the bearer auth dependency (`auth.require_bearer`) — replace the single-env-var bearer with a tokens.json lookup.
- M4 proxy `catalog.py` has the atomic-write + schema-version pattern; reuse it directly for `tokens.py` and `peers.py`.
- M4 install.sh already manages `/etc/cardputer-proxy/` perms with the `cardputer-proxy` group; the new `tokens.json` and `peers.json` slot into the same directory with the same `0640 root:cardputer-proxy` perms.
- M3 device has WG supervisor + chat client that currently read `wg_secrets::k*` and `proxy_secrets::k*`. Both modules get a tiny shim that reads from NVS instead, falling back to the headers **only** if NVS is empty AND the headers are compiled in (kept as a dev convenience, off by default in `platformio.ini`).
- M4 device has `profile_store` (Arduino `Preferences` wrapper for namespace `cprox`). New namespaces `wifi`, `wg`, `proxy`, `device` follow the same pattern.

**Verification style:** Proxy work is unit-testable with `pytest` + FastAPI `TestClient` (M2/M4 pattern). Device work needs hardware — including a microSD card with a real bundle on it. Each task lists its verification style.

---

## Decisions to confirm before starting

User has confirmed (`plan M5` brainstorm, 2026-05-17):

1. **Token model**: one token per device, all-access. Stored server-side in `tokens.json` alongside the existing catalog. Format: `{"device_id": "...", "token": "<urlsafe-b64>", "label": "...", "created_at": "...", "revoked": false}`. No scopes.
2. **Partition table**: switch to OTA-ready layout NOW (factory + ota_0 + ota_1 + nvs + nvs_keys + spiffs). M5 devices ship with the layout M6 expects.
3. **Flash encryption + secure boot v2**: deferred to M6.
4. **Settings screen**: status (Wi-Fi SSID + RSSI, WG handshake age, device id) + reconnect WG + factory reset (wipe all NVS, prompt for new SD bundle on next boot). Accessed via `Fn+S`.

Defaults I'm choosing (flag if any are wrong):

5. **SD file locations**: `/wg.conf` and `/proxy.json` at the SD root (case-sensitive lowercase). Firmware looks for both; partial bundle = error, don't half-commit.
6. **NVS schema versioning**: each namespace stores a `_schema` u8. v1 is everything M5 writes. Mismatched schema on read = treat as empty (forces re-provision).
7. **Atomic commit**: write all four namespaces (`wifi`, `wg`, `proxy`, `device`) first; only **then** delete the SD files. If power fails mid-commit, the SD files are still there on next boot and we retry.
8. **Token format**: 32 bytes from `secrets.token_urlsafe(32)` → ~43 chars URL-safe base64. Same shape as the M2/M3 manually-generated bearer.
9. **Device id format**: short slug (the `<device-name>` passed to `peer add`), validated `^[a-z0-9-]{3,32}$`. Used in WG peer comments and proxy audit logs.
10. **WG keypair source**: `subprocess.run(["wg", "genkey"])` + `wg pubkey`. No Python WG lib. Lobsterboy already has `wireguard-tools` from M1.
11. **UDM integration**: the `peer add` CLI prints a `[Peer]` block at the end. User pastes into UniFi → WireGuard server config manually. Document the steps in the report; automate later.
12. **Bundle leftover safety**: if SD files exist but NVS is already populated AND non-empty, the firmware **does not** overwrite NVS. Shows a "remove SD bundle to boot normally, or factory-reset to re-provision" screen. Prevents accidental re-provision from a stale card.
13. **Settings screen entry**: `Fn+S` from anywhere in chat (input or scrollback). `Fn+S` again or `Esc` closes.
14. **Factory reset**: confirm prompt ("type RESET + Enter"). Wipes all four NVS namespaces (keeps `cprox`/active-profile? — **yes, wipe everything from M5's namespaces**; `cprox` is independent and stays).

---

## File structure

```
proxy/
├── pyproject.toml                              (modified — bump to 0.3.0)
├── src/cardputer_proxy/
│   ├── auth.py                                 (modified — bearer lookup via TokenStore, not env var)
│   ├── app.py                                  (modified — load TokenStore at startup; no other route changes)
│   ├── cli.py                                  (modified — add `peer add/list/revoke` subcommands)
│   ├── tokens.py                               (NEW — atomic JSON store of per-device bearer tokens)
│   ├── peers.py                                (NEW — WG keypair gen + bundle writer)
│   └── (catalog.py, schemas.py, etc. unchanged)
├── tests/
│   ├── test_tokens.py                          (NEW)
│   ├── test_peer_cli.py                        (NEW — uses tmp_path, mocks `wg genkey` via subprocess monkeypatch)
│   ├── test_auth.py                            (modified — TokenStore-backed lookup)
│   └── (rest unchanged)

ops/cardputer-proxy/
├── install.sh                                  (modified — touch empty tokens.json on fresh install w/ 0640)
└── (cardputer-proxy.env loses BEARER_TOKEN; tokens come from tokens.json)

firmware/
├── platformio.ini                              (modified — board_build.partitions = partitions_ota.csv; flag DEV_USE_HEADER_SECRETS off)
├── partitions_ota.csv                          (NEW — DESIGN.md §9 layout)
├── include/
│   ├── proxy_secrets.h.example                 (unchanged — still here for dev convenience)
│   └── wg_secrets.h.example                    (unchanged)
├── src/
│   ├── main.cpp                                (modified — provisioning fork on boot; Fn+S → settings)
│   ├── provisioning.h / .cpp                   (NEW — SD detection, parse, atomic-commit-to-NVS, wipe)
│   ├── sd_bundle.h / .cpp                      (NEW — read & validate wg.conf + proxy.json from SD)
│   ├── nvs_config.h / .cpp                     (NEW — typed accessors for wifi/wg/proxy/device namespaces)
│   ├── settings_view.h / .cpp                  (NEW — status + reconnect + factory reset screen)
│   ├── wifi_sta.h / .cpp                       (modified — read SSID/pass from nvs_config)
│   ├── wg_link.h / .cpp                        (modified — Config built from nvs_config)
│   └── chat_client.cpp                         (modified — host/port/bearer from nvs_config)

docs/
└── m5-provisioning-report.md                   (NEW)
```

---

## Tasks

Each task: scope → files → verification → done criteria.

### Proxy side

#### Task 0 — Bump proxy to 0.3.0 (test: none)
- [ ] `proxy/pyproject.toml`: `version = "0.3.0"`
- [ ] Update `proxy/CHANGELOG.md` (or `README.md`) header.
- **Done when:** `pip show cardputer_proxy` would report 0.3.0 (no test, just version commit so M5 work is bisectable).

#### Task 1 — `tokens.py`: persistent per-device token store (test: pytest)
- [ ] New `tokens.py` mirroring `catalog.py`: schema_version, atomic save via temp file + `os.replace`, load on init, hot-reload not needed (auth is hot path; load once at app startup).
- [ ] Model: `Token(device_id, token, label, created_at, revoked)`.
- [ ] Path from env `CARDPUTER_PROXY_TOKENS_PATH`, default `/etc/cardputer-proxy/tokens.json`.
- [ ] API: `list()`, `get_by_token(s) -> Token|None`, `add(token)`, `revoke(device_id)`.
- [ ] `get_by_token` is constant-time-ish (linear scan over <50 tokens, fine).
- [ ] `test_tokens.py`: round-trip save, lookup hit/miss, revoke marks but doesn't delete, schema-version refusal.
- **Done when:** `pytest proxy/tests/test_tokens.py` is green.

#### Task 2 — `auth.py` reads from TokenStore, not env var (test: pytest)
- [ ] `require_bearer` dep looks up the presented bearer in the loaded TokenStore. Hits → pass. Miss or revoked → 401.
- [ ] `app.py` constructs the TokenStore at startup (analogous to `Catalog`) and injects it via dependency.
- [ ] `test_auth.py`: green token, revoked token (401), wrong token (401), missing header (401).
- [ ] Backward-compat: if `CARDPUTER_PROXY_BEARER_TOKEN` is still set in env AND no `tokens.json` exists, accept that one bearer (smooth M4→M5 upgrade on lobsterboy). Log a warning. Removed in M6.
- **Done when:** all existing tests pass + new auth cases.

#### Task 3 — `peers.py`: WG keypair + bundle writer (test: pytest with subprocess mock)
- [ ] `peers.py`: `gen_keypair()` shells out to `wg genkey | wg pubkey`. Returns `(private, public)`.
- [ ] `write_bundle(out_dir, device_id, wg_cfg, proxy_cfg)`: creates `out_dir/wg.conf` and `out_dir/proxy.json` with mode 0600 (these contain device-side secrets and are meant for transport via SD).
- [ ] `wg.conf` content: standard WireGuard text format with `[Interface]` (private key, device address, MTU 1280) and `[Peer]` (server pubkey, endpoint host:port, AllowedIPs 0.0.0.0/0, PersistentKeepalive 25). All values come from a `peers.json` server-side config (NEW file shipped in install.sh; one entry holds the server's public key, endpoint, and the CIDR pool the CLI allocates from).
- [ ] `proxy.json` content: `{"host": "...", "port": ..., "bearer": "...", "device_id": "...", "default_profile_id": "claude-opus"}`. Host is the WG-internal proxy IP from `peers.json`.
- [ ] Allocate next free IP from the pool by reading existing peers in `peers.json`.
- [ ] `test_peer_cli.py` (covers `peers.py` too): mock subprocess.run for `wg genkey`/`wg pubkey`; verify bundle files exist, perms are 0600, contents parse, allocated IP is sequential.
- **Done when:** unit tests green.

#### Task 4 — `peer add/list/revoke` CLI subcommands (test: pytest end-to-end via cli.main)
- [ ] `cli.py`: add subparsers `peer add <device-id> [--label "..."] [--out DIR]`, `peer list`, `peer revoke <device-id>`.
- [ ] `peer add`: validate device-id slug; gen WG keypair; mint bearer token; allocate IP; write bundle to `./out/<device-id>/`; append to `peers.json` + `tokens.json`; print a `[Peer]` block to stdout for manual UniFi paste.
- [ ] `peer revoke <device-id>`: marks token revoked in `tokens.json`. Does NOT remove the WG peer (user removes manually on UniFi).
- [ ] `peer list`: prints `device-id  label  ip  revoked?  created` table from `peers.json` joined with `tokens.json`.
- [ ] `test_peer_cli.py`: round-trip add → list shows it → revoke → list shows revoked. End-to-end through `cli.main`.
- **Done when:** all tests pass; manual smoke `cardputer-proxy peer add testdev` on lobsterboy writes a bundle to `./out/testdev/`.

#### Task 5 — install.sh seeds peers.json template + handles upgrade (test: manual on lobsterboy)
- [ ] `install.sh`: on fresh install, drop a `peers.json.example` (template with `server_pubkey`, `server_endpoint`, `network_cidr`, empty `peers: []`) at `/etc/cardputer-proxy/peers.json` if absent. Same 0640 root:cardputer-proxy perms.
- [ ] Touch empty `tokens.json` at the same path if absent.
- [ ] Upgrade path from M4: if `cardputer-proxy.env` still has `CARDPUTER_PROXY_BEARER_TOKEN`, leave it; auth Task 2's backward-compat handles it. Document the migration in the report.
- **Done when:** running `sudo ./ops/cardputer-proxy/install.sh` on lobsterboy produces `peers.json` + `tokens.json` with correct perms; existing M4 install still serves chat.

### Device side

#### Task 6 — Partition table swap to OTA-ready layout (test: build + flash)
- [ ] New `firmware/partitions_ota.csv`: per DESIGN.md §9 — bootloader 64K, partition table 4K, nvs 32K, nvs_keys 4K, phy_init 4K, factory 3.5M, ota_0 3.5M, spiffs ~800K, ota_data 8K.
- [ ] `platformio.ini`: `board_build.partitions = partitions_ota.csv`.
- [ ] Confirm `firmware.bin` still fits (was 1.03 MB of 3.4 MB; new factory is 3.5 MB so plenty of room).
- [ ] Confirm NVS namespace `cprox` (from M4) still reads correctly after layout swap — NVS partition keeps its offset and size, just moves in the table. **Test by flashing and verifying the saved profile from M4 still loads.**
- **Done when:** device boots after reflash with new layout, M4 profile picker still shows the previously-selected profile.

#### Task 7 — `nvs_config`: typed NVS accessors for wifi/wg/proxy/device (test: build)
- [ ] `nvs_config.h/cpp`: namespace `wifi` (ssid, pass), `wg` (priv_key, peer_pub, endpoint_host, endpoint_port, addr, netmask, allowed_ip_cidr), `proxy` (host, port, bearer, default_profile_id), `device` (device_id, provisioned_at).
- [ ] Each namespace has `is_provisioned()`, `clear()`, typed getters/setters.
- [ ] Schema u8 in each namespace; mismatched → `is_provisioned()` returns false.
- [ ] `wipe_all()` clears all four (used by factory reset).
- **Done when:** builds cleanly; unit-testable on host would be ideal but Arduino Preferences is hard to mock — just verify build + flash success in Task 11.

#### Task 8 — `sd_bundle`: parse `/wg.conf` + `/proxy.json` from SD (test: device with crafted SD)
- [ ] `sd_bundle.h/cpp`: mount SD on SPI (Cardputer pinout from M5Cardputer board file), look for `/wg.conf` and `/proxy.json`. Both present → return parsed `Bundle`. Either missing → return `Bundle::kAbsent`. Both present but one fails to parse → `Bundle::kInvalid` with a useful error string.
- [ ] `wg.conf` parser: line-based, handle `[Interface]` and `[Peer]` sections; extract PrivateKey, Address (split into addr+netmask), PublicKey, Endpoint (split host:port), AllowedIPs, PersistentKeepalive. Anything else: ignore.
- [ ] `proxy.json` parser: hand-rolled (reuse `proxy_api` JSON walker pattern). Extract host, port, bearer, device_id, default_profile_id.
- [ ] `wipe()`: delete both files from SD; return true on success.
- **Done when:** with a hand-crafted SD card on the device, boot logs "bundle: ok" and prints parsed fields to Serial.

#### Task 9 — `provisioning`: orchestrate first-boot commit + wipe (test: device + SD)
- [ ] `provisioning.h/cpp`: `run_if_present(Stream& log)` returns one of `kNoBundle`, `kAlreadyProvisioned` (NVS populated + SD present → don't touch NVS, show user-visible warning later), `kCommitted`, `kFailed`.
- [ ] Algorithm:
  1. If SD bundle absent → return `kNoBundle`.
  2. If `nvs_config::is_provisioned()` is true → return `kAlreadyProvisioned`. (Decision 12.)
  3. Validate bundle parses fully. If not → return `kFailed`.
  4. Write all four NVS namespaces atomically-ish: write `wifi`, `wg`, `proxy`, `device` in order; if any fails, `wipe_all()` and return `kFailed`.
  5. `sd_bundle::wipe()`. (NVS already has the data; if wipe fails, log + continue.)
  6. Return `kCommitted`.
- [ ] After `kCommitted`, the caller (`main.cpp setup()`) `ESP.restart()`s. Cleaner than continuing into runtime with brand-new state.
- **Done when:** device with fresh NVS + valid SD bundle: boot → commit → restart → boot from NVS → WG up → chat works.

#### Task 10 — Wire wifi_sta, wg_link, chat_client to nvs_config (test: device)
- [ ] `wifi_sta::connect()`: read SSID/pass from `nvs_config::wifi()`. Fallback to `wg_secrets::kWifiSSID` only if `DEV_USE_HEADER_SECRETS` build flag is defined (off by default in `platformio.ini` for M5).
- [ ] `wg_link::Config`: built from `nvs_config::wg()` in `main.cpp`. Same fallback rule.
- [ ] `chat_client::send()`: `proxy_secrets::kHost/kPort/kBearerToken` replaced with `nvs_config::proxy()` getters. Same fallback rule.
- [ ] **Header secrets aren't removed** — they stay as a dev convenience for the developer's own Cardputer when they don't want to provision. The default `platformio.ini` build just ignores them.
- **Done when:** with `DEV_USE_HEADER_SECRETS` undefined and NVS populated from Task 9, device runs the full M4 chat flow.

#### Task 11 — Settings screen (Fn+S): status + reconnect + factory reset (test: device)
- [ ] `settings_view.h/cpp`: full-screen takeover (like picker_view).
- [ ] Status panel: device_id, Wi-Fi SSID + RSSI, WG handshake age (seconds since last), proxy host:port, active profile label.
- [ ] Actions list, navigable with Fn+arrows + Enter:
  - `Reconnect WG`: `wg_link::stop()` + `wg_link::start()`.
  - `Factory reset...`: prompt screen ("type RESET + Enter to confirm"). On confirm: `nvs_config::wipe_all()` + `ESP.restart()`. Boots back into provisioning fork.
  - `Back`: closes the screen.
- [ ] `Fn+S` opens (and closes); `Esc` closes.
- [ ] Settings screen is reachable even if WG is down (so user can factory-reset a broken provision).
- **Done when:** all three actions work on-device.

#### Task 12 — Boot orchestration in main.cpp (test: device, multiple scenarios)
- [ ] `setup()`:
  1. M5 begin + screen on.
  2. `provisioning::run_if_present()`:
     - `kCommitted` → show "provisioned. rebooting." + `ESP.restart()`.
     - `kAlreadyProvisioned` → show "SD bundle ignored — already provisioned. eject SD to clear this message." Continue.
     - `kFailed` → show error + halt.
     - `kNoBundle` → continue.
  3. If `!nvs_config::is_provisioned()` AND no header-secrets fallback → show "insert SD with bundle and reboot" screen + halt.
  4. Otherwise: existing M3/M4 flow (Wi-Fi → NTP → WG → fetch profiles → chat).
- [ ] `loop()`: existing M4 loop + `Fn+S` → settings.
- **Done when:** all four boot scenarios (committed / already-provisioned / failed / no-bundle) display correctly.

### Documentation + ops

#### Task 13 — Migration notes + provisioning runbook (test: read it)
- [ ] `docs/m5-provisioning-report.md`: usual final-report structure (headline, what changed, bugs, deferred items).
- [ ] Inline runbook section: "to provision a new Cardputer":
  1. On lobsterboy: `sudo /opt/cardputer-proxy/.venv/bin/cardputer-proxy peer add <name>`.
  2. Paste the printed `[Peer]` block into UniFi's WireGuard server.
  3. `cp out/<name>/{wg.conf,proxy.json} /mnt/sd && sync && eject`.
  4. Insert SD into Cardputer; reset.
  5. Confirm: status bar shows "ready"; press Fn+S; status panel matches expectations.
- [ ] Migration notes for own device: "to migrate the M4 dev Cardputer to provisioned mode, run `peer add jarod-edc` on lobsterboy, paste the generated bundle to SD, and reset the device. The old `wg_secrets.h` / `proxy_secrets.h` headers will be ignored at runtime."
- **Done when:** report is written and the runbook actually works end-to-end on the dev Cardputer.

#### Task 14 — Tag `m5-complete` + close milestone
- [ ] After PR merge: `git tag -a m5-complete -m "M5 — provisioning & ops"` on the merge commit, push tag.

---

## Verification matrix

| Task | Verification | Hardware? |
|---|---|---|
| 0 — version bump | inspect file | no |
| 1 — tokens.py | pytest | no |
| 2 — auth via tokens | pytest | no |
| 3 — peers.py | pytest (subprocess mock) | no |
| 4 — peer CLI | pytest + lobsterboy smoke | partial (lobsterboy) |
| 5 — install.sh | manual sudo run | partial (lobsterboy) |
| 6 — partition table | reflash + check M4 NVS survives | **yes** |
| 7 — nvs_config | build only | no |
| 8 — sd_bundle | Serial output with crafted SD | **yes** |
| 9 — provisioning orchestrator | end-to-end provision | **yes** |
| 10 — runtime modules read NVS | chat works without header secrets | **yes** |
| 11 — settings screen | navigate + reconnect + factory reset | **yes** |
| 12 — boot orchestration | each of 4 boot states | **yes** |
| 13 — report + runbook | follow the runbook on a real device | **yes** |

---

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| Partition swap orphans M4's saved profile in NVS | NVS partition size + offset stays the same in the new layout; Task 6 explicitly tests M4 profile survives. |
| WG handshake fails after provision because address pool conflict | `peer add` reads the existing `peers.json` and allocates the next free IP from `network_cidr`; uniqueness enforced by checking against current entries before write. |
| SD wipe fails mid-commit, leaving NVS populated + SD bundle present | Decision 12: on next boot, if NVS is populated AND SD is present, do NOT re-commit. Show a "remove SD or factory-reset" screen so the user can recover. |
| User loses the original SD bundle and can't re-provision | `peer add` is idempotent on `device-id`: re-running with the same id regenerates a new bundle to `./out/<id>/` (and rotates the bearer token). Old peer can be `peer revoke`'d. |
| Backward compat: existing dev Cardputer with header secrets stops working when M5 lands | `DEV_USE_HEADER_SECRETS` flag — keeps the M4 build path alive for the dev device until it's provisioned. Migration to provisioning is opt-in per build, not forced. |
| Token rotation breaks an in-flight provisioned device silently | Device handles 401 from proxy by showing "auth failed — re-provision" in status bar (already wired by Task 2's error path). |

---

## Done criteria — M5 ships when

1. ✅ Proxy has `peer add/list/revoke` CLI that produces working bundles.
2. ✅ Proxy auth reads from `tokens.json` (with env-var fallback for backward compat).
3. ✅ Partition layout is OTA-ready (factory + ota_0 + ota_1).
4. ✅ Device first-boot SD detection: present → commit to NVS + wipe + reboot.
5. ✅ Runtime Wi-Fi / WG / proxy config reads from NVS; header secrets are dev-only.
6. ✅ `Fn+S` opens a settings screen with status + reconnect + factory reset.
7. ✅ Factory reset wipes M5 namespaces and boots back into provisioning fork.
8. ✅ Full runbook executed end-to-end on a real device.
9. ✅ Proxy tests green (existing 40+ new ~10).
10. ✅ Report exists at `docs/m5-provisioning-report.md`.
11. ✅ Tag `m5-complete` pushed.
