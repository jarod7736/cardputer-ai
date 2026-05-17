from __future__ import annotations

import json

import pytest
from pytest_httpx import HTTPXMock

from cardputer_proxy.adapters.openai import OpenAIAdapter
from cardputer_proxy.schemas import Auth, ChatCompletionRequest, Message, Profile


PROFILE = Profile(
    id="oai",
    label="OpenAI gpt-mini",
    provider="openai",
    endpoint="https://api.openai.com",
    model="gpt-4o-mini",
    auth=Auth(kind="proxy-secret", secret_ref="openai"),
)


def _stream(*payloads: dict) -> bytes:
    out = b""
    for p in payloads:
        out += b"data: " + json.dumps(p).encode("utf-8") + b"\n\n"
    out += b"data: [DONE]\n\n"
    return out


@pytest.mark.asyncio
async def test_passthrough_translates_oai_chunks(httpx_mock: HTTPXMock):
    httpx_mock.add_response(
        method="POST",
        url="https://api.openai.com/v1/chat/completions",
        content=_stream(
            {"choices": [{"delta": {"role": "assistant"}, "finish_reason": None}]},
            {"choices": [{"delta": {"content": "hi"}, "finish_reason": None}]},
            {"choices": [{"delta": {}, "finish_reason": "stop"}]},
        ),
        headers={"content-type": "text/event-stream"},
    )

    adapter = OpenAIAdapter()
    req = ChatCompletionRequest(
        profile_id="oai", messages=[Message(role="user", content="hi")]
    )
    chunks = []
    async for c in adapter.stream_chat(PROFILE, req, secret="sk-test"):
        chunks.append(c)

    assert chunks[0]["choices"][0]["delta"]["role"] == "assistant"
    assert chunks[1]["choices"][0]["delta"]["content"] == "hi"
    assert chunks[-1]["choices"][0]["finish_reason"] == "stop"
    for c in chunks:
        assert c["model"] == "gpt-4o-mini"


@pytest.mark.asyncio
async def test_request_uses_bearer_auth(httpx_mock: HTTPXMock):
    httpx_mock.add_response(
        method="POST",
        url="https://api.openai.com/v1/chat/completions",
        content=_stream({"choices": [{"delta": {}, "finish_reason": "stop"}]}),
        headers={"content-type": "text/event-stream"},
    )
    adapter = OpenAIAdapter()
    req = ChatCompletionRequest(
        profile_id="oai", messages=[Message(role="user", content="hi")]
    )
    async for _ in adapter.stream_chat(PROFILE, req, secret="sk-test"):
        pass
    sent = httpx_mock.get_request()
    assert sent.headers["authorization"] == "Bearer sk-test"
    body = json.loads(sent.content)
    assert body["model"] == "gpt-4o-mini"
    assert body["stream"] is True
