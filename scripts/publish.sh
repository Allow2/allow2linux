#!/usr/bin/env bash
#
# publish.sh <staging|production> — build the allow2linux Flatpak for ONE channel
# and publish its ostree repo to that channel's prefix on Cloudflare R2
# (served at https://get.allow2.com/steamdeck/<channel>/).
#
#   staging    → Flatpak branch `beta`   → https://get.allow2.com/steamdeck/staging/
#                (internal testers; launcher targets staging-api.allow2.com)
#   production → Flatpak branch `stable` → https://get.allow2.com/steamdeck/stable/
#                (public/prod; launcher is production-locked → api.allow2.com)
#
# TWO INDEPENDENT REPOS / PREFIXES — not one repo with two branches. A fresh CI
# run regenerates the ostree `summary` from ONLY its own build, so co-hosting both
# channels under one prefix would drop the other channel from the summary. Each
# channel therefore gets its own R2 prefix, built fresh (--force-clean) and synced
# with --delete, then its own `summary*` metadata purged.
#
# Idempotent + re-runnable: run it again to ship an update to that channel. Testers
# pick it up via `flatpak update` (or the auto-update systemd timer).
#
# ── The one-command loop ─────────────────────────────────────────────────────
#   ./scripts/publish.sh staging       # ship a beta to internal testers
#   ./scripts/publish.sh production     # ship the production/stable build
#     1. gen-launcher.sh  (bake the channel-specific launcher: env + vid/token)
#     2. flatpak-builder --default-branch=<beta|stable>  (build + export ostree)
#     3. flatpak build-update-repo    (regenerate summary + metadata)
#     4. sync repo/ -> s3://$R2_BUCKET/steamdeck/<staging|stable>/  (Cloudflare R2)
#     5. purge the Cloudflare cache for that prefix's ostree summary files
#
# ── Required environment (NEVER hardcode secrets; see docs/BETA_DELIVERY.md) ──
#   R2_ACCOUNT_ID          Cloudflare account id (for the R2 S3 endpoint host)
#   R2_BUCKET              R2 bucket name (repo lives under the steamdeck/ prefix)
#   R2_ACCESS_KEY_ID       R2 S3 access key id      (aws cli / rclone auth)
#   R2_SECRET_ACCESS_KEY   R2 S3 secret access key
#   CLOUDFLARE_API_TOKEN   token with "Cache Purge" permission on the zone
#   CLOUDFLARE_ZONE_ID     zone id for allow2.com
#
# ── VID/TOKEN (type identifiers, NOT secrets → CI *Variables*) ────────────────
#   staging   : ALLOW2_STAGING_VID (default 21341) / ALLOW2_STAGING_TOKEN
#   production: ALLOW2_PROD_VID    (default 21599) / ALLOW2_PROD_TOKEN
#   (Read by gen-launcher.sh with the known type ids as defaults.)
#
# ── Options (env overrides) ──────────────────────────────────────────────────
#   MANIFEST   default: flatpak/com.allow2.allow2linux.yml (the DEV manifest)
#   APP_ID     default: com.allow2.allow2linux
#   PREFIX     default: steamdeck            (bucket key prefix == URL path)
#   PUBLIC_URL default: https://get.allow2.com/steamdeck
#   SYNC_TOOL  default: aws                  (or: rclone)
#   ALLOW2_VERSION  optional label for logging (e.g. git sha or semver tag)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ── Channel arg → branch + R2 subdir ─────────────────────────────────────────
CHANNEL="${1:-}"
case "${CHANNEL}" in
    staging|beta)
        CHANNEL="staging"; FLATPAK_BRANCH="beta";   SUBDIR="staging" ;;
    production|stable|prod)
        CHANNEL="production"; FLATPAK_BRANCH="stable"; SUBDIR="stable" ;;
    *)
        echo "Usage: $0 <staging|production>" >&2
        echo "  staging    → beta branch   → /steamdeck/staging/ (internal testers)" >&2
        echo "  production → stable branch → /steamdeck/stable/  (public/prod)" >&2
        exit 2 ;;
esac

APP_ID="${APP_ID:-com.allow2.allow2linux}"
MANIFEST="${MANIFEST:-${PROJECT_ROOT}/flatpak/com.allow2.allow2linux.yml}"
PREFIX="${PREFIX:-steamdeck}"
PUBLIC_URL_BASE="${PUBLIC_URL:-https://get.allow2.com/steamdeck}"
PUBLIC_URL="${PUBLIC_URL_BASE}/${SUBDIR}"
SYNC_TOOL="${SYNC_TOOL:-aws}"

# Per-channel build + repo dirs so the two channels never share ostree state.
BUILD_DIR="${PROJECT_ROOT}/flatpak/build-${SUBDIR}"
REPO_DIR="${PROJECT_ROOT}/flatpak/repo-${SUBDIR}"

require() { command -v "$1" >/dev/null 2>&1 || { echo "ERROR: '$1' not found in PATH"; exit 1; }; }
need_env() { local v; for v in "$@"; do [ -n "${!v:-}" ] || { echo "ERROR: env var $v is required"; exit 1; }; done; }

echo "==> allow2linux publish"
echo "    channel : ${CHANNEL}  (branch=${FLATPAK_BRANCH})"
echo "    version : ${ALLOW2_VERSION:-<unset>}"
echo "    app     : ${APP_ID}"
echo "    manifest: ${MANIFEST}"
echo "    target  : ${PUBLIC_URL}/  (bucket prefix: ${PREFIX}/${SUBDIR}/)"

require flatpak-builder

# ── 0. Bake the channel-specific launcher (env + vid/token) ──────────────────
echo "==> [0/5] gen-launcher.sh (channel=${CHANNEL})"
CHANNEL="${CHANNEL}" "${PROJECT_ROOT}/flatpak/gen-launcher.sh"

# ── 1 + 2. Build, export to the channel's ostree repo, refresh summary ───────
echo "==> [1/5] flatpak-builder (build + export to repo-${SUBDIR}/, branch=${FLATPAK_BRANCH})"
flatpak-builder \
    --force-clean \
    --default-branch="${FLATPAK_BRANCH}" \
    --repo="${REPO_DIR}" \
    "${BUILD_DIR}" \
    "${MANIFEST}"

echo "==> [2/5] flatpak build-update-repo (regenerate summary + metadata)"
# Regenerates `summary` (+ `summary.sig` if signing). Testers can never see a
# new commit until the summary is refreshed AND its cache is purged (step 5).
flatpak build-update-repo --generate-static-deltas --prune "${REPO_DIR}"

# ── 3. Sync repo/ to Cloudflare R2 under the channel's prefix ────────────────
echo "==> [3/5] sync repo-${SUBDIR}/ -> R2 (${SYNC_TOOL})"
need_env R2_ACCOUNT_ID R2_BUCKET R2_ACCESS_KEY_ID R2_SECRET_ACCESS_KEY
R2_ENDPOINT="https://${R2_ACCOUNT_ID}.r2.cloudflarestorage.com"

if [ "${SYNC_TOOL}" = "rclone" ]; then
    require rclone
    # rclone handles R2 cleanly (no checksum-algorithm quirks). Config via env:
    export RCLONE_CONFIG_R2BETA_TYPE=s3
    export RCLONE_CONFIG_R2BETA_PROVIDER=Cloudflare
    export RCLONE_CONFIG_R2BETA_ACCESS_KEY_ID="${R2_ACCESS_KEY_ID}"
    export RCLONE_CONFIG_R2BETA_SECRET_ACCESS_KEY="${R2_SECRET_ACCESS_KEY}"
    export RCLONE_CONFIG_R2BETA_ENDPOINT="${R2_ENDPOINT}"
    rclone sync "${REPO_DIR}/" "r2beta:${R2_BUCKET}/${PREFIX}/${SUBDIR}/" --checksum
else
    require aws
    export AWS_ACCESS_KEY_ID="${R2_ACCESS_KEY_ID}"
    export AWS_SECRET_ACCESS_KEY="${R2_SECRET_ACCESS_KEY}"
    # R2 rejects the streaming CRC that newer aws-cli (>=2.23) sends by default.
    # `--checksum-algorithm CRC32` (or AWS_REQUEST_CHECKSUM_CALCULATION=WHEN_REQUIRED)
    # restores compatibility. If your aws-cli predates the change this is a no-op.
    aws s3 sync "${REPO_DIR}/" "s3://${R2_BUCKET}/${PREFIX}/${SUBDIR}/" \
        --endpoint-url "${R2_ENDPOINT}" \
        --checksum-algorithm CRC32 \
        --delete \
        --no-progress
fi

# ── 4. Purge the Cloudflare cache for THIS prefix's ostree metadata ──────────
# The .tgz objects are content-addressed (immutable) so caching them is fine,
# but `summary`, `summary.sig` and `config` change every publish. If they are
# served stale, clients never see the new commit. A Cache Rule that BYPASSES
# cache for */<channel>/summary* is the durable fix (see BETA_DELIVERY.md); this
# purge is the belt-and-braces on top.
echo "==> [4/5] purge Cloudflare cache for ${SUBDIR}/ ostree metadata"
if [ -n "${CLOUDFLARE_API_TOKEN:-}" ] && [ -n "${CLOUDFLARE_ZONE_ID:-}" ]; then
    require curl
    PURGE_FILES=$(cat <<JSON
{"files":[
  "${PUBLIC_URL}/summary",
  "${PUBLIC_URL}/summary.sig",
  "${PUBLIC_URL}/summary.idx",
  "${PUBLIC_URL}/config"
]}
JSON
)
    curl -fsS -X POST \
        "https://api.cloudflare.com/client/v4/zones/${CLOUDFLARE_ZONE_ID}/purge_cache" \
        -H "Authorization: Bearer ${CLOUDFLARE_API_TOKEN}" \
        -H "Content-Type: application/json" \
        --data "${PURGE_FILES}" >/dev/null
    echo "    purged ${SUBDIR}/summary{,.sig,.idx} + config"
else
    echo "    SKIPPED — set CLOUDFLARE_API_TOKEN + CLOUDFLARE_ZONE_ID to auto-purge."
    echo "    Until then, testers may see stale metadata until the CDN TTL expires."
fi

echo ""
echo "==> [5/5] Published ${CHANNEL} → ${PUBLIC_URL}/"
echo "    Users update with:  flatpak update -y ${APP_ID}"
if [ "${CHANNEL}" = "staging" ]; then
    echo "    First-time install:  com.allow2.allow2linux-beta.flatpakref  (Branch=beta)"
else
    echo "    First-time install:  com.allow2.allow2linux.flatpakref       (Branch=stable)"
fi
