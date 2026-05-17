"""FastAPI app factory + routes."""

from __future__ import annotations

import time
from typing import AsyncIterator

import httpx
from fastapi import Depends, FastAPI, HTTPException, status
from fastapi.responses import StreamingResponse

from cardputer_proxy import adapters as adapter_registry
from cardputer_proxy import auth
from cardputer_proxy.config import load_settings
from cardputer_proxy.schemas import (
    ChatCompletionRequest,
    Message,
    Profile,
    ProfileList,
    ProfileUpdate,
    TestResult,
)
from cardputer_proxy.secrets_store import SecretsStore
from cardputer_proxy.sse import chunks_to_sse


def create_app() -> FastAPI:
    settings = load_settings()
    secrets_store = SecretsStore(settings.secret_dir)
    app = FastAPI(title="cardputer-proxy", version="0.2.0")
    app.state.settings = settings

    @app.get("/healthz")
    def healthz() -> dict[str, str]:
        return {"status": "ok"}

    # --- profile catalog (M4) -----------------------------------------------

    @app.get("/v1/profiles", dependencies=[Depends(auth.require_bearer)])
    def list_profiles() -> ProfileList:
        return ProfileList(profiles=list(settings.catalog.list().values()))

    @app.get(
        "/v1/profiles/{profile_id}",
        dependencies=[Depends(auth.require_bearer)],
    )
    def get_profile(profile_id: str) -> Profile:
        p = settings.catalog.get(profile_id)
        if p is None:
            raise HTTPException(404, detail=f"unknown profile: {profile_id}")
        return p

    @app.post(
        "/v1/profiles",
        status_code=201,
        dependencies=[Depends(auth.require_bearer)],
    )
    def create_profile(profile: Profile) -> Profile:
        if settings.catalog.get(profile.id) is not None:
            raise HTTPException(409, detail=f"profile exists: {profile.id}")
        settings.catalog.upsert(profile)
        return profile

    @app.patch(
        "/v1/profiles/{profile_id}",
        dependencies=[Depends(auth.require_bearer)],
    )
    def patch_profile(profile_id: str, patch: ProfileUpdate) -> Profile:
        existing = settings.catalog.get(profile_id)
        if existing is None:
            raise HTTPException(404, detail=f"unknown profile: {profile_id}")
        updates = {k: v for k, v in patch.model_dump(exclude_none=True).items()}
        updated = existing.model_copy(update=updates)
        settings.catalog.upsert(updated)
        return updated

    @app.delete(
        "/v1/profiles/{profile_id}",
        status_code=204,
        dependencies=[Depends(auth.require_bearer)],
    )
    def delete_profile(profile_id: str) -> None:
        if not settings.catalog.delete(profile_id):
            raise HTTPException(404, detail=f"unknown profile: {profile_id}")
        return None

    @app.post(
        "/v1/profiles/{profile_id}/test",
        dependencies=[Depends(auth.require_bearer)],
    )
    async def test_profile(profile_id: str) -> TestResult:
        profile = settings.catalog.get(profile_id)
        if profile is None:
            raise HTTPException(404, detail=f"unknown profile: {profile_id}")
        adapter = adapter_registry.get(profile.provider)
        if adapter is None:
            return TestResult(
                ok=False, latency_ms=0,
                error=f"no adapter for {profile.provider}",
            )

        # Mirror chat-completions secret resolution but tolerate failures
        # by reporting them in the TestResult instead of raising.
        try:
            if profile.auth.kind == "proxy-secret":
                secret = secrets_store.read(profile.auth.secret_ref or "")
            elif profile.auth.kind == "none":
                secret = None
            else:
                return TestResult(
                    ok=False, latency_ms=0,
                    error=f"auth kind {profile.auth.kind} not supported in test",
                )
        except (FileNotFoundError, ValueError) as e:
            return TestResult(ok=False, latency_ms=0, error=str(e))

        ping_req = ChatCompletionRequest(
            profile_id=profile_id,
            messages=[Message(role="user", content="ping")],
            max_tokens=8,
        )
        start = time.perf_counter()
        first_token_ms: int | None = None
        try:
            async for _ in adapter.stream_chat(profile, ping_req, secret=secret):
                if first_token_ms is None:
                    first_token_ms = int((time.perf_counter() - start) * 1000)
        except Exception as e:
            return TestResult(
                ok=False,
                latency_ms=int((time.perf_counter() - start) * 1000),
                error=str(e),
            )
        return TestResult(
            ok=True,
            latency_ms=int((time.perf_counter() - start) * 1000),
            first_token_ms=first_token_ms,
        )

    # --- chat completions ---------------------------------------------------

    @app.post(
        "/v1/chat/completions",
        dependencies=[Depends(auth.require_bearer)],
    )
    async def chat_completions(req: ChatCompletionRequest):
        profile = settings.catalog.get(req.profile_id)
        if profile is None:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"unknown profile: {req.profile_id}",
            )
        adapter = adapter_registry.get(profile.provider)
        if adapter is None:
            raise HTTPException(
                status_code=status.HTTP_501_NOT_IMPLEMENTED,
                detail=f"no adapter for provider: {profile.provider}",
            )

        # Resolve the upstream credential based on the profile's auth.kind.
        secret: str | None
        if profile.auth.kind == "proxy-secret":
            if not profile.auth.secret_ref:
                raise HTTPException(
                    500, detail=f"profile {profile.id}: missing secret_ref"
                )
            try:
                secret = secrets_store.read(profile.auth.secret_ref)
            except (FileNotFoundError, ValueError) as e:
                raise HTTPException(
                    500, detail=f"profile {profile.id}: {e}"
                ) from e
        elif profile.auth.kind == "device-key":
            # M5 will accept X-Device-Provided-Key from the request. For
            # now no device firmware sends it, so explicit 501.
            raise HTTPException(
                501,
                detail=f"profile {profile.id}: device-key auth not wired (M5)",
            )
        elif profile.auth.kind == "none":
            secret = None
        else:
            raise HTTPException(
                500, detail=f"profile {profile.id}: unknown auth kind"
            )

        chunk_iter = adapter.stream_chat(profile, req, secret=secret)

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
