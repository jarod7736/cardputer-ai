from __future__ import annotations

import json

import pytest
from fastapi.testclient import TestClient

from cardputer_proxy.app import create_app


def test_health_endpoint_no_auth_required(client):
    r = client.get("/healthz")
    assert r.status_code == 200
    assert r.json() == {"status": "ok"}


def test_chat_completions_rejects_missing_bearer(client):
    r = client.post("/v1/chat/completions", json={
        "profile_id": "claude-opus",
        "messages": [{"role": "user", "content": "hi"}],
    })
    assert r.status_code == 401


def test_chat_completions_rejects_wrong_bearer(client):
    r = client.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer not-the-token"},
        json={
            "profile_id": "claude-opus",
            "messages": [{"role": "user", "content": "hi"}],
        },
    )
    assert r.status_code == 401


def test_chat_completions_accepts_correct_bearer_unknown_profile(client):
    """Legacy single-bearer path (M4 backward compat) still works."""
    r = client.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer test-bearer"},
        json={
            "profile_id": "no-such-profile",
            "messages": [{"role": "user", "content": "hi"}],
        },
    )
    assert r.status_code == 404


@pytest.fixture
def client_with_tokens(monkeypatch, tmp_path) -> TestClient:
    """Like `client` but seeds a tokens.json with two device tokens."""
    secret_dir = tmp_path / "secrets"
    secret_dir.mkdir()
    (secret_dir / "anthropic_api_key").write_text("test-anthropic-key", encoding="utf-8")
    # Note: no device_bearer_token file → legacy fallback disabled.
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

    tokens_path = tmp_path / "tokens.json"
    tokens_path.write_text(json.dumps({
        "version": 1,
        "tokens": [
            {
                "device_id": "alpha",
                "token": "tok_alpha_aaaaaaaaaaaaaa",
                "label": "alpha device",
                "created_at": "2026-05-17T00:00:00Z",
                "revoked": False,
            },
            {
                "device_id": "beta-revoked",
                "token": "tok_beta_bbbbbbbbbbbbbb",
                "label": "beta device",
                "created_at": "2026-05-17T00:00:00Z",
                "revoked": True,
            },
        ],
    }), encoding="utf-8")
    monkeypatch.setenv("CARDPUTER_PROXY_TOKENS_PATH", str(tokens_path))

    return TestClient(create_app())


def test_tokens_json_accepts_known_token(client_with_tokens):
    r = client_with_tokens.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer tok_alpha_aaaaaaaaaaaaaa"},
        json={
            "profile_id": "no-such-profile",
            "messages": [{"role": "user", "content": "hi"}],
        },
    )
    # Past auth (would be 401 otherwise) → 404 on unknown profile.
    assert r.status_code == 404


def test_tokens_json_rejects_revoked_token(client_with_tokens):
    r = client_with_tokens.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer tok_beta_bbbbbbbbbbbbbb"},
        json={
            "profile_id": "claude-opus",
            "messages": [{"role": "user", "content": "hi"}],
        },
    )
    assert r.status_code == 401
    assert "revoked" in r.json()["detail"].lower()


def test_tokens_json_rejects_unknown_token_without_legacy_fallback(client_with_tokens):
    r = client_with_tokens.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer test-bearer"},
        json={
            "profile_id": "claude-opus",
            "messages": [{"role": "user", "content": "hi"}],
        },
    )
    assert r.status_code == 401
