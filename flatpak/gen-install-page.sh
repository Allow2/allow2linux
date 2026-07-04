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
# The walkthrough is a GUIDED ACCORDION STEPPER: each step is an independent
# <details> panel (native, no framework). Progressive enhancement is
# non-negotiable — every step, every fallback and every command is fully
# readable and expandable with JavaScript DISABLED. JS only ADDS collapse-on-
# advance, reveal-the-fallback, done-ticks, smooth scroll, and the Step 0
# user-agent behaviour. Nothing is ever hidden behind JS such that a no-JS
# reader cannot reach it.
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
      This build targets <code>staging-api.allow2.com</code>. The repo is
      GPG-signed and served over HTTPS. Do not share this page or link,
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

# ── Step 0: shared, user-agent-aware "start on the device" panel ──────────────
# Kept as a reusable shell function (not a separate file) so future generators
# can source this script and reuse it if needed. It bakes NO per-channel value
# (the install entry point https://get.allow2.com/install is fixed), so it is a
# quoted heredoc: fully literal, no shell expansion. Off-device (default, and
# the no-JS state) it stays expanded and prominent; the Step 0 UA logic in the
# page script collapses it to a green tick on an actual Steam Deck.
emit_step0() {
  cat <<'STEP0'
    <details class="step" id="step-0" open>
      <summary><span class="step-num">0</span><span class="step-title">Start on the device you're setting up</span></summary>
      <div class="step-body">
        <p>Do this on the <strong>Steam Deck</strong> itself (or the x86_64 Linux
        desktop you are setting up), not on your phone or another computer. On
        that device, open a web browser and go to:</p>
        <pre class="copyable"><code>https://get.allow2.com/install</code></pre>
        <p class="hint">On a phone or laptop right now? Copy this link and open
        it on the Steam Deck.</p>
        <div class="step-actions">
          <button type="button" class="btn ghost" data-copy="https://get.allow2.com/install">Copy link</button>
          <button type="button" class="btn primary" data-advance>I'm on the device, continue</button>
        </div>
      </div>
    </details>
STEP0
}
STEP0_PANEL="$(emit_step0)"

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
  /* Tap-to-copy affordance (finger-driven on the Deck; also works with a click/keyboard). */
  .copyable { position: relative; cursor: pointer; }
  .copyable:focus-visible { outline: 2px solid #1f6feb; outline-offset: 2px; }
  pre.copyable { padding-right: 96px; }
  .copy-hint {
    position: absolute; top: 8px; right: 8px;
    font-size: .72rem; font-weight: 600; letter-spacing: .02em;
    color: #8b949e; background: #0d1117; border: 1px solid #30363d;
    border-radius: 6px; padding: 3px 9px; pointer-events: none; user-select: none;
  }
  .copyable:hover .copy-hint { color: #e6edf3; border-color: #6e7681; }
  .copyable.copied .copy-hint { color: #3fb950; border-color: #3fb950; background: #0f1c12; }

  /* ── Accordion stepper ─────────────────────────────────────────────────── */
  /* Each step is an independent <details>. Not auto-hiding: several may be open
     at once. scroll-margin-top gives smooth-scroll an offset so an opened panel
     is never tucked under the top of the viewport (or a future sticky header). */
  .steps { margin: 8px 0 0; }
  details.step {
    border: 1px solid #30363d; border-radius: 10px;
    background: #0f141a; margin: 14px 0; overflow: hidden;
    scroll-margin-top: 16px;
  }
  details.step > summary {
    list-style: none; cursor: pointer; user-select: none;
    display: flex; align-items: center; gap: 12px;
    padding: 16px 18px; font-weight: 600; font-size: 1.05rem;
  }
  details.step > summary::-webkit-details-marker { display: none; }
  details.step > summary::after {
    content: ""; flex: none; margin-left: auto;
    width: 9px; height: 9px; border-right: 2px solid #8b949e;
    border-bottom: 2px solid #8b949e; transform: rotate(45deg);
    transition: transform .15s ease;
  }
  details.step[open] > summary::after { transform: rotate(-135deg); }
  details.step[open] > summary { border-bottom: 1px solid #21262d; }
  .step-num {
    flex: none; width: 30px; height: 30px; border-radius: 50%;
    background: #1f6feb; color: #fff; font-weight: 700; font-size: .95rem;
    display: flex; align-items: center; justify-content: center;
  }
  details.step.done > summary .step-num { background: #238636; }
  .step-title { flex: 1 1 auto; }
  .step-body { padding: 6px 18px 20px; }
  .step-body > figure.shot:first-child,
  .step-body > p:first-child { margin-top: 4px; }
  .hint { color: #8b949e; font-size: .9rem; }

  /* Step footer buttons + the inline "didn't work" fallback panel. */
  .step-actions { display: flex; flex-wrap: wrap; gap: 10px; margin-top: 18px; }
  .btn {
    display: inline-flex; align-items: center; gap: 6px;
    padding: 9px 16px; border-radius: 8px; cursor: pointer;
    font: inherit; font-weight: 600; font-size: .92rem;
    border: 1px solid #30363d; background: #21262d; color: #e6edf3;
    text-decoration: none;
  }
  .btn:hover { border-color: #6e7681; }
  .btn:focus-visible { outline: 2px solid #1f6feb; outline-offset: 2px; }
  .btn.primary { background: #238636; border-color: #2ea043; color: #fff; }
  .btn.primary:hover { background: #2ea043; }
  .btn.ghost { background: transparent; }
  .btn.copied { border-color: #3fb950; color: #3fb950; }
  details.fallback {
    margin-top: 16px; border: 1px solid #30363d; border-radius: 8px;
    background: #0b0f14; scroll-margin-top: 16px;
  }
  details.fallback > summary {
    list-style: none; cursor: pointer; user-select: none;
    padding: 12px 16px; font-weight: 600; font-size: .95rem; color: #d29922;
  }
  details.fallback > summary::-webkit-details-marker { display: none; }
  details.fallback[open] > summary { border-bottom: 1px solid #21262d; }
  .fallback-body { padding: 12px 16px 16px; }

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
  <strong>Desktop Mode</strong>. Work through the steps below: tap a step to open
  it, and use the buttons at the bottom of each step to move on.</p>

  <div class="steps">
${STEP0_PANEL}

    <details class="step" id="step-1" open>
      <summary><span class="step-num">1</span><span class="step-title">Switch to Desktop Mode</span></summary>
      <div class="step-body">
        <p>Hold the <strong>STEAM</strong> button &rarr; <strong>Power</strong> &rarr;
        <strong>Switch to Desktop</strong>. (On a regular Linux PC you're already
        on the desktop, so skip this.)</p>
        <figure class="shot">
          <img src="images/desktop-mode.png" alt="Steam Deck Power menu open with 'Switch to Desktop' highlighted" loading="lazy"
               onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">
          <figcaption class="shot-ph" style="display:none">Steam Deck Power menu open, "Switch to Desktop" highlighted</figcaption>
        </figure>
        <div class="step-actions">
          <button type="button" class="btn primary" data-advance>Next</button>
        </div>
      </div>
    </details>

    <details class="step" id="step-2">
      <summary><span class="step-num">2</span><span class="step-title">Install and launch Allow2</span></summary>
      <div class="step-body">
        <p>Open <strong>Konsole</strong> from the <strong>Application Launcher &rarr;
        System</strong>. Tap the command to copy it, paste it into Konsole, and press
        <strong>Enter</strong>. It adds the Flathub runtime remote if it is missing,
        installs Allow2 from the <strong>${CHANNEL_LABEL}</strong> channel
        (<code>${SUBDIR}</code> remote, <code>Branch=${APP_BRANCH}</code>), and
        launches straight into pairing:</p>
        <pre class="copyable"><code>flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo &amp;&amp; flatpak install --user -y --from ${REF_URL} &amp;&amp; flatpak run com.allow2.allow2linux</code></pre>
        <p class="hint">First run sets up the background service and opens the pairing
        screen. In the <strong>Allow2 app</strong>, scan the <strong>QR code</strong>
        (or enter the <strong>6-digit PIN</strong>) and pick the child. That's the
        pairing done.</p>
        <figure class="shot">
          <img src="images/flathub-konsole.png" alt="Konsole terminal running the flatpak command that installs and launches Allow2" loading="lazy"
               onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">
          <figcaption class="shot-ph" style="display:none">Konsole terminal running the flatpak install-and-launch command</figcaption>
        </figure>
        <figure class="shot">
          <img src="images/pairing-qr.png" alt="Allow2 pairing screen showing the QR code and 6-digit PIN" loading="lazy"
               onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">
          <figcaption class="shot-ph" style="display:none">Allow2 pairing screen showing the QR code and the 6-digit PIN</figcaption>
        </figure>
        <div class="step-actions">
          <button type="button" class="btn primary" data-advance>It worked</button>
          <button type="button" class="btn" data-fail data-fallback="fb-2" data-track="deck-install-step-2-failed">Didn't work</button>
        </div>
        <details class="fallback" id="fb-2">
          <summary>Command didn't run? Try the one-click install file</summary>
          <div class="fallback-body">
            <p>If you would rather not use the terminal, or the command above failed,
            open the install file directly:</p>
            <p><a class="btn" href="${REF_URL}">Open the .flatpakref install file</a></p>
            <p class="hint">If it downloads instead of installing, open it from your
            Downloads folder (double-click it) and confirm the install. Then run
            <code>flatpak run com.allow2.allow2linux</code> in Konsole to launch
            pairing.</p>
            <div class="step-actions">
              <button type="button" class="btn primary" data-advance>That worked</button>
              <a class="btn ghost" href="mailto:support@allow2.com?subject=Steam%20Deck%20install%20trouble">Still stuck? Email support</a>
            </div>
          </div>
        </details>
      </div>
    </details>

    <details class="step" id="step-3">
      <summary><span class="step-num">3</span><span class="step-title">Keep it running after a reboot</span></summary>
      <div class="step-body">
        <p>Enable <em>linger</em> so the background service survives a reboot and
        Game Mode. Tap to copy, run it in Konsole, and enter the <code>deck</code>
        password when <code>sudo</code> asks:</p>
        <pre class="copyable"><code>sudo loginctl enable-linger deck</code></pre>
        <div class="step-actions">
          <button type="button" class="btn primary" data-advance>Next</button>
        </div>
      </div>
    </details>

    <details class="step" id="step-4">
      <summary><span class="step-num">4</span><span class="step-title">Confirm it worked (optional)</span></summary>
      <div class="step-body">
        <p>First run installs the <code>systemd --user</code> service and the
        auto-update timer. Check the service is running, the update timer is
        scheduled, and linger is on:</p>
        <pre class="copyable"><code>systemctl --user status allow2linux.service        # active (running)
systemctl --user list-timers | grep allow2linux    # update timer scheduled
loginctl show-user "\$USER" | grep Linger            # Linger=yes</code></pre>
        <figure class="shot">
          <img src="images/service-verify.png" alt="Konsole showing systemctl --user status allow2linux.service active running" loading="lazy"
               onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">
          <figcaption class="shot-ph" style="display:none">Konsole showing systemctl --user status allow2linux.service reporting active (running)</figcaption>
        </figure>
        <div class="step-actions">
          <button type="button" class="btn primary" data-advance>All good</button>
          <button type="button" class="btn" data-fail data-fallback="fb-4" data-track="deck-install-step-4-failed">Didn't work</button>
        </div>
        <details class="fallback" id="fb-4">
          <summary>Service not running? Set it up by hand</summary>
          <div class="fallback-body">
            <p>If first-run couldn't set it up, run the installer from the repo:</p>
            <pre class="copyable"><code>scripts/install-service.sh</code></pre>
            <p class="hint">Or do it manually with the <code>systemctl --user</code>
            enable/start commands and <code>loginctl enable-linger deck</code> from
            the previous step, then re-run the checks above.</p>
            <div class="step-actions">
              <button type="button" class="btn primary" data-advance>That worked</button>
              <a class="btn ghost" href="mailto:support@allow2.com?subject=Steam%20Deck%20service%20setup">Still stuck? Email support</a>
            </div>
          </div>
        </details>
      </div>
    </details>

    <details class="step" id="step-5">
      <summary><span class="step-num">5</span><span class="step-title">Back to Game Mode</span></summary>
      <div class="step-body">
        <p>Double-click <strong>Return to Gaming Mode</strong> on the desktop. Allow2
        keeps running in the background thanks to linger.</p>
        <figure class="shot">
          <img src="images/game-mode.png" alt="Steam Deck desktop showing the 'Return to Gaming Mode' icon" loading="lazy"
               onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">
          <figcaption class="shot-ph" style="display:none">Steam Deck desktop with the "Return to Gaming Mode" icon</figcaption>
        </figure>
        <div class="step-actions">
          <button type="button" class="btn primary" data-advance>Done</button>
        </div>
      </div>
    </details>
  </div>

  <h2 id="after-steps">Automatic updates</h2>
  <p>You're done. New versions arrive automatically (the update timer runs
  ~15&nbsp;min after boot and every 6&nbsp;hours). To force an update now:</p>
  <pre class="copyable"><code>flatpak update --user -y ${APP_ID}</code></pre>

  <h2>Stop kids bypassing it</h2>
  <p>On a Steam Deck the easy escape is dropping into Desktop Mode to switch the
  service off. A few Steam settings close that door. See
  <a href="${HARDENING_URL}">Harden Allow2 against bypass on Steam Deck</a> for the
  layered steps (and an honest account of what they do and don't stop).</p>
  ${CROSS_NOTE}

  <footer>Allow2 Parental Freedom &middot; ${CHANNEL_LABEL} channel</footer>
</div>
<script>
// Self-contained, no external deps. Two layers:
//   1. tap-to-copy on every command block + explicit copy buttons (always on).
//   2. accordion behaviour: advance/collapse, reveal fallback, done-ticks,
//      smooth scroll, and the Step 0 user-agent detection.
// Progressive enhancement: with JS off, every <details> (steps AND fallbacks)
// is still openable by tapping its summary, and every command/link is present.
(function () {
  function copyText(t) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      return navigator.clipboard.writeText(t);
    }
    return new Promise(function (resolve, reject) {
      try {
        var ta = document.createElement('textarea');
        ta.value = t; ta.setAttribute('readonly', '');
        ta.style.position = 'absolute'; ta.style.left = '-9999px';
        document.body.appendChild(ta); ta.select();
        document.execCommand('copy');
        document.body.removeChild(ta);
        resolve();
      } catch (e) { reject(e); }
    });
  }

  // Inert analytics stub. No network, no PII. Wire to real analytics later.
  window.a2track = window.a2track || function () {};

  // ── 1. tap-to-copy on command blocks ──────────────────────────────────────
  function enhanceCopyable(el) {
    var cmd = el.textContent.trim();
    var idle = 'Tap to copy';
    el.setAttribute('role', 'button');
    el.setAttribute('tabindex', '0');
    el.setAttribute('aria-label', 'Copy command to clipboard');
    var hint = document.createElement('span');
    hint.className = 'copy-hint';
    hint.setAttribute('aria-hidden', 'true');
    hint.textContent = idle;
    el.appendChild(hint);
    var timer = null;
    function flash() {
      el.classList.add('copied');
      hint.textContent = 'Copied!';
      if (timer) { clearTimeout(timer); }
      timer = setTimeout(function () {
        el.classList.remove('copied');
        hint.textContent = idle;
      }, 1500);
    }
    function activate() { copyText(cmd).then(flash).catch(function () {}); }
    el.addEventListener('click', activate);
    el.addEventListener('keydown', function (e) {
      if (e.key === 'Enter' || e.key === ' ' || e.key === 'Spacebar') {
        e.preventDefault(); activate();
      }
    });
  }

  // Explicit "Copy link" style buttons carrying their text in data-copy.
  function enhanceCopyButton(btn) {
    var text = btn.getAttribute('data-copy');
    var label = btn.textContent;
    var timer = null;
    btn.addEventListener('click', function () {
      copyText(text).then(function () {
        btn.classList.add('copied');
        btn.textContent = 'Copied!';
        if (timer) { clearTimeout(timer); }
        timer = setTimeout(function () {
          btn.classList.remove('copied');
          btn.textContent = label;
        }, 1500);
      }).catch(function () {});
    });
  }

  // ── 2. accordion ──────────────────────────────────────────────────────────
  var steps = [];
  function collectSteps() {
    steps = Array.prototype.slice.call(document.querySelectorAll('details.step'));
  }
  function reveal(el) {
    if (!el) { return; }
    // scroll-margin-top (CSS) supplies the offset so the panel top clears any
    // sticky header rather than hiding under it.
    if (el.scrollIntoView) {
      el.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }
  }
  function markDone(step, doneLabel) {
    step.classList.add('done');
    var num = step.querySelector('.step-num');
    if (num) { num.textContent = '✓'; }
    if (doneLabel) {
      var title = step.querySelector('.step-title');
      if (title) { title.textContent = doneLabel; }
    }
  }
  function advance(step) {
    markDone(step);
    step.open = false;
    var i = steps.indexOf(step);
    var next = (i === -1) ? null : steps[i + 1];
    if (next) {
      next.open = true;
      reveal(next);
    } else {
      reveal(document.getElementById('after-steps'));
    }
  }
  function onAdvanceClick(e) {
    // Works from a step footer button AND from a "worked" button inside a
    // fallback: closest('details.step') resolves to the owning step either way.
    var step = e.currentTarget.closest('details.step');
    if (step) { advance(step); }
  }
  function onFailClick(e) {
    var btn = e.currentTarget;
    var track = btn.getAttribute('data-track');
    if (track) { try { window.a2track && a2track(track); } catch (err) {} }
    var fb = document.getElementById(btn.getAttribute('data-fallback'));
    if (fb) {
      fb.open = true;          // reveal the fallback, do NOT advance
      reveal(fb);
    }
  }

  function initAccordion() {
    collectSteps();
    Array.prototype.forEach.call(document.querySelectorAll('[data-advance]'), function (b) {
      b.addEventListener('click', onAdvanceClick);
    });
    Array.prototype.forEach.call(document.querySelectorAll('[data-fail]'), function (b) {
      b.addEventListener('click', onFailClick);
    });
    // Step 0 user-agent behaviour: on an actual Steam Deck / SteamOS, collapse
    // Step 0 to a green tick and open Step 1. Off-device (and no-JS) it stays in
    // its expanded "start on the device" state, which is the safe default.
    var onDeck = /SteamOS|Steam ?Deck|Valve/i.test(navigator.userAgent || '');
    if (onDeck) {
      var step0 = document.getElementById('step-0');
      if (step0) { markDone(step0, "You're on your Steam Deck"); step0.open = false; }
      var step1 = document.getElementById('step-1');
      if (step1) { step1.open = true; }
    }
  }

  function init() {
    Array.prototype.forEach.call(document.querySelectorAll('.copyable'), enhanceCopyable);
    Array.prototype.forEach.call(document.querySelectorAll('[data-copy]'), enhanceCopyButton);
    initAccordion();
  }
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else { init(); }
})();
</script>
</body>
</html>
EOF

echo "==> install page: channel=${CHANNEL} (${CHANNEL_LABEL}) → ${SUBDIR}/index.html"
echo "    install command ref → ${REF_URL}"
if [ "${IS_STAGING}" -eq 1 ]; then
  echo "    marked noindex/nofollow + internal-only banner; no link to public/stable"
fi
echo "    wrote ${OUT}"
