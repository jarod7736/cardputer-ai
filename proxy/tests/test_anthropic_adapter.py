from __future__ import annotations

import json

import pytest
from pytest_httpx import HTTPXMock

from cardputer_proxy.adapters.anthropic import AnthropicAdapter
from cardputer_proxy.schemas import ChatCompletionRequest, Message, Profile


PROFILE = Profile(
    id="claude-opus",
    label="Claude Opus 4.7",
    provider="anthropic",
    endpoint="https://api.anthropic.com",
    model="claude-opus-4-7",
)


def _anthropic_sse_bytes() -> bytes:
    events = [
        "event: message_start\n"
        'data: {"type":"message_start","message":{"id":"msg_1","model":"claude-opus-4-7"}}\n\n',
        "event: content_block_start\n"
        'data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}\n\n',
        "event: content_block_delta\n"
        'data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello "}}\n\n',
        "event: content_block_delta\n"
        'data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"world"}}\n\n',
        "event: content_block_stop\n"
        'data: {"type":"content_block_stop","index":0}\n\n',
        "event: message_delta\n"
        'data: {"type":"message_delta","delta":{"stop_reason":"end_turn"}}\n\n',
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n",
    ]
    return "".join(events).encode("utf-8")


@pytest.mark.asyncio
async def test_stream_chat_translates_text_deltas(httpx_mock: HTTPXMock):
    httpx_mock.add_response(
        method="POST",
        url="https://api.anthropic.com/v1/messages",
        content=_anthropic_sse_bytes(),
        headers={"content-type": "text/event-stream"},
    )

    adapter = AnthropicAdapter()
    req = ChatCompletionRequest(
        profile_id="claude-opus",
        messages=[Message(role="user", content="Say hello")],
    )

    chunks: list[dict] = []
    async for c in adapter.stream_chat(PROFILE, req, secret="sk-test"):
        chunks.append(c)

    # First chunk is the role announcement
    assert chunks[0]["choices"][0]["delta"]["role"] == "assistant"
    # Subsequent text chunks
    texts = [
        c["choices"][0]["delta"].get("content", "")
        for c in chunks
        if "content" in c["choices"][0]["delta"]
    ]
    assert "".join(texts) == "Hello world"
    # Final chunk has a finish_reason
    assert chunks[-1]["choices"][0]["finish_reason"] == "stop"


@pytest.mark.asyncio
async def test_stream_chat_extracts_system_messages(httpx_mock: HTTPXMock):
    httpx_mock.add_response(
        method="POST",
        url="https://api.anthropic.com/v1/messages",
        content=_anthropic_sse_bytes(),
        headers={"content-type": "text/event-stream"},
    )

    adapter = AnthropicAdapter()
    req = ChatCompletionRequest(
        profile_id="claude-opus",
        messages=[
            Message(role="system", content="You are terse."),
            Message(role="user", content="Hi"),
        ],
    )
    async for _ in adapter.stream_chat(PROFILE, req, secret="sk-test"):
        pass

    sent = httpx_mock.get_request()
    assert sent is not None
    body = json.loads(sent.content)
    assert body["system"] == "You are terse."
    assert all(m["role"] != "system" for m in body["messages"])
    assert body["model"] == "claude-opus-4-7"
    assert body["stream"] is True
