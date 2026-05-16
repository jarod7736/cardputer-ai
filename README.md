# cardputer-ai

A pocket-sized, provider-agnostic AI client for the **M5Stack Cardputer ADV**.

The device acts as a thin chat/agent terminal; a homelab proxy holds API keys and translates between the device's wire protocol and whichever upstream provider (Anthropic, OpenAI, Ollama, etc.) the active **profile** points at. Off-network access is via WireGuard back to the home UDM.

```
                ┌──────────────────────────────────────┐
                │           Cardputer ADV              │
                │   (ESP32-S3, ST7789V2 240x135 LCD,   │
                │    TCA8418 keyboard, microSD,        │
                │    WireGuard client in firmware)     │
                └──────────────────────────────────────┘
                                  │
                           WireGuard tunnel
                                  │
                  ┌───────────────▼───────────────┐
                  │      UDM (WG server)          │
                  └───────────────┬───────────────┘
                                  │   LAN / Tailscale
                  ┌───────────────▼───────────────┐
                  │   lobsterboy — claude-proxy   │
                  │   - OAI-compatible ingress    │
                  │   - profile + secret store    │
                  │   - upstream adapters         │
                  └───┬──────┬───────┬──────┬─────┘
                      │      │       │      │
              Anthropic  OpenAI  Ollama   OpenClaw / other
```

## Status

🚧 **Design phase.** See [`docs/DESIGN.md`](docs/DESIGN.md) for the full spec.

## Repository layout (planned)

```
cardputer-ai/
├── README.md
├── docs/
│   └── DESIGN.md            ← architecture, protocol, UI, build plan
├── firmware/                ← PlatformIO project for the Cardputer ADV
│   ├── platformio.ini
│   └── src/
├── proxy/                   ← lobsterboy-hosted API proxy (Python/FastAPI)
│   ├── pyproject.toml
│   └── src/cardputer_proxy/
├── provisioning/            ← SD-card config templates, WG keygen scripts
└── ops/                     ← systemd unit, container compose, WG configs
```

## Hardware target

**M5Stack Cardputer-ADV** — [docs](https://docs.m5stack.com/en/core/Cardputer-Adv)

- ESP32-S3FN8 (Xtensa LX7, dual-core 240 MHz)
- 8 MB flash, microSD slot
- 240×135 ST7789V2 LCD
- TCA8418 keyboard controller
- BMI270 IMU, ES8311 audio codec, IR emitter
- 1750 mAh battery
- Wi-Fi 4 + BLE 5

## License

TBD.
