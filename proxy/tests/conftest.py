from __future__ import annotations

import json

import pytest
from fastapi.testclient import TestClient

from cardputer_proxy.app import create_app


@pytest.fixture
def client(monkeypatch, tmp_path) -> TestClient:
    """A fresh FastAPI TestClient per test, with secrets + catalog under tmp_path."""
    secret_dir = tmp_path / "secrets"
    secret_dir.mkdir()
    (secret_dir / "anthropic_api_key").write_text("test-anthropic-key", encoding="utf-8")
    (secret_dir / "device_bearer_token").write_text("test-bearer", encoding="utf-8")
    monkeypatch.setenv("CARDPUTER_PROXY_SECRETS_DIR", str(secret_dir))

    catalog_path = tmp_path / "profiles.json"
    catalog_path.write_text(json.dumps({
        "version": 1,
        "profiles": [{
            "id": "claude-opus",
            "label": "Claude Opus 4.7",
            "provider": "anthropic",
            "endpoint": "https://api.anthropic.com",
            "model": "claude-opus-4-7",
            "max_tokens": 4096,
            "temperature": 1.0,
        }],
    }), encoding="utf-8")
    monkeypatch.setenv("CARDPUTER_PROXY_CATALOG_PATH", str(catalog_path))

    app = create_app()
    return TestClient(app)
