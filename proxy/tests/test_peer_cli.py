from __future__ import annotations

import json
import subprocess
from pathlib import Path

import pytest

from cardputer_proxy import cli, peers as peers_mod


@pytest.fixture
def env(monkeypatch, tmp_path):
    """Set up a peers.json with server config, empty tokens.json, and a fake `wg`."""
    peers_path = tmp_path / "peers.json"
    peers_path.write_text(json.dumps({
        "version": 1,
        "config": {
            "server_pubkey": "SERVERPUB",
            "server_endpoint": "vpn.example.com:51820",
            "network_cidr": "10.42.0.0/24",
            "proxy_host": "10.42.0.1",
            "proxy_port": 8420,
            "peers": [],
        },
    }), encoding="utf-8")
    monkeypatch.setenv("CARDPUTER_PROXY_PEERS_PATH", str(peers_path))
    monkeypatch.setenv("CARDPUTER_PROXY_TOKENS_PATH", str(tmp_path / "tokens.json"))

    def fake_run(cmd, **kw):
        if cmd == ["wg", "genkey"]:
            return subprocess.CompletedProcess(cmd, 0, stdout="DEVPRIV\n", stderr="")
        if cmd == ["wg", "pubkey"]:
            return subprocess.CompletedProcess(cmd, 0, stdout=f"PUB_{kw['input']}\n", stderr="")
        raise AssertionError(f"unexpected cmd: {cmd}")

    monkeypatch.setattr(peers_mod.subprocess, "run", fake_run)
    return tmp_path


def test_peer_add_writes_bundle_and_appends_to_stores(env, capsys, tmp_path):
    out_dir = tmp_path / "bundle"
    assert cli.main(["peer", "add", "alpha", "--out", str(out_dir)]) == 0

    assert (out_dir / "wg.conf").exists()
    assert (out_dir / "proxy.json").exists()

    wg = (out_dir / "wg.conf").read_text()
    assert "PrivateKey = DEVPRIV" in wg
    assert "PublicKey = SERVERPUB" in wg
    assert "Endpoint = vpn.example.com:51820" in wg
    assert "Address = 10.42.0.2/32" in wg

    pj = json.loads((out_dir / "proxy.json").read_text())
    assert pj["device_id"] == "alpha"
    assert pj["host"] == "10.42.0.1"
    assert pj["port"] == 8420
    assert len(pj["bearer"]) >= 20  # secrets.token_urlsafe(32) gives ~43

    # Server peer block printed to stdout for manual paste
    out = capsys.readouterr().out
    assert "PublicKey = PUB_DEVPRIV" in out
    assert "AllowedIPs = 10.42.0.2/32" in out


def test_peer_add_then_list_then_revoke(env, tmp_path, capsys):
    cli.main(["peer", "add", "alpha", "--out", str(tmp_path / "a")])
    cli.main(["peer", "add", "beta", "--label", "beta box", "--out", str(tmp_path / "b")])

    capsys.readouterr()  # discard add output
    assert cli.main(["peer", "list"]) == 0
    listing = capsys.readouterr().out
    assert "alpha" in listing
    assert "beta" in listing
    assert "10.42.0.2/32" in listing
    assert "10.42.0.3/32" in listing
    assert "active" in listing

    assert cli.main(["peer", "revoke", "alpha"]) == 0

    capsys.readouterr()
    cli.main(["peer", "list"])
    listing = capsys.readouterr().out
    # alpha now shows revoked, beta still active
    lines = [ln for ln in listing.splitlines() if "alpha" in ln or "beta" in ln]
    assert any("alpha" in ln and "revoked" in ln for ln in lines)
    assert any("beta" in ln and "active" in ln for ln in lines)


def test_peer_add_refuses_duplicate(env, tmp_path):
    cli.main(["peer", "add", "alpha", "--out", str(tmp_path / "a")])
    with pytest.raises(SystemExit) as exc:
        cli.main(["peer", "add", "alpha", "--out", str(tmp_path / "a2")])
    assert "exists" in str(exc.value).lower()


def test_peer_add_refuses_invalid_device_id(env, tmp_path):
    with pytest.raises(SystemExit) as exc:
        cli.main(["peer", "add", "UPPER", "--out", str(tmp_path / "x")])
    assert "invalid" in str(exc.value).lower()


def test_peer_add_refuses_unconfigured_server(monkeypatch, tmp_path):
    """If peers.json has empty server_pubkey, peer add should refuse."""
    p = tmp_path / "peers.json"
    p.write_text(json.dumps({
        "version": 1,
        "config": {
            "server_pubkey": "",
            "server_endpoint": "",
            "network_cidr": "10.42.0.0/24",
            "proxy_host": "10.42.0.1",
            "proxy_port": 8420,
            "peers": [],
        },
    }), encoding="utf-8")
    monkeypatch.setenv("CARDPUTER_PROXY_PEERS_PATH", str(p))
    monkeypatch.setenv("CARDPUTER_PROXY_TOKENS_PATH", str(tmp_path / "tokens.json"))
    with pytest.raises(SystemExit) as exc:
        cli.main(["peer", "add", "alpha", "--out", str(tmp_path / "x")])
    assert "peers.json" in str(exc.value)


def test_peer_revoke_unknown_returns_nonzero(env):
    assert cli.main(["peer", "revoke", "ghost"]) == 1


def test_peer_revoke_double_returns_nonzero(env, tmp_path):
    cli.main(["peer", "add", "alpha", "--out", str(tmp_path / "a")])
    assert cli.main(["peer", "revoke", "alpha"]) == 0
    assert cli.main(["peer", "revoke", "alpha"]) == 1
