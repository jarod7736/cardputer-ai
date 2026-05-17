"""Secrets management CLI.

Usage:
    cardputer-proxy secrets list
    cardputer-proxy secrets set <name>           # value on stdin
    cardputer-proxy secrets rotate <name>        # alias for set
    cardputer-proxy secrets delete <name>
    cardputer-proxy secrets show-path <name>     # prints path; never the value

Files live in /etc/cardputer-proxy/secrets/ (override with
$CARDPUTER_PROXY_SECRETS_DIR).
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
    args = parser.parse_args(argv)
    return args.func(args)
