#!/usr/bin/env bash
#
# install.sh — the PRIMARY curl|bash installer for allow2linux.
#
#   curl -sSL https://get.allow2.com/linux | bash
#
# Parent-runnable, no root required for the default user-local install. It:
#   1. detects the CPU arch (x86_64 / aarch64),
#   2. bootstraps a Node >= 18 runtime into the user's data dir if none is present
#      (no host Docker needed),
#   3. downloads the prebuilt per-arch overlay binary + the daemon tarball,
#   4. lays them out under the user's data dir mirroring the repo `packages/`
#      layout (so the daemon's overlay-bridge finds the binary with no code change),
#   5. writes + enables the `systemd --user` unit, enables linger, and starts the
#      daemon so pairing can begin.
#
# The daemon endpoint is PRODUCTION-LOCKED: the launcher this script writes bakes
# ALLOW2_PRODUCTION=1 (the SDK guard), so this public installer can never attach
# to staging. Beta/staging ships only via the Flatpak `staging` prefix.
#
# NOTE [unverified-device]: authored, not run from this environment. The download
# targets are published by .github/workflows/release.yml on a semver tag. Verify
# on a real Linux host / Steam Deck. Locally checked with `bash -n` only.
#
set -euo pipefail

# ── Config (overridable via env) ─────────────────────────────────────────────
# Assets are attached to the GitHub Release; `latest/download/<asset>` is a
# stable URL GitHub maintains. `https://get.allow2.com/linux` is a Cloudflare
# redirect to THIS script's latest release asset (see docs/DISTRIBUTION.md); the
# script then pulls its payload from the same release via BASE_URL below.
REPO="${ALLOW2_REPO:-Allow2/allow2linux}"
BASE_URL="${ALLOW2_LINUX_BASE:-https://github.com/${REPO}/releases/latest/download}"
NODE_VERSION="${ALLOW2_NODE_VERSION:-20.18.0}"

DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}/allow2linux"
DAEMON_DIR="${DATA_HOME}/packages/allow2linux"
OVERLAY_DIR="${DATA_HOME}/packages/allow2-lock-overlay"
NODE_DIR="${DATA_HOME}/node"
BIN_DIR="${DATA_HOME}/bin"
UNIT_DIR="${HOME}/.config/systemd/user"

say()  { printf '==> %s\n' "$*"; }
warn() { printf 'WARNING: %s\n' "$*" >&2; }
die()  { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1; }

# ── 1. Detect arch ───────────────────────────────────────────────────────────
RAW_ARCH="$(uname -m)"
case "${RAW_ARCH}" in
    x86_64|amd64)   ARCH="x86_64"; NODE_ARCH="x64" ;;
    aarch64|arm64)  ARCH="aarch64"; NODE_ARCH="arm64" ;;
    *) die "unsupported architecture: ${RAW_ARCH} (need x86_64 or aarch64)" ;;
esac
say "architecture: ${ARCH}"

need curl || die "curl is required"
need tar  || die "tar is required"

mkdir -p "${DATA_HOME}" "${DAEMON_DIR}" "${OVERLAY_DIR}" "${BIN_DIR}" "${UNIT_DIR}"

# ── 2. Resolve / bootstrap Node >= 18 ────────────────────────────────────────
node_ok() {
    local n="$1"
    "$n" -e 'process.exit(parseInt(process.versions.node) >= 18 ? 0 : 1)' >/dev/null 2>&1
}

NODE_BIN=""
if need node && node_ok node; then
    NODE_BIN="$(command -v node)"
    say "using system Node: ${NODE_BIN} ($(node -v))"
elif [ -x "${NODE_DIR}/bin/node" ] && node_ok "${NODE_DIR}/bin/node"; then
    NODE_BIN="${NODE_DIR}/bin/node"
    say "using bootstrapped Node: ${NODE_BIN}"
else
    say "no suitable Node found — bootstrapping Node ${NODE_VERSION} into ${NODE_DIR}"
    NODE_PKG="node-v${NODE_VERSION}-linux-${NODE_ARCH}"
    NODE_TARBALL="${NODE_PKG}.tar.gz"
    NODE_URL="https://nodejs.org/dist/v${NODE_VERSION}/${NODE_TARBALL}"
    TMP_NODE="$(mktemp -d)"
    curl -fsSL "${NODE_URL}" -o "${TMP_NODE}/${NODE_TARBALL}" \
        || die "failed to download Node from ${NODE_URL}"
    tar -xzf "${TMP_NODE}/${NODE_TARBALL}" -C "${TMP_NODE}"
    rm -rf "${NODE_DIR}"
    mv "${TMP_NODE}/${NODE_PKG}" "${NODE_DIR}"
    rm -rf "${TMP_NODE}"
    NODE_BIN="${NODE_DIR}/bin/node"
    node_ok "${NODE_BIN}" || die "bootstrapped Node is unusable"
    say "bootstrapped Node: $(${NODE_BIN} -v)"
fi

# ── 3. Download daemon + overlay payloads ────────────────────────────────────
# release.yml publishes:
#   allow2linux-daemon.tar.gz            (src, config, systemd, package.json, node_modules)
#   allow2-lock-overlay-<arch>.tar.gz    (binary + assets/)
TMP_DL="$(mktemp -d)"
trap 'rm -rf "${TMP_DL}"' EXIT

say "downloading daemon…"
curl -fsSL "${BASE_URL}/allow2linux-daemon.tar.gz" -o "${TMP_DL}/daemon.tar.gz" \
    || die "failed to download daemon from ${BASE_URL}/allow2linux-daemon.tar.gz"

say "downloading overlay (${ARCH})…"
curl -fsSL "${BASE_URL}/allow2-lock-overlay-${ARCH}.tar.gz" -o "${TMP_DL}/overlay.tar.gz" \
    || die "failed to download overlay from ${BASE_URL}/allow2-lock-overlay-${ARCH}.tar.gz"

# ── 4. Lay out under the user data dir (mirrors repo packages/ layout) ───────
# overlay-bridge.js resolves the binary at <daemon>/../../allow2-lock-overlay/
# allow2-lock-overlay — the repo layout — so mirroring it here needs NO code
# change. The overlay resolves its fonts as <binary_dir>/assets.
say "installing daemon → ${DAEMON_DIR}"
rm -rf "${DAEMON_DIR}"; mkdir -p "${DAEMON_DIR}"
tar -xzf "${TMP_DL}/daemon.tar.gz" -C "${DAEMON_DIR}"

say "installing overlay → ${OVERLAY_DIR}"
rm -rf "${OVERLAY_DIR}"; mkdir -p "${OVERLAY_DIR}"
tar -xzf "${TMP_DL}/overlay.tar.gz" -C "${OVERLAY_DIR}"
chmod 755 "${OVERLAY_DIR}/allow2-lock-overlay" 2>/dev/null || true

# ── Production-locked launcher (bakes ALLOW2_PRODUCTION=1) ────────────────────
LAUNCHER="${BIN_DIR}/allow2linux"
cat > "${LAUNCHER}" <<EOF
#!/bin/sh
# GENERATED by scripts/install.sh — production-locked. DO NOT EDIT.
export ALLOW2_PRODUCTION=1
export NODE_ENV=production
export ALLOW2_VID=21599
export ALLOW2_TOKEN=x9AUeUPpiweHTNCR
exec "${NODE_BIN}" "${DAEMON_DIR}/src/index.js" "\$@"
EOF
chmod 755 "${LAUNCHER}"
say "launcher: ${LAUNCHER} (production-locked)"

# ── 5. systemd --user unit + enable + linger ─────────────────────────────────
cat > "${UNIT_DIR}/allow2linux.service" <<EOF
[Unit]
Description=Allow2 Parental Freedom for Linux
Documentation=https://github.com/${REPO}
After=network-online.target graphical-session.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=${LAUNCHER}
Restart=always
RestartSec=5
TimeoutStopSec=10

[Install]
WantedBy=default.target
EOF
say "wrote ${UNIT_DIR}/allow2linux.service"

# Pre-set the daemon's first-run marker so first-run does NOT overwrite our
# production-locked unit with a node-direct ExecStart (which would drop the prod
# lock). We do linger + enable here instead — the exact steps first-run performs.
mkdir -p "${HOME}/.allow2"
: > "${HOME}/.allow2/.setup-done"

if need systemctl; then
    systemctl --user daemon-reload 2>/dev/null || warn "systemctl --user daemon-reload failed (no user bus?)"
    systemctl --user enable --now allow2linux.service 2>/dev/null \
        || warn "could not enable the service — start it in a graphical session with: systemctl --user enable --now allow2linux.service"
else
    warn "systemctl not found — this system may not use systemd; start the daemon manually: ${LAUNCHER}"
fi

if need loginctl; then
    if [ ! -e "/var/lib/systemd/linger/${USER}" ]; then
        loginctl enable-linger "${USER}" 2>/dev/null \
            || warn "enable-linger failed — run: sudo loginctl enable-linger ${USER}"
    fi
fi

cat <<EOF

==> allow2linux installed (production channel).

Next: launch the app and pair with the Allow2 phone app.
  - The daemon is running as a systemd --user service.
  - Open the Allow2 app on your phone and scan the QR (or enter the 6-digit PIN).

Status / control:
  systemctl --user status allow2linux.service
  systemctl --user restart allow2linux.service

Uninstall:
  systemctl --user disable --now allow2linux.service
  rm -rf "${DATA_HOME}" "${UNIT_DIR}/allow2linux.service" "${HOME}/.allow2/.setup-done"
EOF
