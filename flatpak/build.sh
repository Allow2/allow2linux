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
#   ./build.sh          # builds .flatpak bundle
#   ./build.sh install  # builds + installs locally
#   ./build.sh deploy   # builds + copies to Steam Deck via SSH
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_ID="com.allow2.allow2linux"
MANIFEST="${SCRIPT_DIR}/${APP_ID}.yml"
BUILD_DIR="${SCRIPT_DIR}/build"
REPO_DIR="${SCRIPT_DIR}/repo"
BUNDLE="${SCRIPT_DIR}/${APP_ID}.flatpak"

# Steam Deck SSH target (override with DECK_HOST env var)
DECK_HOST="${DECK_HOST:-deck@steamdeck.local}"

echo "==> Building ${APP_ID}"

# Build
flatpak-builder \
    --force-clean \
    --repo="${REPO_DIR}" \
    "${BUILD_DIR}" \
    "${MANIFEST}"

echo "==> Creating single-file bundle"

flatpak build-bundle \
    "${REPO_DIR}" \
    "${BUNDLE}" \
    "${APP_ID}"

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
