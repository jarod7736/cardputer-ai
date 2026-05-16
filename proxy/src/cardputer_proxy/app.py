"""FastAPI app factory + routes."""

from __future__ import annotations

from fastapi import Depends, FastAPI, HTTPException, status

from cardputer_proxy import auth
from cardputer_proxy.config import load_settings
from cardputer_proxy.schemas import ChatCompletionRequest


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
        # Adapter dispatch + SSE streaming land in Task 4.
        raise HTTPException(
            status_code=status.HTTP_501_NOT_IMPLEMENTED,
            detail="anthropic adapter wired up in Task 4",
        )

    return app
