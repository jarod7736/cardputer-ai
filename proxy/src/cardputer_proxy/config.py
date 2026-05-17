"""Runtime config + secret loading.

Secrets and the profile catalog are loaded once at app startup. The
catalog is a mutable in-process object (CRUD goes through it); secrets
are read on each request from disk.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

from cardputer_proxy.catalog import Catalog
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
    catalog: Catalog
    secret_dir: Path

    def get_profile(self, profile_id: str):
        return self.catalog.get(profile_id)


def _read_secret_file(path: Path, name: str) -> str:
    if not path.is_file():
        raise RuntimeError(f"missing secret file: {name} at {path}")
    val = path.read_text(encoding="utf-8").strip()
    if not val:
        raise RuntimeError(f"empty secret file: {name} at {path}")
    return val


def load_settings() -> Settings:
    secret_dir = Path(
        os.environ.get("CARDPUTER_PROXY_SECRETS_DIR", "/etc/cardputer-proxy/secrets")
    )
    anthropic_key = _read_secret_file(
        secret_dir / "anthropic_api_key", "anthropic_api_key"
    )
    bearer = _read_secret_file(
        secret_dir / "device_bearer_token", "device_bearer_token"
    )

    catalog_path = Path(
        os.environ.get(
            "CARDPUTER_PROXY_CATALOG_PATH", "/etc/cardputer-proxy/profiles.json"
        )
    )
    catalog = Catalog(catalog_path)
    # Seed with the M2 default so existing M3 firmware keeps working when
    # there's no on-disk catalog yet (fresh install).
    if not catalog.list():
        catalog.upsert(DEFAULT_PROFILE)

    return Settings(
        anthropic_api_key=anthropic_key,
        device_bearer_token=bearer,
        catalog=catalog,
        secret_dir=secret_dir,
    )
