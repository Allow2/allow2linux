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

echo "==> Generating node-sources.json from yarn.lock..."
# Find the repo root (rebuild-clean.sh may be run from /tmp/flathub-test)
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
YARN_LOCK="${REPO_ROOT}/packages/allow2linux/yarn.lock"

if [ ! -f "${YARN_LOCK}" ]; then
    echo "ERROR: yarn.lock not found at ${YARN_LOCK}"
    echo "Make sure the allow2linux repo is checked out."
    exit 1
fi

if ! command -v flatpak-node-generator &>/dev/null; then
    echo "Installing flatpak-node-generator..."
    pip3 install --user flatpak-node-generator
fi

flatpak-node-generator yarn "${YARN_LOCK}" -o node-sources.json
ENTRIES=$(grep -c '"type"' node-sources.json)
echo "    Generated ${ENTRIES} entries from ${YARN_LOCK}"

echo "==> Nuking flatpak-builder cache..."
rm -rf .flatpak-builder/git .flatpak-builder/build .flatpak-builder/checksums

echo "==> Clean build + install..."
flatpak-builder --user --force-clean --install build "${MANIFEST}"

echo "==> Done. Launch from Discover or run:"
echo "    flatpak run ${APP_ID}"
