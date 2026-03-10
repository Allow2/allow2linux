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

# Fetch the latest yarn.lock from GitHub
curl -fsSL -o yarn.lock.tmp \
    "https://raw.githubusercontent.com/Allow2/allow2linux/main/packages/allow2linux/yarn.lock"
echo "    Fetched yarn.lock ($(wc -c < yarn.lock.tmp) bytes)"

# Generate node-sources.json directly from yarn.lock (no pip needed)
python3 -c '
import json, re, sys

with open("yarn.lock.tmp") as f:
    content = f.read()

entries = []
for m in re.finditer(
    r"resolved \"(https://[^\"]+\.tgz)(?:#[^\"]*)?\"\s*\n\s*integrity (sha\d+-\S+)",
    content
):
    url, integrity = m.group(1), m.group(2)
    filename = url.rsplit("/", 1)[-1]
    algo, b64_digest = integrity.split("-", 1)
    # flatpak-builder expects hex, yarn.lock has base64
    import base64
    hex_digest = base64.b64decode(b64_digest).hex()
    entry = {
        "type": "file",
        "url": url,
        algo: hex_digest,
        "dest": "flatpak-node/yarn-mirror",
        "dest-filename": filename,
    }
    entries.append(entry)

if not entries:
    print("ERROR: no packages found in yarn.lock", file=sys.stderr)
    sys.exit(1)

with open("node-sources.json", "w") as f:
    json.dump(entries, f, indent=2)
    f.write("\n")

print(f"    Generated {len(entries)} entries")
'
rm -f yarn.lock.tmp

echo "==> Nuking flatpak-builder cache (all of it)..."
rm -rf .flatpak-builder

echo "==> Building and exporting to local repo..."
flatpak-builder --user --repo=repo --force-clean build "${MANIFEST}"

echo "==> Updating repo appstream metadata..."
flatpak build-update-repo repo

echo "==> Registering local dev remote..."
flatpak remote-add --user --no-gpg-verify --if-not-exists allow2-dev repo

echo "==> Installing from local repo..."
flatpak install --user --reinstall --noninteractive allow2-dev "${APP_ID}"

echo "==> Refreshing Discover appstream cache..."
flatpak update --user --appstream || true

echo "==> Done. Launch from Discover or run:"
echo "    flatpak run ${APP_ID}"
