#!/usr/bin/env bash
#
# publish-beta.sh — build the allow2linux Flatpak and publish the ostree repo to
# the internal beta channel (Cloudflare R2, served at https://get.allow2.com/steamdeck/).
#
# Idempotent + re-runnable: run it again to ship an update. Testers pick it up
# via `flatpak update` (or the auto-update systemd timer).
#
# ── The one-command loop ─────────────────────────────────────────────────────
#   ./scripts/publish-beta.sh
#     1. flatpak-builder --repo=repo  (build + export ostree commit)
#     2. flatpak build-update-repo    (regenerate summary + metadata)
#     3. sync repo/ -> s3://$R2_BUCKET/steamdeck/  (Cloudflare R2, S3 API)
#     4. purge the Cloudflare cache for the ostree summary files
#
# ── Required environment (NEVER hardcode secrets; see docs/BETA_DELIVERY.md) ──
#   R2_ACCOUNT_ID          Cloudflare account id (for the R2 S3 endpoint host)
#   R2_BUCKET              R2 bucket name (repo lives under the steamdeck/ prefix)
#   R2_ACCESS_KEY_ID       R2 S3 access key id      (aws cli / rclone auth)
#   R2_SECRET_ACCESS_KEY   R2 S3 secret access key
#   CLOUDFLARE_API_TOKEN   token with "Cache Purge" permission on the zone
#   CLOUDFLARE_ZONE_ID     zone id for allow2.com
#
# ── Options (env overrides) ──────────────────────────────────────────────────
#   MANIFEST   default: flatpak/com.allow2.allow2linux.yml (the DEV manifest)
#   APP_ID     default: com.allow2.allow2linux
#   PREFIX     default: steamdeck            (bucket key prefix == URL path)
#   PUBLIC_URL default: https://get.allow2.com/steamdeck
#   SYNC_TOOL  default: aws                  (or: rclone)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

APP_ID="${APP_ID:-com.allow2.allow2linux}"
MANIFEST="${MANIFEST:-${PROJECT_ROOT}/flatpak/com.allow2.allow2linux.yml}"
PREFIX="${PREFIX:-steamdeck}"
PUBLIC_URL="${PUBLIC_URL:-https://get.allow2.com/steamdeck}"
SYNC_TOOL="${SYNC_TOOL:-aws}"

BUILD_DIR="${PROJECT_ROOT}/flatpak/build-dir"
REPO_DIR="${PROJECT_ROOT}/flatpak/repo"

require() { command -v "$1" >/dev/null 2>&1 || { echo "ERROR: '$1' not found in PATH"; exit 1; }; }
need_env() { local v; for v in "$@"; do [ -n "${!v:-}" ] || { echo "ERROR: env var $v is required"; exit 1; }; done; }

echo "==> allow2linux beta publish"
echo "    app     : ${APP_ID}"
echo "    manifest: ${MANIFEST}"
echo "    target  : ${PUBLIC_URL}  (bucket prefix: ${PREFIX}/)"

require flatpak-builder

# ── 1 + 2. Build, export to the ostree repo, refresh summary ─────────────────
echo "==> [1/4] flatpak-builder (build + export to repo/)"
flatpak-builder \
    --force-clean \
    --repo="${REPO_DIR}" \
    "${BUILD_DIR}" \
    "${MANIFEST}"

echo "==> [2/4] flatpak build-update-repo (regenerate summary + metadata)"
# Regenerates `summary` (+ `summary.sig` if signing). Testers can never see a
# new commit until the summary is refreshed AND its cache is purged (step 4).
flatpak build-update-repo --generate-static-deltas --prune "${REPO_DIR}"

# ── 3. Sync repo/ to Cloudflare R2 under the steamdeck/ prefix ────────────────
echo "==> [3/4] sync repo/ -> R2 (${SYNC_TOOL})"
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
    rclone sync "${REPO_DIR}/" "r2beta:${R2_BUCKET}/${PREFIX}/" --checksum
else
    require aws
    export AWS_ACCESS_KEY_ID="${R2_ACCESS_KEY_ID}"
    export AWS_SECRET_ACCESS_KEY="${R2_SECRET_ACCESS_KEY}"
    # R2 rejects the streaming CRC that newer aws-cli (>=2.23) sends by default.
    # `--checksum-algorithm CRC32` (or AWS_REQUEST_CHECKSUM_CALCULATION=WHEN_REQUIRED)
    # restores compatibility. If your aws-cli predates the change this is a no-op.
    aws s3 sync "${REPO_DIR}/" "s3://${R2_BUCKET}/${PREFIX}/" \
        --endpoint-url "${R2_ENDPOINT}" \
        --checksum-algorithm CRC32 \
        --delete \
        --no-progress
fi

# ── 4. Purge the Cloudflare cache for the ostree summary/metadata ────────────
# The .tgz objects are content-addressed (immutable) so caching them is fine,
# but `summary`, `summary.sig` and `config` change every publish. If they are
# served stale, clients never see the new commit. A Cache Rule that BYPASSES
# cache for */summary* is the durable fix (see BETA_DELIVERY.md); this purge is
# the belt-and-braces on top.
echo "==> [4/4] purge Cloudflare cache for ostree metadata"
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
    echo "    purged summary/summary.sig/summary.idx/config"
else
    echo "    SKIPPED — set CLOUDFLARE_API_TOKEN + CLOUDFLARE_ZONE_ID to auto-purge."
    echo "    Until then, testers may see stale metadata until the CDN TTL expires."
fi

echo ""
echo "==> Published. Testers update with:  flatpak update -y ${APP_ID}"
echo "    First-time install file: com.allow2.allow2linux.flatpakref (Url=${PUBLIC_URL}/)"
