"""Helpers to format OAI-shaped chunks as Server-Sent Events."""

from __future__ import annotations

import json
from typing import AsyncIterator


def to_sse_event(chunk: dict) -> str:
    return f"data: {json.dumps(chunk, separators=(',', ':'))}\n\n"


def done_event() -> str:
    return "data: [DONE]\n\n"


async def chunks_to_sse(chunks: AsyncIterator[dict]) -> AsyncIterator[bytes]:
    """Wrap a chunk iterator into an SSE byte stream, terminated by [DONE]."""
    async for ch in chunks:
        yield to_sse_event(ch).encode("utf-8")
    yield done_event().encode("utf-8")
