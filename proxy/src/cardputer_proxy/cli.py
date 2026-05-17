"""Ops CLI: secrets + peer management.

Usage:
    cardputer-proxy secrets list
    cardputer-proxy secrets set <name>           # value on stdin
    cardputer-proxy secrets rotate <name>        # alias for set
    cardputer-proxy secrets delete <name>
    cardputer-proxy secrets show-path <name>     # prints path; never the value

    cardputer-proxy peer add <device-id> [--label "..."] [--out DIR]
    cardputer-proxy peer list
    cardputer-proxy peer revoke <device-id>

Secrets live in /etc/cardputer-proxy/secrets/ (override with
$CARDPUTER_PROXY_SECRETS_DIR). Peer registry lives in
/etc/cardputer-proxy/peers.json (override with $CARDPUTER_PROXY_PEERS_PATH).
Tokens live in /etc/cardputer-proxy/tokens.json (override with
$CARDPUTER_PROXY_TOKENS_PATH).
"""

from __future__ import annotations

import argparse
import os
import re
import secrets
import stat
import sys
from pathlib import Path

from cardputer_proxy import peers as peers_mod
from cardputer_proxy.peers import PeerStore, PeerStoreError
from cardputer_proxy.schemas import Peer, Token
from cardputer_proxy.tokens import TokenStore, TokenStoreError


_VALID = re.compile(r"^[a-zA-Z0-9_.-]+$")
_DEVICE_ID_RE = re.compile(r"^[a-z0-9-]{3,32}$")


def _peers_path() -> Path:
    return Path(
        os.environ.get(
            "CARDPUTER_PROXY_PEERS_PATH", "/etc/cardputer-proxy/peers.json"
        )
    )


def _tokens_path() -> Path:
    return Path(
        os.environ.get(
            "CARDPUTER_PROXY_TOKENS_PATH", "/etc/cardputer-proxy/tokens.json"
        )
    )


def _secrets_dir() -> Path:
    return Path(
        os.environ.get(
            "CARDPUTER_PROXY_SECRETS_DIR", "/etc/cardputer-proxy/secrets"
        )
    )


def _validate_name(name: str) -> None:
    if not name or not _VALID.match(name):
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
    # 0640 root:cardputer-proxy so the service user can read; rest of
    # the world can't. chown only works when running as root, which the
    # service install expects.
    p.chmod(stat.S_IRUSR | stat.S_IWUSR | stat.S_IRGRP)  # 0640
    try:
        import grp
        gid = grp.getgrnam("cardputer-proxy").gr_gid
        os.chown(p, 0, gid)
    except (KeyError, PermissionError):
        # Group doesn't exist or we're not root (test runs). Leave as is.
        pass
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


# ---------------------------------------------------------------------------
# peer subcommands
# ---------------------------------------------------------------------------


def _validate_device_id(device_id: str) -> None:
    if not _DEVICE_ID_RE.match(device_id):
        sys.exit(f"invalid device-id (^[a-z0-9-]{{3,32}}$): {device_id!r}")


def cmd_peer_add(args: argparse.Namespace) -> int:
    _validate_device_id(args.device_id)
    peer_store = PeerStore(_peers_path())
    cfg = peer_store.config
    if not cfg.server_pubkey or not cfg.server_endpoint:
        sys.exit(
            "peers.json: server_pubkey/server_endpoint unset. "
            "Edit /etc/cardputer-proxy/peers.json first."
        )
    if peer_store.get(args.device_id) is not None:
        sys.exit(f"device_id already exists: {args.device_id}")

    priv, pub = peers_mod.gen_keypair()
    address = peer_store.allocate_address()
    bearer = secrets.token_urlsafe(32)
    now = peers_mod.utc_now_iso()

    # Tokens first — if it fails we haven't allocated the address yet
    # in the on-disk peer list.
    token_store = TokenStore(_tokens_path())
    try:
        token_store.add(Token(
            device_id=args.device_id,
            token=bearer,
            label=args.label or args.device_id,
            created_at=now,
        ))
    except TokenStoreError as e:
        sys.exit(f"failed to write token: {e}")

    try:
        peer_store.add(Peer(
            device_id=args.device_id,
            public_key=pub,
            address=address,
            label=args.label or args.device_id,
            created_at=now,
        ))
    except PeerStoreError as e:
        # Roll back the token write so peer_add is atomic.
        token_store.revoke(args.device_id)
        sys.exit(f"failed to write peer: {e}")

    wg_conf = peers_mod.render_wg_conf(
        device_private_key=priv,
        device_address=address,
        server_public_key=cfg.server_pubkey,
        server_endpoint=cfg.server_endpoint,
    )
    proxy_json = peers_mod.render_proxy_json(
        host=cfg.proxy_host or address.split("/")[0],  # default to peer's own IP if proxy_host unset
        port=cfg.proxy_port,
        bearer=bearer,
        device_id=args.device_id,
    )

    out_dir = Path(args.out or f"./out/{args.device_id}")
    wg_path, pj_path = peers_mod.write_bundle(
        out_dir, wg_conf=wg_conf, proxy_json=proxy_json
    )

    print(f"wrote {wg_path}", file=sys.stderr)
    print(f"wrote {pj_path}", file=sys.stderr)
    print("", file=sys.stderr)
    print("paste this [Peer] block into the WireGuard server config:", file=sys.stderr)
    print("", file=sys.stderr)
    print(peers_mod.render_server_peer_block(
        device_id=args.device_id,
        public_key=pub,
        address=address,
    ))
    return 0


def cmd_peer_list(args: argparse.Namespace) -> int:
    peer_store = PeerStore(_peers_path())
    token_store = TokenStore(_tokens_path())
    rows = []
    for p in peer_store.peers():
        t = token_store.get(p.device_id)
        rows.append((
            p.device_id,
            (p.label or "")[:20],
            p.address,
            "revoked" if (t and t.revoked) else "active",
            p.created_at,
        ))
    if not rows:
        print("(no peers)")
        return 0
    widths = [max(len(r[i]) for r in rows) for i in range(5)]
    headers = ("device-id", "label", "address", "status", "created")
    widths = [max(w, len(h)) for w, h in zip(widths, headers)]
    fmt = "  ".join(f"{{:{w}}}" for w in widths)
    print(fmt.format(*headers))
    print(fmt.format(*("-" * w for w in widths)))
    for r in rows:
        print(fmt.format(*r))
    return 0


def cmd_peer_revoke(args: argparse.Namespace) -> int:
    _validate_device_id(args.device_id)
    token_store = TokenStore(_tokens_path())
    if token_store.revoke(args.device_id):
        print(f"revoked: {args.device_id}")
        print("note: WG peer entry is NOT removed from the server. "
              "Remove it manually if you want to drop network reach.",
              file=sys.stderr)
        return 0
    print(f"no active token for: {args.device_id}", file=sys.stderr)
    return 1


# ---------------------------------------------------------------------------
# top-level dispatcher
# ---------------------------------------------------------------------------


def _build_secrets_parser(sub_root) -> None:
    p = sub_root.add_parser("secrets")
    sub = p.add_subparsers(dest="secrets_cmd", required=True)
    sub.add_parser("list").set_defaults(func=cmd_list)
    p_set = sub.add_parser("set")
    p_set.add_argument("name")
    p_set.set_defaults(func=cmd_set)
    p_rot = sub.add_parser("rotate")
    p_rot.add_argument("name")
    p_rot.set_defaults(func=cmd_set)
    p_del = sub.add_parser("delete")
    p_del.add_argument("name")
    p_del.set_defaults(func=cmd_delete)
    p_sho = sub.add_parser("show-path")
    p_sho.add_argument("name")
    p_sho.set_defaults(func=cmd_show_path)


def _build_peer_parser(sub_root) -> None:
    p = sub_root.add_parser("peer")
    sub = p.add_subparsers(dest="peer_cmd", required=True)
    p_add = sub.add_parser("add")
    p_add.add_argument("device_id")
    p_add.add_argument("--label", default="")
    p_add.add_argument("--out", default=None,
                       help="bundle output dir (default ./out/<device-id>)")
    p_add.set_defaults(func=cmd_peer_add)
    sub.add_parser("list").set_defaults(func=cmd_peer_list)
    p_rev = sub.add_parser("revoke")
    p_rev.add_argument("device_id")
    p_rev.set_defaults(func=cmd_peer_revoke)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="cardputer-proxy")
    sub = parser.add_subparsers(dest="cmd", required=True)
    _build_secrets_parser(sub)
    _build_peer_parser(sub)

    # Backward-compat: accept legacy "list / set / etc." top-level forms
    # by treating them as `secrets <cmd>` so existing M4 muscle memory
    # still works. Detected by first positional not being secrets|peer.
    if argv is None:
        argv = sys.argv[1:]
    if argv and argv[0] not in ("secrets", "peer", "-h", "--help"):
        argv = ["secrets", *argv]

    args = parser.parse_args(argv)
    return args.func(args)
