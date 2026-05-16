"""Adapter protocol — what every provider adapter must implement."""

from __future__ import annotations

from typing import AsyncIterator, Protocol

from cardputer_proxy.schemas import ChatCompletionRequest, Profile


class ChatChunk:
    """Helpers that produce OAI-shaped chunk dicts."""

    @staticmethod
    def role(model: str) -> dict:
        return {
            "object": "chat.completion.chunk",
            "model": model,
            "choices": [{"index": 0, "delta": {"role": "assistant"}, "finish_reason": None}],
        }

    @staticmethod
    def text(model: str, text: str) -> dict:
        return {
            "object": "chat.completion.chunk",
            "model": model,
            "choices": [{"index": 0, "delta": {"content": text}, "finish_reason": None}],
        }

    @staticmethod
    def done(model: str, reason: str = "stop") -> dict:
        return {
            "object": "chat.completion.chunk",
            "model": model,
            "choices": [{"index": 0, "delta": {}, "finish_reason": reason}],
        }


class Adapter(Protocol):
    async def stream_chat(
        self,
        profile: Profile,
        request: ChatCompletionRequest,
        secret: str,
    ) -> AsyncIterator[dict]:
        """Yield OAI-shaped chunk dicts. The caller wraps them in SSE."""
        ...
