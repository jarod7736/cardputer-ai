from __future__ import annotations

from pytest_httpx import HTTPXMock


_ANTHROPIC_STREAM = (
    b"event: message_start\n"
    b'data: {"type":"message_start","message":{"id":"msg_1","model":"claude-opus-4-7"}}\n\n'
    b"event: content_block_delta\n"
    b'data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"hi there"}}\n\n'
    b"event: message_stop\n"
    b'data: {"type":"message_stop"}\n\n'
)


def test_passthrough_streams_oai_sse(client, httpx_mock: HTTPXMock):
    httpx_mock.add_response(
        method="POST",
        url="https://api.anthropic.com/v1/messages",
        content=_ANTHROPIC_STREAM,
        headers={"content-type": "text/event-stream"},
    )

    with client.stream(
        "POST",
        "/v1/chat/completions",
        headers={"Authorization": "Bearer test-bearer"},
        json={
            "profile_id": "claude-opus",
            "messages": [{"role": "user", "content": "say hi"}],
        },
    ) as r:
        assert r.status_code == 200
        assert "text/event-stream" in r.headers["content-type"]
        body = b"".join(r.iter_bytes()).decode("utf-8")

    assert '"role":"assistant"' in body
    assert '"content":"hi there"' in body
    assert '"finish_reason":"stop"' in body
    assert "data: [DONE]" in body


def test_upstream_5xx_surfaces_as_502(client, httpx_mock):
    httpx_mock.add_response(
        method="POST",
        url="https://api.anthropic.com/v1/messages",
        status_code=503,
        json={"error": "overloaded"},
    )
    r = client.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer test-bearer"},
        json={
            "profile_id": "claude-opus",
            "messages": [{"role": "user", "content": "hi"}],
        },
    )
    assert r.status_code == 502
    body = r.json()
    assert body["detail"]["upstream"] == "anthropic"
    assert body["detail"]["upstream_status"] == 503
