# Cardputer-AI — Design Document

**Status:** Draft v0.1
**Date:** 2026-05-16
**Author:** Jarod (with Claude Opus 4.7)
**Target hardware:** M5Stack Cardputer-ADV (ESP32-S3FN8)

---

## 1. Goal

A pocket-sized, **provider-agnostic** AI client running on the Cardputer-ADV. The device:

- Sends prompts to Claude (or any OAI-compatible provider) and streams responses to a small LCD.
- Works **from anywhere with internet** via WireGuard back to the home network.
- Lets the user **switch model / provider / endpoint on-device** without reflashing.
- Manages and monitors long-running agents (research, CTO, OpenClaw sessions) — v2.

Non-goals for v1: voice I/O, image input/output, on-device tool execution, multi-user.

---

## 2. High-level architecture

```
[Cardputer ADV] ──WireGuard──▶ [UDM] ──LAN──▶ [lobsterboy: claude-proxy]
                                                          │
                                                          ├── Anthropic Messages API
                                                          ├── OpenAI Chat Completions
                                                          ├── Ollama (Desktop, via Tailscale)
                                                          ├── OpenClaw (already OAI-compatible)
                                                          └── Any OAI-compatible endpoint
```

**Why this shape:**

- **WireGuard, not a public HTTPS endpoint.** Nothing publicly exposed except the UDM's WG UDP port. Stolen device → delete its peer entry on the UDM → instant revocation.
- **Proxy holds the keys.** API keys never live on the device by default. Profile entries can opt into device-stored keys for ad-hoc providers.
- **Proxy normalizes wire formats.** Device speaks **one** protocol (OpenAI-compatible chat completions). The proxy translates outbound to Anthropic-native, Ollama-native, etc. Less firmware code, central control.
- **Profiles are canonical on the proxy.** Device pulls on connect, edits push back. Losing a device ≠ losing profile config; future second client (phone, web) sees the same list.

---

## 3. Hardware constraints (per [M5 docs](https://docs.m5stack.com/en/core/Cardputer-Adv))

| Item | Value | Implication |
|---|---|---|
| SoC | ESP32-S3FN8 (Xtensa LX7, dual 240 MHz) | `esp_wireguard` known-good; mbedTLS + lwIP fully supported. |
| Flash | 8 MB internal | Partition budget is tight (app + OTA + NVS + SPIFFS). |
| RAM / PSRAM | Not in spec sheet — **verify on hardware via `esp_chip_info()`** | Scrollback may need to spill to microSD if no PSRAM. |
| Display | ST7789V2, 240×135 | ~30×12 chars at 8×11 font. Stream-friendly with smart wrap. |
| Keyboard | TCA8418 matrix controller | I²C; full QWERTY w/ modifiers. |
| Storage | microSD | First-boot provisioning vector — drop config JSON, device commits to NVS, wipes file. |
| Audio | ES8311 codec | v2: TTS playback of responses. |
| IMU | BMI270 | v2: wake-on-shake, gesture nav. |
| IR | TX | v2: macros / remote actions. |
| Battery | 1750 mAh | Tens of hours idle; aim for ~6h active chat. |
| Wireless | Wi-Fi 4, BLE 5 | Captive-portal Wi-Fi is the only real gotcha for WG. |
| Secure element | None | WG keys in NVS, protected by flash-encryption + secure-boot v2. |

---

## 4. Wire protocol (device ↔ proxy)

The device speaks **OpenAI-compatible Chat Completions** plus a small admin/control surface. SSE for streaming.

### 4.1 Chat completion (streaming)

```http
POST /v1/chat/completions
Authorization: Bearer <device-token>
Content-Type: application/json

{
  "profile_id": "claude-opus",     # device picks profile; proxy resolves model+key
  "stream": true,
  "messages": [
    {"role": "system",    "content": "..."},
    {"role": "user",      "content": "What's the weather like in Austin?"},
    {"role": "assistant", "content": "..."},
    {"role": "user",      "content": "What about tomorrow?"}
  ],
  "max_tokens": 4096,              # optional, overrides profile default
  "temperature": 1.0
}
```

Response: `text/event-stream`, OpenAI-style `data: {...}\n\n` chunks ending with `data: [DONE]`. Proxy translates upstream events (Anthropic `content_block_delta`, Ollama tokens, etc.) into this format.

### 4.2 Profile management (proxy is source of truth)

```http
GET    /v1/profiles                       → list (without secrets)
GET    /v1/profiles/{id}                  → single
POST   /v1/profiles                       → create
PATCH  /v1/profiles/{id}                  → update (rename, swap model, etc.)
DELETE /v1/profiles/{id}                  → delete
POST   /v1/profiles/{id}/test             → ping upstream, return {latency_ms, first_token_ms, ok}
```

Profile shape:

```json
{
  "id": "claude-opus",
  "label": "Claude Opus 4.7",
  "provider": "anthropic",
  "endpoint": "https://api.anthropic.com",
  "model": "claude-opus-4-7",
  "max_tokens": 4096,
  "temperature": 1.0,
  "auth": {
    "kind": "proxy-secret",
    "secret_ref": "anthropic_main"
  },
  "system_prompt": null,
  "icon": "claude",
  "color": "#cc785c"
}
```

`auth.kind` is one of:

- `proxy-secret` — proxy looks up `secret_ref` in its secret store. **Default.**
- `device-key` — device sends an `X-Device-Provided-Key` header per request; proxy forwards as-is. For users who want to enter a key on the device.
- `none` — keyless (e.g. local Ollama).

### 4.3 Secrets (write-only, server-side)

Secrets are **never** returned over the API. Initial seeding via a CLI on lobsterboy:

```bash
cardputer-proxy secrets set anthropic_main 'sk-ant-...'
cardputer-proxy secrets list           # names only
cardputer-proxy secrets rotate anthropic_main 'sk-ant-...'
cardputer-proxy secrets delete openai_old
```

### 4.4 Device tokens

Each Cardputer has a unique bearer token (separate from the WireGuard key). Stored on device in NVS, used in `Authorization: Bearer ...`. Tokens can be scoped (`chat`, `profiles:read`, `profiles:write`, `admin`). Revocable server-side. Defense-in-depth — without WG you can't reach the proxy anyway, but useful for audit trail + per-device profile preferences.

### 4.5 Conversation history

v1: client-side only — scrollback buffer in PSRAM (or SD if no PSRAM). Survives sleep, lost on power cycle.

v2: optional server-side history per device token (`POST /v1/conversations`, `GET /v1/conversations/{id}`).

---

## 5. Provider adapters (proxy side)

Each `provider` value maps to an adapter module:

| `provider` | Upstream API | Notes |
|---|---|---|
| `anthropic` | `POST /v1/messages` | Translate OAI `messages` → Anthropic `system` + `messages`. Map SSE events. |
| `openai` | `POST /v1/chat/completions` | Pass-through w/ minor header swap. |
| `ollama` | `POST /api/chat` or `/v1/chat/completions` | Prefer OAI-compatible path. |
| `openclaw` | `POST /v1/chat/completions` | Already OAI-compatible — pass-through. |
| `openai-compatible` | generic | Used for vLLM, llama.cpp server, LM Studio, etc. |

Adapter contract (Python):

```python
class Adapter(Protocol):
    async def stream_chat(
        self,
        profile: Profile,
        request: ChatRequest,
        secret: str | None,
    ) -> AsyncIterator[ChatChunk]:
        ...
```

The proxy core handles auth, profile lookup, secret resolution, and SSE re-emission; adapters only deal with the upstream wire format.

---

## 6. Device UI / state machine

```
┌──────────────────────────────────────────────────────────────┐
│ ◆ Claude Opus 4.7              ▮▮▯  9:42pm    holdfast 📶    │ ← status bar
├──────────────────────────────────────────────────────────────┤
│ you> what's the weather like in austin?                       │
│                                                              │
│ claude> Austin is currently 78°F and sunny, with light winds  │
│ from the south. Expect highs near 92°F today...              │
│                                                              │
│ you>_                                                         │ ← input line
└──────────────────────────────────────────────────────────────┘
   [Fn+P] Profile  [Fn+H] History  [Fn+S] Settings  [Fn+N] New
```

### Screens

1. **Chat (default)** — streaming responses, scrollback (PgUp/PgDn), input line with cursor.
2. **Profile picker** (`Fn+P`) — list of profiles, ↑/↓ to highlight, Enter to switch, `+` to add, `e` to edit, `d` to delete, `t` to test.
3. **Profile editor** — fields: label, provider type, endpoint, model, max_tokens, temperature, auth kind, key/secret-ref.
4. **History** (`Fn+H`) — recent conversations, Enter to resume.
5. **Settings** (`Fn+S`) — WG status + reconnect, Wi-Fi picker, device token (masked), firmware version, OTA check, factory reset.
6. **First-boot wizard** — Wi-Fi → WG handshake check → proxy ping → ready.

### Status bar

Persistent top line: active-profile icon + label, battery, time, Wi-Fi/WG state (`📶` = Wi-Fi+WG ok, `▲` = Wi-Fi only, `✕` = offline).

### Input affordances

- **Enter** sends. **Shift+Enter** newline. **Esc** cancels in-flight request. **Ctrl+L** clears scrollback. **Ctrl+C** twice quickly = power off.
- **Function-row** chord keys (Fn+P/H/S/N) for screen nav.
- **Long-press Fn+P** = quick-cycle to next profile without leaving the chat screen.

### State machine (high level)

```
BOOT
 └─▶ LOAD_NVS ─▶ JOIN_WIFI ─▶ WG_HANDSHAKE ─▶ PROXY_HELLO
                    │              │                │
                    ▼              ▼                ▼
              CAPTIVE_PORTAL   WG_RETRY        AUTH_FAIL
                                                   │
                                                   ▼
                                              READY  ───▶ CHAT  ⇄  PROFILE_EDIT
                                                            │
                                                            ▼
                                                          SETTINGS
```

---

## 7. Provisioning flow

Goal: get a fresh Cardputer onto the user's WG network without serial cables.

1. **Flash firmware** via USB once (PlatformIO).
2. **Generate a peer** server-side:

    ```bash
    cardputer-proxy peer add jarod-edc
    # → writes ./out/jarod-edc/
    #     wg.conf       (private key + server pubkey + endpoint + allowed IPs)
    #     proxy.json    (device token + proxy URL)
    # Also calls UDM API to add the peer (or prints config to paste in UI)
    ```

3. **Copy `wg.conf` + `proxy.json`** to the root of a microSD card.
4. **Insert + boot.** Firmware detects the files, copies into NVS (encrypted), wipes them from SD, reboots into normal mode.
5. **First-run wizard** verifies Wi-Fi, WG handshake, proxy ping, and the user picks an initial profile.

Re-provisioning (rotate keys, switch proxy): same flow with new SD contents — firmware overwrites the existing NVS entries.

---

## 8. Security model

| Threat | Mitigation |
|---|---|
| Device lost / stolen | Delete peer on UDM → no network reach. Revoke device token → no proxy access. Flash encryption protects on-device NVS at rest. |
| API key leakage | Keys live on lobsterboy, never on device by default. `device-key` auth is opt-in per profile with clear UI warning. |
| MITM on local Wi-Fi | All traffic inside WG tunnel; ChaCha20-Poly1305. |
| Replay / abuse via stolen token | Per-token rate limits, audit log of every request, revocation. |
| Firmware tampering | Secure boot v2 + flash encryption enabled in `platformio.ini`. |
| SD-card config left behind after provisioning | Firmware atomically wipes `wg.conf` / `proxy.json` after NVS commit. |
| Captive portals trapping outbound DNS/UDP | First-boot wizard surfaces "Wi-Fi connected, WG handshake failed — try another network" rather than silent fail. |

---

## 9. Partition / storage budget (8 MB flash)

| Partition | Size | Purpose |
|---|---|---|
| `bootloader` | 64 KB | |
| `partition table` | 4 KB | |
| `nvs` | 32 KB | Wi-Fi creds, WG keys, device token, active profile id, prefs |
| `nvs_keys` | 4 KB | Flash-encryption keys |
| `phy_init` | 4 KB | |
| `factory` (app) | 3.5 MB | Firmware A slot |
| `ota_0` | 3.5 MB | Firmware B slot for OTA |
| `spiffs` | ~800 KB | Fonts, UI assets, last-known-good profile cache |

Scrollback buffer = runtime RAM/PSRAM, not flash. If no PSRAM, spill to microSD as a ring file.

---

## 10. Build plan

### Milestone 0 — Hardware confirmation (½ day)

- [ ] Flash a "hello world" PlatformIO sketch; confirm screen, keyboard, microSD work.
- [ ] Print `esp_chip_info()` + heap stats; record actual RAM / PSRAM.
- [ ] Verify mbedTLS + lwIP compile clean in ESP-IDF 5.x.

### Milestone 1 — WireGuard tunnel (1-2 days)

- [ ] Add `esp_wireguard` (or `esp_wireguard_compat`) to firmware project.
- [ ] Hard-code a test config in code, confirm `ping 192.168.1.181` works over the tunnel.
- [ ] Configure WG server on the UDM (one peer for the dev unit). Document the steps.

### Milestone 2 — Proxy MVP (1-2 days)

- [ ] FastAPI app on lobsterboy: `/v1/chat/completions` (Anthropic adapter only), bearer-token auth, hard-coded profile.
- [ ] systemd unit + reverse-proxy entry on lobsterboy.
- [ ] `curl` test from laptop on Tailnet works.

### Milestone 3 — Device chat MVP (2-3 days)

- [ ] Minimal chat screen: input line, scrollback, SSE consumer.
- [ ] Connects over WG, hits proxy, streams a Claude response.
- [ ] Demo-able pocket Claude. **First usable build.**

### Milestone 4 — Profile system (2-3 days)

- [ ] Proxy: profile CRUD endpoints + on-disk JSON store + secret store.
- [ ] Adapters: OpenAI + Ollama + generic-OAI in addition to Anthropic.
- [ ] Device: profile picker UI, profile editor UI, profile-test affordance.

### Milestone 5 — Provisioning & ops (1-2 days)

- [ ] `cardputer-proxy peer add` CLI that mints WG peer + device token + writes SD bundle.
- [ ] First-boot SD-import flow in firmware.
- [ ] Settings screen: WG status, reconnect, factory reset.

### Milestone 6 — Polish (open-ended)

- [ ] OTA from lobsterboy.
- [ ] Token / cost meter in status bar.
- [ ] System prompt presets, per-profile.
- [ ] History persistence.
- [ ] v2: agent monitoring (research_agent, cto_agent, OpenClaw sessions).

---

## 11. Open questions

1. **PSRAM presence on Cardputer-ADV** — answers determine scrollback strategy. Resolve in M0.
2. **UDM WireGuard API** — does the Network app expose a REST/SSH path for scripted peer add, or do we manage peers via the Network UI only? Affects `peer add` CLI.
3. **Direct-mode fallback** — should the device carry minimal Anthropic + OpenAI adapter code so it can bypass the proxy when WG fails? +~20 KB flash. Recommendation: **yes**, opt-in per profile (`direct: true`), key required.
4. **License** — pick before first commit beyond docs. Probably MIT or Apache-2.0.
5. **Conversation history at rest** — keep client-side only, or persist on proxy for cross-device continuity? v1 = client-only; revisit in v2.

---

## 12. Out of scope (v1)

- Voice input/output (ES8311 + mic)
- Image inputs (vision models)
- On-device tool execution (MCP client on the Cardputer itself)
- Multi-user / shared device
- BLE companion-app pairing
- Custom keyboard layouts / IME

---

## 13. References

- [M5Stack Cardputer-ADV product page](https://docs.m5stack.com/en/core/Cardputer-Adv)
- [`esp_wireguard` library](https://github.com/trombik/esp_wireguard)
- [Anthropic Messages API](https://docs.anthropic.com/en/api/messages)
- [OpenAI Chat Completions API](https://platform.openai.com/docs/api-reference/chat)
- [Ollama API](https://github.com/ollama/ollama/blob/main/docs/api.md)
- [UniFi WireGuard VPN Server docs](https://help.ui.com/hc/en-us/articles/115015389828)
- [ESP-IDF 5.x Programming Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/index.html)
- [PlatformIO ESP32-S3 platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)
