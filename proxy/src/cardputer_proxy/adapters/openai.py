"""OpenAI-compatible Chat Completions adapter.

The wire format on the device is already OAI Chat Completions, so this
is mostly a pass-through with a header rewrite. Used directly for the
'openai' provider, and as the base for Ollama / generic-OAI.
"""

from __future__ import annotations

import json
from typing import AsyncIterator

import httpx

from cardputer_proxy.schemas import ChatCompletionRequest, Profile


class OpenAIAdapter:
    PATH = "/v1/chat/completions"
    AUTH_HEADER = "Authorization"
    AUTH_VALUE_TEMPLATE = "Bearer {}"

    async def stream_chat(
        self,
        profile: Profile,
        request: ChatCompletionRequest,
        secret: str | None,
    ) -> AsyncIterator[dict]:
        body: dict = {
            "model": profile.model,
            "messages": [m.model_dump() for m in request.messages],
            "stream": True,
        }
        if request.max_tokens is not None:
            body["max_tokens"] = request.max_tokens
        elif profile.max_tokens:
            body["max_tokens"] = profile.max_tokens
        if request.temperature is not None:
            body["temperature"] = request.temperature
        elif profile.temperature is not None:
            body["temperature"] = profile.temperature

        headers = {
            "content-type": "application/json",
            "accept": "text/event-stream",
        }
        if secret is not None:
            headers[self.AUTH_HEADER] = self.AUTH_VALUE_TEMPLATE.format(secret)

        url = profile.endpoint.rstrip("/") + self.PATH
        async with httpx.AsyncClient(timeout=httpx.Timeout(60.0, read=300.0)) as cx:
            async with cx.stream("POST", url, headers=headers, json=body) as resp:
                resp.raise_for_status()
                async for line in resp.aiter_lines():
                    if not line.startswith("data: "):
                        continue
                    payload = line[6:]
                    if payload == "[DONE]":
                        break
                    try:
                        evt = json.loads(payload)
                    except json.JSONDecodeError:
                        continue
                    # Trim noisy fields so the device sees a consistent shape.
                    yield {
                        "object": "chat.completion.chunk",
                        "model": profile.model,
                        "choices": evt.get("choices", []),
                    }


def _register() -> None:
    from cardputer_proxy.adapters import register
    register("openai", OpenAIAdapter)


_register()
