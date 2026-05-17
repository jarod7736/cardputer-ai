# M4 Profile System Report

**Date completed:** 2026-05-17
**Branch / commit at completion:** `main` @ HEAD (PRs #10 + firmware PR)
**Hardware:** M5Stack Cardputer-ADV (ESP32-S3FN8, 8 MB flash, no PSRAM)

## Headline

Cardputer-AI is now provider-agnostic. The proxy keeps a JSON catalog of
profiles on disk, looks up upstream credentials per request, and dispatches
to the right adapter (Anthropic / OpenAI / Ollama / generic OAI-compatible).
The device pulls the catalog over the tunnel, pickers a profile via a
full-screen list, persists the choice in NVS, and routes each chat
request through the active profile. **Swap models with a few keystrokes —
no reflash.**

## Final firmware budget (post-M4)

| Resource | Used | Available | Δ vs M3 |
|---|---|---|---|
| Flash (app slot) | 1 026 921 B (30.1 %) | 3 407 872 B | +6 425 B |
| RAM (internal SRAM) | 40 812 B (12.5 %) | 327 680 B | +112 B |

Picker + NVS + catalog parser + HID arrow handling fit in ~6 KB of flash.
Plenty of headroom.

## Proxy — 40 tests, all pass

- `catalog.py` — atomic JSON persistence (`os.replace` over a tempfile),
  schema version pin, rollback on save failure.
- `secrets_store.py` — read-only file access with a `^[A-Za-z0-9_.-]+$`
  validator that refuses traversal.
- 4 adapters under `adapters/`: anthropic (carried from M2), openai (new),
  ollama (subclass), generic_oai (subclass). Each registers itself at
  import time via a small `adapter_registry`.
- `POST /v1/profiles/{id}/test` for ops sanity checks. Returns
  `{ok, latency_ms, first_token_ms, error}`.
- `cardputer-proxy secrets {list,set,rotate,delete,show-path}` —
  argparse, stdin-only for values, never logs the value.
- `profiles.json.example` ships 4 starters (Opus, Sonnet, gpt-4o-mini,
  Llama 3 8B); `install.sh` drops it on fresh installs without clobbering.

## Device

- `proxy_api`: hand-rolled JSON walker that GETs `/v1/profiles` and
  parses into `vector<Profile>` (id, label, provider, model). Avoids
  pulling ArduinoJson onto the path; the response shape is fixed.
- `profile_store`: thin Arduino `Preferences` wrapper, NVS namespace
  `cprox`, key `profile_id`. Survives reboots.
- `picker_view`: full-screen list, 30 s idle timeout, current profile
  marked with `*`. Cursor seeded to the active profile so re-opening
  shows your current selection highlighted.
- `chat_client::send()` now takes a `profile_id` parameter; the body's
  `profile_id` field is the only thing the proxy needs to dispatch.
- Status bar carries the active profile label; assistant turn prefix
  also uses it (`gpt-4o-mini> ` etc., not the hard-coded `claude> `).

## Bugs that actually bit us

### 1. Secrets directory was `0700 root:root` — service couldn't read M4 secrets

M2's systemd unit used `LoadCredential=` to mount **just the two M2
secrets** (anthropic key, bearer) into a runtime tmpfs the service user
could read. New M4 secrets sitting in `/etc/cardputer-proxy/secrets/`
itself were invisible to the service: it couldn't even traverse the
directory. Symptom: `/v1/profiles/openai-4o-mini/test` returned
`{"ok": false, "error": "secret missing: openai_api_key"}` despite
the file existing.

**Fix:** dropped `LoadCredential=`, set the directory to
`0750 root:cardputer-proxy` and files to `0640 root:cardputer-proxy`.
`install.sh` now `chmod 0640 + chown root:cardputer-proxy` any
pre-existing secrets so upgrades from M2 just work. `cli.py cmd_set`
writes new secrets with the same perms.

### 2. Arrow keys never produce HID scan codes 0x4F-0x52 on this keyboard

First arrow-key attempt watched `state.hid_keys` for HID arrow codes —
nothing came through. M5Cardputer's `KeysState.hid_keys` is populated
**only** via `_kb_asciimap[]`, which maps printable ASCII to HID, never
arrow codes. The Cardputer's arrows are labeled on `,./;` and only fire
as printable chars with `state.fn == true`.

**Fix:** detect `state.fn` and map word chars `,` `.` `;` `/` to KB_LEFT
KB_DOWN KB_UP KB_RIGHT, swallowing the printable. Bonus: Fn+`\`` → Esc,
since the bare backtick has no other home.

### 3. Assistant prefix was hard-coded `claude> `

First post-picker test: switched to OpenAI, sent a message, response
streamed in — labeled `claude> `. The profile actually routed to OpenAI
fine; the prefix was a leftover from M3 when there was only one model.

**Fix:** prefix uses the active profile's catalog label.

### 4. OpenAI `502 Bad Gateway` from streaming endpoint (one-time)

Proxy log showed `httpx.ConnectError: All connection attempts failed`
to `api.openai.com`. Curl from the same host worked on the next
attempt. Best guess: a transient cloudflare/connection flap, not
reproducible after retry. Not patched — would be premature.
**Worth recording** because the symptom looked code-side.

## Out of scope (deferred to M4.5 / M5)

Per plan, intentionally not built:

- Device-side profile editor (add/edit/delete from the keyboard) —
  use the proxy's CRUD endpoints or the secrets CLI for now.
- Fn+T "test from device" — easy after the picker; ship in M4.5.
- Per-device bearer tokens with scopes — M5.
- Esc-to-cancel, PgUp/PgDn scrollback, multi-line input — UX polish.
- `device-key` auth-kind device-side support — proxy accepts it (501s
  for now); device wire-up lands with M5 provisioning.

## Done criteria — all met

1. ✅ Proxy holds a persistent JSON profile catalog with CRUD endpoints
2. ✅ Four adapters wired and selected from the profile's `provider`
3. ✅ Per-profile secrets resolved at request time from
   `/etc/cardputer-proxy/secrets/`
4. ✅ `cardputer-proxy secrets` CLI exists and refuses path traversal
5. ✅ Device pulls `/v1/profiles` on boot + on picker open
6. ✅ Device persists active profile id in NVS across reboots
7. ✅ Status bar + assistant-turn prefix reflect the active profile
8. ✅ End-to-end verified on device: gpt-4o-mini and claude-opus
   both stream correctly after switching
9. ✅ 40 proxy tests pass
10. ✅ This report exists
11. (Tag `m4-complete` next)
