"""Bearer-token authentication dependency.

M5: per-device tokens via TokenStore. The legacy M4 single bearer is
accepted as a fallback iff it still exists on disk (smooth upgrade).
"""

from __future__ import annotations

import hmac

from fastapi import Depends, Header, HTTPException, Request, status

from cardputer_proxy.config import Settings


def settings_from_request(request: Request) -> Settings:
    return request.app.state.settings  # type: ignore[no-any-return]


def require_bearer(
    authorization: str | None = Header(default=None),
    settings: Settings = Depends(settings_from_request),
) -> None:
    if not authorization or not authorization.lower().startswith("bearer "):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="missing bearer")
    presented = authorization.split(" ", 1)[1].strip()

    token = settings.token_store.get_by_token(presented)
    if token is not None:
        if token.revoked:
            raise HTTPException(
                status_code=status.HTTP_401_UNAUTHORIZED, detail="token revoked"
            )
        return

    if settings.legacy_bearer_token is not None and hmac.compare_digest(
        presented.encode("utf-8"),
        settings.legacy_bearer_token.encode("utf-8"),
    ):
        return

    raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid bearer")
