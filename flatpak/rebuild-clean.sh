#!/bin/bash
#
# Clean rebuild for local Flatpak testing on Steam Deck.
# Run from /tmp/flathub-test (or wherever your manifest + sources live).
#
# Usage:
#   cd /tmp/flathub-test
#   bash rebuild-clean.sh
#

set -euo pipefail

APP_ID="com.allow2.allow2linux"

# Auto-detect manifest (yml or yaml)
MANIFEST=""
for f in "${APP_ID}.yml" "${APP_ID}.yaml" "${APP_ID}.json"; do
    if [ -f "$f" ]; then
        MANIFEST="$f"
        break
    fi
done

if [ -z "${MANIFEST}" ]; then
    echo "ERROR: No manifest found. Expected ${APP_ID}.yml in $(pwd)"
    echo "Files here:"
    ls -la
    exit 1
fi

echo "==> Using manifest: ${MANIFEST}"

echo "==> Killing old instances..."
pkill -9 -f allow2linux 2>/dev/null || true
pkill -9 -f allow2-lock-overlay 2>/dev/null || true
fuser -k 3000/tcp 2>/dev/null || true
sleep 2
fuser -k 3000/tcp 2>/dev/null || true

echo "==> Fetching latest node-sources.json from GitHub..."
# Try both possible paths (flathub/ subdir and flatpak/ root)
if curl -fsSL -o node-sources.json \
    "https://raw.githubusercontent.com/Allow2/allow2linux/main/flatpak/flathub/node-sources.json" 2>/dev/null; then
    echo "    Downloaded from flatpak/flathub/ ($(wc -c < node-sources.json) bytes)"
elif curl -fsSL -o node-sources.json \
    "https://raw.githubusercontent.com/Allow2/allow2linux/main/flatpak/node-sources.json" 2>/dev/null; then
    echo "    Downloaded from flatpak/ ($(wc -c < node-sources.json) bytes)"
else
    echo "    WARNING: Could not fetch from GitHub, using local copy"
fi

echo "==> Nuking flatpak-builder cache..."
rm -rf .flatpak-builder/git .flatpak-builder/build .flatpak-builder/checksums

echo "==> Clean build + install..."
flatpak-builder --user --force-clean --install build "${MANIFEST}"

echo "==> Done. Launch from Discover or run:"
echo "    flatpak run ${APP_ID}"
