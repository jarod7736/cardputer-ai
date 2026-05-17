from __future__ import annotations

import json
import os
import pytest

from cardputer_proxy.catalog import Catalog, CatalogError
from cardputer_proxy.schemas import Profile


def _sample() -> Profile:
    return Profile(
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
    s = _sample()
    c.upsert(s)
    assert c.list() == {s.id: s}

    c2 = Catalog(f)
    assert c2.get(s.id) == s


def test_delete(tmp_path):
    f = tmp_path / "profiles.json"
    c = Catalog(f)
    s = _sample()
    c.upsert(s)
    assert c.delete(s.id) is True
    assert c.delete(s.id) is False
    assert c.list() == {}


def test_atomic_write_no_partial_on_failure(tmp_path, monkeypatch):
    f = tmp_path / "profiles.json"
    c = Catalog(f)
    c.upsert(_sample())
    raw = f.read_text(encoding="utf-8")

    def fail_replace(*a, **kw):
        raise OSError("disk full")
    monkeypatch.setattr(os, "replace", fail_replace)

    with pytest.raises(CatalogError):
        updated = Profile(**{**_sample().model_dump(), "label": "changed"})
        c.upsert(updated)
    # Original file untouched
    assert f.read_text(encoding="utf-8") == raw


def test_version_mismatch_rejected(tmp_path):
    f = tmp_path / "profiles.json"
    f.write_text(json.dumps({"version": 99, "profiles": []}), encoding="utf-8")
    with pytest.raises(CatalogError):
        Catalog(f)
