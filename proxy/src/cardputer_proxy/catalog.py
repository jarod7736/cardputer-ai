"""Persistent profile catalog with atomic writes.

JSON-on-disk. Tiny enough (< 1 KB typical) that we always load and save
the whole file. Atomic via tempfile + os.replace in the same directory.
"""

from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path

from cardputer_proxy.schemas import Profile


SCHEMA_VERSION = 1


class CatalogError(RuntimeError):
    pass


class Catalog:
    def __init__(self, path: Path):
        self._path = path
        self._profiles: dict[str, Profile] = {}
        self._load()

    def _load(self) -> None:
        if not self._path.exists():
            return
        try:
            raw = json.loads(self._path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as e:
            raise CatalogError(f"failed to read {self._path}: {e}") from e
        if raw.get("version") != SCHEMA_VERSION:
            raise CatalogError(
                f"unsupported catalog schema version {raw.get('version')!r}"
            )
        for entry in raw.get("profiles", []):
            p = Profile.model_validate(entry)
            self._profiles[p.id] = p

    def _save(self) -> None:
        out = {
            "version": SCHEMA_VERSION,
            "profiles": [p.model_dump() for p in self._profiles.values()],
        }
        body = json.dumps(out, indent=2, ensure_ascii=False) + "\n"
        directory = self._path.parent
        directory.mkdir(parents=True, exist_ok=True)
        tmp_name = None
        try:
            with tempfile.NamedTemporaryFile(
                "w",
                dir=directory,
                prefix=".profiles.",
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
            raise CatalogError(f"failed to write {self._path}: {e}") from e

    # Public surface
    def list(self) -> dict[str, Profile]:
        return dict(self._profiles)

    def get(self, profile_id: str) -> Profile | None:
        return self._profiles.get(profile_id)

    def upsert(self, profile: Profile) -> None:
        previous = self._profiles.get(profile.id)
        self._profiles[profile.id] = profile
        try:
            self._save()
        except CatalogError:
            # Roll back in-memory so a failed write doesn't leak partial state.
            if previous is None:
                self._profiles.pop(profile.id, None)
            else:
                self._profiles[profile.id] = previous
            raise

    def delete(self, profile_id: str) -> bool:
        if profile_id not in self._profiles:
            return False
        removed = self._profiles.pop(profile_id)
        try:
            self._save()
        except CatalogError:
            self._profiles[profile_id] = removed
            raise
        return True
