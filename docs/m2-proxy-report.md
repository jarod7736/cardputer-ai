# M2 Proxy Report — claude-proxy on lobsterboy

**Date completed:** 2026-05-16
**Branch / commit at completion:** `m2-execution` @ `5c78799`
**Host:** lobsterboy (Ubuntu 24.04.4 LTS)

## Deployment specifics

| Item | Value |
|---|---|
| OS | Ubuntu 24.04.4 LTS (Noble Numbat) |
| Python | 3.12.3 (`/usr/bin/python3.12`) — system default; no PPA or pyenv |
| App path | `/opt/cardputer-proxy/` |
| venv | `/opt/cardputer-proxy/.venv/` (created by `install.sh`) |
| Config dir | `/etc/cardputer-proxy/` (mode 0750, root:cardputer-proxy) |
| Secrets dir | `/etc/cardputer-proxy/secrets/` (mode 0700, root:root) |
| Service user | `cardputer-proxy` (system, nologin) |
| systemd unit | `/etc/systemd/system/cardputer-proxy.service` |
| Listen | `127.0.0.1:8420` by default; switch to `0.0.0.0` via `CARDPUTER_PROXY_LISTEN_HOST` in `/etc/cardputer-proxy/cardputer-proxy.env` |

## Install-time gotcha

`install.sh` originally hard-coded `PYTHON=python3.11`, which Ubuntu 24.04
doesn't ship by default (only 3.12). Fixed in commit `5c78799`: the script
now auto-detects any Python 3.11+ on `PATH`. Override with `PYTHON=python3.X`
if you need a specific interpreter.

## Secret sourcing

The Anthropic API key was already on lobsterboy in `~jarod7736/.cardputer-ai.env`
(raw key on a single line, no `KEY=` prefix). `install.sh` does not touch
secrets; they were copied into place manually before running install:

```bash
sudo install -d -m 0700 -o root -g root /etc/cardputer-proxy/secrets
sudo install -m 0600 -o root -g root ~/.cardputer-ai.env \
  /etc/cardputer-proxy/secrets/anthropic_api_key
sudo bash -c "head -c 32 /dev/urandom | base64 | tr -d '\n' \
  > /etc/cardputer-proxy/secrets/device_bearer_token"
sudo chmod 0600 /etc/cardputer-proxy/secrets/device_bearer_token
```

The bearer token is what the Cardputer will use in `Authorization: Bearer ...`
once M3 wires the device side. Save it — it's a runtime credential and
isn't recoverable from the API key.

## Confirmation that secrets stay out of the world

- `systemctl cat cardputer-proxy.service` shows only the `LoadCredential=` lines
  pointing at the secret files; no key material.
- The service environment (visible via `cat /proc/$(pgrep -f cardputer_proxy)/environ`)
  contains `CARDPUTER_PROXY_SECRETS_DIR=<systemd credentials path>` but no
  `ANTHROPIC_API_KEY`. The key file lives inside the systemd credentials
  tmpfs only for the lifetime of the process and is not readable by other
  users.
- No env file references the key.

## End-to-end proof

```text
$ curl -N -s \
    -H "Authorization: Bearer $BEARER" \
    -H "Content-Type: application/json" \
    -d '{"profile_id":"claude-opus","messages":[{"role":"user","content":"in 5 words: what is wireguard"}]}' \
    http://127.0.0.1:8420/v1/chat/completions
data: {"object":"chat.completion.chunk","model":"claude-opus-4-7","choices":[{"index":0,"delta":{"role":"assistant"},"finish_reason":null}]}

data: {"object":"chat.completion.chunk","model":"claude-opus-4-7","choices":[{"index":0,"delta":{"content":"F"},"finish_reason":null}]}

data: {"object":"chat.completion.chunk","model":"claude-opus-4-7","choices":[{"index":0,"delta":{"content":"ast, modern, secure VPN protocol."},"finish_reason":null}]}

data: {"object":"chat.completion.chunk","model":"claude-opus-4-7","choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}

data: [DONE]
```

All four expected event shapes (role, text deltas, finish, `[DONE]`) are
present. Total tokens used: tiny — single short user prompt, ~6-token
response.

## Remaining for Cardputer-side access (M3 prereq)

The proxy currently listens on `127.0.0.1:8420` only. For the device to
reach it from inside the WG tunnel, expose it on the Tailscale/WG-reachable
interfaces:

```bash
echo "CARDPUTER_PROXY_LISTEN_HOST=0.0.0.0" \
  | sudo tee /etc/cardputer-proxy/cardputer-proxy.env
sudo systemctl restart cardputer-proxy
```

If lobsterboy runs `ufw`, scope port 8420 to Tailnet + WG subnet:

```bash
sudo ufw allow in on tailscale0 to any port 8420 proto tcp
sudo ufw allow from 192.168.14.0/24 to any port 8420 proto tcp
```

(Skip the second line if lobsterboy reaches the WG subnet via Tailscale
subnet routing rather than directly.)

## Lessons captured for M3+

1. **No anthropic SDK** — direct httpx streaming made the wire-format
   translation easier to read and debug than any SDK call would have. Keep
   this pattern for the OpenAI/Ollama adapters in M4 unless one of them
   needs something we can't easily express in raw httpx.

2. **Pre-flight error translation matters.** Drawing the first chunk
   inside a try/except *before* opening the StreamingResponse means
   upstream 4xx/5xx surfaces as a clean JSON 502, not as a half-written
   event-stream the device would have to parse around. The Cardputer
   firmware in M3 will assume any SSE response is a normal stream; the
   502 path saves a lot of edge-case handling.

3. **`LoadCredential=` is the right shape** for service secrets on
   modern systemd — the key file is read once at service start and
   tmpfs-mounted into the service namespace. No `EnvironmentFile=` with
   secrets in it; `systemctl cat` is clean.

4. **`install.sh` auto-detection** is worth the 20 lines. Hard-coded
   Python versions break on every other distro. M3-and-later install
   scripts (provisioning CLI, OTA, etc.) should follow this pattern.

## Open items deferred

- **TLS**: not needed for M2 (Tailnet + WG both encrypt). Revisit when
  anything gets exposed publicly.
- **Per-device bearer tokens with scopes**: M5 provisioning.
- **Profile CRUD endpoints + multiple adapters**: M4.
- **Rate limiting / token accounting**: M6 polish.
