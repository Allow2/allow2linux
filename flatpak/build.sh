#!/bin/bash
#
# Build allow2linux Flatpak bundle for sideloading onto Steam Deck or other Linux.
#
# Prerequisites (on build machine):
#   brew install flatpak flatpak-builder   # macOS (or apt install on Linux)
#   flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
#   flatpak install flathub org.freedesktop.Platform//23.08 org.freedesktop.Sdk//23.08
#
# Usage:
#   cd allow2linux/flatpak
#   ./build.sh                       # builds .flatpak bundle (production/stable by default)
#   CHANNEL=staging ./build.sh       # builds the beta/staging bundle (targets staging-api)
#   ./build.sh install               # builds + installs locally
#   ./build.sh deploy                # builds + copies to Steam Deck via SSH
#
# CHANNEL selects the endpoint the launcher is baked for (see gen-launcher.sh):
#   production (default) → Flatpak Branch=stable, api.allow2.com (production-locked)
#   staging              → Flatpak Branch=beta,   staging-api.allow2.com (internal test)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_ID="com.allow2.allow2linux"
MANIFEST="${SCRIPT_DIR}/${APP_ID}.yml"
BUILD_DIR="${SCRIPT_DIR}/build"
REPO_DIR="${SCRIPT_DIR}/repo"
BUNDLE="${SCRIPT_DIR}/${APP_ID}.flatpak"

# Channel → branch (see gen-launcher.sh). Default: production/stable (safe default).
CHANNEL="${CHANNEL:-production}"
case "${CHANNEL}" in
    staging|beta)       FLATPAK_BRANCH="beta" ;;
    production|stable)  FLATPAK_BRANCH="stable" ;;
    *) echo "ERROR: unknown CHANNEL='${CHANNEL}' (expected staging|production)"; exit 1 ;;
esac

# Steam Deck SSH target (override with DECK_HOST env var)
DECK_HOST="${DECK_HOST:-deck@steamdeck.local}"

echo "==> Building ${APP_ID}  (channel=${CHANNEL}, branch=${FLATPAK_BRANCH})"

# Bake the channel-specific launcher BEFORE flatpak-builder runs.
CHANNEL="${CHANNEL}" "${SCRIPT_DIR}/gen-launcher.sh"

# Build
flatpak-builder \
    --force-clean \
    --default-branch="${FLATPAK_BRANCH}" \
    --repo="${REPO_DIR}" \
    "${BUILD_DIR}" \
    "${MANIFEST}"

echo "==> Creating single-file bundle"

flatpak build-bundle \
    "${REPO_DIR}" \
    "${BUNDLE}" \
    "${APP_ID}" \
    "${FLATPAK_BRANCH}"

BUNDLE_SIZE=$(du -h "${BUNDLE}" | cut -f1)
echo "==> Bundle created: ${BUNDLE} (${BUNDLE_SIZE})"

case "${1:-}" in
    install)
        echo "==> Installing locally"
        flatpak install --user --bundle -y "${BUNDLE}"
        echo "==> Done. Run with: flatpak run ${APP_ID}"
        ;;
    deploy)
        echo "==> Deploying to Steam Deck (${DECK_HOST})"
        scp "${BUNDLE}" "${DECK_HOST}:~/${APP_ID}.flatpak"
        ssh "${DECK_HOST}" "flatpak install --user --bundle -y ~/${APP_ID}.flatpak && rm ~/${APP_ID}.flatpak"
        echo "==> Deployed. Run on Deck with: flatpak run ${APP_ID}"
        ;;
    bundle)
        echo "==> Bundle ready at: ${BUNDLE}"
        echo "    Copy to device and install with:"
        echo "    flatpak install --user --bundle ${APP_ID}.flatpak"
        ;;
    *)
        echo "==> Bundle ready at: ${BUNDLE}"
        echo ""
        echo "Next steps:"
        echo "  ./build.sh install   — install locally"
        echo "  ./build.sh deploy    — deploy to Steam Deck via USB/SSH"
        echo "  ./build.sh bundle    — just build the bundle file"
        echo ""
        echo "Manual install on device:"
        echo "  scp ${BUNDLE} deck@steamdeck.local:~/"
        echo "  ssh deck@steamdeck.local flatpak install --user --bundle ~/${APP_ID}.flatpak"
        ;;
esac
