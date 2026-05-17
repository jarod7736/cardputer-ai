"""Per-device bearer tokens, persisted as JSON.

Mirrors catalog.py: atomic save via tempfile + os.replace, schema
version pin, full-file rewrite on every mutation. Tokens are small
enough (< a few dozen per device) that linear scans on the auth hot
path are fine.
"""

from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path

from cardputer_proxy.schemas import Token


SCHEMA_VERSION = 1


class TokenStoreError(RuntimeError):
    pass


class TokenStore:
    def __init__(self, path: Path):
        self._path = path
        self._tokens: dict[str, Token] = {}  # keyed by device_id
        self._load()

    def _load(self) -> None:
        if not self._path.exists():
            return
        try:
            raw = json.loads(self._path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as e:
            raise TokenStoreError(f"failed to read {self._path}: {e}") from e
        if raw.get("version") != SCHEMA_VERSION:
            raise TokenStoreError(
                f"unsupported tokens schema version {raw.get('version')!r}"
            )
        for entry in raw.get("tokens", []):
            t = Token.model_validate(entry)
            self._tokens[t.device_id] = t

    def _save(self) -> None:
        out = {
            "version": SCHEMA_VERSION,
            "tokens": [t.model_dump() for t in self._tokens.values()],
        }
        body = json.dumps(out, indent=2, ensure_ascii=False) + "\n"
        directory = self._path.parent
        directory.mkdir(parents=True, exist_ok=True)
        tmp_name = None
        try:
            with tempfile.NamedTemporaryFile(
                "w",
                dir=directory,
                prefix=".tokens.",
                suffix=".tmp",
                delete=False,
                encoding="utf-8",
            ) as tf:
                tf.write(body)
                tmp_name = tf.name
            os.replace(tmp_name, self._path)
        except OSError as e:
            if tmp_name is not None:
                try:
                    os.unlink(tmp_name)
                except OSError:
                    pass
            raise TokenStoreError(f"failed to write {self._path}: {e}") from e

    def list(self) -> list[Token]:
        return list(self._tokens.values())

    def get(self, device_id: str) -> Token | None:
        return self._tokens.get(device_id)

    def get_by_token(self, token_value: str) -> Token | None:
        for t in self._tokens.values():
            if t.token == token_value:
                return t
        return None

    def add(self, token: Token) -> None:
        if token.device_id in self._tokens:
            raise TokenStoreError(f"device_id already exists: {token.device_id}")
        self._tokens[token.device_id] = token
        try:
            self._save()
        except TokenStoreError:
            self._tokens.pop(token.device_id, None)
            raise

    def revoke(self, device_id: str) -> bool:
        existing = self._tokens.get(device_id)
        if existing is None or existing.revoked:
            return False
        updated = existing.model_copy(update={"revoked": True})
        self._tokens[device_id] = updated
        try:
            self._save()
        except TokenStoreError:
            self._tokens[device_id] = existing
            raise
        return True
