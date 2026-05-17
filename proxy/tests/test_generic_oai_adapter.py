from __future__ import annotations

import pytest
from pytest_httpx import HTTPXMock

from cardputer_proxy.adapters.generic_oai import GenericOAIAdapter
from cardputer_proxy.schemas import Auth, ChatCompletionRequest, Message, Profile


@pytest.mark.asyncio
async def test_uses_bearer_when_secret_present(httpx_mock: HTTPXMock):
    profile = Profile(
        id="vllm",
        label="vLLM",
        provider="openai-compatible",
        endpoint="http://internal.box:8000",
        model="meta-llama/Llama-3.1-8B-Instruct",
        auth=Auth(kind="proxy-secret", secret_ref="vllm_token"),
    )
    httpx_mock.add_response(
        method="POST",
        url="http://internal.box:8000/v1/chat/completions",
        content=b'data: {"choices":[{"delta":{}, "finish_reason":"stop"}]}\n\ndata: [DONE]\n\n',
        headers={"content-type": "text/event-stream"},
    )

    adapter = GenericOAIAdapter()
    req = ChatCompletionRequest(
        profile_id="vllm", messages=[Message(role="user", content="hi")]
    )
    async for _ in adapter.stream_chat(profile, req, secret="sk-internal"):
        pass
    sent = httpx_mock.get_request()
    assert sent.headers["authorization"] == "Bearer sk-internal"


@pytest.mark.asyncio
async def test_omits_auth_when_no_secret(httpx_mock: HTTPXMock):
    profile = Profile(
        id="local",
        label="local",
        provider="openai-compatible",
        endpoint="http://127.0.0.1:8001",
        model="local-model",
        auth=Auth(kind="none"),
    )
    httpx_mock.add_response(
        method="POST",
        url="http://127.0.0.1:8001/v1/chat/completions",
        content=b"data: [DONE]\n\n",
        headers={"content-type": "text/event-stream"},
    )
    adapter = GenericOAIAdapter()
    req = ChatCompletionRequest(
        profile_id="local", messages=[Message(role="user", content="hi")]
    )
    async for _ in adapter.stream_chat(profile, req, secret=None):
        pass
    sent = httpx_mock.get_request()
    assert "authorization" not in {k.lower() for k in sent.headers.keys()}
