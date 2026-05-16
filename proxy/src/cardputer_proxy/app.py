"""FastAPI app factory + routes."""

from __future__ import annotations

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
            # M4 introduces other adapters; until then, anything else is 501.
            raise HTTPException(
                status_code=status.HTTP_501_NOT_IMPLEMENTED,
                detail=f"adapter not implemented: {profile.provider}",
            )

        adapter = AnthropicAdapter()
        chunk_iter = adapter.stream_chat(profile, req, secret=settings.anthropic_api_key)
        return StreamingResponse(
            chunks_to_sse(chunk_iter),
            media_type="text/event-stream",
        )

    return app
