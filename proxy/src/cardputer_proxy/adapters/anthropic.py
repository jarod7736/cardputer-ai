"""Anthropic Messages API adapter.

Translates one OAI Chat Completions request into one Anthropic Messages
request, and re-emits Anthropic's SSE event stream as OAI-shaped chunks.

We do this by hand (no Anthropic SDK) for three reasons:
1. The SDK is heavy for what we need (auth header + JSON POST + SSE read).
2. Doing the translation explicitly documents the wire-format mapping in
   one place a reader can audit.
3. We want fine control over the SSE pass-through and connection lifetime,
   which is awkward through the SDK's higher-level abstractions.
"""

from __future__ import annotations

import json
from typing import AsyncIterator

import httpx

from cardputer_proxy.adapters.base import ChatChunk
from cardputer_proxy.schemas import ChatCompletionRequest, Profile


ANTHROPIC_VERSION = "2023-06-01"


def _split_system(req: ChatCompletionRequest) -> tuple[str | None, list[dict]]:
    """Pull system messages out into Anthropic's `system` field."""
    system_parts: list[str] = []
    messages: list[dict] = []
    for m in req.messages:
        if m.role == "system":
            system_parts.append(m.content)
        else:
            messages.append({"role": m.role, "content": m.content})
    system = "\n\n".join(system_parts) if system_parts else None
    return system, messages


class AnthropicAdapter:
    async def stream_chat(
        self,
        profile: Profile,
        request: ChatCompletionRequest,
        secret: str | None,
    ) -> AsyncIterator[dict]:
        system, messages = _split_system(request)
        body: dict = {
            "model": profile.model,
            "messages": messages,
            "stream": True,
            "max_tokens": request.max_tokens or profile.max_tokens,
        }
        if system is not None:
            body["system"] = system
        if request.temperature is not None:
            body["temperature"] = request.temperature
        elif profile.temperature is not None:
            body["temperature"] = profile.temperature

        headers = {
            "x-api-key": secret,
            "anthropic-version": ANTHROPIC_VERSION,
            "content-type": "application/json",
            "accept": "text/event-stream",
        }

        url = profile.endpoint.rstrip("/") + "/v1/messages"
        emitted_role = False
        async with httpx.AsyncClient(timeout=httpx.Timeout(60.0, read=300.0)) as cx:
            async with cx.stream("POST", url, headers=headers, json=body) as resp:
                resp.raise_for_status()
                async for line in resp.aiter_lines():
                    if not line.startswith("data: "):
                        continue
                    payload = line[len("data: "):]
                    if payload == "[DONE]":
                        break
                    try:
                        evt = json.loads(payload)
                    except json.JSONDecodeError:
                        continue

                    ev_type = evt.get("type")
                    if ev_type == "message_start" and not emitted_role:
                        emitted_role = True
                        yield ChatChunk.role(profile.model)
                    elif ev_type == "content_block_delta":
                        delta = evt.get("delta") or {}
                        if delta.get("type") == "text_delta":
                            text = delta.get("text") or ""
                            if text:
                                yield ChatChunk.text(profile.model, text)
                    elif ev_type == "message_stop":
                        yield ChatChunk.done(profile.model, reason="stop")
                        break
                    # All other event types (content_block_start/stop,
                    # message_delta with usage info, tool blocks) are
                    # intentionally dropped for M2.


def _register() -> None:
    # Lazy import: avoid circular dep with adapters/__init__.py.
    from cardputer_proxy.adapters import register
    register("anthropic", AnthropicAdapter)


_register()
