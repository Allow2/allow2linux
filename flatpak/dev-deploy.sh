#!/bin/bash
#
# Fast dev deployment to Steam Deck over USB/SSH — no Flatpak build needed.
# Syncs source files directly and restarts the daemon.
#
# Usage:
#   ./dev-deploy.sh                          # one-shot sync + restart
#   ./dev-deploy.sh watch                    # continuous sync on file changes
#   DECK_HOST=192.168.100.2 ./dev-deploy.sh  # custom IP
#
# Prerequisites:
#   - SSH access to Steam Deck (enable in Settings → Developer)
#   - Node.js installed on Deck (see README)
#   - For watch mode: brew install fswatch (macOS) or inotify-tools (Linux)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# SDK location: check common relative paths, or override with SDK_ROOT env var
if [ -n "${SDK_ROOT:-}" ]; then
    : # already set by caller
elif [ -d "${PROJECT_ROOT}/../sdk/node" ]; then
    SDK_ROOT="$(cd "${PROJECT_ROOT}/../sdk/node" && pwd)"
elif [ -d "${PROJECT_ROOT}/../../sdk/node" ]; then
    SDK_ROOT="$(cd "${PROJECT_ROOT}/../../sdk/node" && pwd)"
else
    echo "ERROR: Cannot find allow2 SDK. Set SDK_ROOT=/path/to/sdk/node"
    exit 1
fi

DECK_HOST="${DECK_HOST:-deck@steamdeck.local}"
REMOTE_DIR="~/allow2"

NODE_VERSION="${NODE_VERSION:-v20.18.0}"

echo "==> Project root: ${PROJECT_ROOT}"
echo "==> SDK root: ${SDK_ROOT}"

ensure_node() {
    echo "==> Checking Node.js on Deck..."
    if ssh "${DECK_HOST}" "command -v node &>/dev/null"; then
        local remote_ver
        remote_ver=$(ssh "${DECK_HOST}" "node --version")
        echo "    Node.js ${remote_ver} already installed"
        return 0
    fi

    echo "==> Installing Node.js ${NODE_VERSION} to ~/node on Deck..."
    ssh "${DECK_HOST}" bash -s "${NODE_VERSION}" <<'INSTALL_EOF'
        set -e
        NODE_VERSION="$1"
        ARCH=$(uname -m)
        case "$ARCH" in
            x86_64)  ARCH_LABEL="x64" ;;
            aarch64) ARCH_LABEL="arm64" ;;
            *) echo "Unsupported arch: $ARCH"; exit 1 ;;
        esac

        TARBALL="node-${NODE_VERSION}-linux-${ARCH_LABEL}.tar.xz"
        URL="https://nodejs.org/dist/${NODE_VERSION}/${TARBALL}"

        echo "    Downloading ${URL}..."
        curl -fsSL -o "/tmp/${TARBALL}" "${URL}"

        echo "    Extracting to ~/node..."
        rm -rf ~/node
        mkdir -p ~/node
        tar -xf "/tmp/${TARBALL}" -C ~/node --strip-components=1
        rm "/tmp/${TARBALL}"

        # Add to PATH in .bashrc if not already there
        if ! grep -q 'HOME/node/bin' ~/.bashrc 2>/dev/null; then
            echo '' >> ~/.bashrc
            echo '# Node.js (installed by allow2linux dev-deploy)' >> ~/.bashrc
            echo 'export PATH="$HOME/node/bin:$PATH"' >> ~/.bashrc
        fi

        echo "    Node.js $(~/node/bin/node --version) installed"
INSTALL_EOF
}

sync_files() {
    echo "==> Syncing SDK to ${DECK_HOST}:${REMOTE_DIR}/sdk/node/"
    rsync -avz --delete \
        --exclude node_modules \
        --exclude .git \
        "${SDK_ROOT}/" \
        "${DECK_HOST}:${REMOTE_DIR}/sdk/node/"

    echo "==> Syncing allow2linux to ${DECK_HOST}:${REMOTE_DIR}/allow2linux/"
    rsync -avz --delete \
        --exclude node_modules \
        --exclude .git \
        --exclude 'flatpak/build' \
        --exclude 'flatpak/repo' \
        --exclude '*.flatpak' \
        "${PROJECT_ROOT}/" \
        "${DECK_HOST}:${REMOTE_DIR}/allow2linux/"

    echo "==> Installing dependencies on Deck"
    ssh "${DECK_HOST}" "export PATH=\$HOME/node/bin:\$PATH && cd ${REMOTE_DIR}/sdk/node && npm install --omit=dev 2>&1 | tail -1"
    ssh "${DECK_HOST}" "export PATH=\$HOME/node/bin:\$PATH && cd ${REMOTE_DIR}/allow2linux/packages/allow2linux && npm install --omit=dev 2>&1 | tail -1"
}

restart_daemon() {
    echo "==> Restarting allow2linux daemon"
    ssh "${DECK_HOST}" "export PATH=\$HOME/node/bin:\$PATH && systemctl --user restart allow2linux 2>/dev/null || echo 'Service not installed — run manually with: PATH=\$HOME/node/bin:\$PATH node ~/allow2/allow2linux/packages/allow2linux/src/index.js'"
}

ensure_fonts() {
    local ASSETS_DIR="${PROJECT_ROOT}/packages/allow2-lock-overlay/assets"
    local REGULAR="${ASSETS_DIR}/Inter-Regular.ttf"
    local BOLD="${ASSETS_DIR}/Inter-Bold.ttf"

    if [ -f "${REGULAR}" ] && [ -f "${BOLD}" ]; then
        return 0
    fi

    echo "==> Downloading Inter font files..."
    mkdir -p "${ASSETS_DIR}"

    local TMPZIP="/tmp/Inter-font.zip"
    curl -fsSL -o "${TMPZIP}" \
        "https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip"

    # Extract just the two static TTFs we need
    unzip -o -j "${TMPZIP}" "Inter-4.1/InterDesktop/Inter-Regular.ttf" -d "${ASSETS_DIR}" 2>/dev/null || true
    unzip -o -j "${TMPZIP}" "Inter-4.1/InterDesktop/Inter-Bold.ttf" -d "${ASSETS_DIR}" 2>/dev/null || true

    # Fallback: structure may vary between releases — try alternate paths
    if [ ! -f "${REGULAR}" ] || [ ! -f "${BOLD}" ]; then
        # List what's in the zip and find the right files
        local reg_path bold_path
        reg_path=$(unzip -l "${TMPZIP}" | grep -i 'Inter-Regular\.ttf' | head -1 | awk '{print $NF}')
        bold_path=$(unzip -l "${TMPZIP}" | grep -i 'Inter-Bold\.ttf' | head -1 | awk '{print $NF}')
        [ -n "${reg_path}" ] && unzip -o -j "${TMPZIP}" "${reg_path}" -d "${ASSETS_DIR}"
        [ -n "${bold_path}" ] && unzip -o -j "${TMPZIP}" "${bold_path}" -d "${ASSETS_DIR}"
    fi

    rm -f "${TMPZIP}"

    if [ -f "${REGULAR}" ] && [ -f "${BOLD}" ]; then
        echo "    Inter-Regular.ttf and Inter-Bold.ttf installed"
    else
        echo "    WARNING: Could not extract font files. Download manually from https://rsms.me/inter/"
    fi
}

build_overlay() {
    local OVERLAY_DIR="${PROJECT_ROOT}/packages/allow2-lock-overlay"
    local BINARY="${OVERLAY_DIR}/allow2-lock-overlay"

    # If binary already exists and source hasn't changed, skip
    if [ -f "${BINARY}" ]; then
        local src_newest
        src_newest=$(find "${OVERLAY_DIR}/src" -name '*.c' -o -name '*.h' -newer "${BINARY}" 2>/dev/null | head -1)
        if [ -z "${src_newest}" ]; then
            echo "==> Overlay binary up to date, skipping build"
            return 0
        fi
    fi

    # Cross-compile in Docker (Debian matches SteamOS glibc)
    if command -v docker &>/dev/null; then
        local IMAGE_TAG="allow2-overlay-builder"

        # Build the image once (cached after first run)
        if ! docker image inspect "${IMAGE_TAG}" >/dev/null 2>&1; then
            echo "==> Building Docker build image (one-time)..."
            docker build --platform linux/amd64 -t "${IMAGE_TAG}" - <<'DOCKERFILE'
FROM debian:bookworm-slim
RUN apt-get update -qq && \
    apt-get install -y -qq gcc make libsdl2-dev libsdl2-ttf-dev libx11-dev >/dev/null 2>&1 && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /build
DOCKERFILE
        fi

        echo "==> Cross-compiling overlay..."
        docker run --rm --platform linux/amd64 \
            -v "${OVERLAY_DIR}:/build" \
            -w /build \
            "${IMAGE_TAG}" \
            bash -c 'make clean && make'
        if [ -f "${BINARY}" ]; then
            echo "    Overlay binary built successfully"
        else
            echo "    WARNING: Docker build produced no binary"
            return 0
        fi
    else
        echo "==> Skipping overlay build (Docker not available)"
        echo "    Install Docker to cross-compile, or build on-device:"
        echo "      sudo steamos-readonly disable"
        echo "      sudo pacman -S base-devel sdl2 sdl2_ttf sdl2_image"
        echo "      sudo steamos-readonly enable"
        return 0
    fi
}

deploy_env() {
    # Look for .env in allow2linux root or flatpak dir
    local env_file=""
    if [ -f "${PROJECT_ROOT}/.env" ]; then
        env_file="${PROJECT_ROOT}/.env"
    elif [ -f "${SCRIPT_DIR}/.env" ]; then
        env_file="${SCRIPT_DIR}/.env"
    fi

    if [ -n "${env_file}" ]; then
        echo "==> Deploying .env to Deck (~/.allow2/.env)"
        ssh "${DECK_HOST}" "mkdir -p ~/.allow2 && chmod 700 ~/.allow2"
        scp "${env_file}" "${DECK_HOST}:~/.allow2/.env"
        ssh "${DECK_HOST}" "chmod 600 ~/.allow2/.env"
    else
        echo "==> No .env file found (checked ${PROJECT_ROOT}/.env and ${SCRIPT_DIR}/.env)"
        echo "    Create one with:"
        echo "      ALLOW2_API_URL=https://staging-api.allow2.com"
        echo "      ALLOW2_VID=12345"
        echo "      ALLOW2_TOKEN=your-token-here"
    fi
}

install_service() {
    echo "==> Installing systemd user service"
    ssh "${DECK_HOST}" bash <<'SERVICE_EOF'
        set -e
        SERVICE_DIR="$HOME/.config/systemd/user"
        mkdir -p "${SERVICE_DIR}"

        cat > "${SERVICE_DIR}/allow2linux.service" <<UNIT
[Unit]
Description=Allow2 Parental Freedom for Linux
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
EnvironmentFile=-%h/.allow2/.env
Environment=PATH=%h/node/bin:/usr/bin:/bin
Environment=NODE_ENV=production
ExecStart=%h/node/bin/node %h/allow2/allow2linux/packages/allow2linux/src/index.js
Restart=always
RestartSec=5

[Install]
WantedBy=default.target
UNIT

        systemctl --user daemon-reload
        systemctl --user enable allow2linux
        echo "    Service installed and enabled (loads ~/.allow2/.env)"
SERVICE_EOF
}

# Bootstrap Node.js if needed, fetch fonts, build overlay, then sync everything
ensure_node
ensure_fonts
build_overlay
sync_files
deploy_env
install_service
restart_daemon

echo "==> Deploy complete"

case "${1:-}" in
    watch)
        echo "==> Watching for changes (Ctrl+C to stop)..."

        if command -v fswatch &>/dev/null; then
            # macOS
            fswatch -o \
                "${SDK_ROOT}/src/" \
                "${PROJECT_ROOT}/packages/allow2linux/src/" \
                "${PROJECT_ROOT}/packages/allow2linux/config/" \
                "${PROJECT_ROOT}/packages/allow2-lock-overlay/src/" \
            | while read -r _; do
                echo ""
                echo "==> Change detected, rebuilding + syncing..."
                build_overlay
                sync_files
                restart_daemon
                echo "==> Waiting for changes..."
            done
        elif command -v inotifywait &>/dev/null; then
            # Linux
            while inotifywait -r -e modify,create,delete \
                "${SDK_ROOT}/src/" \
                "${PROJECT_ROOT}/packages/allow2linux/src/" \
                "${PROJECT_ROOT}/packages/allow2linux/config/" \
                "${PROJECT_ROOT}/packages/allow2-lock-overlay/src/"; do
                echo ""
                echo "==> Change detected, rebuilding + syncing..."
                build_overlay
                sync_files
                restart_daemon
                echo "==> Waiting for changes..."
            done
        else
            echo "ERROR: Install fswatch (macOS: brew install fswatch) or inotify-tools (Linux) for watch mode"
            exit 1
        fi
        ;;
esac
