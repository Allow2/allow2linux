#!/usr/bin/env bash
#
# gen-install-page.sh — generate the channel-specific static install walkthrough
# (index.html) that is published alongside the .flatpakref to each channel's R2
# prefix and served at https://get.allow2.com/steamdeck/<channel>/.
#
# ONE generator, TWO channels — mirrors gen-launcher.sh: a single HTML body with
# per-channel values (ref filename, channel label, baked URLs, security banner)
# baked in from shell variables. No divergent per-channel HTML files (no drift).
#
# ── Channels ─────────────────────────────────────────────────────────────────
#   staging     (BETA, Flatpak Branch=beta, prefix /steamdeck/staging/):
#               INTERNAL TESTER ONLY. The page carries a prominent security
#               banner, is marked noindex/nofollow, and NEVER links to or
#               advertises the stable/public page (no public discoverability of
#               the beta — carries the beta ref's security note).
#
#   production  (STABLE, Flatpak Branch=stable, prefix /steamdeck/stable/):
#               the PUBLIC-facing install page. Never cross-links to staging.
#
# ── Usage ────────────────────────────────────────────────────────────────────
#   ./gen-install-page.sh <staging|production> [output-path]
#   CHANNEL=staging ./gen-install-page.sh                     (env fallback)
# Default CHANNEL is production (the safe default). Default output is
# ./install-page-<subdir>.html next to this script.
#
# ── Options (env overrides) ──────────────────────────────────────────────────
#   PUBLIC_URL   default: https://get.allow2.com/steamdeck   (base; /<subdir>/<ref>)
#   APP_ID       default: com.allow2.allow2linux             (for `flatpak update`)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

CHANNEL="${1:-${CHANNEL:-production}}"
PUBLIC_URL_BASE="${PUBLIC_URL:-https://get.allow2.com/steamdeck}"
APP_ID="${APP_ID:-com.allow2.allow2linux}"

# ── Channel → subdir + ref filename + labels (mirrors gen-launcher.sh) ────────
case "${CHANNEL}" in
  staging|beta)
    SUBDIR="staging"
    REF_FILE="com.allow2.allow2linux-beta.flatpakref"
    APP_BRANCH="beta"
    CHANNEL_LABEL="Internal Beta (staging)"
    IS_STAGING=1
    ;;
  production|stable|prod)
    SUBDIR="stable"
    REF_FILE="com.allow2.allow2linux.flatpakref"
    APP_BRANCH="stable"
    CHANNEL_LABEL="Stable"
    IS_STAGING=0
    ;;
  *)
    echo "ERROR: unknown CHANNEL='${CHANNEL}' (expected: staging|production)" >&2
    exit 1
    ;;
esac

OUT="${2:-${SCRIPT_DIR}/install-page-${SUBDIR}.html}"

REF_URL="${PUBLIC_URL_BASE}/${SUBDIR}/${REF_FILE}"

# Per-channel cross-link to THIS channel's hardening page (baked absolute so
# staging links to staging and stable to stable — never cross-channel).
HARDENING_URL="${PUBLIC_URL_BASE}/${SUBDIR}/hardening.html"

# ── Per-channel security chrome ──────────────────────────────────────────────
# staging: prominent internal-only banner + noindex (no public discoverability).
# stable : public-facing, indexable, no banner.
if [ "${IS_STAGING}" -eq 1 ]; then
  ROBOTS_META='<meta name="robots" content="noindex,nofollow">'
  PAGE_TITLE="Allow2 Parental Freedom: Internal Beta (staging) install"
  SECURITY_BANNER='<div class="banner beta">
      <strong>INTERNAL BETA (staging). For the internal tester only.</strong>
      This build targets <code>staging-api.allow2.com</code> and is unsigned
      (<code>gpg-verify=false</code> over HTTPS). Do not share this page or link,
      and never promote this build to the stable/public channel.
    </div>'
  # Staging page NEVER advertises or links the public/stable channel.
  CROSS_NOTE=''
else
  ROBOTS_META=''
  PAGE_TITLE="Allow2 Parental Freedom: install on Steam Deck"
  SECURITY_BANNER=''
  CROSS_NOTE=''
fi

# ── Emit the page (unquoted heredoc: shell vars expand; literal $ escaped) ────
cat > "${OUT}" <<EOF
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
${ROBOTS_META}
<title>${PAGE_TITLE}</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 0;
    background: #0e1116; color: #e6edf3;
    font: 16px/1.6 -apple-system, "Segoe UI", Roboto, "Noto Sans", Ubuntu, sans-serif;
  }
  .wrap { max-width: 780px; margin: 0 auto; padding: 32px 20px 80px; }
  header { text-align: center; margin-bottom: 28px; }
  h1 { font-size: 1.9rem; margin: 0 0 6px; }
  .channel {
    display: inline-block; margin-top: 8px; padding: 3px 12px;
    border-radius: 999px; font-size: .8rem; font-weight: 600;
    letter-spacing: .04em; text-transform: uppercase;
    background: #1f6feb33; color: #79c0ff; border: 1px solid #1f6feb66;
  }
  h2 { font-size: 1.2rem; margin: 34px 0 10px; }
  p { margin: 10px 0; }
  a { color: #79c0ff; }
  code {
    background: #161b22; border: 1px solid #30363d; border-radius: 5px;
    padding: 1px 6px; font-family: ui-monospace, "SF Mono", Menlo, Consolas, monospace;
    font-size: .9em; word-break: break-all;
  }
  pre {
    background: #161b22; border: 1px solid #30363d; border-radius: 8px;
    padding: 14px 16px; overflow-x: auto; margin: 12px 0;
  }
  pre code { background: none; border: 0; padding: 0; }
  ol.steps { list-style: none; counter-reset: step; margin: 0; padding: 0; }
  ol.steps > li {
    counter-increment: step; position: relative;
    padding: 4px 0 22px 52px; border-left: 2px solid #21262d; margin-left: 16px;
  }
  ol.steps > li:last-child { border-left-color: transparent; }
  ol.steps > li::before {
    content: counter(step);
    position: absolute; left: -17px; top: 0;
    width: 32px; height: 32px; border-radius: 50%;
    background: #1f6feb; color: #fff; font-weight: 700;
    display: flex; align-items: center; justify-content: center;
  }
  .step-title { font-weight: 600; font-size: 1.05rem; margin-bottom: 4px; }
  .cta {
    display: inline-block; margin: 14px 0 4px; padding: 14px 26px;
    background: #238636; color: #fff; text-decoration: none;
    border-radius: 8px; font-size: 1.15rem; font-weight: 700;
    border: 1px solid #2ea043;
  }
  .cta:hover { background: #2ea043; }
  .hint { color: #8b949e; font-size: .9rem; }
  figure.shot { margin: 12px 0 6px; }
  figure.shot img {
    display: block; max-width: 100%; height: auto;
    border: 1px solid #30363d; border-radius: 8px;
  }
  .shot-ph {
    align-items: center; gap: 10px;
    padding: 18px 16px; border: 1px dashed #30363d; border-radius: 8px;
    background: #0b0f14; color: #8b949e; font-size: .88rem;
  }
  .shot-ph::before {
    content: "SCREENSHOT"; flex: none; font-size: .62rem; font-weight: 700;
    letter-spacing: .08em; color: #6e7681; border: 1px solid #30363d;
    border-radius: 4px; padding: 3px 6px; background: #161b22;
  }
  .banner {
    border-radius: 8px; padding: 14px 18px; margin: 0 0 24px;
    font-size: .95rem;
  }
  .banner.beta { background: #3d1d1d; border: 1px solid #f85149; color: #ffb4ab; }
  .banner strong { display: block; margin-bottom: 4px; color: #ff7b72; }
  footer { margin-top: 48px; color: #6e7681; font-size: .85rem; text-align: center; }
</style>
</head>
<body>
<div class="wrap">
  <header>
    <h1>Allow2 Parental Freedom</h1>
    <div class="channel">${CHANNEL_LABEL}</div>
  </header>

  ${SECURITY_BANNER}

  <p>Install Allow2 Parental Freedom on your <strong>Steam Deck</strong> (or any
  x86_64 Linux desktop with Flatpak). It takes a couple of minutes in
  <strong>Desktop Mode</strong>.</p>

  <ol class="steps">
    <li>
      <div class="step-title">Switch to Desktop Mode</div>
      <p>Hold the <strong>STEAM</strong> button &rarr; <strong>Power</strong> &rarr;
      <strong>Switch to Desktop</strong>. (On a regular Linux PC you're already
      on the desktop, so skip this.)</p>
      <figure class="shot">
        <img src="images/desktop-mode.png" alt="Steam Deck Power menu open with 'Switch to Desktop' highlighted" loading="lazy"
             onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">
        <figcaption class="shot-ph" style="display:none">Steam Deck Power menu open, "Switch to Desktop" highlighted</figcaption>
      </figure>
    </li>
    <li>
      <div class="step-title">One-time: add Flathub for the runtime</div>
      <p>Allow2 uses the Freedesktop runtime from Flathub. If Flathub isn't set up
      yet, open a terminal (Konsole) and run:</p>
      <pre><code>flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo</code></pre>
      <p class="hint">On SteamOS Flathub is usually already present, so this is a no-op.</p>
      <figure class="shot">
        <img src="images/flathub-konsole.png" alt="Konsole terminal showing the flatpak remote-add Flathub command" loading="lazy"
             onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">
        <figcaption class="shot-ph" style="display:none">Konsole terminal running the flatpak remote-add Flathub command</figcaption>
      </figure>
    </li>
    <li>
      <div class="step-title">Install Allow2</div>
      <p>Click the button below. Your software installer (Discover) opens and
      installs Allow2 from the <strong>${CHANNEL_LABEL}</strong> channel:</p>
      <p><a class="cta" href="${REF_URL}">Install on Steam Deck</a></p>
      <figure class="shot">
        <img src="images/discover-install.png" alt="Discover software installer showing Allow2 with the Install button" loading="lazy"
             onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">
        <figcaption class="shot-ph" style="display:none">Discover software installer open on the Allow2 page, Install button visible</figcaption>
      </figure>
      <p class="hint">Button not working? Copy-paste this into Konsole instead:</p>
      <pre><code>flatpak install --from ${REF_URL}</code></pre>
      <p class="hint">This adds the Allow2 <code>${SUBDIR}</code> remote
      (<code>Branch=${APP_BRANCH}</code>) and installs the app.</p>
    </li>
    <li>
      <div class="step-title">Launch &amp; pair</div>
      <p>Run Allow2 (Applications menu, or <code>flatpak run ${APP_ID}</code>). It
      shows a pairing screen. In the <strong>Allow2 parent app</strong>, scan the
      <strong>QR code</strong> (or type the <strong>6-digit PIN</strong>) and pick
      the child. That's the pairing done.</p>
      <figure class="shot">
        <img src="images/pairing-qr.png" alt="Allow2 pairing screen showing the QR code and 6-digit PIN" loading="lazy"
             onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">
        <figcaption class="shot-ph" style="display:none">Allow2 pairing screen showing the QR code and the 6-digit PIN</figcaption>
      </figure>
    </li>
    <li>
      <div class="step-title">Verify the background service</div>
      <p>On first run Allow2 installs its <code>systemd --user</code> service, the
      auto-update timer, and enables <em>linger</em> (so it keeps running when
      you're in Game Mode / logged out). Confirm:</p>
      <pre><code>systemctl --user status allow2linux.service        # active (running)
systemctl --user list-timers | grep allow2linux    # update timer scheduled
loginctl show-user "\$USER" | grep Linger            # Linger=yes</code></pre>
      <figure class="shot">
        <img src="images/service-verify.png" alt="Konsole showing systemctl --user status allow2linux.service active running" loading="lazy"
             onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">
        <figcaption class="shot-ph" style="display:none">Konsole showing systemctl --user status allow2linux.service reporting active (running)</figcaption>
      </figure>
      <p class="hint">If first-run couldn't set it up, run
      <code>scripts/install-service.sh</code> from the repo (or the manual
      <code>systemctl --user</code> / <code>loginctl enable-linger</code> steps).</p>
    </li>
    <li>
      <div class="step-title">Back to Game Mode</div>
      <p>Double-click <strong>Return to Gaming Mode</strong> on the desktop. Allow2
      keeps running in the background thanks to linger.</p>
      <figure class="shot">
        <img src="images/game-mode.png" alt="Steam Deck desktop showing the 'Return to Gaming Mode' icon" loading="lazy"
             onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">
        <figcaption class="shot-ph" style="display:none">Steam Deck desktop with the "Return to Gaming Mode" icon</figcaption>
      </figure>
    </li>
  </ol>

  <h2>Automatic updates</h2>
  <p>You're done. New versions arrive automatically (the update timer runs
  ~15&nbsp;min after boot and every 6&nbsp;hours). To force an update now:</p>
  <pre><code>flatpak update -y ${APP_ID}</code></pre>

  <h2>Stop kids bypassing it</h2>
  <p>On a Steam Deck the easy escape is dropping into Desktop Mode to switch the
  service off. A few Steam settings close that door. See
  <a href="${HARDENING_URL}">Harden Allow2 against bypass on Steam Deck</a> for the
  layered steps (and an honest account of what they do and don't stop).</p>
  ${CROSS_NOTE}

  <footer>Allow2 Parental Freedom &middot; ${CHANNEL_LABEL} channel</footer>
</div>
</body>
</html>
EOF

echo "==> install page: channel=${CHANNEL} (${CHANNEL_LABEL}) → ${SUBDIR}/index.html"
echo "    install button → ${REF_URL}"
if [ "${IS_STAGING}" -eq 1 ]; then
  echo "    marked noindex/nofollow + internal-only banner; no link to public/stable"
fi
echo "    wrote ${OUT}"
