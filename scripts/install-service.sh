#!/usr/bin/env bash
#
# install-service.sh — reliable, manual installer for the allow2linux
# `systemd --user` service + auto-update timer + linger.
#
# The daemon attempts all of this automatically on first launch
# (src/first-run.js). This script is the guaranteed fallback: run it once in
# Desktop Mode (Konsole) if first-run couldn't reach the host user manager from
# inside the Flatpak sandbox. Safe to re-run (idempotent).
#
# Usage (on the device, as the normal user — NOT root):
#   ./install-service.sh                 # Flatpak install (default)
#   EXEC_MODE=node ./install-service.sh  # direct-node dev install
#
set -euo pipefail

APP_ID="${APP_ID:-com.allow2.allow2linux}"
EXEC_MODE="${EXEC_MODE:-flatpak}"        # flatpak | node
UNIT_DIR="${HOME}/.config/systemd/user"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="$(cd "${SCRIPT_DIR}/../packages/allow2linux" && pwd 2>/dev/null || echo "")"

mkdir -p "${UNIT_DIR}"

# ── Main service ─────────────────────────────────────────────────────────────
if [ "${EXEC_MODE}" = "node" ]; then
    NODE_BIN="$(command -v node || echo "${HOME}/node/bin/node")"
    INDEX_JS="${PKG_DIR}/src/index.js"
    EXEC_START="${NODE_BIN} ${INDEX_JS}"
else
    EXEC_START="/usr/bin/flatpak run ${APP_ID}"
fi

cat > "${UNIT_DIR}/allow2linux.service" <<UNIT
[Unit]
Description=Allow2 Parental Freedom for Linux
Documentation=https://github.com/Allow2/allow2linux
After=network-online.target graphical-session.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=${EXEC_START}
Restart=always
RestartSec=5
TimeoutStopSec=10

[Install]
WantedBy=default.target
UNIT
echo "==> wrote allow2linux.service (ExecStart=${EXEC_START})"

# ── Auto-update timer (copy the shipped units if present) ─────────────────────
if [ -n "${PKG_DIR}" ] && [ -f "${PKG_DIR}/systemd/allow2linux-update.timer" ]; then
    cp "${PKG_DIR}/systemd/allow2linux-update.service" "${UNIT_DIR}/"
    cp "${PKG_DIR}/systemd/allow2linux-update.timer"   "${UNIT_DIR}/"
    echo "==> installed auto-update timer units"
fi

systemctl --user daemon-reload
systemctl --user enable --now allow2linux.service
if [ -f "${UNIT_DIR}/allow2linux-update.timer" ]; then
    systemctl --user enable --now allow2linux-update.timer
fi

# ── Linger: run at boot without an interactive login ─────────────────────────
if [ ! -e "/var/lib/systemd/linger/${USER}" ]; then
    echo "==> enabling linger for ${USER}"
    loginctl enable-linger "${USER}" || \
        echo "    WARNING: enable-linger failed — run: sudo loginctl enable-linger ${USER}"
else
    echo "==> linger already enabled"
fi

echo ""
echo "==> Done. Status:"
systemctl --user --no-pager status allow2linux.service | head -5 || true
