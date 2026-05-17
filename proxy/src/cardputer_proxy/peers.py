"""WG keypair generation, peer registry, bundle writer.

Server-side config lives in peers.json — server pubkey + endpoint + the
network CIDR pool we allocate from, plus the list of provisioned peers.
The on-disk format is a single JSON object (versioned via SCHEMA_VERSION).

Bundle writer produces two files per device, intended to land on the root
of a microSD card the device reads on first boot:

  wg.conf      — standard WireGuard text format
  proxy.json   — device-side proxy contact info + its bearer token
"""

from __future__ import annotations

import ipaddress
import json
import os
import stat
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path

from cardputer_proxy.schemas import Peer, PeersFile


SCHEMA_VERSION = 1


class PeerStoreError(RuntimeError):
    pass


class PeerStore:
    def __init__(self, path: Path):
        self._path = path
        self._data: PeersFile = PeersFile()
        self._load()

    def _load(self) -> None:
        if not self._path.exists():
            return
        try:
            raw = json.loads(self._path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as e:
            raise PeerStoreError(f"failed to read {self._path}: {e}") from e
        if raw.get("version") != SCHEMA_VERSION:
            raise PeerStoreError(
                f"unsupported peers schema version {raw.get('version')!r}"
            )
        self._data = PeersFile.model_validate(raw.get("config", {}))

    def _save(self) -> None:
        out = {"version": SCHEMA_VERSION, "config": self._data.model_dump()}
        body = json.dumps(out, indent=2, ensure_ascii=False) + "\n"
        directory = self._path.parent
        directory.mkdir(parents=True, exist_ok=True)
        tmp_name = None
        try:
            with tempfile.NamedTemporaryFile(
                "w",
                dir=directory,
                prefix=".peers.",
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
            raise PeerStoreError(f"failed to write {self._path}: {e}") from e

    @property
    def config(self) -> PeersFile:
        return self._data

    def peers(self) -> list[Peer]:
        return list(self._data.peers)

    def get(self, device_id: str) -> Peer | None:
        for p in self._data.peers:
            if p.device_id == device_id:
                return p
        return None

    def add(self, peer: Peer) -> None:
        if self.get(peer.device_id) is not None:
            raise PeerStoreError(f"device_id already exists: {peer.device_id}")
        previous = list(self._data.peers)
        self._data.peers.append(peer)
        try:
            self._save()
        except PeerStoreError:
            self._data.peers = previous
            raise

    def allocate_address(self) -> str:
        """Pick the next unused host address in network_cidr.

        Reserves the first host (commonly the server). Raises if the pool
        is empty.
        """
        cidr = self._data.network_cidr
        if not cidr:
            raise PeerStoreError("peers.json: network_cidr is unset")
        net = ipaddress.ip_network(cidr, strict=False)
        used = {ipaddress.ip_interface(p.address).ip for p in self._data.peers}
        first = True
        for host in net.hosts():
            if first:
                first = False
                continue  # skip .1 (server)
            if host not in used:
                # Return as a CIDR-style interface (host/32 for v4, /128 for v6).
                bits = 32 if isinstance(host, ipaddress.IPv4Address) else 128
                return f"{host}/{bits}"
        raise PeerStoreError(f"no free addresses in {cidr}")


# ---------------------------------------------------------------------------
# WG keypair generation
# ---------------------------------------------------------------------------


def gen_keypair() -> tuple[str, str]:
    """Run `wg genkey | wg pubkey` and return (private, public).

    Requires the `wireguard-tools` package on PATH; the same package M1
    used to mint the server keypair.
    """
    try:
        priv = subprocess.run(
            ["wg", "genkey"], check=True, capture_output=True, text=True
        ).stdout.strip()
        pub = subprocess.run(
            ["wg", "pubkey"], check=True, capture_output=True, text=True, input=priv
        ).stdout.strip()
    except FileNotFoundError as e:
        raise PeerStoreError("wg binary not found on PATH (install wireguard-tools)") from e
    except subprocess.CalledProcessError as e:
        raise PeerStoreError(f"wg keypair generation failed: {e.stderr.strip()}") from e
    if not priv or not pub:
        raise PeerStoreError("wg returned empty keypair")
    return priv, pub


# ---------------------------------------------------------------------------
# Bundle writer
# ---------------------------------------------------------------------------


def _write_0600(path: Path, body: str) -> None:
    path.write_text(body, encoding="utf-8")
    path.chmod(stat.S_IRUSR | stat.S_IWUSR)


def render_wg_conf(
    *,
    device_private_key: str,
    device_address: str,
    server_public_key: str,
    server_endpoint: str,
    allowed_ips: str = "0.0.0.0/0",
    persistent_keepalive: int = 25,
    mtu: int = 1280,
) -> str:
    return (
        "[Interface]\n"
        f"PrivateKey = {device_private_key}\n"
        f"Address = {device_address}\n"
        f"MTU = {mtu}\n"
        "\n"
        "[Peer]\n"
        f"PublicKey = {server_public_key}\n"
        f"AllowedIPs = {allowed_ips}\n"
        f"Endpoint = {server_endpoint}\n"
        f"PersistentKeepalive = {persistent_keepalive}\n"
    )


def render_proxy_json(
    *,
    host: str,
    port: int,
    bearer: str,
    device_id: str,
    default_profile_id: str = "claude-opus",
) -> str:
    body = {
        "host": host,
        "port": port,
        "bearer": bearer,
        "device_id": device_id,
        "default_profile_id": default_profile_id,
    }
    return json.dumps(body, indent=2) + "\n"


def write_bundle(
    out_dir: Path,
    *,
    wg_conf: str,
    proxy_json: str,
) -> tuple[Path, Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    wg_path = out_dir / "wg.conf"
    pj_path = out_dir / "proxy.json"
    _write_0600(wg_path, wg_conf)
    _write_0600(pj_path, proxy_json)
    return wg_path, pj_path


def render_server_peer_block(
    *,
    device_id: str,
    public_key: str,
    address: str,
) -> str:
    """Text the user pastes into the WireGuard server config."""
    return (
        f"# {device_id}\n"
        "[Peer]\n"
        f"PublicKey = {public_key}\n"
        f"AllowedIPs = {address}\n"
    )


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
