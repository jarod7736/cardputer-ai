"""FastAPI app factory + routes."""

from __future__ import annotations

from typing import AsyncIterator

import httpx
from fastapi import Depends, FastAPI, HTTPException, status
from fastapi.responses import StreamingResponse

from cardputer_proxy import auth
from cardputer_proxy.adapters.anthropic import AnthropicAdapter
from cardputer_proxy.config import load_settings
from cardputer_proxy.schemas import ChatCompletionRequest
from cardputer_proxy.sse import chunks_to_sse


def create_app() -> FastAPI:
    settings = load_settings()
    app = FastAPI(title="cardputer-proxy", version="0.1.0")
    app.state.settings = settings

    @app.get("/healthz")
    def healthz() -> dict[str, str]:
        return {"status": "ok"}

    @app.post(
        "/v1/chat/completions",
        dependencies=[Depends(auth.require_bearer)],
    )
    async def chat_completions(req: ChatCompletionRequest):
        profile = settings.get_profile(req.profile_id)
        if profile is None:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"unknown profile: {req.profile_id}",
            )
        if profile.provider != "anthropic":
            raise HTTPException(
                status_code=status.HTTP_501_NOT_IMPLEMENTED,
                detail=f"adapter not implemented: {profile.provider}",
            )

        adapter = AnthropicAdapter()
        chunk_iter = adapter.stream_chat(profile, req, secret=settings.anthropic_api_key)

        # Pre-flight: draw the first chunk before we start sending SSE bytes.
        # This way an upstream 4xx/5xx surfaces as a clean structured 502
        # instead of as a half-written event-stream.
        try:
            first = await chunk_iter.__anext__()
        except httpx.HTTPStatusError as e:
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail={"upstream": "anthropic", "upstream_status": e.response.status_code},
            ) from e
        except StopAsyncIteration:
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail={"upstream": "anthropic", "upstream_status": None},
            )

        async def _wrapped() -> AsyncIterator[dict]:
            yield first
            async for ch in chunk_iter:
                yield ch

        return StreamingResponse(
            chunks_to_sse(_wrapped()),
            media_type="text/event-stream",
        )

    return app
