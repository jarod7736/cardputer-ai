"""Read-only access to per-secret files under /etc/cardputer-proxy/secrets/."""

from __future__ import annotations

import re
from pathlib import Path


_VALID = re.compile(r"^[a-zA-Z0-9_.-]+$")


class SecretsStore:
    def __init__(self, root: Path):
        self._root = root

    def _validate(self, name: str) -> Path:
        if not name or not _VALID.match(name):
            raise ValueError(f"invalid secret name: {name!r}")
        # Prevent traversal: resolved path must stay inside _root
        root_resolved = self._root.resolve()
        path = (self._root / name).resolve()
        if not str(path).startswith(str(root_resolved) + "/") and path != root_resolved:
            raise ValueError(f"secret path escape: {name!r}")
        return path

    def read(self, name: str) -> str:
        p = self._validate(name)
        if not p.is_file():
            raise FileNotFoundError(f"secret missing: {name}")
        val = p.read_text(encoding="utf-8").strip()
        if not val:
            raise ValueError(f"empty secret: {name}")
        return val
