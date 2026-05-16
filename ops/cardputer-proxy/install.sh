#!/usr/bin/env bash
# Idempotent deploy script for cardputer-proxy. Run on the target host (lobsterboy).
# Re-running upgrades the venv from the current repo HEAD.
set -euo pipefail

REPO_DIR="${REPO_DIR:-$(cd "$(dirname "$0")/../.." && pwd)}"
APP_DIR="/opt/cardputer-proxy"
CFG_DIR="/etc/cardputer-proxy"
SECRETS_DIR="$CFG_DIR/secrets"
LOG_DIR="/var/log/cardputer-proxy"
SVC_USER="cardputer-proxy"
SVC_UNIT_NAME="cardputer-proxy.service"
PYTHON="${PYTHON:-python3.11}"

if [[ $EUID -ne 0 ]]; then
  echo "Run as root: sudo $0" >&2
  exit 1
fi

# 1. System user (idempotent)
if ! id -u "$SVC_USER" >/dev/null 2>&1; then
  useradd --system --home-dir "$APP_DIR" --shell /usr/sbin/nologin "$SVC_USER"
fi

# 2. Directories
install -d -m 0755 -o "$SVC_USER" -g "$SVC_USER" "$APP_DIR" "$LOG_DIR"
install -d -m 0750 -o root        -g "$SVC_USER" "$CFG_DIR"
install -d -m 0700 -o root        -g root         "$SECRETS_DIR"

# 3. Sync source code (exclude .venv so we don't clobber the runtime env,
#    and tests / dev artifacts).
rsync -a --delete \
  --exclude '.venv' --exclude '__pycache__' --exclude 'tests' \
  --exclude '.pytest_cache' --exclude '.ruff_cache' --exclude '.mypy_cache' \
  "$REPO_DIR/proxy/" "$APP_DIR/"
chown -R "$SVC_USER:$SVC_USER" "$APP_DIR"

# 4. venv + deps
if [[ ! -x "$APP_DIR/.venv/bin/python" ]]; then
  sudo -u "$SVC_USER" "$PYTHON" -m venv "$APP_DIR/.venv"
fi
sudo -u "$SVC_USER" "$APP_DIR/.venv/bin/pip" install --quiet --upgrade pip wheel
sudo -u "$SVC_USER" "$APP_DIR/.venv/bin/pip" install --quiet "$APP_DIR"

# 5. Systemd unit
install -m 0644 "$REPO_DIR/ops/cardputer-proxy/cardputer-proxy.service" \
        "/etc/systemd/system/$SVC_UNIT_NAME"

# 6. Reload + start (or restart if already enabled)
systemctl daemon-reload
if systemctl is-enabled --quiet "$SVC_UNIT_NAME"; then
  systemctl restart "$SVC_UNIT_NAME"
else
  systemctl enable --now "$SVC_UNIT_NAME"
fi

systemctl status "$SVC_UNIT_NAME" --no-pager --lines 5
