# M4 — Profile System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Cardputer-AI provider-agnostic. The proxy gains persistent profile CRUD, additional upstream adapters (OpenAI, Ollama, generic OAI-compatible), and a tiny secrets-management CLI. The device gains a profile picker that lists what the proxy serves, switches the active profile in NVS, and shows the active profile in the status bar. After M4, swapping models (or providers) is a few keystrokes on the device — no reflash.

**Architecture:** Profiles are **canonical on the proxy**. Lives on disk as a JSON file the proxy hot-loads at startup (and rewrites atomically on CRUD). Per-profile secrets are stored in the existing `/etc/cardputer-proxy/secrets/` tree under `secret_ref` names; the proxy looks them up at request time. The device pulls the catalog on demand, caches the IDs and labels in NVS, and persists *which* profile is currently active. Each chat request includes the active `profile_id` (already shipped in M3); the proxy resolves model + endpoint + secret server-side.

**Tech Stack:**
- Proxy: same FastAPI + httpx stack as M2/M3, no new dependencies. Stdlib `json` + atomic-write helper for storage. `Typer` would be nice for the secrets CLI but adds a dep — use stdlib `argparse` instead. `pydantic` validates incoming profile shapes.
- Device: Arduino's `Preferences` library (already part of the espressif32 core, ships NVS access) for storing the active profile id. No new lib_deps.

**Out of scope for M4** (explicitly deferred):
- **Device-side profile editor (add/edit/delete)** — users manage via the proxy's CRUD endpoints or the secrets CLI on lobsterboy. The device only *selects* profiles in M4. Adding the editor is a small follow-up that can land as M4.5 or fold into M5 provisioning.
- **Test-from-device** (Fn+T) — easy to add after the picker works; ship if there's time, otherwise leave for M4.5.
- **Per-device bearer tokens with scopes** — DESIGN.md §4.4; M5.
- **Multi-line input, scroll history (PgUp/PgDn), Esc to cancel** — UX polish that doesn't gate M4.
- **`device-key` auth-kind wire path** — the proxy *accepts* it (M4 Task 3) but the device firmware doesn't yet provide one. Wire-up happens in M5 along with NVS-stored device-side keys.

**Carry-overs from earlier milestones:**
- M2 proxy already has bearer auth, the `Profile` pydantic model, and one hard-coded `claude-opus` profile via `DEFAULT_PROFILE`. We replace that default with a JSON-backed catalog (`config.py`).
- M3 device already sends `profile_id` in every request and uses `proxy_secrets::kProfileId`. We replace the hard-coded `kProfileId` with an NVS-resolved active profile id.
- M3 device has a status bar with detail text — we'll repurpose part of it for the active profile name.

**Verification style:** Proxy work is unit-testable end-to-end with FastAPI's `TestClient` (M2 pattern). Device work needs hardware verification (M3 pattern). Each task lists its verification style.

---

## Decisions to confirm before starting

Sensible defaults below; flag if any are wrong.

1. **Profile catalog file**: `/etc/cardputer-proxy/profiles.json` (mode 0640, root:cardputer-proxy). Hot-read on every `/v1/profiles` GET so a CLI edit shows up without restart; cached in-process for individual chat-completion requests (no per-request disk read).
2. **Atomic writes**: `os.replace()` over a tempfile in the same directory.
3. **Schema migration**: file embeds a `"version": 1` field. Older versions get refused with a clear error. M5 may bump it.
4. **Adapter registry**: a small mapping `{provider_name: AdapterClass}` in `adapters/__init__.py`. New adapters register at import time.
5. **OpenAI endpoint default**: `https://api.openai.com`. Model passed through; we don't try to translate model names.
6. **Ollama**: prefer the OAI-compatible path (`/v1/chat/completions`) over Ollama-native (`/api/chat`). One less translator.
7. **Generic OAI-compatible**: same as OpenAI but no implicit endpoint — must be set per-profile. Useful for vLLM, LM Studio, llama.cpp server, etc.
8. **Secrets CLI**: invoked as `sudo /opt/cardputer-proxy/.venv/bin/cardputer-proxy secrets …`. (`cardputer-proxy` is the existing console_script entry; we add `secrets` as a subcommand.) Reads/writes only files in `/etc/cardputer-proxy/secrets/`.
9. **Active profile on device**: stored in NVS under namespace `cprox`, key `profile_id`. Defaults to `claude-opus` if NVS is empty.
10. **Catalog fetch cadence**: device pulls `/v1/profiles` on boot and whenever the user opens the picker (Fn+P). No polling.

---

## File structure

```
proxy/
├── pyproject.toml                              (modified — bump version, no new deps)
├── src/cardputer_proxy/
│   ├── config.py                               (modified — JSON catalog instead of DEFAULT_PROFILE)
│   ├── schemas.py                              (modified — add ProfileList, ProfileUpdate, TestResult)
│   ├── auth.py                                 (unchanged)
│   ├── app.py                                  (modified — profile CRUD routes + adapter dispatch)
│   ├── catalog.py                              (NEW — persistent profile store w/ atomic writes)
│   ├── secrets_store.py                        (NEW — read/write/list secrets files)
│   ├── cli.py                                  (NEW — `cardputer-proxy secrets ...` argparse)
│   ├── sse.py                                  (unchanged)
│   └── adapters/
│       ├── __init__.py                         (modified — registry)
│       ├── base.py                             (unchanged + minor — Adapter Protocol stays)
│       ├── anthropic.py                        (unchanged)
│       ├── openai.py                           (NEW — pass-through OAI with header swap)
│       ├── ollama.py                           (NEW — OAI-compatible path on Ollama server)
│       └── generic_oai.py                      (NEW — OAI-compatible generic for vLLM/LM Studio/etc.)
├── tests/
│   ├── conftest.py                             (modified — write a profiles.json into tmp_path)
│   ├── test_catalog.py                         (NEW)
│   ├── test_profile_crud.py                    (NEW)
│   ├── test_secrets_resolution.py              (NEW)
│   ├── test_openai_adapter.py                  (NEW)
│   ├── test_ollama_adapter.py                  (NEW)
│   ├── test_generic_oai_adapter.py             (NEW)
│   ├── test_profile_test_endpoint.py           (NEW)
│   ├── test_secrets_cli.py                     (NEW)
│   ├── test_schemas.py                         (unchanged)
│   ├── test_auth.py                            (unchanged)
│   ├── test_chat_completions_passthrough.py    (modified — profile id is now from JSON)
│   └── test_anthropic_adapter.py               (unchanged)

ops/cardputer-proxy/
├── install.sh                                  (modified — create profiles.json if absent, fix perms)
└── profiles.json.example                       (NEW — starter catalog with 4 profile entries)

firmware/
├── src/
│   ├── main.cpp                                (modified — picker handling + active-profile threading)
│   ├── proxy_api.h / proxy_api.cpp             (NEW — fetch /v1/profiles into Profile structs)
│   ├── profile_store.h / profile_store.cpp     (NEW — NVS persistence for active profile id)
│   ├── picker_view.h / picker_view.cpp         (NEW — list-selection screen)
│   ├── chat_client.h / chat_client.cpp         (modified — take profile_id at send time)
│   └── chat_view.cpp                           (modified — show active profile in status bar)

docs/
└── m4-profile-system-report.md                 (NEW — what worked, what surprised)
```

---

## PROXY-SIDE TASKS (run locally, deploy at end)

### Task 0: Bump pyproject version + plan migrations

**Files:**
- Modify: `proxy/pyproject.toml`

- [ ] **Step 1: Bump `proxy/pyproject.toml` version**

```toml
version = "0.2.0"
```

- [ ] **Step 2: Commit**

```bash
cd /home/jarod7736/workspace/cardputer-ai
git add proxy/pyproject.toml
git commit -m "chore(m4): bump proxy version to 0.2.0 — profile system"
```

---

### Task 1: Profile catalog persistence (test first)

**Files:**
- Create: `proxy/tests/test_catalog.py`
- Create: `proxy/src/cardputer_proxy/catalog.py`

- [ ] **Step 1: Create `proxy/tests/test_catalog.py`**

```python
from __future__ import annotations

import json
import pytest

from cardputer_proxy.catalog import Catalog, CatalogError
from cardputer_proxy.schemas import Profile


SAMPLE = Profile(
    id="claude-opus",
    label="Claude Opus 4.7",
    provider="anthropic",
    endpoint="https://api.anthropic.com",
    model="claude-opus-4-7",
    max_tokens=4096,
    temperature=1.0,
)


def test_load_empty_returns_empty(tmp_path):
    f = tmp_path / "profiles.json"
    c = Catalog(f)
    assert c.list() == {}


def test_round_trip(tmp_path):
    f = tmp_path / "profiles.json"
    c = Catalog(f)
    c.upsert(SAMPLE)
    assert c.list() == {SAMPLE.id: SAMPLE}

    c2 = Catalog(f)  # fresh load from disk
    assert c2.get(SAMPLE.id) == SAMPLE


def test_delete(tmp_path):
    f = tmp_path / "profiles.json"
    c = Catalog(f)
    c.upsert(SAMPLE)
    assert c.delete(SAMPLE.id) is True
    assert c.delete(SAMPLE.id) is False
    assert c.list() == {}


def test_atomic_write_no_partial_on_failure(tmp_path, monkeypatch):
    f = tmp_path / "profiles.json"
    c = Catalog(f)
    c.upsert(SAMPLE)
    raw = f.read_text(encoding="utf-8")

    # Simulate a partial write by forcing os.replace to raise after the
    # tempfile is written.
    import os
    def fail_replace(*a, **kw):
        raise OSError("disk full")
    monkeypatch.setattr(os, "replace", fail_replace)
    with pytest.raises(CatalogError):
        c.upsert(SAMPLE._replace(label="changed") if hasattr(SAMPLE, "_replace")
                 else Profile(**{**SAMPLE.model_dump(), "label": "changed"}))
    # Original file untouched
    assert f.read_text(encoding="utf-8") == raw


def test_version_mismatch_rejected(tmp_path):
    f = tmp_path / "profiles.json"
    f.write_text(json.dumps({"version": 99, "profiles": []}), encoding="utf-8")
    with pytest.raises(CatalogError):
        Catalog(f)
```

- [ ] **Step 2: Run, expect import errors**

```bash
cd proxy && .venv/bin/pytest tests/test_catalog.py -q
```

- [ ] **Step 3: Implement `proxy/src/cardputer_proxy/catalog.py`**

```python
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
            try:
                if "tmp_name" in dir():
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
        self._profiles[profile.id] = profile
        self._save()

    def delete(self, profile_id: str) -> bool:
        if profile_id not in self._profiles:
            return False
        del self._profiles[profile_id]
        self._save()
        return True
```

- [ ] **Step 4: Run tests, expect pass**

```bash
.venv/bin/pytest tests/test_catalog.py -q
```

- [ ] **Step 5: Commit**

```bash
git add proxy/tests/test_catalog.py proxy/src/cardputer_proxy/catalog.py
git commit -m "feat(m4): persistent profile catalog with atomic writes (test first)"
```

---

### Task 2: Profile CRUD endpoints (test first)

**Files:**
- Modify: `proxy/src/cardputer_proxy/schemas.py` (add request/response models)
- Modify: `proxy/src/cardputer_proxy/config.py` (Settings owns a Catalog)
- Modify: `proxy/src/cardputer_proxy/app.py` (mount routes)
- Create: `proxy/tests/test_profile_crud.py`
- Modify: `proxy/tests/conftest.py` (seed a profiles.json fixture)

- [ ] **Step 1: Extend schemas with request/response shapes**

In `schemas.py`, append:

```python
class ProfileList(BaseModel):
    profiles: list[Profile]


class ProfileUpdate(BaseModel):
    label: str | None = None
    endpoint: str | None = None
    model: str | None = None
    max_tokens: int | None = Field(default=None, ge=1, le=200_000)
    temperature: float | None = Field(default=None, ge=0.0, le=2.0)


class TestResult(BaseModel):
    ok: bool
    latency_ms: int
    first_token_ms: int | None = None
    error: str | None = None
```

(`provider` is not in `ProfileUpdate` — switching providers re-creates the profile; we don't migrate state on a provider change.)

- [ ] **Step 2: Modify `config.py` so `Settings` owns a `Catalog`**

```python
from cardputer_proxy.catalog import Catalog
# ...
@dataclass(frozen=True)
class Settings:
    anthropic_api_key: str
    device_bearer_token: str
    catalog: Catalog
    secret_dir: Path

    def get_profile(self, profile_id: str):
        return self.catalog.get(profile_id)
```

In `load_settings()`:

```python
catalog_path = Path(
    os.environ.get("CARDPUTER_PROXY_CATALOG_PATH",
                   "/etc/cardputer-proxy/profiles.json")
)
catalog = Catalog(catalog_path)
# If empty (first run), seed with the M2 default so existing M3 firmware keeps working.
if not catalog.list():
    catalog.upsert(DEFAULT_PROFILE)

return Settings(
    anthropic_api_key=anthropic_key,
    device_bearer_token=bearer,
    catalog=catalog,
    secret_dir=secret_dir,
)
```

Drop the old `profiles` dict and the TOML-loading branch — JSON catalog supersedes it.

- [ ] **Step 3: Update `conftest.py` to write a profiles.json under `tmp_path`**

In the existing `client` fixture, after creating `secret_dir`:

```python
catalog_path = tmp_path / "profiles.json"
catalog_path.write_text(json.dumps({
    "version": 1,
    "profiles": [{
        "id": "claude-opus",
        "label": "Claude Opus 4.7",
        "provider": "anthropic",
        "endpoint": "https://api.anthropic.com",
        "model": "claude-opus-4-7",
        "max_tokens": 4096,
        "temperature": 1.0,
    }],
}), encoding="utf-8")
monkeypatch.setenv("CARDPUTER_PROXY_CATALOG_PATH", str(catalog_path))
```

- [ ] **Step 4: Create `proxy/tests/test_profile_crud.py`**

```python
from __future__ import annotations


def auth():
    return {"Authorization": "Bearer test-bearer"}


def test_list_profiles_returns_seeded(client):
    r = client.get("/v1/profiles", headers=auth())
    assert r.status_code == 200
    body = r.json()
    assert any(p["id"] == "claude-opus" for p in body["profiles"])


def test_get_profile_404_for_unknown(client):
    r = client.get("/v1/profiles/no-such", headers=auth())
    assert r.status_code == 404


def test_create_profile(client):
    body = {
        "id": "openai-mini",
        "label": "OpenAI gpt-mini",
        "provider": "openai",
        "endpoint": "https://api.openai.com",
        "model": "gpt-4o-mini",
        "max_tokens": 1024,
        "temperature": 0.7,
    }
    r = client.post("/v1/profiles", json=body, headers=auth())
    assert r.status_code == 201
    assert r.json()["id"] == "openai-mini"

    # Confirm it's listed
    r2 = client.get("/v1/profiles", headers=auth())
    assert any(p["id"] == "openai-mini" for p in r2.json()["profiles"])


def test_create_profile_rejects_duplicate(client):
    body = {
        "id": "claude-opus",
        "label": "dup",
        "provider": "anthropic",
        "endpoint": "https://api.anthropic.com",
        "model": "claude-opus-4-7",
    }
    r = client.post("/v1/profiles", json=body, headers=auth())
    assert r.status_code == 409


def test_patch_profile(client):
    r = client.patch("/v1/profiles/claude-opus",
                     json={"label": "Renamed"},
                     headers=auth())
    assert r.status_code == 200
    assert r.json()["label"] == "Renamed"


def test_delete_profile(client):
    r = client.delete("/v1/profiles/claude-opus", headers=auth())
    assert r.status_code == 204
    r2 = client.get("/v1/profiles/claude-opus", headers=auth())
    assert r2.status_code == 404


def test_profile_routes_require_auth(client):
    assert client.get("/v1/profiles").status_code == 401
    assert client.post("/v1/profiles", json={}).status_code == 401
    assert client.patch("/v1/profiles/x", json={}).status_code == 401
    assert client.delete("/v1/profiles/x").status_code == 401
```

- [ ] **Step 5: Implement the routes in `app.py`**

```python
from cardputer_proxy.schemas import (
    ChatCompletionRequest, Profile, ProfileList, ProfileUpdate,
)

# ... inside create_app, after the chat_completions route ...

    @app.get("/v1/profiles", dependencies=[Depends(auth.require_bearer)])
    def list_profiles() -> ProfileList:
        return ProfileList(profiles=list(settings.catalog.list().values()))

    @app.get("/v1/profiles/{profile_id}",
             dependencies=[Depends(auth.require_bearer)])
    def get_profile(profile_id: str) -> Profile:
        p = settings.catalog.get(profile_id)
        if p is None:
            raise HTTPException(status_code=404, detail=f"unknown profile: {profile_id}")
        return p

    @app.post("/v1/profiles",
              status_code=201,
              dependencies=[Depends(auth.require_bearer)])
    def create_profile(profile: Profile) -> Profile:
        if settings.catalog.get(profile.id) is not None:
            raise HTTPException(status_code=409, detail=f"profile exists: {profile.id}")
        settings.catalog.upsert(profile)
        return profile

    @app.patch("/v1/profiles/{profile_id}",
               dependencies=[Depends(auth.require_bearer)])
    def patch_profile(profile_id: str, patch: ProfileUpdate) -> Profile:
        existing = settings.catalog.get(profile_id)
        if existing is None:
            raise HTTPException(status_code=404, detail=f"unknown profile: {profile_id}")
        updated = existing.model_copy(update={
            k: v for k, v in patch.model_dump(exclude_none=True).items()
        })
        settings.catalog.upsert(updated)
        return updated

    @app.delete("/v1/profiles/{profile_id}",
                status_code=204,
                dependencies=[Depends(auth.require_bearer)])
    def delete_profile(profile_id: str) -> None:
        if not settings.catalog.delete(profile_id):
            raise HTTPException(status_code=404, detail=f"unknown profile: {profile_id}")
```

- [ ] **Step 6: Run, expect pass**

```bash
.venv/bin/pytest tests/test_profile_crud.py -q
```

- [ ] **Step 7: Commit**

```bash
git add proxy/src/cardputer_proxy/schemas.py proxy/src/cardputer_proxy/config.py proxy/src/cardputer_proxy/app.py proxy/tests/conftest.py proxy/tests/test_profile_crud.py
git commit -m "feat(m4): /v1/profiles CRUD endpoints (test first)"
```

---

### Task 3: Per-profile secret resolution + auth.kind dispatch (test first)

**Files:**
- Modify: `proxy/src/cardputer_proxy/schemas.py` (add `Auth` block to `Profile`)
- Create: `proxy/src/cardputer_proxy/secrets_store.py`
- Modify: `proxy/src/cardputer_proxy/app.py` (resolve secret per profile)
- Create: `proxy/tests/test_secrets_resolution.py`

- [ ] **Step 1: Extend `Profile` with an optional auth block**

In `schemas.py`:

```python
from typing import Literal

class Auth(BaseModel):
    kind: Literal["proxy-secret", "device-key", "none"]
    secret_ref: str | None = None


class Profile(BaseModel):
    id: str
    label: str
    provider: Literal["anthropic", "openai", "ollama", "openai-compatible"]
    endpoint: str
    model: str
    max_tokens: int = 4096
    temperature: float = 1.0
    auth: Auth = Auth(kind="proxy-secret", secret_ref="anthropic_api_key")
```

The default keeps M3 behavior — `secret_ref="anthropic_api_key"` is the file M2 created on lobsterboy.

- [ ] **Step 2: Create `proxy/src/cardputer_proxy/secrets_store.py`**

```python
"""Read-only access to the per-secret files under /etc/cardputer-proxy/secrets/."""

from __future__ import annotations

import re
from pathlib import Path


_VALID = re.compile(r"^[a-zA-Z0-9_.-]+$")


class SecretsStore:
    def __init__(self, root: Path):
        self._root = root

    def _validate(self, name: str) -> Path:
        if not _VALID.match(name):
            raise ValueError(f"invalid secret name: {name!r}")
        # Prevent traversal: resolved path must stay inside _root
        path = (self._root / name).resolve()
        if not str(path).startswith(str(self._root.resolve())):
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
```

- [ ] **Step 3: Wire secret resolution into the chat handler**

In `app.py`, replace the `settings.anthropic_api_key` argument with a per-request resolution:

```python
from cardputer_proxy.secrets_store import SecretsStore
# at top of create_app:
secrets_store = SecretsStore(settings.secret_dir)

# inside chat_completions:
        auth_kind = profile.auth.kind
        secret: str | None = None
        if auth_kind == "proxy-secret":
            if not profile.auth.secret_ref:
                raise HTTPException(500, detail=f"profile {profile.id}: missing secret_ref")
            try:
                secret = secrets_store.read(profile.auth.secret_ref)
            except (FileNotFoundError, ValueError) as e:
                raise HTTPException(500, detail=f"profile {profile.id}: {e}") from e
        elif auth_kind == "device-key":
            # M5 will accept X-Device-Provided-Key from the request. For
            # now it's a 501 — no device firmware sends this header yet.
            raise HTTPException(
                501,
                detail=f"profile {profile.id}: device-key auth not wired (M5)",
            )
        elif auth_kind == "none":
            secret = None
        else:
            raise HTTPException(500, detail=f"profile {profile.id}: unknown auth kind")

        # Then later, replace settings.anthropic_api_key:
        chunk_iter = adapter.stream_chat(profile, req, secret=secret)
```

The `Adapter.stream_chat` signature already accepts `secret: str`. Make it `secret: str | None` in `adapters/base.py` to accommodate the `none` case.

- [ ] **Step 4: Create `proxy/tests/test_secrets_resolution.py`**

```python
from __future__ import annotations

from pytest_httpx import HTTPXMock


_STREAM = (
    b"event: message_start\n"
    b'data: {"type":"message_start","message":{"id":"m","model":"claude-opus-4-7"}}\n\n'
    b"event: content_block_delta\n"
    b'data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"x"}}\n\n'
    b"event: message_stop\n"
    b'data: {"type":"message_stop"}\n\n'
)


def test_chat_uses_secret_from_profile_ref(client, httpx_mock: HTTPXMock):
    # The seeded claude-opus profile has auth.secret_ref="anthropic_api_key"
    # which conftest wrote as "test-anthropic-key".
    httpx_mock.add_response(
        method="POST",
        url="https://api.anthropic.com/v1/messages",
        content=_STREAM,
        headers={"content-type": "text/event-stream"},
    )
    r = client.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer test-bearer"},
        json={"profile_id": "claude-opus",
              "messages": [{"role": "user", "content": "hi"}]},
    )
    assert r.status_code == 200
    sent = httpx_mock.get_request()
    assert sent.headers["x-api-key"] == "test-anthropic-key"


def test_chat_with_missing_secret_returns_500(client, tmp_path, monkeypatch):
    # Create a profile whose secret doesn't exist.
    r = client.post(
        "/v1/profiles",
        headers={"Authorization": "Bearer test-bearer"},
        json={
            "id": "missing-secret",
            "label": "broken",
            "provider": "anthropic",
            "endpoint": "https://api.anthropic.com",
            "model": "claude-opus-4-7",
            "auth": {"kind": "proxy-secret", "secret_ref": "does_not_exist"},
        },
    )
    assert r.status_code == 201
    r2 = client.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer test-bearer"},
        json={"profile_id": "missing-secret",
              "messages": [{"role": "user", "content": "hi"}]},
    )
    assert r2.status_code == 500


def test_device_key_auth_returns_501_for_now(client):
    client.post(
        "/v1/profiles",
        headers={"Authorization": "Bearer test-bearer"},
        json={
            "id": "byok",
            "label": "BYO",
            "provider": "anthropic",
            "endpoint": "https://api.anthropic.com",
            "model": "claude-opus-4-7",
            "auth": {"kind": "device-key"},
        },
    )
    r = client.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer test-bearer"},
        json={"profile_id": "byok",
              "messages": [{"role": "user", "content": "hi"}]},
    )
    assert r.status_code == 501
```

- [ ] **Step 5: Run, expect pass**

```bash
.venv/bin/pytest tests/test_secrets_resolution.py tests/test_profile_crud.py -q
```

- [ ] **Step 6: Commit**

```bash
git add proxy/src/cardputer_proxy/schemas.py proxy/src/cardputer_proxy/secrets_store.py proxy/src/cardputer_proxy/app.py proxy/src/cardputer_proxy/adapters/base.py proxy/tests/test_secrets_resolution.py
git commit -m "feat(m4): per-profile secret resolution via /etc/.../secrets/<ref>; device-key 501s"
```

---

### Task 4: Adapter registry — refactor existing AnthropicAdapter into the pattern

**Files:**
- Modify: `proxy/src/cardputer_proxy/adapters/__init__.py` (registry)
- Modify: `proxy/src/cardputer_proxy/adapters/anthropic.py` (no behavioral change, just register)
- Modify: `proxy/src/cardputer_proxy/app.py` (lookup via registry instead of hard-coded)

- [ ] **Step 1: Make `adapters/__init__.py` the registry**

```python
"""Adapter registry. Each adapter module appends itself to ADAPTERS at
import time. Look up by provider name with `get(name)`."""

from __future__ import annotations

from typing import Callable

from cardputer_proxy.adapters.base import Adapter


_ADAPTERS: dict[str, Callable[[], Adapter]] = {}


def register(provider: str, factory: Callable[[], Adapter]) -> None:
    _ADAPTERS[provider] = factory


def get(provider: str) -> Adapter | None:
    factory = _ADAPTERS.get(provider)
    return factory() if factory else None


# Import side effect: each adapter file calls register(...) on import.
from cardputer_proxy.adapters import anthropic  # noqa: F401
```

- [ ] **Step 2: Make `anthropic.py` register itself**

At the bottom of `anthropic.py`:

```python
from cardputer_proxy.adapters import register
register("anthropic", AnthropicAdapter)
```

- [ ] **Step 3: Replace hard-coded dispatch in `app.py`**

```python
from cardputer_proxy import adapters as adapter_registry

# inside chat_completions, after profile resolution:
        adapter = adapter_registry.get(profile.provider)
        if adapter is None:
            raise HTTPException(
                status_code=501,
                detail=f"no adapter for provider: {profile.provider}",
            )
        chunk_iter = adapter.stream_chat(profile, req, secret=secret)
```

- [ ] **Step 4: Run full test suite, expect pass**

```bash
.venv/bin/pytest -q
```

- [ ] **Step 5: Commit**

```bash
git add proxy/src/cardputer_proxy/adapters/ proxy/src/cardputer_proxy/app.py
git commit -m "refactor(m4): adapter registry — modules register at import time"
```

---

### Task 5: OpenAI adapter (test first)

**Files:**
- Create: `proxy/tests/test_openai_adapter.py`
- Create: `proxy/src/cardputer_proxy/adapters/openai.py`
- Modify: `proxy/src/cardputer_proxy/adapters/__init__.py` (import the new module)

OpenAI's Chat Completions API *is* our wire format, so the adapter is mostly a header swap (`Authorization: Bearer <key>` instead of `x-api-key`) and direct SSE pass-through. The OAI server already emits chunks shaped like:

```
data: {"choices":[{"delta":{"content":"...","role":"assistant"},"finish_reason":null}]}
```

So we strip any non-essential fields (id, created, etc.) and pass `delta` through as-is. The Anthropic-style helpers don't apply here.

- [ ] **Step 1: Create `proxy/tests/test_openai_adapter.py`**

```python
from __future__ import annotations

import json

import pytest
from pytest_httpx import HTTPXMock

from cardputer_proxy.adapters.openai import OpenAIAdapter
from cardputer_proxy.schemas import Auth, ChatCompletionRequest, Message, Profile


PROFILE = Profile(
    id="oai", label="OpenAI gpt-mini",
    provider="openai",
    endpoint="https://api.openai.com",
    model="gpt-4o-mini",
    auth=Auth(kind="proxy-secret", secret_ref="openai"),
)


def _stream(*payloads: dict) -> bytes:
    out = b""
    for p in payloads:
        out += b"data: " + json.dumps(p).encode("utf-8") + b"\n\n"
    out += b"data: [DONE]\n\n"
    return out


@pytest.mark.asyncio
async def test_passthrough_translates_oai_chunks(httpx_mock: HTTPXMock):
    httpx_mock.add_response(
        method="POST",
        url="https://api.openai.com/v1/chat/completions",
        content=_stream(
            {"choices": [{"delta": {"role": "assistant"}, "finish_reason": None}]},
            {"choices": [{"delta": {"content": "hi"}, "finish_reason": None}]},
            {"choices": [{"delta": {}, "finish_reason": "stop"}]},
        ),
        headers={"content-type": "text/event-stream"},
    )

    adapter = OpenAIAdapter()
    req = ChatCompletionRequest(profile_id="oai",
                                messages=[Message(role="user", content="hi")])
    chunks = []
    async for c in adapter.stream_chat(PROFILE, req, secret="sk-test"):
        chunks.append(c)

    assert chunks[0]["choices"][0]["delta"]["role"] == "assistant"
    assert chunks[1]["choices"][0]["delta"]["content"] == "hi"
    assert chunks[-1]["choices"][0]["finish_reason"] == "stop"


@pytest.mark.asyncio
async def test_request_uses_bearer_auth(httpx_mock: HTTPXMock):
    httpx_mock.add_response(
        method="POST",
        url="https://api.openai.com/v1/chat/completions",
        content=_stream({"choices": [{"delta": {}, "finish_reason": "stop"}]}),
        headers={"content-type": "text/event-stream"},
    )
    adapter = OpenAIAdapter()
    req = ChatCompletionRequest(profile_id="oai",
                                messages=[Message(role="user", content="hi")])
    async for _ in adapter.stream_chat(PROFILE, req, secret="sk-test"):
        pass
    sent = httpx_mock.get_request()
    assert sent.headers["authorization"] == "Bearer sk-test"
    body = json.loads(sent.content)
    assert body["model"] == "gpt-4o-mini"
    assert body["stream"] is True
```

- [ ] **Step 2: Implement `proxy/src/cardputer_proxy/adapters/openai.py`**

```python
"""OpenAI-compatible Chat Completions adapter (and base for other OAI-style services).

The wire format is already what the device speaks, so this is almost
entirely a pass-through with a header rewrite.
"""

from __future__ import annotations

import json
from typing import AsyncIterator

import httpx

from cardputer_proxy.adapters.base import Adapter
from cardputer_proxy.adapters import register
from cardputer_proxy.schemas import ChatCompletionRequest, Profile


class OpenAIAdapter:
    PATH = "/v1/chat/completions"
    AUTH_HEADER = "Authorization"
    AUTH_VALUE_TEMPLATE = "Bearer {}"

    async def stream_chat(
        self,
        profile: Profile,
        request: ChatCompletionRequest,
        secret: str | None,
    ) -> AsyncIterator[dict]:
        body = {
            "model": profile.model,
            "messages": [m.model_dump() for m in request.messages],
            "stream": True,
        }
        if request.max_tokens is not None:
            body["max_tokens"] = request.max_tokens
        elif profile.max_tokens:
            body["max_tokens"] = profile.max_tokens
        if request.temperature is not None:
            body["temperature"] = request.temperature
        elif profile.temperature is not None:
            body["temperature"] = profile.temperature

        headers = {"content-type": "application/json",
                   "accept": "text/event-stream"}
        if secret is not None:
            headers[self.AUTH_HEADER] = self.AUTH_VALUE_TEMPLATE.format(secret)

        url = profile.endpoint.rstrip("/") + self.PATH
        async with httpx.AsyncClient(timeout=httpx.Timeout(60.0, read=300.0)) as cx:
            async with cx.stream("POST", url, headers=headers, json=body) as resp:
                resp.raise_for_status()
                async for line in resp.aiter_lines():
                    if not line.startswith("data: "):
                        continue
                    payload = line[6:]
                    if payload == "[DONE]":
                        break
                    try:
                        evt = json.loads(payload)
                    except json.JSONDecodeError:
                        continue
                    # Trim noisy fields so we yield what the device expects.
                    out = {
                        "object": "chat.completion.chunk",
                        "model": profile.model,
                        "choices": evt.get("choices", []),
                    }
                    yield out


register("openai", OpenAIAdapter)
```

- [ ] **Step 3: Import it in `adapters/__init__.py`**

Add after the anthropic import:

```python
from cardputer_proxy.adapters import openai  # noqa: F401
```

- [ ] **Step 4: Run, expect pass**

```bash
.venv/bin/pytest tests/test_openai_adapter.py -q
```

- [ ] **Step 5: Commit**

```bash
git add proxy/src/cardputer_proxy/adapters/openai.py proxy/src/cardputer_proxy/adapters/__init__.py proxy/tests/test_openai_adapter.py
git commit -m "feat(m4): openai adapter (pass-through with bearer header swap)"
```

---

### Task 6: Ollama adapter (test first)

**Files:**
- Create: `proxy/tests/test_ollama_adapter.py`
- Create: `proxy/src/cardputer_proxy/adapters/ollama.py`
- Modify: `proxy/src/cardputer_proxy/adapters/__init__.py`

Ollama's `/v1/chat/completions` endpoint is OpenAI-compatible. We inherit from `OpenAIAdapter` to avoid duplication and just override the path (some Ollama deployments behind reverse proxies expose under different prefixes) and skip auth by default.

- [ ] **Step 1: Create `proxy/tests/test_ollama_adapter.py`**

```python
from __future__ import annotations

import json

import pytest
from pytest_httpx import HTTPXMock

from cardputer_proxy.adapters.ollama import OllamaAdapter
from cardputer_proxy.schemas import Auth, ChatCompletionRequest, Message, Profile


PROFILE = Profile(
    id="ollama-llama3",
    label="Ollama Llama 3 8B",
    provider="ollama",
    endpoint="http://lobsterboy:11434",
    model="llama3:8b",
    auth=Auth(kind="none"),
)


def _stream(*payloads: dict) -> bytes:
    out = b""
    for p in payloads:
        out += b"data: " + json.dumps(p).encode("utf-8") + b"\n\n"
    out += b"data: [DONE]\n\n"
    return out


@pytest.mark.asyncio
async def test_passthrough_no_auth_header(httpx_mock: HTTPXMock):
    httpx_mock.add_response(
        method="POST",
        url="http://lobsterboy:11434/v1/chat/completions",
        content=_stream(
            {"choices": [{"delta": {"role": "assistant"}}]},
            {"choices": [{"delta": {"content": "hello"}}]},
            {"choices": [{"delta": {}, "finish_reason": "stop"}]},
        ),
        headers={"content-type": "text/event-stream"},
    )

    adapter = OllamaAdapter()
    req = ChatCompletionRequest(
        profile_id="ollama-llama3",
        messages=[Message(role="user", content="hi")],
    )

    chunks = []
    async for c in adapter.stream_chat(PROFILE, req, secret=None):
        chunks.append(c)
    assert chunks[1]["choices"][0]["delta"]["content"] == "hello"

    sent = httpx_mock.get_request()
    assert "authorization" not in {k.lower() for k in sent.headers.keys()}
```

- [ ] **Step 2: Implement `proxy/src/cardputer_proxy/adapters/ollama.py`**

```python
"""Ollama adapter — uses Ollama's OpenAI-compatible endpoint."""

from __future__ import annotations

from cardputer_proxy.adapters.openai import OpenAIAdapter
from cardputer_proxy.adapters import register


class OllamaAdapter(OpenAIAdapter):
    PATH = "/v1/chat/completions"
    # Override: Ollama doesn't require auth, so even if `secret` is set
    # we just don't send it. The base class's stream_chat is fine.


register("ollama", OllamaAdapter)
```

- [ ] **Step 3: Register in `adapters/__init__.py`**

```python
from cardputer_proxy.adapters import ollama  # noqa: F401
```

- [ ] **Step 4: Run, expect pass**

```bash
.venv/bin/pytest tests/test_ollama_adapter.py -q
```

- [ ] **Step 5: Commit**

```bash
git add proxy/src/cardputer_proxy/adapters/ollama.py proxy/src/cardputer_proxy/adapters/__init__.py proxy/tests/test_ollama_adapter.py
git commit -m "feat(m4): ollama adapter (inherits OpenAIAdapter, no auth header by default)"
```

---

### Task 7: Generic OAI-compatible adapter (test first)

For vLLM, LM Studio, llama.cpp server, etc. Same as OpenAI but auth is optional and endpoint must be set per-profile (no implicit default).

**Files:**
- Create: `proxy/tests/test_generic_oai_adapter.py`
- Create: `proxy/src/cardputer_proxy/adapters/generic_oai.py`
- Modify: `proxy/src/cardputer_proxy/adapters/__init__.py`

- [ ] **Step 1: Create `proxy/tests/test_generic_oai_adapter.py`**

```python
from __future__ import annotations

import json

import pytest
from pytest_httpx import HTTPXMock

from cardputer_proxy.adapters.generic_oai import GenericOAIAdapter
from cardputer_proxy.schemas import Auth, ChatCompletionRequest, Message, Profile


@pytest.mark.asyncio
async def test_uses_bearer_when_secret_present(httpx_mock: HTTPXMock):
    profile = Profile(
        id="vllm",
        label="vLLM",
        provider="openai-compatible",
        endpoint="http://internal.box:8000",
        model="meta-llama/Llama-3.1-8B-Instruct",
        auth=Auth(kind="proxy-secret", secret_ref="vllm_token"),
    )
    httpx_mock.add_response(
        method="POST",
        url="http://internal.box:8000/v1/chat/completions",
        content=b'data: {"choices":[{"delta":{}, "finish_reason":"stop"}]}\n\ndata: [DONE]\n\n',
        headers={"content-type": "text/event-stream"},
    )

    adapter = GenericOAIAdapter()
    req = ChatCompletionRequest(profile_id="vllm",
                                messages=[Message(role="user", content="hi")])
    async for _ in adapter.stream_chat(profile, req, secret="sk-internal"):
        pass
    sent = httpx_mock.get_request()
    assert sent.headers["authorization"] == "Bearer sk-internal"


@pytest.mark.asyncio
async def test_omits_auth_when_no_secret(httpx_mock: HTTPXMock):
    profile = Profile(
        id="local",
        label="local",
        provider="openai-compatible",
        endpoint="http://127.0.0.1:8001",
        model="local-model",
        auth=Auth(kind="none"),
    )
    httpx_mock.add_response(
        method="POST",
        url="http://127.0.0.1:8001/v1/chat/completions",
        content=b'data: [DONE]\n\n',
        headers={"content-type": "text/event-stream"},
    )
    adapter = GenericOAIAdapter()
    req = ChatCompletionRequest(profile_id="local",
                                messages=[Message(role="user", content="hi")])
    async for _ in adapter.stream_chat(profile, req, secret=None):
        pass
    sent = httpx_mock.get_request()
    assert "authorization" not in {k.lower() for k in sent.headers.keys()}
```

- [ ] **Step 2: Implement `proxy/src/cardputer_proxy/adapters/generic_oai.py`**

```python
"""Generic OAI-compatible adapter for vLLM, LM Studio, llama.cpp server, etc.

Same wire shape as OpenAI; the only differences are that there's no
implicit endpoint default and the auth header is optional.
"""

from __future__ import annotations

from cardputer_proxy.adapters.openai import OpenAIAdapter
from cardputer_proxy.adapters import register


class GenericOAIAdapter(OpenAIAdapter):
    pass


register("openai-compatible", GenericOAIAdapter)
```

- [ ] **Step 3: Register in `adapters/__init__.py`**

```python
from cardputer_proxy.adapters import generic_oai  # noqa: F401
```

- [ ] **Step 4: Run, expect pass**

```bash
.venv/bin/pytest tests/test_generic_oai_adapter.py -q
```

- [ ] **Step 5: Commit**

```bash
git add proxy/src/cardputer_proxy/adapters/generic_oai.py proxy/src/cardputer_proxy/adapters/__init__.py proxy/tests/test_generic_oai_adapter.py
git commit -m "feat(m4): generic openai-compatible adapter (vLLM/LM Studio/llama.cpp server)"
```

---

### Task 8: Profile test endpoint (test first)

`POST /v1/profiles/{id}/test` — small upstream ping that returns `{ok, latency_ms, first_token_ms?, error?}`. Reuses the adapter the profile points at, sends a fixed minimal prompt, times until first chunk arrives.

**Files:**
- Modify: `proxy/src/cardputer_proxy/app.py`
- Create: `proxy/tests/test_profile_test_endpoint.py`

- [ ] **Step 1: Add test**

```python
from __future__ import annotations

from pytest_httpx import HTTPXMock


_TINY = (
    b"event: message_start\n"
    b'data: {"type":"message_start","message":{"id":"m","model":"claude-opus-4-7"}}\n\n'
    b"event: content_block_delta\n"
    b'data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"."}}\n\n'
    b"event: message_stop\n"
    b'data: {"type":"message_stop"}\n\n'
)


def test_profile_test_success(client, httpx_mock: HTTPXMock):
    httpx_mock.add_response(
        method="POST",
        url="https://api.anthropic.com/v1/messages",
        content=_TINY,
        headers={"content-type": "text/event-stream"},
    )
    r = client.post(
        "/v1/profiles/claude-opus/test",
        headers={"Authorization": "Bearer test-bearer"},
    )
    assert r.status_code == 200
    body = r.json()
    assert body["ok"] is True
    assert body["latency_ms"] >= 0
    assert body["first_token_ms"] is not None


def test_profile_test_upstream_failure(client, httpx_mock: HTTPXMock):
    httpx_mock.add_response(
        method="POST",
        url="https://api.anthropic.com/v1/messages",
        status_code=500,
        json={"error": "bad"},
    )
    r = client.post(
        "/v1/profiles/claude-opus/test",
        headers={"Authorization": "Bearer test-bearer"},
    )
    assert r.status_code == 200            # the test endpoint reports success-of-test, not upstream success
    body = r.json()
    assert body["ok"] is False
    assert body["error"]


def test_profile_test_unknown_profile_404(client):
    r = client.post(
        "/v1/profiles/no-such/test",
        headers={"Authorization": "Bearer test-bearer"},
    )
    assert r.status_code == 404
```

- [ ] **Step 2: Implement the route**

In `app.py`, after the DELETE /v1/profiles/{id}:

```python
import time
from cardputer_proxy.schemas import TestResult

    @app.post("/v1/profiles/{profile_id}/test",
              dependencies=[Depends(auth.require_bearer)])
    async def test_profile(profile_id: str) -> TestResult:
        profile = settings.catalog.get(profile_id)
        if profile is None:
            raise HTTPException(404, detail=f"unknown profile: {profile_id}")
        adapter = adapter_registry.get(profile.provider)
        if adapter is None:
            return TestResult(ok=False, latency_ms=0,
                              error=f"no adapter for {profile.provider}")

        # Resolve secret (mirror the chat handler).
        try:
            if profile.auth.kind == "proxy-secret":
                secret = secrets_store.read(profile.auth.secret_ref or "")
            elif profile.auth.kind == "none":
                secret = None
            else:
                return TestResult(ok=False, latency_ms=0,
                                  error=f"auth kind {profile.auth.kind} not supported in test")
        except (FileNotFoundError, ValueError) as e:
            return TestResult(ok=False, latency_ms=0, error=str(e))

        req = ChatCompletionRequest(
            profile_id=profile_id,
            messages=[{"role": "user", "content": "ping"}],
            max_tokens=8,
        )
        start = time.perf_counter()
        first_token_ms: int | None = None
        try:
            async for _ in adapter.stream_chat(profile, req, secret=secret):
                if first_token_ms is None:
                    first_token_ms = int((time.perf_counter() - start) * 1000)
        except Exception as e:
            return TestResult(ok=False,
                              latency_ms=int((time.perf_counter() - start) * 1000),
                              error=str(e))
        return TestResult(
            ok=True,
            latency_ms=int((time.perf_counter() - start) * 1000),
            first_token_ms=first_token_ms,
        )
```

- [ ] **Step 3: Run, expect pass**

```bash
.venv/bin/pytest tests/test_profile_test_endpoint.py -q
```

- [ ] **Step 4: Commit**

```bash
git add proxy/src/cardputer_proxy/app.py proxy/tests/test_profile_test_endpoint.py
git commit -m "feat(m4): POST /v1/profiles/{id}/test — returns ok + latency + first-token-ms"
```

---

### Task 9: Secrets management CLI (`cardputer-proxy secrets ...`)

**Files:**
- Create: `proxy/src/cardputer_proxy/cli.py`
- Modify: `proxy/src/cardputer_proxy/__main__.py` (subcommand dispatch)
- Modify: `proxy/pyproject.toml` (keep the existing `cardputer-proxy` entry but it now dispatches)
- Create: `proxy/tests/test_secrets_cli.py`

- [ ] **Step 1: Create `proxy/src/cardputer_proxy/cli.py`**

```python
"""Secrets management CLI.

Usage:
    cardputer-proxy secrets list
    cardputer-proxy secrets set <name>   # value on stdin
    cardputer-proxy secrets rotate <name>  # alias for set
    cardputer-proxy secrets delete <name>
    cardputer-proxy secrets show-path <name>  # prints absolute path; never prints value

Files live in /etc/cardputer-proxy/secrets/ (override with $CARDPUTER_PROXY_SECRETS_DIR).
"""

from __future__ import annotations

import argparse
import os
import re
import stat
import sys
from pathlib import Path


_VALID = re.compile(r"^[a-zA-Z0-9_.-]+$")


def _secrets_dir() -> Path:
    return Path(os.environ.get("CARDPUTER_PROXY_SECRETS_DIR",
                               "/etc/cardputer-proxy/secrets"))


def _validate_name(name: str) -> None:
    if not _VALID.match(name):
        sys.exit(f"invalid secret name: {name!r}")


def cmd_list(args: argparse.Namespace) -> int:
    d = _secrets_dir()
    if not d.is_dir():
        print(f"no secrets directory: {d}", file=sys.stderr)
        return 1
    for p in sorted(d.iterdir()):
        if p.is_file():
            print(p.name)
    return 0


def cmd_set(args: argparse.Namespace) -> int:
    _validate_name(args.name)
    d = _secrets_dir()
    d.mkdir(parents=True, exist_ok=True)
    if sys.stdin.isatty():
        print("Reading secret from stdin. Type, then Ctrl-D.", file=sys.stderr)
    value = sys.stdin.read().strip()
    if not value:
        sys.exit("refusing to write an empty secret")
    p = d / args.name
    p.write_text(value, encoding="utf-8")
    p.chmod(stat.S_IRUSR | stat.S_IWUSR)  # 0600
    return 0


def cmd_delete(args: argparse.Namespace) -> int:
    _validate_name(args.name)
    p = _secrets_dir() / args.name
    if not p.exists():
        print(f"missing: {args.name}", file=sys.stderr)
        return 1
    p.unlink()
    return 0


def cmd_show_path(args: argparse.Namespace) -> int:
    _validate_name(args.name)
    print(_secrets_dir() / args.name)
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="cardputer-proxy secrets")
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list").set_defaults(func=cmd_list)
    p_set = sub.add_parser("set"); p_set.add_argument("name"); p_set.set_defaults(func=cmd_set)
    p_rot = sub.add_parser("rotate"); p_rot.add_argument("name"); p_rot.set_defaults(func=cmd_set)
    p_del = sub.add_parser("delete"); p_del.add_argument("name"); p_del.set_defaults(func=cmd_delete)
    p_sho = sub.add_parser("show-path"); p_sho.add_argument("name"); p_sho.set_defaults(func=cmd_show_path)
    args = parser.parse_args(argv)
    return args.func(args)
```

- [ ] **Step 2: Dispatch in `__main__.py`**

```python
"""Entry point.

Default: run the server. With `secrets ...` args, run the secrets CLI.
"""

from __future__ import annotations

import os
import sys
import uvicorn


def main() -> None:
    argv = sys.argv[1:]
    if argv and argv[0] == "secrets":
        from cardputer_proxy import cli
        sys.exit(cli.main(argv[1:]))

    host = os.environ.get("CARDPUTER_PROXY_LISTEN_HOST", "127.0.0.1")
    port = int(os.environ.get("CARDPUTER_PROXY_LISTEN_PORT", "8420"))
    uvicorn.run(
        "cardputer_proxy.app:create_app",
        host=host, port=port, factory=True,
        log_level="info", access_log=False,
    )


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Create `proxy/tests/test_secrets_cli.py`**

```python
from __future__ import annotations

import io
import sys

from cardputer_proxy import cli


def test_set_and_list_round_trip(tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("CARDPUTER_PROXY_SECRETS_DIR", str(tmp_path))
    monkeypatch.setattr(sys, "stdin", io.StringIO("hello-secret\n"))
    assert cli.main(["set", "my_key"]) == 0
    assert (tmp_path / "my_key").read_text(encoding="utf-8").strip() == "hello-secret"
    # File is 0600
    assert (tmp_path / "my_key").stat().st_mode & 0o777 == 0o600

    assert cli.main(["list"]) == 0
    out = capsys.readouterr().out
    assert "my_key" in out


def test_set_empty_refuses(tmp_path, monkeypatch):
    monkeypatch.setenv("CARDPUTER_PROXY_SECRETS_DIR", str(tmp_path))
    monkeypatch.setattr(sys, "stdin", io.StringIO("   \n"))
    try:
        cli.main(["set", "k"])
    except SystemExit as e:
        assert "empty" in str(e).lower()


def test_delete_unknown_returns_nonzero(tmp_path, monkeypatch):
    monkeypatch.setenv("CARDPUTER_PROXY_SECRETS_DIR", str(tmp_path))
    rc = cli.main(["delete", "nope"])
    assert rc == 1


def test_invalid_name_rejected(tmp_path, monkeypatch):
    monkeypatch.setenv("CARDPUTER_PROXY_SECRETS_DIR", str(tmp_path))
    try:
        cli.main(["show-path", "../etc/shadow"])
    except SystemExit as e:
        assert "invalid" in str(e).lower()
```

- [ ] **Step 4: Run, expect pass**

```bash
.venv/bin/pytest tests/test_secrets_cli.py -q
```

- [ ] **Step 5: Commit**

```bash
git add proxy/src/cardputer_proxy/cli.py proxy/src/cardputer_proxy/__main__.py proxy/tests/test_secrets_cli.py
git commit -m "feat(m4): cardputer-proxy secrets CLI (list/set/rotate/delete/show-path)"
```

---

### Task 10: Update install.sh + ship a profiles.json.example

**Files:**
- Modify: `ops/cardputer-proxy/install.sh`
- Create: `ops/cardputer-proxy/profiles.json.example`

- [ ] **Step 1: Create `ops/cardputer-proxy/profiles.json.example`**

```json
{
  "version": 1,
  "profiles": [
    {
      "id": "claude-opus",
      "label": "Claude Opus 4.7",
      "provider": "anthropic",
      "endpoint": "https://api.anthropic.com",
      "model": "claude-opus-4-7",
      "max_tokens": 4096,
      "temperature": 1.0,
      "auth": {"kind": "proxy-secret", "secret_ref": "anthropic_api_key"}
    },
    {
      "id": "claude-sonnet",
      "label": "Claude Sonnet 4.6",
      "provider": "anthropic",
      "endpoint": "https://api.anthropic.com",
      "model": "claude-sonnet-4-6",
      "max_tokens": 4096,
      "temperature": 1.0,
      "auth": {"kind": "proxy-secret", "secret_ref": "anthropic_api_key"}
    },
    {
      "id": "openai-4o-mini",
      "label": "OpenAI gpt-4o-mini",
      "provider": "openai",
      "endpoint": "https://api.openai.com",
      "model": "gpt-4o-mini",
      "max_tokens": 2048,
      "temperature": 0.7,
      "auth": {"kind": "proxy-secret", "secret_ref": "openai_api_key"}
    },
    {
      "id": "ollama-llama3",
      "label": "Ollama Llama 3 8B",
      "provider": "ollama",
      "endpoint": "http://lobsterboy:11434",
      "model": "llama3:8b",
      "max_tokens": 2048,
      "temperature": 0.7,
      "auth": {"kind": "none"}
    }
  ]
}
```

- [ ] **Step 2: Update `install.sh` to drop in the example if no catalog exists**

Append before the systemd-reload step:

```bash
# 5.5 — profile catalog (don't clobber an existing one).
if [[ ! -f "$CFG_DIR/profiles.json" ]]; then
  install -m 0640 -o root -g "$SVC_USER" \
    "$REPO_DIR/ops/cardputer-proxy/profiles.json.example" \
    "$CFG_DIR/profiles.json"
  echo "Installed default profiles.json. Edit with: sudo \$EDITOR $CFG_DIR/profiles.json"
fi
```

- [ ] **Step 3: Commit**

```bash
git add ops/cardputer-proxy/install.sh ops/cardputer-proxy/profiles.json.example
git commit -m "feat(m4): install.sh drops a 4-profile profiles.json.example on fresh install"
```

---

## DEVICE-SIDE TASKS (run after Tasks 0-10 are deployed)

### Task 11: proxy_api — fetch /v1/profiles into Profile structs

**Files:**
- Create: `firmware/src/proxy_api.h`
- Create: `firmware/src/proxy_api.cpp`

- [ ] **Step 1: Create `proxy_api.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <vector>

namespace proxy_api {

struct Profile {
  String id;
  String label;
  String provider;
  String model;
};

struct FetchResult {
  bool   ok;
  char   error[80];
  std::vector<Profile> profiles;
};

// GET /v1/profiles via the WG tunnel; return the parsed list.
FetchResult fetch_profiles();

}  // namespace proxy_api
```

- [ ] **Step 2: Create `proxy_api.cpp`**

```cpp
#include "proxy_api.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <cstdio>
#include <cstring>

#include "proxy_secrets.h"

namespace proxy_api {

// Tiny JSON walker. Looks for `"profiles":[ { ... }, { ... } ]` and
// extracts id/label/provider/model from each entry. We do NOT parse
// arbitrary JSON; the proxy emits a predictable shape we control.
static const char* skip_ws(const char* p) {
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
  return p;
}

static const char* read_string(const char* p, String& out) {
  out = "";
  if (*p != '"') return nullptr;
  ++p;
  while (*p && *p != '"') {
    if (*p == '\\' && p[1]) {
      char c = p[1];
      switch (c) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case '"': out += '"';  break;
        case '\\': out += '\\'; break;
        default: out += c;
      }
      p += 2;
    } else {
      out += *p++;
    }
  }
  if (*p != '"') return nullptr;
  return p + 1;
}

static const char* find_field(const char* obj_start, const char* obj_end,
                              const char* key) {
  // Naive: search "key": within the object.
  String pat = String("\"") + key + "\":";
  const char* hit = std::strstr(obj_start, pat.c_str());
  if (!hit || hit >= obj_end) return nullptr;
  return skip_ws(hit + pat.length());
}

static const char* find_obj_end(const char* obj_start) {
  if (*obj_start != '{') return nullptr;
  int depth = 0;
  bool in_str = false;
  bool esc    = false;
  for (const char* p = obj_start; *p; ++p) {
    if (in_str) {
      if (esc) { esc = false; continue; }
      if (*p == '\\') { esc = true; continue; }
      if (*p == '"')  { in_str = false; continue; }
    } else {
      if (*p == '"') { in_str = true; continue; }
      if (*p == '{') ++depth;
      else if (*p == '}') { --depth; if (depth == 0) return p + 1; }
    }
  }
  return nullptr;
}

FetchResult fetch_profiles() {
  FetchResult r{false, "", {}};
  WiFiClient cx;
  cx.setTimeout(10000);
  if (!cx.connect(proxy_secrets::kHost, proxy_secrets::kPort)) {
    std::snprintf(r.error, sizeof(r.error), "connect failed");
    return r;
  }
  cx.printf(
      "GET /v1/profiles HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Authorization: Bearer %s\r\n"
      "Accept: application/json\r\n"
      "Connection: close\r\n\r\n",
      proxy_secrets::kHost, proxy_secrets::kBearerToken);

  // Drain headers.
  String body;
  body.reserve(2048);
  bool in_body = false;
  String line; line.reserve(128);
  uint32_t deadline = millis() + 15000;
  while ((int32_t)(deadline - millis()) > 0 && (cx.connected() || cx.available())) {
    while (cx.available()) {
      int c = cx.read();
      if (c < 0) break;
      if (!in_body) {
        if (c == '\r') continue;
        if (c == '\n') {
          if (line.length() == 0) { in_body = true; }
          else                    { line = ""; }
          continue;
        }
        line += (char) c;
      } else {
        body += (char) c;
      }
    }
    if (!cx.connected() && !cx.available()) break;
    delay(2);
  }
  cx.stop();

  // Walk "profiles":[ ... ]
  const char* p = std::strstr(body.c_str(), "\"profiles\":");
  if (!p) { std::snprintf(r.error, sizeof(r.error), "no profiles field"); return r; }
  p = skip_ws(p + 11);
  if (*p != '[') { std::snprintf(r.error, sizeof(r.error), "bad json"); return r; }
  ++p;
  while (true) {
    p = skip_ws(p);
    if (*p == ']') break;
    if (*p != '{') { std::snprintf(r.error, sizeof(r.error), "bad json"); return r; }
    const char* obj_end = find_obj_end(p);
    if (!obj_end) { std::snprintf(r.error, sizeof(r.error), "unterminated obj"); return r; }

    Profile prof;
    if (const char* v = find_field(p, obj_end, "id"))       read_string(v, prof.id);
    if (const char* v = find_field(p, obj_end, "label"))    read_string(v, prof.label);
    if (const char* v = find_field(p, obj_end, "provider")) read_string(v, prof.provider);
    if (const char* v = find_field(p, obj_end, "model"))    read_string(v, prof.model);
    if (prof.id.length() > 0) r.profiles.push_back(std::move(prof));

    p = obj_end;
    p = skip_ws(p);
    if (*p == ',') ++p;
  }
  r.ok = true;
  return r;
}

}  // namespace proxy_api
```

- [ ] **Step 3: Build (no main wire-up yet)**

```bash
cd firmware && pio run
```

- [ ] **Step 4: Commit**

```bash
git add firmware/src/proxy_api.h firmware/src/proxy_api.cpp
git commit -m "feat(m4): proxy_api — GET /v1/profiles, parse into Profile structs"
```

---

### Task 12: profile_store — NVS persistence for active profile id

**Files:**
- Create: `firmware/src/profile_store.h`
- Create: `firmware/src/profile_store.cpp`

- [ ] **Step 1: Create `profile_store.h`**

```cpp
#pragma once
#include <Arduino.h>

namespace profile_store {

void begin();
String active_profile_id();           // empty if never set
void   set_active_profile_id(const String& id);

}  // namespace profile_store
```

- [ ] **Step 2: Create `profile_store.cpp`**

```cpp
#include "profile_store.h"
#include <Preferences.h>

namespace profile_store {

static Preferences s_prefs;
static const char* kNs  = "cprox";
static const char* kKey = "profile_id";

void begin() {
  s_prefs.begin(kNs, /*readOnly=*/false);
}

String active_profile_id() {
  return s_prefs.getString(kKey, "");
}

void set_active_profile_id(const String& id) {
  s_prefs.putString(kKey, id);
}

}  // namespace profile_store
```

- [ ] **Step 3: Build**

```bash
pio run
```

- [ ] **Step 4: Commit**

```bash
git add firmware/src/profile_store.h firmware/src/profile_store.cpp
git commit -m "feat(m4): profile_store — NVS-backed active profile id"
```

---

### Task 13: picker_view — list-selection screen

**Files:**
- Create: `firmware/src/picker_view.h`
- Create: `firmware/src/picker_view.cpp`

- [ ] **Step 1: Create `picker_view.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <vector>
#include "proxy_api.h"

namespace picker_view {

// Render the picker over the existing chat_view canvas. Returns true if
// the user selected a profile; selected_out is set to its id. Returns
// false if the user cancelled (Esc).
bool run(const std::vector<proxy_api::Profile>& profiles,
         const String& current_id,
         String& selected_out);

}  // namespace picker_view
```

- [ ] **Step 2: Create `picker_view.cpp`**

```cpp
#include "picker_view.h"
#include <M5Cardputer.h>
#include "keyboard_input.h"

namespace picker_view {

static void draw(const std::vector<proxy_api::Profile>& profiles,
                 size_t cursor, const String& current_id) {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.setTextSize(2);
  d.setCursor(0, 0);
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.println(" select profile");
  for (size_t i = 0; i < profiles.size(); ++i) {
    uint16_t fg = (i == cursor) ? TFT_BLACK : TFT_WHITE;
    uint16_t bg = (i == cursor) ? TFT_YELLOW : TFT_BLACK;
    d.setTextColor(fg, bg);
    d.setCursor(0, 20 + (int)i * 16);
    const char* mark = (profiles[i].id == current_id) ? "*" : " ";
    String row = String(mark) + " " + profiles[i].label;
    if (row.length() > 19) row = row.substring(0, 19);
    d.print(row);
  }
}

bool run(const std::vector<proxy_api::Profile>& profiles,
         const String& current_id,
         String& selected_out) {
  if (profiles.empty()) return false;
  size_t cursor = 0;
  for (size_t i = 0; i < profiles.size(); ++i) {
    if (profiles[i].id == current_id) { cursor = i; break; }
  }
  draw(profiles, cursor, current_id);

  uint32_t deadline = millis() + 30000;       // 30 s of inactivity = cancel
  while ((int32_t)(deadline - millis()) > 0) {
    int k = keyboard_input::poll();
    if (k == keyboard_input::KB_NONE) { delay(15); continue; }
    deadline = millis() + 30000;
    if (k == keyboard_input::KB_ESCAPE) return false;
    if (k == keyboard_input::KB_ENTER) {
      selected_out = profiles[cursor].id;
      return true;
    }
    if (k == 'j' || k == 'J') { if (cursor + 1 < profiles.size()) ++cursor; }
    else if (k == 'k' || k == 'K') { if (cursor > 0) --cursor; }
    else if (k == ';') { if (cursor + 1 < profiles.size()) ++cursor; }  // wrap-friendly alt
    else if (k == 'l' || k == 'L') { if (cursor + 1 < profiles.size()) ++cursor; }
    draw(profiles, cursor, current_id);
  }
  return false;
}

}  // namespace picker_view
```

> Cardputer keys: the ADV keyboard's arrow keys are emitted as `;` (right) and so on — the M5Cardputer library reports them as ASCII codes. j/k as vi-style fallbacks let users navigate without hunting for the arrow modifier. Refine the keybinds during execution by adding raw-code logging if needed.

- [ ] **Step 3: Build**

```bash
pio run
```

- [ ] **Step 4: Commit**

```bash
git add firmware/src/picker_view.h firmware/src/picker_view.cpp
git commit -m "feat(m4): picker_view — list-selection screen with j/k cursor + Enter to pick"
```

---

### Task 14: chat_client takes profile_id at send time

**Files:**
- Modify: `firmware/src/chat_client.h`
- Modify: `firmware/src/chat_client.cpp`

- [ ] **Step 1: Update `chat_client.h`**

```cpp
SendResult send(const String& profile_id,
                const String& user_text,
                std::function<void(const String&)> on_delta,
                String& assistant_out);
```

- [ ] **Step 2: Update `chat_client.cpp`**

In `build_body`, replace `proxy_secrets::kProfileId` with the passed-in id:

```cpp
static String build_body(const String& profile_id,
                          const std::vector<Message>& msgs) {
  String out;
  out.reserve(256 + msgs.size() * 64);
  out += "{\"profile_id\":\"";
  out += profile_id;
  ...
```

And the `send` signature accordingly. The default `kProfileId` from `proxy_secrets.h` becomes the *fallback* used by `main.cpp` if NVS is empty.

- [ ] **Step 3: Build, commit**

```bash
pio run
git add firmware/src/chat_client.*
git commit -m "feat(m4): chat_client::send takes profile_id at send time"
```

---

### Task 15: main.cpp wires picker (Fn+P or key 'p') + active profile

**Files:**
- Modify: `firmware/src/main.cpp`
- Modify: `firmware/src/chat_view.cpp` (status bar shows active label)
- Modify: `firmware/src/chat_view.h` (add `set_active_profile_label`)

- [ ] **Step 1: Add label setter to chat_view**

In `chat_view.h`:
```cpp
void set_active_profile_label(const char* label);
```

In `chat_view.cpp` add a static `s_profile_label[24]` and append it to the status row draw.

- [ ] **Step 2: In `main.cpp` — pull catalog on boot, persist active**

After WG comes up:

```cpp
profile_store::begin();
String active = profile_store::active_profile_id();
if (active.length() == 0) active = proxy_secrets::kProfileId;

auto pr = proxy_api::fetch_profiles();
std::vector<proxy_api::Profile> g_profiles = pr.ok ? pr.profiles : std::vector<proxy_api::Profile>{};
const char* label_for_active = "?";
for (auto& p : g_profiles) {
  if (p.id == active) { label_for_active = p.label.c_str(); break; }
}
chat_view::set_active_profile_label(label_for_active);
```

In `loop()`, add a hotkey to open the picker — use `'p'` for now (no Fn key handling yet; M4.5):

```cpp
if (k == 'p' && s_input.length() == 0) {
  // Re-fetch in case the catalog changed on the proxy.
  auto pr2 = proxy_api::fetch_profiles();
  if (pr2.ok) g_profiles = pr2.profiles;
  String chosen;
  if (picker_view::run(g_profiles, active, chosen)) {
    active = chosen;
    profile_store::set_active_profile_id(active);
    for (auto& p : g_profiles) {
      if (p.id == active) {
        chat_view::set_active_profile_label(p.label.c_str()); break;
      }
    }
  }
  chat_view::mark_dirty();
  redraw();
}
```

Replace `chat_client::send(...)` call site with the active profile id.

- [ ] **Step 3: Build, flash, verify on device**

Boot the device. Status bar should show the active profile label.

Press `p` with empty input. The picker should fill the screen with the proxy's profiles, marking the active one with `*`. j/k moves the highlight, Enter switches the active profile, Esc cancels. After picking, the chat screen redraws with the new label.

Send a chat message. The proxy uses whichever profile is now active. Bonus: switch to an Ollama or OpenAI profile (once the proxy is deployed with that catalog) — Claude is no longer the only voice.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/main.cpp firmware/src/chat_view.cpp firmware/src/chat_view.h
git commit -m "feat(m4): main wires picker on 'p'; status bar shows active profile label"
```

---

### Task 16: Finalize report + tag

**Files:**
- Create: `docs/m4-profile-system-report.md`

- [ ] **Step 1: Write the report**

Capture:
- Final flash + RAM (likely up by ~30-40 KB for the picker/proxy_api).
- Whether per-profile secrets work end-to-end (test OpenAI + Ollama if you have keys/services for them).
- Any UX papercuts on the picker (j/k vs proper arrows, label truncation).
- What it feels like to switch profiles mid-conversation (we wipe history? keep it? — recommended: keep, since the OAI shape transfers between providers).

- [ ] **Step 2: Tag + push**

```bash
git tag -a m4-complete -m "M4 profile system complete — provider-agnostic Cardputer-AI"
git push -u origin m4-execution
git push origin m4-complete
```

Merge the PR.

---

## Done criteria

M4 is complete when **all** of these are true:

1. Proxy test suite passes (≥ 25 tests covering catalog, CRUD, secrets resolution, three new adapters, profile-test endpoint, secrets CLI).
2. `cardputer-proxy secrets list` shows the existing `anthropic_api_key` and `device_bearer_token`, plus any newly-added secrets.
3. Calling `curl /v1/profiles` returns the catalog (≥ 1 profile).
4. Calling `curl /v1/profiles/<id>/test` returns `{ok: true, latency_ms, first_token_ms}` for any profile whose upstream is reachable.
5. Device boots, fetches the catalog, shows the active profile label in the status bar.
6. Pressing `p` opens the picker; selecting a different profile actually changes which upstream the next chat hits (verify with curl that the proxy logs / handles the different provider).
7. Active profile id persists across power-cycle.
8. `docs/m4-profile-system-report.md` exists.
9. Tag `m4-complete` exists on origin/main.

---

## Open items pushed to M4.5 / M5 / later

- **Device-side profile editor / add / delete** (M4.5 if you want it before M5).
- **Test-from-device** (Fn+T).
- **Profile *icons* in the status bar** — DESIGN.md §6 calls out an icon + color per profile; we ship label-only for M4.
- **Long-press Fn+P quick-cycle** — needs the Fn modifier wired up.
- **Per-device bearer tokens with scopes** — M5.
- **Device-key auth on the wire** — proxy accepts the kind today but the firmware doesn't send an `X-Device-Provided-Key` yet; M5.
- **Profile catalog hot-reload on proxy** — the catalog reads from disk at startup only (each `Catalog` instance loads once). CRUD goes through the in-process instance, so it stays consistent, but a `secrets`-CLI edit to a file (rather than via API) isn't picked up until restart. Acceptable for M4; add an explicit "reload" admin endpoint in M5 if needed.
