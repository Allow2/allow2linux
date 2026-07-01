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
#     5. publish the one-click .flatpakref + the static install page (index.html)
#        + the hardening page (hardening.html) + any page images into the same
#        prefix (the gen-*-page.sh scripts bake per-channel URLs/labels)
#     6. purge the Cloudflare cache for that prefix's summary + ref + both pages
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
        CHANNEL="staging"; FLATPAK_BRANCH="beta";   SUBDIR="staging"
        REF_FILE="com.allow2.allow2linux-beta.flatpakref" ;;
    production|stable|prod)
        CHANNEL="production"; FLATPAK_BRANCH="stable"; SUBDIR="stable"
        REF_FILE="com.allow2.allow2linux.flatpakref" ;;
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
echo "==> [0/6] gen-launcher.sh (channel=${CHANNEL})"
CHANNEL="${CHANNEL}" "${PROJECT_ROOT}/flatpak/gen-launcher.sh"

# ── 1 + 2. Build, export to the channel's ostree repo, refresh summary ───────
echo "==> [1/6] flatpak-builder (build + export to repo-${SUBDIR}/, branch=${FLATPAK_BRANCH})"
flatpak-builder \
    --force-clean \
    --default-branch="${FLATPAK_BRANCH}" \
    --repo="${REPO_DIR}" \
    "${BUILD_DIR}" \
    "${MANIFEST}"

echo "==> [2/6] flatpak build-update-repo (regenerate summary + metadata)"
# Regenerates `summary` (+ `summary.sig` if signing). Testers can never see a
# new commit until the summary is refreshed AND its cache is purged (step 5).
flatpak build-update-repo --generate-static-deltas --prune "${REPO_DIR}"

# ── 3. Sync repo/ to Cloudflare R2 under the channel's prefix ────────────────
echo "==> [3/6] sync repo-${SUBDIR}/ -> R2 (${SYNC_TOOL})"
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

# ── 4. Publish the one-click .flatpakref + the static install page ───────────
# These sit in the SAME prefix as the ostree repo, so they MUST be uploaded
# AFTER the `--delete` sync above (which would otherwise remove them). On the
# next run the sync deletes them and this step re-uploads — idempotent.
#   /steamdeck/<channel>/<ref>            one-click install (Content-Type flatpak.ref)
#   /steamdeck/<channel>/index.html       the install walkthrough (text/html)
#   /steamdeck/<channel>/hardening.html   the harden-against-bypass guide (text/html)
#   /steamdeck/<channel>/images/          page screenshots, if present (step 4b)
echo "==> [4/6] publish ${REF_FILE} + install page + hardening page -> R2 (${SYNC_TOOL})"

# Generate the channel's install + hardening pages (baked URLs/labels;
# staging=internal-only). Both generators take the BASE url and append
# /<subdir>/… themselves, so pass PUBLIC_URL_BASE (not the subdir-suffixed
# PUBLIC_URL used for purging). The pages cross-link within the SAME channel.
INSTALL_PAGE="${PROJECT_ROOT}/flatpak/install-page-${SUBDIR}.html"
HARDENING_PAGE="${PROJECT_ROOT}/flatpak/hardening-page-${SUBDIR}.html"
PUBLIC_URL="${PUBLIC_URL_BASE}" APP_ID="${APP_ID}" \
    "${PROJECT_ROOT}/flatpak/gen-install-page.sh" "${CHANNEL}" "${INSTALL_PAGE}"
PUBLIC_URL="${PUBLIC_URL_BASE}" \
    "${PROJECT_ROOT}/flatpak/gen-hardening-page.sh" "${CHANNEL}" "${HARDENING_PAGE}"

REF_SRC="${PROJECT_ROOT}/${REF_FILE}"
[ -f "${REF_SRC}" ] || { echo "ERROR: ${REF_SRC} not found"; exit 1; }

# Upload one file to the channel prefix with an explicit Content-Type, honouring
# SYNC_TOOL (creds already exported by the sync step above).
r2_put() {  # r2_put <local-file> <dest-key> <content-type>
    local src="$1" key="$2" ctype="$3"
    if [ "${SYNC_TOOL}" = "rclone" ]; then
        rclone copyto "${src}" "r2beta:${R2_BUCKET}/${key}" \
            --header-upload "Content-Type: ${ctype}"
    else
        aws s3 cp "${src}" "s3://${R2_BUCKET}/${key}" \
            --endpoint-url "${R2_ENDPOINT}" \
            --checksum-algorithm CRC32 \
            --content-type "${ctype}" \
            --no-progress
    fi
}

r2_put "${REF_SRC}"        "${PREFIX}/${SUBDIR}/${REF_FILE}"     "application/vnd.flatpak.ref"
r2_put "${INSTALL_PAGE}"   "${PREFIX}/${SUBDIR}/index.html"      "text/html"
r2_put "${HARDENING_PAGE}" "${PREFIX}/${SUBDIR}/hardening.html"  "text/html"
echo "    uploaded ${SUBDIR}/${REF_FILE} + ${SUBDIR}/index.html + ${SUBDIR}/hardening.html"

# ── 4a. Publish the domain-root robots.txt (excludes the staging beta) ────────
# This is a DOMAIN-ROOT object (served at get.allow2.com/robots.txt), NOT under
# steamdeck/<channel>/ — robots.txt is only honoured at the site root. It is
# channel-agnostic (Disallow: /steamdeck/staging/, stable stays crawlable), so we
# (re-)upload it on EVERY publish regardless of channel: idempotent + cheap, and
# it can't be clobbered by the per-channel --delete sync (that only scopes the
# steamdeck/<channel>/ prefix).
ROBOTS_SRC="${PROJECT_ROOT}/robots.txt"
[ -f "${ROBOTS_SRC}" ] || { echo "ERROR: ${ROBOTS_SRC} not found"; exit 1; }
r2_put "${ROBOTS_SRC}" "robots.txt" "text/plain"
echo "    uploaded robots.txt -> bucket root (served at get.allow2.com/robots.txt)"

# ── 4b. Publish page images (if any) into the SAME prefix's images/ dir ───────
# Both pages reference images/<slug>.png relative to the channel prefix. When the
# repo has a flatpak/images/ dir (real screenshots dropped in later), mirror it to
# /steamdeck/<channel>/images/. No dir yet → no-op (pages show onerror captions).
IMAGES_SRC="${PROJECT_ROOT}/flatpak/images"
if [ -d "${IMAGES_SRC}" ] && [ -n "$(ls -A "${IMAGES_SRC}" 2>/dev/null)" ]; then
    echo "==> [4b] sync images/ -> ${SUBDIR}/images/ (${SYNC_TOOL})"
    if [ "${SYNC_TOOL}" = "rclone" ]; then
        rclone copy "${IMAGES_SRC}/" "r2beta:${R2_BUCKET}/${PREFIX}/${SUBDIR}/images/"
    else
        aws s3 sync "${IMAGES_SRC}/" "s3://${R2_BUCKET}/${PREFIX}/${SUBDIR}/images/" \
            --endpoint-url "${R2_ENDPOINT}" \
            --checksum-algorithm CRC32 \
            --no-progress
    fi
    echo "    synced $(ls -1 "${IMAGES_SRC}" | wc -l | tr -d ' ') image(s) to ${SUBDIR}/images/"
else
    echo "==> [4b] no flatpak/images/ dir yet — skipping image upload (pages use onerror captions)"
fi

# ── 5. Purge the Cloudflare cache for THIS prefix's ostree metadata + pages ──
# The .tgz objects are content-addressed (immutable) so caching them is fine,
# but `summary`, `summary.sig` and `config` change every publish. If they are
# served stale, clients never see the new commit. A Cache Rule that BYPASSES
# cache for */<channel>/summary* is the durable fix (see BETA_DELIVERY.md); this
# purge is the belt-and-braces on top. We also purge the .flatpakref + index.html
# so a re-publish is picked up immediately.
echo "==> [5/6] purge Cloudflare cache for ${SUBDIR}/ ostree metadata + install page"
if [ -n "${CLOUDFLARE_API_TOKEN:-}" ] && [ -n "${CLOUDFLARE_ZONE_ID:-}" ]; then
    require curl
    PURGE_FILES=$(cat <<JSON
{"files":[
  "${PUBLIC_URL}/summary",
  "${PUBLIC_URL}/summary.sig",
  "${PUBLIC_URL}/summary.idx",
  "${PUBLIC_URL}/config",
  "${PUBLIC_URL}/index.html",
  "${PUBLIC_URL}/hardening.html",
  "${PUBLIC_URL}/${REF_FILE}",
  "https://get.allow2.com/robots.txt"
]}
JSON
)
    curl -fsS -X POST \
        "https://api.cloudflare.com/client/v4/zones/${CLOUDFLARE_ZONE_ID}/purge_cache" \
        -H "Authorization: Bearer ${CLOUDFLARE_API_TOKEN}" \
        -H "Content-Type: application/json" \
        --data "${PURGE_FILES}" >/dev/null
    echo "    purged ${SUBDIR}/summary{,.sig,.idx} + config + index.html + hardening.html + ${REF_FILE} + root robots.txt"
else
    echo "    SKIPPED — set CLOUDFLARE_API_TOKEN + CLOUDFLARE_ZONE_ID to auto-purge."
    echo "    Until then, testers may see stale metadata until the CDN TTL expires."
fi

echo ""
echo "==> [6/6] Published ${CHANNEL} → ${PUBLIC_URL}/"
echo "    One-click install:  ${PUBLIC_URL}/${REF_FILE}"
echo "    Install page:       ${PUBLIC_URL}/  (index.html)"
echo "    Hardening page:     ${PUBLIC_URL}/hardening.html"
echo "    Users update with:  flatpak update -y ${APP_ID}"
if [ "${CHANNEL}" = "staging" ]; then
    echo "    First-time install:  com.allow2.allow2linux-beta.flatpakref  (Branch=beta)"
else
    echo "    First-time install:  com.allow2.allow2linux.flatpakref       (Branch=stable)"
fi
