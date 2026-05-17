from __future__ import annotations

from pytest_httpx import HTTPXMock


_TINY = (
    b"event: message_start\n"
    b'data: {"type":"message_start","message":{"id":"m","model":"claude-opus-4-7"}}\n\n'
    b"event: content_block_delta\n"
    b'data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"."}}\n\n'
    b"event: message_stop\n"
    b'data: {"type":"message_stop"}\n\n'
)


def test_profile_test_success(client, httpx_mock: HTTPXMock):
    httpx_mock.add_response(
        method="POST",
        url="https://api.anthropic.com/v1/messages",
        content=_TINY,
        headers={"content-type": "text/event-stream"},
    )
    r = client.post(
        "/v1/profiles/claude-opus/test",
        headers={"Authorization": "Bearer test-bearer"},
    )
    assert r.status_code == 200
    body = r.json()
    assert body["ok"] is True
    assert body["latency_ms"] >= 0
    assert body["first_token_ms"] is not None


def test_profile_test_upstream_failure(client, httpx_mock: HTTPXMock):
    httpx_mock.add_response(
        method="POST",
        url="https://api.anthropic.com/v1/messages",
        status_code=500,
        json={"error": "bad"},
    )
    r = client.post(
        "/v1/profiles/claude-opus/test",
        headers={"Authorization": "Bearer test-bearer"},
    )
    # The /test endpoint reports the *result* of the test, not the
    # upstream status — so the HTTP response itself is 200.
    assert r.status_code == 200
    body = r.json()
    assert body["ok"] is False
    assert body["error"]


def test_profile_test_unknown_profile_404(client):
    r = client.post(
        "/v1/profiles/no-such/test",
        headers={"Authorization": "Bearer test-bearer"},
    )
    assert r.status_code == 404
