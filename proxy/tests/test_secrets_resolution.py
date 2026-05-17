from __future__ import annotations

from pytest_httpx import HTTPXMock


_STREAM = (
    b"event: message_start\n"
    b'data: {"type":"message_start","message":{"id":"m","model":"claude-opus-4-7"}}\n\n'
    b"event: content_block_delta\n"
    b'data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"x"}}\n\n'
    b"event: message_stop\n"
    b'data: {"type":"message_stop"}\n\n'
)


def test_chat_uses_secret_from_profile_ref(client, httpx_mock: HTTPXMock):
    # Seeded claude-opus profile defaults to auth.secret_ref="anthropic_api_key"
    # which conftest wrote as "test-anthropic-key".
    httpx_mock.add_response(
        method="POST",
        url="https://api.anthropic.com/v1/messages",
        content=_STREAM,
        headers={"content-type": "text/event-stream"},
    )
    r = client.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer test-bearer"},
        json={
            "profile_id": "claude-opus",
            "messages": [{"role": "user", "content": "hi"}],
        },
    )
    assert r.status_code == 200
    sent = httpx_mock.get_request()
    assert sent.headers["x-api-key"] == "test-anthropic-key"


def test_chat_with_missing_secret_returns_500(client):
    # Create a profile whose secret doesn't exist on disk.
    r = client.post(
        "/v1/profiles",
        headers={"Authorization": "Bearer test-bearer"},
        json={
            "id": "missing-secret",
            "label": "broken",
            "provider": "anthropic",
            "endpoint": "https://api.anthropic.com",
            "model": "claude-opus-4-7",
            "auth": {"kind": "proxy-secret", "secret_ref": "does_not_exist"},
        },
    )
    assert r.status_code == 201
    r2 = client.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer test-bearer"},
        json={
            "profile_id": "missing-secret",
            "messages": [{"role": "user", "content": "hi"}],
        },
    )
    assert r2.status_code == 500


def test_device_key_auth_returns_501_for_now(client):
    client.post(
        "/v1/profiles",
        headers={"Authorization": "Bearer test-bearer"},
        json={
            "id": "byok",
            "label": "BYO",
            "provider": "anthropic",
            "endpoint": "https://api.anthropic.com",
            "model": "claude-opus-4-7",
            "auth": {"kind": "device-key"},
        },
    )
    r = client.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer test-bearer"},
        json={
            "profile_id": "byok",
            "messages": [{"role": "user", "content": "hi"}],
        },
    )
    assert r.status_code == 501
