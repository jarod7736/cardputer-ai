from __future__ import annotations

import pytest

from cardputer_proxy.schemas import Token
from cardputer_proxy.tokens import TokenStore, TokenStoreError


def _tok(device_id="dev-one", token="tok_aaaaaaaaaaaaaaaaaaaa", label="dev"):
    return Token(
        device_id=device_id,
        token=token,
        label=label,
        created_at="2026-05-17T00:00:00Z",
    )


def test_round_trip_save_and_load(tmp_path):
    p = tmp_path / "tokens.json"
    s1 = TokenStore(p)
    s1.add(_tok("alpha", "tok_alphaalphaalphaaa"))
    s1.add(_tok("beta", "tok_betabetabetabetab"))

    s2 = TokenStore(p)
    assert {t.device_id for t in s2.list()} == {"alpha", "beta"}
    assert s2.get_by_token("tok_alphaalphaalphaaa").device_id == "alpha"


def test_get_by_token_miss_returns_none(tmp_path):
    s = TokenStore(tmp_path / "tokens.json")
    s.add(_tok())
    assert s.get_by_token("nope") is None


def test_revoke_marks_but_does_not_delete(tmp_path):
    p = tmp_path / "tokens.json"
    s = TokenStore(p)
    s.add(_tok("dev-x"))
    assert s.revoke("dev-x") is True
    assert s.revoke("dev-x") is False  # idempotent: already revoked
    assert s.get("dev-x").revoked is True

    s2 = TokenStore(p)
    assert s2.get("dev-x").revoked is True


def test_revoke_unknown_returns_false(tmp_path):
    s = TokenStore(tmp_path / "tokens.json")
    assert s.revoke("ghost") is False


def test_duplicate_device_id_refused(tmp_path):
    s = TokenStore(tmp_path / "tokens.json")
    s.add(_tok("dup"))
    with pytest.raises(TokenStoreError):
        s.add(_tok("dup", token="tok_otherothereother1"))


def test_unsupported_schema_version_refused(tmp_path):
    p = tmp_path / "tokens.json"
    p.write_text('{"version": 99, "tokens": []}', encoding="utf-8")
    with pytest.raises(TokenStoreError):
        TokenStore(p)


def test_corrupt_file_refused(tmp_path):
    p = tmp_path / "tokens.json"
    p.write_text("{ not json", encoding="utf-8")
    with pytest.raises(TokenStoreError):
        TokenStore(p)


def test_empty_directory_creates_on_save(tmp_path):
    p = tmp_path / "nested" / "deeper" / "tokens.json"
    s = TokenStore(p)
    s.add(_tok())
    assert p.exists()


def test_invalid_device_id_rejected_by_pydantic():
    with pytest.raises(ValueError):
        Token(
            device_id="UPPER",
            token="tok_xxxxxxxxxxxxxxxxxxxx",
            created_at="2026-05-17T00:00:00Z",
        )
