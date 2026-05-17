from __future__ import annotations

import json
import stat
import subprocess
from pathlib import Path

import pytest

from cardputer_proxy import peers
from cardputer_proxy.peers import PeerStore, PeerStoreError
from cardputer_proxy.schemas import Peer


def _peer(device_id="dev-one", address="10.42.0.2/32", public_key="PUBKEY"):
    return Peer(
        device_id=device_id,
        public_key=public_key,
        address=address,
        created_at=peers.utc_now_iso(),
    )


def test_store_round_trip(tmp_path):
    p = tmp_path / "peers.json"
    s1 = PeerStore(p)
    s1.add(_peer("alpha", "10.42.0.2/32"))
    s1.add(_peer("beta", "10.42.0.3/32"))

    s2 = PeerStore(p)
    assert {x.device_id for x in s2.peers()} == {"alpha", "beta"}


def test_allocate_address_skips_server_one(tmp_path):
    p = tmp_path / "peers.json"
    p.write_text(json.dumps({
        "version": 1,
        "config": {
            "server_pubkey": "X",
            "server_endpoint": "host:51820",
            "network_cidr": "10.42.0.0/24",
            "proxy_host": "10.42.0.1",
            "proxy_port": 8420,
            "peers": [],
        },
    }), encoding="utf-8")
    s = PeerStore(p)
    assert s.allocate_address() == "10.42.0.2/32"
    s.add(_peer("aaa", s.allocate_address()))
    assert s.allocate_address() == "10.42.0.3/32"


def test_allocate_address_pool_exhausted(tmp_path):
    p = tmp_path / "peers.json"
    p.write_text(json.dumps({
        "version": 1,
        "config": {
            "server_pubkey": "X",
            "server_endpoint": "host:51820",
            # /30 leaves 2 host addresses (.1 and .2); .1 reserved, so 1 peer slot.
            "network_cidr": "10.42.0.0/30",
            "proxy_host": "10.42.0.1",
            "proxy_port": 8420,
            "peers": [{
                "device_id": "only",
                "public_key": "X",
                "address": "10.42.0.2/32",
                "label": "",
                "created_at": "2026-05-17T00:00:00Z",
            }],
        },
    }), encoding="utf-8")
    s = PeerStore(p)
    with pytest.raises(PeerStoreError):
        s.allocate_address()


def test_duplicate_device_id_refused(tmp_path):
    s = PeerStore(tmp_path / "peers.json")
    s.add(_peer("dup", "10.42.0.2/32"))
    with pytest.raises(PeerStoreError):
        s.add(_peer("dup", "10.42.0.3/32"))


def test_write_bundle_mode_0600(tmp_path):
    wg = peers.render_wg_conf(
        device_private_key="DEVPRIV",
        device_address="10.42.0.2/32",
        server_public_key="SRVPUB",
        server_endpoint="vpn.example.com:51820",
    )
    pj = peers.render_proxy_json(
        host="10.42.0.1", port=8420, bearer="tok_xxxx", device_id="dev"
    )
    wg_path, pj_path = peers.write_bundle(tmp_path / "out", wg_conf=wg, proxy_json=pj)
    assert wg_path.stat().st_mode & 0o777 == 0o600
    assert pj_path.stat().st_mode & 0o777 == 0o600
    assert "PrivateKey = DEVPRIV" in wg_path.read_text()
    body = json.loads(pj_path.read_text())
    assert body == {
        "host": "10.42.0.1",
        "port": 8420,
        "bearer": "tok_xxxx",
        "device_id": "dev",
        "default_profile_id": "claude-opus",
    }


def test_gen_keypair_uses_wg_binary(monkeypatch):
    calls: list[list[str]] = []

    def fake_run(cmd, **kw):
        calls.append(cmd)
        if cmd == ["wg", "genkey"]:
            return subprocess.CompletedProcess(cmd, 0, stdout="PRIVKEY\n", stderr="")
        if cmd == ["wg", "pubkey"]:
            assert kw.get("input") == "PRIVKEY"
            return subprocess.CompletedProcess(cmd, 0, stdout="PUBKEY\n", stderr="")
        raise AssertionError(f"unexpected cmd: {cmd}")

    monkeypatch.setattr(peers.subprocess, "run", fake_run)
    priv, pub = peers.gen_keypair()
    assert priv == "PRIVKEY"
    assert pub == "PUBKEY"
    assert calls == [["wg", "genkey"], ["wg", "pubkey"]]


def test_gen_keypair_missing_wg_binary(monkeypatch):
    def fake_run(cmd, **kw):
        raise FileNotFoundError

    monkeypatch.setattr(peers.subprocess, "run", fake_run)
    with pytest.raises(PeerStoreError, match="wg binary not found"):
        peers.gen_keypair()


def test_render_server_peer_block_shape():
    block = peers.render_server_peer_block(
        device_id="alpha", public_key="PUB", address="10.42.0.2/32"
    )
    assert "# alpha" in block
    assert "PublicKey = PUB" in block
    assert "AllowedIPs = 10.42.0.2/32" in block
