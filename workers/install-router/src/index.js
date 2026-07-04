// get.allow2.com/install — universal, User-Agent-routing install entry point.
//
// One URL for every Allow2 installer. A parent (or a child) hits
// get.allow2.com/install on ANY device; we sniff the User-Agent and 302 them
// to the right per-platform install page. If we can't confidently tell what
// device they're on (very common — see the SteamOS caveat below, and the
// "parent setting up from an iPad" case), we serve a small self-contained
// "choose your device" page so they can pick.
//
// Dependency-free Cloudflare Worker (ES module format). No external fetches.
//
// This lives in the allow2linux repo for now because the get.allow2.com
// tooling (scripts/publish.sh, flatpak/gen-install-page.sh) lives here. It is
// NOT Steam-Deck-specific — it is the cross-platform funnel entry point.

// ---------------------------------------------------------------------------
// Per-platform install targets. Update these as real pages ship.
// ---------------------------------------------------------------------------

// REAL — the existing Steam Deck / Linux install page published by
// scripts/publish.sh + flatpak/gen-install-page.sh in this repo.
const STEAMDECK_INSTALL_URL = "https://get.allow2.com/steamdeck/stable/";

// PLACEHOLDER — no dedicated Windows install page yet. Defaults to the main
// site so the funnel never dead-ends. Operator: replace with the real URL.
const WINDOWS_INSTALL_URL = "https://allow2.com/";

// PLACEHOLDER — no dedicated macOS install page yet. Defaults to main site.
const MAC_INSTALL_URL = "https://allow2.com/";

// PLACEHOLDER — no dedicated Android install page yet. Defaults to main site.
const ANDROID_INSTALL_URL = "https://allow2.com/";

// ---------------------------------------------------------------------------
// Platform detection.
//
// CAVEAT (SteamOS / Steam Deck): the stock Steam Deck browser User-Agent is
// NOT always distinctive. Some builds report "SteamOS" or reference "Valve",
// but many just look like a generic desktop Linux Chrome/Firefox UA with no
// Deck marker at all. Because the Deck IS the primary target of the Linux
// installer in this repo, we deliberately treat a generic Linux-desktop UA as
// the Steam Deck / Linux install case. Mobile Linux (Android) is detected
// FIRST and excluded, so this only catches desktop-class Linux.
// ---------------------------------------------------------------------------

function detectPlatform(userAgent) {
  const ua = (userAgent || "").toLowerCase();

  // Android must be checked before generic Linux — Android UAs contain "linux".
  if (ua.includes("android")) return "android";

  // Explicit Steam Deck / SteamOS / Valve markers, when present.
  if (
    ua.includes("steamos") ||
    ua.includes("steam deck") ||
    ua.includes("steamdeck") ||
    ua.includes("valve")
  ) {
    return "steamdeck";
  }

  // iOS / iPadOS — these are parents OFF the target device. Don't guess an
  // installer for them; send them to the chooser so they can pick (or read
  // the "open this on the Deck" hint).
  if (
    ua.includes("iphone") ||
    ua.includes("ipad") ||
    ua.includes("ipod")
  ) {
    return "unknown";
  }

  if (ua.includes("windows")) return "windows";

  // macOS (but not iOS, already excluded above).
  if (ua.includes("mac os x") || ua.includes("macintosh")) return "mac";

  // Generic desktop Linux — treated as the Steam Deck / Linux install (see
  // the CAVEAT above: stock Deck browsers often present as plain Linux).
  if (ua.includes("linux") || ua.includes("x11") || ua.includes("cros")) {
    return "steamdeck";
  }

  return "unknown";
}

function targetFor(platform) {
  switch (platform) {
    case "steamdeck":
      return STEAMDECK_INSTALL_URL;
    case "windows":
      return WINDOWS_INSTALL_URL;
    case "mac":
      return MAC_INSTALL_URL;
    case "android":
      return ANDROID_INSTALL_URL;
    default:
      return null; // -> chooser page
  }
}

// ---------------------------------------------------------------------------
// "Choose your device" fallback page. Self-contained, responsive, no external
// assets. NOTE: visible copy avoids the spaced em-dash (an AI-generated tell)
// per the get.allow2.com published-content style rule.
// ---------------------------------------------------------------------------

function chooserPage() {
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex">
<title>Install Allow2 &middot; Choose your device</title>
<style>
  :root { color-scheme: light dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    line-height: 1.5;
    background: #0f1220;
    color: #f4f5fb;
    display: flex;
    min-height: 100vh;
    align-items: center;
    justify-content: center;
    padding: 1.5rem;
  }
  .card {
    width: 100%;
    max-width: 30rem;
    text-align: center;
  }
  h1 { font-size: 1.5rem; margin: 0 0 .25rem; }
  p.lead { margin: 0 0 1.5rem; color: #b9bdd6; }
  .grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: .75rem;
  }
  @media (max-width: 22rem) { .grid { grid-template-columns: 1fr; } }
  a.btn {
    display: block;
    padding: 1rem;
    border-radius: .75rem;
    background: #1c2140;
    color: #f4f5fb;
    text-decoration: none;
    font-weight: 600;
    font-size: 1.05rem;
    border: 1px solid #2c3360;
    transition: background .15s ease, transform .05s ease;
  }
  a.btn:hover { background: #262c56; }
  a.btn:active { transform: translateY(1px); }
  a.btn .sub { display: block; font-weight: 400; font-size: .8rem; color: #9aa0c4; margin-top: .2rem; }
  .hint {
    margin-top: 1.5rem;
    font-size: .9rem;
    color: #9aa0c4;
    background: #171b30;
    border: 1px solid #2c3360;
    border-radius: .6rem;
    padding: .75rem 1rem;
  }
</style>
</head>
<body>
  <main class="card">
    <h1>Install Allow2</h1>
    <p class="lead">Pick the device you want to set up.</p>
    <div class="grid">
      <a class="btn" href="${STEAMDECK_INSTALL_URL}">Steam Deck / Linux<span class="sub">SteamOS or desktop Linux</span></a>
      <a class="btn" href="${WINDOWS_INSTALL_URL}">Windows<span class="sub">Windows 10 and 11</span></a>
      <a class="btn" href="${MAC_INSTALL_URL}">macOS<span class="sub">Apple silicon and Intel</span></a>
      <a class="btn" href="${ANDROID_INSTALL_URL}">Android<span class="sub">Phones and tablets</span></a>
    </div>
    <p class="hint">Setting up a Steam Deck? Open this page on the Deck itself.</p>
  </main>
</body>
</html>`;
}

// ---------------------------------------------------------------------------
// Worker entry.
// ---------------------------------------------------------------------------

export default {
  async fetch(request) {
    if (request.method !== "GET") {
      return new Response("Method Not Allowed", {
        status: 405,
        headers: { "Allow": "GET", "Content-Type": "text/plain; charset=utf-8" },
      });
    }

    const platform = detectPlatform(request.headers.get("User-Agent"));
    const target = targetFor(platform);

    if (target) {
      return new Response(null, {
        status: 302,
        headers: {
          "Location": target,
          // Don't cache the redirect — routing depends on the User-Agent, and
          // targets change as real pages ship.
          "Cache-Control": "no-store",
          "Vary": "User-Agent",
        },
      });
    }

    return new Response(chooserPage(), {
      status: 200,
      headers: {
        "Content-Type": "text/html; charset=utf-8",
        "Cache-Control": "no-store",
        "Vary": "User-Agent",
      },
    });
  },
};

// Exported for local testing harnesses (see README). Not used by the runtime.
export { detectPlatform, targetFor };
