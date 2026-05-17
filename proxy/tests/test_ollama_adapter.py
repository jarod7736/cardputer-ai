from __future__ import annotations

import json

import pytest
from pytest_httpx import HTTPXMock

from cardputer_proxy.adapters.ollama import OllamaAdapter
from cardputer_proxy.schemas import Auth, ChatCompletionRequest, Message, Profile


PROFILE = Profile(
    id="ollama-llama3",
    label="Ollama Llama 3 8B",
    provider="ollama",
    endpoint="http://lobsterboy:11434",
    model="llama3:8b",
    auth=Auth(kind="none"),
)


def _stream(*payloads: dict) -> bytes:
    out = b""
    for p in payloads:
        out += b"data: " + json.dumps(p).encode("utf-8") + b"\n\n"
    out += b"data: [DONE]\n\n"
    return out


@pytest.mark.asyncio
async def test_passthrough_no_auth_header_when_secret_none(httpx_mock: HTTPXMock):
    httpx_mock.add_response(
        method="POST",
        url="http://lobsterboy:11434/v1/chat/completions",
        content=_stream(
            {"choices": [{"delta": {"role": "assistant"}}]},
            {"choices": [{"delta": {"content": "hello"}}]},
            {"choices": [{"delta": {}, "finish_reason": "stop"}]},
        ),
        headers={"content-type": "text/event-stream"},
    )

    adapter = OllamaAdapter()
    req = ChatCompletionRequest(
        profile_id="ollama-llama3",
        messages=[Message(role="user", content="hi")],
    )

    chunks = []
    async for c in adapter.stream_chat(PROFILE, req, secret=None):
        chunks.append(c)
    assert chunks[1]["choices"][0]["delta"]["content"] == "hello"

    sent = httpx_mock.get_request()
    assert "authorization" not in {k.lower() for k in sent.headers.keys()}
