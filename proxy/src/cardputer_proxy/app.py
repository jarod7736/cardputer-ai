"""FastAPI app factory + routes.

This file grows across the M2 tasks. Task 2 ships the absolute minimum
(create_app + /healthz) so the test fixture's import works. Task 3 adds
bearer auth; Task 4 adds the chat-completions route.
"""

from __future__ import annotations

from fastapi import FastAPI

from cardputer_proxy.config import load_settings


def create_app() -> FastAPI:
    settings = load_settings()  # loaded once at startup; held in closure
    app = FastAPI(title="cardputer-proxy", version="0.1.0")

    @app.get("/healthz")
    def healthz() -> dict[str, str]:
        return {"status": "ok"}

    # Stash for later route handlers to reach via app.state.
    app.state.settings = settings
    return app
