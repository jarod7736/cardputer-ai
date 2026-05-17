"""Entry point.

Default: run the server. With `secrets ...` args, dispatch to the
secrets CLI (same console_script handles both subcommands).
"""

from __future__ import annotations

import os
import sys

import uvicorn


def main() -> None:
    argv = sys.argv[1:]
    if argv and argv[0] in ("secrets", "peer"):
        from cardputer_proxy import cli
        # cli.main expects the leading subcommand ("secrets" or "peer").
        sys.exit(cli.main(argv))

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
