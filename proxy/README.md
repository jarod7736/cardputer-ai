# proxy/ — Cardputer-AI claude-proxy

The OAI-compatible HTTP proxy that the Cardputer-AI firmware connects to.
Holds API keys, translates between the device's wire format and provider
upstreams (Anthropic for M2; OpenAI/Ollama/OpenClaw arrive in M4).

## Dev

```bash
cd proxy
python3.11 -m venv .venv
.venv/bin/pip install -e ".[dev]"
.venv/bin/pytest                    # run the test suite
.venv/bin/python -m cardputer_proxy # dev server on 127.0.0.1:8420
```

## Deploy

See `../ops/cardputer-proxy/install.sh` for the one-shot deploy script.
The service runs under systemd on the host (e.g. lobsterboy) as the
`cardputer-proxy` system user. Secrets are loaded via `LoadCredential=`
from files in `/etc/cardputer-proxy/secrets/`.
