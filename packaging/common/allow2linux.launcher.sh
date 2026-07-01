#!/bin/sh
# allow2linux — production-locked launcher (installed as /usr/bin/allow2linux by
# the .deb / .rpm packages, and mirrored by scripts/install.sh for curl|bash).
#
# PRODUCTION SAFETY (mirrors flatpak/gen-launcher.sh's `production` output):
# bake ALLOW2_PRODUCTION=1 + NODE_ENV=production so the Allow2 SDK's hard guard
# ignores ALLOW2_ENV / ALLOW2_API_URL entirely and can NEVER attach to staging.
# The public deb/rpm/curl builds are ALWAYS production — there is no beta channel
# for these formats (beta ships only over the Flatpak `staging` prefix).
#
# vid/token are TYPE IDENTIFIERS (which integration this is), not secrets — the
# production defaults match gen-launcher.sh (21599 / x9AUeUPpiweHTNCR).
set -eu

export ALLOW2_PRODUCTION=1
export NODE_ENV=production
export ALLOW2_VID=21599
export ALLOW2_TOKEN=x9AUeUPpiweHTNCR

# Resolve a Node >= 18 runtime. Prefer a system node; fall back to a Node that
# scripts/install.sh may have bootstrapped into the user's data dir.
NODE_BIN=""
if command -v node >/dev/null 2>&1; then
    NODE_BIN="$(command -v node)"
elif [ -x "${XDG_DATA_HOME:-$HOME/.local/share}/allow2linux/node/bin/node" ]; then
    NODE_BIN="${XDG_DATA_HOME:-$HOME/.local/share}/allow2linux/node/bin/node"
fi

if [ -z "${NODE_BIN}" ]; then
    echo "allow2linux: no Node.js runtime found (need >= 18). Install nodejs or re-run the installer." >&2
    exit 1
fi

# The daemon entry point. The .deb/.rpm install it here; install.sh mirrors the
# repo layout under the user's data dir (see that script's DAEMON_DIR).
INDEX_JS="/usr/lib/allow2linux/src/index.js"
if [ ! -f "${INDEX_JS}" ]; then
    INDEX_JS="${XDG_DATA_HOME:-$HOME/.local/share}/allow2linux/packages/allow2linux/src/index.js"
fi

exec "${NODE_BIN}" "${INDEX_JS}" "$@"
