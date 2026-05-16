"""Runtime config + secret loading.

Secrets and the profile catalog are loaded once at app startup, not on
each request, so swapping a secret requires a service restart. Intentional:
secret rotation is rare and the simplicity is worth more than hot-reload.
"""

from __future__ import annotations

import os
import tomllib
from dataclasses import dataclass
from pathlib import Path

from cardputer_proxy.schemas import Profile


DEFAULT_PROFILE = Profile(
    id="claude-opus",
    label="Claude Opus 4.7",
    provider="anthropic",
    endpoint="https://api.anthropic.com",
    model="claude-opus-4-7",
    max_tokens=4096,
    temperature=1.0,
)


@dataclass(frozen=True)
class Settings:
    anthropic_api_key: str
    device_bearer_token: str
    profiles: dict[str, Profile]

    def get_profile(self, profile_id: str) -> Profile | None:
        return self.profiles.get(profile_id)


def _read_secret_file(path: Path, name: str) -> str:
    if not path.is_file():
        raise RuntimeError(f"missing secret file: {name} at {path}")
    val = path.read_text(encoding="utf-8").strip()
    if not val:
        raise RuntimeError(f"empty secret file: {name} at {path}")
    return val


def load_settings() -> Settings:
    secret_dir = Path(os.environ.get("CARDPUTER_PROXY_SECRETS_DIR", "/etc/cardputer-proxy/secrets"))
    anthropic_key = _read_secret_file(secret_dir / "anthropic_api_key", "anthropic_api_key")
    bearer = _read_secret_file(secret_dir / "device_bearer_token", "device_bearer_token")

    profiles: dict[str, Profile] = {DEFAULT_PROFILE.id: DEFAULT_PROFILE}
    config_path_raw = os.environ.get("CARDPUTER_PROXY_CONFIG_PATH")
    if config_path_raw:
        config_path = Path(config_path_raw)
        if config_path.is_file():
            data = tomllib.loads(config_path.read_text(encoding="utf-8"))
            new_profiles: dict[str, Profile] = {}
            for entry in data.get("profiles", []):
                p = Profile.model_validate(entry)
                new_profiles[p.id] = p
            if new_profiles:
                profiles = new_profiles

    return Settings(
        anthropic_api_key=anthropic_key,
        device_bearer_token=bearer,
        profiles=profiles,
    )
