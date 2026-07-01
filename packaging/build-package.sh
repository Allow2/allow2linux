#!/usr/bin/env bash
#
# build-package.sh — build a REAL .deb or .rpm for allow2linux from a single
# staged payload tree, using fpm (one spec → both formats).
#
# The package installs:
#   /usr/lib/allow2linux/                 daemon (src, config, systemd, package.json + node_modules)
#   /usr/lib/allow2/allow2-lock-overlay   prebuilt SDL2 overlay binary (per-arch)
#   /usr/lib/allow2/assets/               overlay fonts + logos (Inter, BMPs)
#   /usr/bin/allow2linux                  production-locked launcher (bakes ALLOW2_PRODUCTION=1)
#   /usr/lib/systemd/user/allow2linux.service   systemd --user unit (ExecStart=/usr/bin/allow2linux)
#   /usr/share/applications/…desktop      de-Flatpak'd desktop entry (Exec=allow2linux)
#   /usr/share/icons/hicolor/…            app icons
#   /usr/share/metainfo/…metainfo.xml     AppStream metadata
#
# The daemon's first-run (src/first-run.js) wires the per-user service + linger
# on first launch; the postinstall also `systemctl --global enable`s it.
#
# NOTE [unverified-device]: authored, not run from this environment. The real
# build runs on CI/Linux (see .github/workflows/release.yml). Verified locally
# only with `bash -n`.
#
# ── allow2 SDK RESOLUTION (DECIDED: git-ref, alpha phase) ────────────────────
# The daemon's `allow2` dep is pulled from GIT, not npm, during alpha:
# packages/allow2linux/package.json declares
#   "allow2": "github:Allow2/allow2node#v2.0.0-alpha"
# (the published allow2@alpha predates the staging-guard + offline changes; the
# git branch has them). So this script resolves deps with `npm install` (NOT
# `npm ci`) against package.json's git-ref. We copy ONLY package.json into the
# stage (never the stale monorepo package-lock.json, whose file: path is wrong).
#
# PREREQS (build env): `git` + network — npm clones the repo. If Allow2/allow2node
# is PRIVATE, the git-install needs a token; the CI jobs set one from
# secrets.SDK_GIT_TOKEN via `git config url.insteadOf`. FUTURE: when v2 lands on
# npm `latest`, flip the dep to "allow2": "^2.0.0" (one-line change). See
# docs/DISTRIBUTION.md.
#
# Usage:
#   packaging/build-package.sh --format deb|rpm --arch x86_64|aarch64 \
#       --overlay /path/to/prebuilt/allow2-lock-overlay \
#       --version 1.0.0-alpha.1 [--out dist/]
#
set -euo pipefail

FORMAT=""
ARCH=""
OVERLAY_BIN=""
VERSION=""
OUT_DIR="dist"

while [ $# -gt 0 ]; do
    case "$1" in
        --format)  FORMAT="$2"; shift 2 ;;
        --arch)    ARCH="$2"; shift 2 ;;
        --overlay) OVERLAY_BIN="$2"; shift 2 ;;
        --version) VERSION="$2"; shift 2 ;;
        --out)     OUT_DIR="$2"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

[ -n "${FORMAT}" ]      || { echo "ERROR: --format deb|rpm required" >&2; exit 2; }
[ -n "${ARCH}" ]        || { echo "ERROR: --arch x86_64|aarch64 required" >&2; exit 2; }
[ -n "${OVERLAY_BIN}" ] || { echo "ERROR: --overlay <prebuilt binary> required" >&2; exit 2; }
[ -n "${VERSION}" ]     || { echo "ERROR: --version <semver> required" >&2; exit 2; }
[ -f "${OVERLAY_BIN}" ] || { echo "ERROR: overlay binary not found: ${OVERLAY_BIN}" >&2; exit 2; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
COMMON="${SCRIPT_DIR}/common"
DAEMON_SRC="${REPO_ROOT}/packages/allow2linux"
OVERLAY_SRC="${REPO_ROOT}/packages/allow2-lock-overlay"
DATA_SRC="${REPO_ROOT}/data"

# ── Map our arch names to per-format package arch names ──────────────────────
case "${FORMAT}:${ARCH}" in
    deb:x86_64)  PKG_ARCH="amd64" ;;
    deb:aarch64) PKG_ARCH="arm64" ;;
    rpm:x86_64)  PKG_ARCH="x86_64" ;;
    rpm:aarch64) PKG_ARCH="aarch64" ;;
    *) echo "ERROR: unsupported --format/--arch: ${FORMAT}/${ARCH}" >&2; exit 2 ;;
esac

# ── Runtime dependencies (SDL2 + X11 for the overlay, Node >= 18 for daemon) ──
# Package names differ deb vs rpm.
if [ "${FORMAT}" = "deb" ]; then
    DEPS=( "nodejs (>= 18.0.0)" "libsdl2-2.0-0" "libsdl2-ttf-2.0-0" "libx11-6" )
else
    DEPS=( "nodejs >= 18.0.0" "SDL2" "SDL2_ttf" "libX11" )
fi

echo "==> Building ${FORMAT} for ${ARCH} (pkg-arch ${PKG_ARCH}) v${VERSION}"

STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT

# ── /usr/lib/allow2linux — the daemon ────────────────────────────────────────
DEST_DAEMON="${STAGE}/usr/lib/allow2linux"
mkdir -p "${DEST_DAEMON}"
cp -r "${DAEMON_SRC}/src" "${DAEMON_SRC}/config" "${DAEMON_SRC}/systemd" \
      "${DAEMON_SRC}/package.json" "${DEST_DAEMON}/"

# Install production deps into the staged daemon. `npm install` (NOT `npm ci`)
# so the allow2 git-ref in package.json is fetched fresh; no lockfile is staged.
echo "==> Installing daemon deps (allow2 SDK via git-ref in package.json)"
command -v git >/dev/null 2>&1 || { echo "ERROR: git is required to install the allow2 git-ref" >&2; exit 1; }
( cd "${DEST_DAEMON}" && npm install --omit=dev --no-optional )

# ── /usr/lib/allow2 — overlay binary + assets (path matches overlay-bridge.js) ─
DEST_OVERLAY="${STAGE}/usr/lib/allow2"
mkdir -p "${DEST_OVERLAY}/assets"
install -m 755 "${OVERLAY_BIN}" "${DEST_OVERLAY}/allow2-lock-overlay"
# Overlay resolves fonts/logos as <binary_dir>/assets (src/main.c resolve_assets_path).
cp "${OVERLAY_SRC}/assets/Inter-Regular.ttf" \
   "${OVERLAY_SRC}/assets/Inter-Bold.ttf" \
   "${OVERLAY_SRC}/assets/allow2-logo-128.bmp" \
   "${OVERLAY_SRC}/assets/allow2-icon-64.bmp" \
   "${DEST_OVERLAY}/assets/"

# ── /usr/bin/allow2linux — production-locked launcher ────────────────────────
mkdir -p "${STAGE}/usr/bin"
install -m 755 "${COMMON}/allow2linux.launcher.sh" "${STAGE}/usr/bin/allow2linux"

# ── /usr/lib/systemd/user — the systemd --user unit ──────────────────────────
mkdir -p "${STAGE}/usr/lib/systemd/user"
# Strip the leading comment block? Keep it — systemd ignores '#' lines.
install -m 644 "${COMMON}/allow2linux.service" "${STAGE}/usr/lib/systemd/user/allow2linux.service"

# ── Desktop entry + metainfo + icons ─────────────────────────────────────────
mkdir -p "${STAGE}/usr/share/applications" "${STAGE}/usr/share/metainfo"
install -m 644 "${COMMON}/com.allow2.allow2linux.desktop" \
        "${STAGE}/usr/share/applications/com.allow2.allow2linux.desktop"
install -m 644 "${DATA_SRC}/com.allow2.allow2linux.metainfo.xml" \
        "${STAGE}/usr/share/metainfo/com.allow2.allow2linux.metainfo.xml"

# Icons — scalable SVG + the rasterised PNGs present in data/.
for sz in scalable 64x64 128x128 256x256 512x512; do
    if [ "${sz}" = "scalable" ]; then
        SRC_ICON="${DATA_SRC}/icons/hicolor/scalable/apps/com.allow2.allow2linux.svg"
        EXT="svg"
    else
        SRC_ICON="${DATA_SRC}/icons/hicolor/${sz}/apps/com.allow2.allow2linux.png"
        EXT="png"
    fi
    if [ -f "${SRC_ICON}" ]; then
        DEST_ICON="${STAGE}/usr/share/icons/hicolor/${sz}/apps"
        mkdir -p "${DEST_ICON}"
        install -m 644 "${SRC_ICON}" "${DEST_ICON}/com.allow2.allow2linux.${EXT}"
    fi
done

# ── fpm invocation (one spec → both formats via --format) ────────────────────
mkdir -p "${OUT_DIR}"

# fpm wants dependencies as repeated --depends flags.
DEP_ARGS=()
for d in "${DEPS[@]}"; do
    DEP_ARGS+=( --depends "${d}" )
done

# deb-only flags.
FMT_ARGS=()
if [ "${FORMAT}" = "deb" ]; then
    FMT_ARGS+=( --deb-no-default-config-files )
fi

fpm \
    --input-type dir \
    --output-type "${FORMAT}" \
    --name allow2linux \
    --version "${VERSION}" \
    --architecture "${PKG_ARCH}" \
    --maintainer "Allow2 Pty Ltd <support@allow2.com>" \
    --vendor "Allow2 Pty Ltd" \
    --license "MIT" \
    --url "https://github.com/Allow2/allow2linux" \
    --description "Allow2 Parental Freedom for Linux — Steam Deck, desktops, and more.
Activity-aware parental freedom: per-activity quotas, day-types, progressive
warnings, request-more-time, and a fullscreen lock overlay. The Allow2 cloud is
the authority; this agent consumes it." \
    "${DEP_ARGS[@]}" \
    --after-install "${COMMON}/postinstall.sh" \
    --after-remove "${COMMON}/postremove.sh" \
    "${FMT_ARGS[@]}" \
    --package "${OUT_DIR}/allow2linux_${VERSION}_${ARCH}.${FORMAT}" \
    -C "${STAGE}" \
    usr

echo "==> Wrote ${OUT_DIR}/allow2linux_${VERSION}_${ARCH}.${FORMAT}"
