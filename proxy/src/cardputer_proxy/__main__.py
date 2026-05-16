"""Entry point for `python -m cardputer_proxy` and the console script."""

from __future__ import annotations

import os

import uvicorn


def main() -> None:
    host = os.environ.get("CARDPUTER_PROXY_LISTEN_HOST", "127.0.0.1")
    port = int(os.environ.get("CARDPUTER_PROXY_LISTEN_PORT", "8420"))
    uvicorn.run(
        "cardputer_proxy.app:create_app",
        host=host,
        port=port,
        factory=True,
        log_level="info",
        access_log=False,
    )


if __name__ == "__main__":
    main()
