"""Bearer-token authentication dependency.

M2 has a single token; M5 will move to per-device tokens with scopes.
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
    """Reject if Authorization is missing or doesn't match the configured token.

    Uses constant-time comparison so we don't leak token length via timing.
    """
    if not authorization or not authorization.lower().startswith("bearer "):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="missing bearer")
    presented = authorization.split(" ", 1)[1].strip()
    expected = settings.device_bearer_token
    if not hmac.compare_digest(presented.encode("utf-8"), expected.encode("utf-8")):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid bearer")
