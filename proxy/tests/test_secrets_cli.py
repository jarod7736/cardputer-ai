from __future__ import annotations

import io
import sys

import pytest

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
    with pytest.raises(SystemExit) as e:
        cli.main(["set", "k"])
    assert "empty" in str(e.value).lower()


def test_delete_unknown_returns_nonzero(tmp_path, monkeypatch):
    monkeypatch.setenv("CARDPUTER_PROXY_SECRETS_DIR", str(tmp_path))
    rc = cli.main(["delete", "nope"])
    assert rc == 1


def test_invalid_name_rejected(tmp_path, monkeypatch):
    monkeypatch.setenv("CARDPUTER_PROXY_SECRETS_DIR", str(tmp_path))
    with pytest.raises(SystemExit) as e:
        cli.main(["show-path", "../etc/shadow"])
    assert "invalid" in str(e.value).lower()


def test_rotate_overwrites(tmp_path, monkeypatch):
    monkeypatch.setenv("CARDPUTER_PROXY_SECRETS_DIR", str(tmp_path))
    monkeypatch.setattr(sys, "stdin", io.StringIO("first\n"))
    assert cli.main(["set", "k"]) == 0
    monkeypatch.setattr(sys, "stdin", io.StringIO("second\n"))
    assert cli.main(["rotate", "k"]) == 0
    assert (tmp_path / "k").read_text(encoding="utf-8").strip() == "second"


def test_show_path_prints_full_path(tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("CARDPUTER_PROXY_SECRETS_DIR", str(tmp_path))
    assert cli.main(["show-path", "thing"]) == 0
    out = capsys.readouterr().out.strip()
    assert out == str(tmp_path / "thing")
