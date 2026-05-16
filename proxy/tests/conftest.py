from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

from cardputer_proxy.app import create_app


@pytest.fixture
def client(monkeypatch, tmp_path) -> TestClient:
    """A fresh FastAPI TestClient per test, with secrets pointed at tmp dirs."""
    secret_dir = tmp_path / "secrets"
    secret_dir.mkdir()
    (secret_dir / "anthropic_api_key").write_text("test-anthropic-key", encoding="utf-8")
    (secret_dir / "device_bearer_token").write_text("test-bearer", encoding="utf-8")
    monkeypatch.setenv("CARDPUTER_PROXY_SECRETS_DIR", str(secret_dir))
    monkeypatch.setenv("CARDPUTER_PROXY_CONFIG_PATH", "")
    app = create_app()
    return TestClient(app)
