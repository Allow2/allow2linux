#!/usr/bin/env bash
#
# gen-hardening-page.sh — generate the channel-specific static "harden against
# bypass" walkthrough (hardening.html) that is published alongside the install
# page (index.html) into each channel's R2 prefix and served at
# https://get.allow2.com/steamdeck/<channel>/hardening.html.
#
# ONE generator, TWO channels — mirrors gen-install-page.sh exactly: a single
# HTML body with per-channel values (title, robots meta, internal-beta banner,
# baked cross-link URL back to the install page) baked in from shell variables.
# No divergent per-channel HTML files (no drift). This is the SINGLE source of
# truth for the hardening page; the old static draft (site/hardening.draft.html)
# has been retired in favour of this generator.
#
# ── Channels ─────────────────────────────────────────────────────────────────
#   staging     (BETA, prefix /steamdeck/staging/):
#               INTERNAL TESTER ONLY. The page carries a prominent internal-beta
#               banner + security note, is marked noindex/nofollow, and NEVER
#               links to or advertises the stable/public channel (its cross-link
#               back to the install page stays inside /staging/).
#
#   production  (STABLE, prefix /steamdeck/stable/):
#               the PUBLIC-facing hardening page. Indexable, no beta references.
#               Never cross-links to staging.
#
# ── Usage ────────────────────────────────────────────────────────────────────
#   ./gen-hardening-page.sh <staging|production> [output-path]
#   CHANNEL=staging ./gen-hardening-page.sh                   (env fallback)
# Default CHANNEL is production (the safe default). Default output is
# ./hardening-page-<subdir>.html next to this script; it is uploaded as
# hardening.html by publish.sh.
#
# ── Options (env overrides) ──────────────────────────────────────────────────
#   PUBLIC_URL   default: https://get.allow2.com/steamdeck   (base; /<subdir>/…)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

CHANNEL="${1:-${CHANNEL:-production}}"
PUBLIC_URL_BASE="${PUBLIC_URL:-https://get.allow2.com/steamdeck}"

# ── Channel → subdir + labels (mirrors gen-install-page.sh) ───────────────────
case "${CHANNEL}" in
  staging|beta)
    SUBDIR="staging"
    CHANNEL_LABEL="Internal Beta (staging)"
    IS_STAGING=1
    ;;
  production|stable|prod)
    SUBDIR="stable"
    CHANNEL_LABEL="Stable"
    IS_STAGING=0
    ;;
  *)
    echo "ERROR: unknown CHANNEL='${CHANNEL}' (expected: staging|production)" >&2
    exit 1
    ;;
esac

OUT="${2:-${SCRIPT_DIR}/hardening-page-${SUBDIR}.html}"

# Per-channel cross-link back to THIS channel's install page. Absolute + baked so
# staging links to staging and stable links to stable — never cross-channel.
INSTALL_URL="${PUBLIC_URL_BASE}/${SUBDIR}/index.html"

# ── Per-channel security chrome ──────────────────────────────────────────────
# staging: prominent internal-only banner + security note + noindex (no public
#          discoverability). stable: public-facing, indexable, no banner.
if [ "${IS_STAGING}" -eq 1 ]; then
  ROBOTS_META='<meta name="robots" content="noindex,nofollow">'
  PAGE_TITLE="Harden allow2linux on Steam Deck: Internal Beta (staging)"
  SECURITY_BANNER='<div class="callout bad">
    <p style="margin:0"><strong>INTERNAL BETA (staging). For the internal tester only.</strong>
    This hardening guide accompanies the beta build that targets
    <code>staging-api.allow2.com</code> and is unsigned
    (<code>gpg-verify=false</code> over HTTPS). Do not share this page or link,
    and never promote it to the stable/public channel.</p>
  </div>'
  # Staging-only internal pointer to the sourced notes + coverage matrix.
  INTERNAL_NOTE='<p class="src">Internal (staging) only. Full sourced notes &amp; the coverage matrix:
  <code>examples/linux/docs/STEAM_FAMILY_HARDENING.md</code>.</p>'
else
  ROBOTS_META=''
  PAGE_TITLE="Harden allow2linux on Steam Deck &middot; Allow2 Parental Freedom"
  SECURITY_BANNER=''
  INTERNAL_NOTE=''
fi

# ── Emit the page (unquoted heredoc: shell vars expand; no literal $ in body) ─
cat > "${OUT}" <<EOF
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
${ROBOTS_META}
<title>${PAGE_TITLE}</title>
<meta name="description" content="Layered steps to make allow2linux much harder to bypass on a Steam Deck using Steam Families / Family View, plus an honest account of what these controls do and don't stop.">
<style>
  :root{
    --bg:#0d1117; --bg2:#161b22; --card:#161b22; --line:#30363d;
    --ink:#e6edf3; --muted:#9da7b3; --brand:#f5a623; --brand2:#ffc15e;
    --ok:#3fb950; --warn:#d29922; --bad:#f85149; --link:#58a6ff;
    --radius:14px;
  }
  *{box-sizing:border-box}
  html{-webkit-text-size-adjust:100%}
  body{
    margin:0; background:var(--bg); color:var(--ink);
    font:16px/1.6 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Ubuntu,Cantarell,"Noto Sans",sans-serif;
    padding:env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left);
  }
  .wrap{max-width:860px;margin:0 auto;padding:28px 20px 72px}
  a{color:var(--link);text-decoration:none}
  a:hover{text-decoration:underline}
  header.hero{
    background:linear-gradient(160deg,#1b2230 0%,#0d1117 70%);
    border:1px solid var(--line);border-radius:var(--radius);
    padding:26px 24px;margin-bottom:26px;
  }
  .eyebrow{color:var(--brand);font-weight:700;letter-spacing:.06em;text-transform:uppercase;font-size:12px;margin:0 0 8px}
  h1{font-size:clamp(24px,5vw,34px);line-height:1.2;margin:0 0 12px}
  h2{font-size:20px;margin:34px 0 12px;padding-bottom:8px;border-bottom:1px solid var(--line)}
  h3{font-size:16px;margin:22px 0 8px;color:var(--brand2)}
  p{margin:0 0 14px}
  .lede{color:var(--muted);font-size:17px;margin:0}
  .card{background:var(--card);border:1px solid var(--line);border-radius:var(--radius);padding:18px 20px;margin:18px 0}
  .callout{border-left:4px solid var(--brand);background:#1a1712;border-radius:8px;padding:14px 16px;margin:18px 0}
  .callout.honest{border-left-color:var(--warn);background:#1c1809}
  .callout.good{border-left-color:var(--ok);background:#0f1c12}
  .callout.bad{border-left-color:var(--bad);background:#1c1211}
  .callout strong{color:var(--brand2)}
  ol.steps{counter-reset:step;list-style:none;padding:0;margin:0}
  ol.steps>li{
    counter-increment:step;position:relative;padding:12px 12px 12px 52px;margin:10px 0;
    background:var(--bg2);border:1px solid var(--line);border-radius:10px;
  }
  ol.steps>li::before{
    content:counter(step);position:absolute;left:12px;top:12px;
    width:28px;height:28px;border-radius:50%;background:var(--brand);color:#1a1200;
    font-weight:800;display:flex;align-items:center;justify-content:center;font-size:14px;
  }
  code,kbd{background:#0b0f14;border:1px solid var(--line);border-radius:6px;padding:1px 6px;font:13px/1.4 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;color:#e3b341}
  kbd{color:#e6edf3}
  .flag{display:inline-block;font-size:11px;font-weight:700;letter-spacing:.03em;background:#3a2d0a;color:var(--brand2);border:1px solid #5c470f;border-radius:5px;padding:1px 6px;vertical-align:middle}
  table{width:100%;border-collapse:collapse;margin:14px 0;font-size:14px;display:block;overflow-x:auto}
  th,td{border:1px solid var(--line);padding:9px 10px;text-align:left;vertical-align:top}
  th{background:#1b2230;color:var(--brand2);font-size:12px;text-transform:uppercase;letter-spacing:.04em}
  .pill{display:inline-block;font-weight:700;font-size:12px;border-radius:20px;padding:2px 10px;white-space:nowrap}
  .pill.block{background:#0f2c17;color:#56d364;border:1px solid #1c5a2e}
  .pill.raise{background:#2c2409;color:#e3b341;border:1px solid #5c470f}
  .pill.none{background:#2c1211;color:#ff7b72;border:1px solid #6e2420}
  .pill.detect{background:#10251c;color:#6fd3a0;border:1px solid #1c5a3a}
  .two{display:grid;grid-template-columns:1fr 1fr;gap:14px}
  @media(max-width:620px){.two{grid-template-columns:1fr}}
  .box-title{font-weight:700;margin:0 0 8px;display:flex;align-items:center;gap:8px}
  .dot{width:10px;height:10px;border-radius:50%;display:inline-block}
  .dot.g{background:var(--ok)} .dot.r{background:var(--bad)}
  ul.tight{margin:6px 0 0;padding-left:20px}
  ul.tight li{margin:4px 0}
  .backlink{display:inline-block;margin-top:8px;background:var(--brand);color:#1a1200;font-weight:700;border-radius:10px;padding:11px 18px}
  .backlink:hover{background:var(--brand2);text-decoration:none}
  footer{margin-top:44px;padding-top:18px;border-top:1px solid var(--line);color:var(--muted);font-size:13px}
  .src{font-size:13px;color:var(--muted)}
  .src a{word-break:break-all}
  figure.shot{margin:14px 0}
  figure.shot img{display:block;max-width:100%;height:auto;border:1px solid var(--line);border-radius:10px}
  .shot-ph{align-items:center;gap:10px;padding:16px;border:1px dashed var(--line);border-radius:10px;background:var(--bg2);color:var(--muted);font-size:14px}
  .shot-ph::before{content:"SCREENSHOT";flex:none;font-size:10px;font-weight:700;letter-spacing:.08em;color:var(--brand2);border:1px solid #5c470f;border-radius:5px;padding:3px 6px;background:#3a2d0a}
</style>
</head>
<body>
<div class="wrap">

<header class="hero">
  <p class="eyebrow">Allow2 &middot; Parental Freedom for Linux</p>
  <h1>Harden allow2linux against bypass on Steam Deck</h1>
  <p class="lede">Layered steps that make it much harder for a child to switch off allow2linux,
  using Steam&rsquo;s own <strong>Family&nbsp;/&nbsp;Steam&nbsp;Families</strong> controls to lock down the
  Steam Deck. Honest about what it stops, and what it can&rsquo;t.</p>
</header>

${SECURITY_BANNER}

<div class="callout honest">
  <p style="margin:0"><strong>Read this first: this is hardening, not a lock.</strong> These steps make
  bypassing allow2linux <em>much harder</em>. No honest parental app claims to be unbreakable, and neither do
  we: any on-device control has a ceiling on a <em>stock</em> Steam Deck. What makes tampering a losing move is
  that Allow2 lives <strong>off the device</strong>. It records each Deck&rsquo;s check-ins and
  <strong>tells you when one goes dark</strong>, so a bypass gets the child caught rather than rewarded. See
  <a href="#backstop">&ldquo;Even if it&rsquo;s bypassed&rdquo;</a> at the bottom.</p>
</div>

<h2 id="why">Why a Steam Deck is easy to bypass</h2>
<p>allow2linux runs as a background service in your child&rsquo;s own session. The quickest way to defeat it is
to leave <strong>Game Mode</strong> for <strong>Desktop Mode</strong> (a full Linux desktop with a
terminal) and simply stop the service. <strong>Game Mode has no terminal</strong>, so if your child
can&rsquo;t reach Desktop Mode, that easy escape closes. That single lock is the highest-value step on this page.</p>

<h2 id="step1">Step 1: PIN-lock Desktop Mode (the biggest single win)</h2>
<p>Steam Deck can require your parental PIN before it will switch from Game Mode into Desktop Mode. Turn it on.</p>
<ol class="steps">
  <li>On the Deck in <strong>Game Mode</strong>, open <strong>Settings</strong>.</li>
  <li>Open the <strong>Security</strong> section. <span class="flag">verify on your Deck</span>
      <br><span class="src">The 2024&ndash;25 Steam Families revamp moved some menus; on some builds these
      toggles live inside the parental-controls flow rather than a top-level &ldquo;Security&rdquo; page.</span></li>
  <li>Turn <strong>ON</strong>: <code>When switching to desktop mode</code>. This forces a PIN prompt before
      Game&nbsp;Mode&nbsp;&rarr;&nbsp;Desktop&nbsp;Mode.</li>
  <li>Turn <strong>ON</strong>: <code>Before showing login screen</code>, which also blocks switching to
      a different, unmanaged Steam account.</li>
  <li>Set / confirm the <strong>PIN</strong> so the toggles lock in the ON position. Under Steam Families,
      unlocking a gated feature means <strong>requesting access from the managing adult</strong> (e.g. for an
      hour) rather than a purely local PIN. <span class="flag">confirm wording on your Deck</span></li>
</ol>
<figure class="shot">
  <img src="images/security-toggles.png" alt="Steam Deck Game Mode Security settings with the desktop-mode PIN toggle turned on" loading="lazy"
       onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">
  <figcaption class="shot-ph" style="display:none">Steam Deck Game Mode Settings, Security page with "When switching to desktop mode" toggled ON</figcaption>
</figure>
<div class="callout bad">
  <p style="margin:0"><strong>This only works if your child does NOT have the Steam account password or the
  family email.</strong> Either one can reset the PIN and re-open Desktop Mode. Keep both away from the child.
  Some guides say Desktop Mode is <em>locked by default</em> for child accounts, but don&rsquo;t assume it;
  <strong>check it is actually ON</strong>. <span class="flag">verify on your Deck</span></p>
</div>

<h2 id="step2">Step 2: Add Steam Families playtime limits (a second, independent layer)</h2>
<p>These don&rsquo;t protect allow2linux, but they add Steam&rsquo;s own limits for Steam games, so even
if allow2linux is defeated, Steam still enforces time limits on Steam content.</p>
<ol class="steps">
  <li>On the <strong>parent&rsquo;s</strong> Steam account: <code>Steam &rarr; Settings &rarr; Family</code>.</li>
  <li><strong>Create a Steam Family</strong>, then add your child. (Your child must first be a Steam
      <strong>Friend</strong> of your account.)</li>
  <li><code>Manage your Steam Family</code> &rarr; open the child &rarr; enable
      <code>Enable parental controls for this user</code>. (May ask for a Steam Mobile Authenticator code.)</li>
  <li>Turn on <strong>playtime limits</strong>: set a daily <strong>Time Limit</strong> and/or
      <strong>scheduled access windows</strong> (e.g. only 3pm&ndash;8pm).</li>
  <li>Optionally restrict the <strong>game allow-list</strong>, <strong>purchases</strong>, and
      <strong>chat/community</strong>.</li>
</ol>
<figure class="shot">
  <img src="images/steam-family.png" alt="Steam Settings Family page showing Manage Steam Family with playtime limits" loading="lazy"
       onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">
  <figcaption class="shot-ph" style="display:none">Steam Settings, Family page: Manage Steam Family with playtime limits and access windows</figcaption>
</figure>
<div class="callout">
  <p style="margin:0"><strong>What Steam Families does <em>not</em> cover:</strong> it&rsquo;s
  <strong>Steam-only</strong> (no desktop apps, browsers, emulators, or non-Steam launchers) and
  <strong>per-account, not per-device</strong>, so a new or offline account sidesteps it. That gap is
  exactly what allow2linux fills: per-device, per-child, all activities, with one shared quota across every
  device your child uses.</p>
</div>

<h2 id="step3">Step 3: Set a sudo / desktop password</h2>
<p>On a fresh Deck the desktop <code>sudo</code> password is unset, so anyone in Desktop Mode has full
control. Set one (as the parent).</p>
<ol class="steps">
  <li>Enter <strong>Desktop Mode</strong> once, yourself.</li>
  <li>Open <strong>Konsole</strong>, run <code>passwd</code>, and set a password only you know.</li>
</ol>
<div class="callout honest">
  <p style="margin:0"><strong>Speed-bump, not a wall.</strong> A sudo password only slows down someone who is
  already in Desktop Mode. With hands-on physical access it can be reset without a credential, as on any
  consumer handheld. Worth doing, but don&rsquo;t rely on it alone: if someone goes that far, the
  <a href="#backstop">backstop below</a> is what still bites.</p>
</div>

<h2 id="matrix">What this actually stops, and what it doesn&rsquo;t</h2>
<p>Honest coverage for each way a child defeats allow2linux:</p>
<table>
  <thead><tr><th>How a child bypasses allow2linux</th><th>Result with the steps above</th></tr></thead>
  <tbody>
    <tr><td>Switch to <strong>Desktop Mode</strong> and stop / disable the service</td>
        <td><span class="pill block">Blocks</span>, if the PIN is set and the child lacks the password/email</td></tr>
    <tr><td>Switch to a <strong>different / new Steam account</strong></td>
        <td><span class="pill raise">Raises the bar</span>: account switching is PIN-gated; a brand-new account still isn&rsquo;t managed</td></tr>
    <tr><td>Add a <strong>new Linux user</strong> to get an unmanaged session</td>
        <td><span class="pill raise">Raises the bar</span>: needs Desktop Mode (gated), and allow2linux challenges <em>any</em> unmapped account anyway</td></tr>
    <tr><td>Enable <strong>Developer Mode</strong></td>
        <td><span class="pill raise">Raises the bar</span>: indirectly, via the Desktop Mode lock <span class="flag">unverified</span></td></tr>
    <tr><td>Wiping or replacing the operating system entirely (past any on-device control)</td>
        <td><span class="pill detect">Detected off-device</span>: no on-device app survives this, but the Deck goes silent and Allow2 flags it to you (see the <a href="#backstop">backstop</a>)</td></tr>
  </tbody>
</table>

<div class="two">
  <div class="card">
    <p class="box-title"><span class="dot g"></span>What these steps genuinely lock</p>
    <ul class="tight">
      <li>The easy, no-tools, no-reboot escape: dropping into Desktop Mode to kill the service.</li>
      <li>Swapping to another Steam account to dodge limits.</li>
      <li>A second, Steam-enforced time limit on Steam games.</li>
    </ul>
  </div>
  <div class="card">
    <p class="box-title"><span class="dot r"></span>What sits past any on-device control</p>
    <ul class="tight">
      <li>Wiping or replacing the whole operating system. No parental app on any device survives that.</li>
      <li>Anything done once someone has the Steam password or family email (keep both away from the child).</li>
      <li>The honest answer to both: Allow2 <strong>notices the Deck going dark and tells you</strong>, so it
      becomes a conversation, not a silent free pass.</li>
    </ul>
  </div>
</div>

<h2 id="backstop">Even if it&rsquo;s bypassed: the backstop</h2>
<div class="callout good">
  <p style="margin:0 0 10px"><strong>Bypassing allow2linux is not consequence-free, because Allow2 is the
  authority and it lives off the device.</strong></p>
  <ul class="tight" style="padding-left:18px">
    <li>Your child&rsquo;s time is a <strong>single shared quota per activity, across every device</strong>
    (Deck, console, phone), not a per-device timer.</li>
    <li>Every device <strong>checks in with Allow2</strong>. When the Deck goes dark, Allow2 is designed to
    <strong>alert you</strong> (&ldquo;haven&rsquo;t heard from this device in a while&rdquo;), so tampering
    surfaces instead of hiding.</li>
    <li>Time gamed while bypassed is designed to be <strong>reconciled against that shared pool</strong>, so it
    comes off the <em>next</em> allowance, on the Deck or any other device. Tampering borrows time the platform
    claws back; it does not earn free time.</li>
    <li>Net effect: a bypass becomes a <strong>conversation with a child who got caught</strong>, not a silent win.</li>
  </ul>
</div>
<p class="src">Positioning: coarse on-device control is a <strong>deterrent</strong>; the durable strength is the
layered stack <em>plus</em> Allow2&rsquo;s off-device authority that detects and reconciles overage. (Full
cross-device reconciliation and go-dark alerting are on the allow2linux alpha roadmap; the pooled-quota
authority is the platform&rsquo;s design.)</p>

<h2 id="durable">For the strongest setup: add off-device controls</h2>
<p>The only layer a child <em>cannot</em> switch off on the device is one that isn&rsquo;t on the device.
Add <strong>router / DNS time and content controls</strong> as the durable backstop. They keep working even if
the Deck&rsquo;s own software is removed, because they live on your network, not the Deck.</p>

<div style="text-align:center;margin-top:30px">
  <a class="backlink" href="${INSTALL_URL}">&larr; Back to the install page</a>
  <p class="src" style="margin-top:10px">Or start at <a href="${PUBLIC_URL_BASE}/${SUBDIR}/">get.allow2.com/steamdeck/${SUBDIR}</a></p>
</div>

<footer>
  <p><strong>Accuracy note.</strong> Steam revamped its parental system in 2024&ndash;2025
  (&ldquo;Steam&nbsp;Families&rdquo;, replacing the older &ldquo;Family&nbsp;View&rdquo;). Menu paths above are
  drawn from current guides and community reports; Valve&rsquo;s own help pages did not expose their body text
  to automated checking. <strong>Please confirm the exact wording on your own Deck</strong>, especially items
  marked <span class="flag">verify on your Deck</span> / <span class="flag">unverified</span>.</p>
  <p class="src"><strong>Sources:</strong>
    <a href="https://www.therundown.today/guides/steam-families-parental-controls-complete-setup-guide">Steam Families setup guide (The Rundown)</a> &middot;
    <a href="https://impulsec.com/parental-control-software/parental-controls-steam-deck/">Parental controls on Steam Deck (Impulsec)</a> &middot;
    <a href="https://www.internetmatters.org/parental-controls/gaming-consoles/steam/">Steam parental controls (Internet Matters)</a> &middot;
    <a href="https://stories.truple.io/posts/1f33294e-b756-4e26-b39b-016a7e157b9b/1749176527323">Steam Deck parental controls guide (Truple)</a> &middot;
    <a href="https://help.steampowered.com/en/faqs/view/6B1A-66BE-E911-3D98">Steam Support: Family View</a> &middot;
    <a href="https://www.gamingonlinux.com/guides/view/how-to-set-change-and-reset-your-steamos-steam-deck-desktop-sudo-password/">SteamOS sudo password (GamingOnLinux)</a> &middot;
    <a href="https://help.steampowered.com/en/faqs/view/1B71-EDF2-EB6D-2BB3">Steam Support: Boot Manager / recovery</a>
  </p>
  ${INTERNAL_NOTE}
</footer>

</div>
</body>
</html>
EOF

echo "==> hardening page: channel=${CHANNEL} (${CHANNEL_LABEL}) → ${SUBDIR}/hardening.html"
echo "    back to install → ${INSTALL_URL}"
if [ "${IS_STAGING}" -eq 1 ]; then
  echo "    marked noindex/nofollow + internal-beta banner; no link to public/stable"
fi
echo "    wrote ${OUT}"
