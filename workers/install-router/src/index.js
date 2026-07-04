// get.allow2.com/install — universal, User-Agent-routing install entry point.
//
// One URL for every Allow2 installer. A parent (or a child) hits
// get.allow2.com/install on ANY device; we sniff the User-Agent and 302 them
// to the right per-platform install page. If we can't confidently tell what
// device they're on (very common — see the SteamOS caveat below, and the
// "parent setting up from an iPad" case), we serve a small self-contained
// "choose your device" page so they can pick.
//
// Dependency-free Cloudflare Worker (ES module format).
//
// CO-HOSTED STAGING + PRODUCTION (same code, two envs):
// The SAME Worker source powers TWO environments, co-hosted on get.allow2.com,
// exactly like get.allow2.com already co-hosts steamdeck/stable + steamdeck/staging:
//   • production — route get.allow2.com/install  → STABLE install pages,
//       manifests at get.allow2.com/install/<platform>.json, indexable.
//   • staging    — route get.allow2.com/staging  → STAGING install pages,
//       manifests at get.allow2.com/staging/install/<platform>.json, noindex.
// The per-env behaviour is NOT hardcoded — it is read from the wrangler
// `[env.<name>.vars]` binding (the fetch handler receives `env`). See config().
//
// DATA-DRIVEN TARGETS (this is the important part):
// The per-platform install URLs are NOT baked into this Worker. At request time
// the Worker fetches a tiny per-platform manifest from R2 — for prod
//   https://get.allow2.com/install/<platform>.json
// and for staging
//   https://get.allow2.com/staging/install/<platform>.json
// — edge-cached for 5 minutes, and 302s to that manifest's `.url`. Each platform
// repo publishes its OWN manifest from its OWN release pipeline (the Steam Deck
// one is written by scripts/publish.sh in this repo). Shipping a new install
// page therefore needs NO Worker redeploy — the publish step just rewrites its
// manifest entry and the edge picks it up within the TTL.
//
// The only things genuinely baked into the Worker are (a) the LIST of known
// platforms and (b) the UA→platform detection logic — that's real logic, not
// configuration. Everything URL-shaped lives in the manifests.
//
// Manifest JSON shape (see README.md "Manifest schema"):
//   {
//     "platform":  "steamdeck",
//     "url":       "https://get.allow2.com/steamdeck/stable/",
//     "label":     "Steam Deck / Linux",     // optional — chooser button title
//     "sub":       "SteamOS or desktop Linux",// optional — chooser button subtitle
//     "updatedAt": "2026-07-04T00:00:00Z"     // optional — informational
//   }
//
// This lives in the allow2linux repo for now because the get.allow2.com
// tooling (scripts/publish.sh, flatpak/gen-install-page.sh) lives here. It is
// NOT Steam-Deck-specific — it is the cross-platform funnel entry point.

// ---------------------------------------------------------------------------
// Baked-in configuration — the ONLY things not sourced from a manifest.
// ---------------------------------------------------------------------------

// The known platforms. This is genuine logic (detectPlatform maps into it, and
// the chooser renders one button per entry). Adding a platform means adding it
// here + shipping its manifest — no URL ever lives in this file.
const KNOWN_PLATFORMS = ["steamdeck", "windows", "mac", "android"];

// FAIL-SAFE fallback. If a manifest is missing / errors / is malformed, redirect
// here instead of dead-ending or throwing. Never leave the user stranded. This
// is the ONE truly hardcoded target: an always-safe, always-live URL that can
// never be misconfigured by an env var.
const DEFAULT_URL = "https://allow2.com/";

// ---------------------------------------------------------------------------
// Per-environment configuration — the channel/prod-vs-staging knobs.
//
// Sourced from the wrangler `[env.<name>.vars]` binding (the fetch handler is
// passed `env`). NOTHING stable/prod-specific is hardcoded here: the two
// co-hosted envs differ ONLY by these vars.
//   CHANNEL         "stable" (prod) | "staging"        — informational tag.
//   MANIFEST_BASE   base for per-platform manifest fetch
//                     prod    https://get.allow2.com/install
//                     staging https://get.allow2.com/staging/install
//   DEFAULT_INSTALL channel-appropriate Steam Deck install page — the fail-safe
//                   target for the deck (the primary Linux target) when its
//                   manifest can't be read.
//                     prod    https://get.allow2.com/steamdeck/stable/
//                     staging https://get.allow2.com/steamdeck/staging/
//   NOINDEX         "1" on staging → emit X-Robots-Tag: noindex, nofollow on
//                   EVERY response; falsy/omitted on prod → never emit it.
//
// Every default below is PROD-SAFE (stable pages, NOT noindex). A mis-set or
// missing env therefore fails toward production behaviour — it can never
// accidentally deindex prod or point prod at staging pages.
function config(env) {
  const e = env || {};
  return {
    channel: e.CHANNEL || "stable",
    manifestBase: e.MANIFEST_BASE || "https://get.allow2.com/install",
    defaultInstall: e.DEFAULT_INSTALL || "https://get.allow2.com/steamdeck/stable/",
    // Truthy only for an explicit opt-in ("1"/"true"). "" / "0" / unset → false.
    noindex: e.NOINDEX === "1" || e.NOINDEX === "true",
  };
}

// The X-Robots-Tag header, added to EVERY response in a noindex (staging) env
// and NEVER in prod. Returned as a spreadable object so each Response builder
// just `...noindexHeader(cfg)` into its headers.
function noindexHeader(cfg) {
  return cfg.noindex ? { "X-Robots-Tag": "noindex, nofollow" } : {};
}

// Built-in chooser labels — used ONLY when a manifest omits label/sub (or can't
// be fetched). The manifest's label/sub win when present.
const PLATFORM_DEFAULTS = {
  steamdeck: { label: "Steam Deck / Linux", sub: "SteamOS or desktop Linux" },
  windows: { label: "Windows", sub: "Windows 10 and 11" },
  mac: { label: "macOS", sub: "Apple silicon and Intel" },
  android: { label: "Android", sub: "Phones and tablets" },
};

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

// ---------------------------------------------------------------------------
// Manifest lookup — the per-file, request-time, edge-cached read from R2.
// ---------------------------------------------------------------------------

// Fetch + validate one platform's manifest. Returns the parsed object on
// success, or null on ANY failure (unknown platform, 404, network error,
// malformed JSON, missing/blank `url`). Callers treat null as "use the
// fail-safe" — this function NEVER throws.
async function fetchManifest(platform, cfg = config()) {
  if (!KNOWN_PLATFORMS.includes(platform)) return null;
  try {
    const res = await fetch(`${cfg.manifestBase}/${platform}.json`, {
      // Edge-cache the manifest so we don't hit R2 on every request. 5 min TTL
      // means a freshly-published install URL is live within 5 minutes even
      // without an explicit purge (publish.sh also purges it for instant pickup).
      cf: { cacheTtl: 300, cacheEverything: true },
    });
    if (!res.ok) return null;
    const data = await res.json();
    if (!data || typeof data.url !== "string" || data.url.length === 0) {
      return null;
    }
    return data;
  } catch (_err) {
    return null;
  }
}

// Resolve a known platform to a redirect target. Fail-safe when the manifest
// can't be read: for the Steam Deck (the primary Linux target) fall back to the
// channel-appropriate DEFAULT_INSTALL page (stable on prod, staging on staging);
// for every other platform fall back to the universal DEFAULT_URL. Both keep the
// worker channel-correct without hardcoding stable/prod.
async function resolveUrl(platform, cfg = config()) {
  const manifest = await fetchManifest(platform, cfg);
  if (manifest && manifest.url) return manifest.url;
  if (platform === "steamdeck") return cfg.defaultInstall || DEFAULT_URL;
  return DEFAULT_URL;
}

// ---------------------------------------------------------------------------
// "Choose your device" fallback page. Self-contained, responsive, no external
// assets. Buttons carry NO baked URLs — each links back to /install?platform=<p>
// so the Worker resolves the real target through the SAME manifest lookup.
// Labels/subs come from the manifests when available, else the built-in
// defaults. NOTE: visible copy avoids the spaced em-dash (an AI-generated tell)
// per the get.allow2.com published-content style rule.
// ---------------------------------------------------------------------------

function escapeHtml(str) {
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

// Build the chooser button list, preferring each manifest's label/sub and
// falling back to PLATFORM_DEFAULTS. Manifests are fetched in parallel (all
// edge-cached); a failed fetch simply uses the defaults.
async function chooserItems(cfg = config()) {
  const manifests = await Promise.all(KNOWN_PLATFORMS.map((p) => fetchManifest(p, cfg)));
  return KNOWN_PLATFORMS.map((platform, i) => {
    const m = manifests[i] || {};
    const d = PLATFORM_DEFAULTS[platform] || { label: platform, sub: "" };
    return {
      platform,
      label: typeof m.label === "string" && m.label ? m.label : d.label,
      sub: typeof m.sub === "string" && m.sub ? m.sub : d.sub,
    };
  });
}

// `selfPath` is the route this Worker is actually serving on — "/install" in
// prod, "/staging" in staging. The chooser buttons must link back to the SAME
// path (with ?platform=), NOT a hardcoded "/install", so a chooser rendered by
// the staging Worker keeps the visitor on the staging Worker (staging pages),
// never bouncing them onto the prod route. Derived from the request pathname.
function chooserPage(items, selfPath = "/install") {
  const buttons = items
    .map(
      (it) =>
        `      <a class="btn" href="${escapeHtml(selfPath)}?platform=${encodeURIComponent(it.platform)}">${escapeHtml(it.label)}<span class="sub">${escapeHtml(it.sub)}</span></a>`,
    )
    .join("\n");

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
${buttons}
    </div>
    <p class="hint">Setting up a Steam Deck? Open this page on the Deck itself.</p>
  </main>
</body>
</html>`;
}

// Render the chooser as a Response. Always succeeds — manifest failures fall
// back to built-in labels; the page never dead-ends.
async function chooserResponse(cfg = config(), selfPath = "/install") {
  const items = await chooserItems(cfg);
  return new Response(chooserPage(items, selfPath), {
    status: 200,
    headers: {
      "Content-Type": "text/html; charset=utf-8",
      // Don't cache the chooser response itself — the manifests it reads are
      // edge-cached individually. The page depends on the User-Agent path.
      "Cache-Control": "no-store",
      "Vary": "User-Agent",
      // Staging only: keep the chooser out of search indexes at the HTTP layer.
      ...noindexHeader(cfg),
    },
  });
}

function redirectResponse(target, cfg = config()) {
  return new Response(null, {
    status: 302,
    headers: {
      "Location": target,
      // Don't cache the redirect — routing depends on the User-Agent and on the
      // manifest, which is edge-cached at the fetch layer (not here).
      "Cache-Control": "no-store",
      "Vary": "User-Agent",
      // Staging only: even the 302 carries noindex, nofollow.
      ...noindexHeader(cfg),
    },
  });
}

// ---------------------------------------------------------------------------
// Worker entry.
// ---------------------------------------------------------------------------

export default {
  async fetch(request, env) {
    // Resolve the per-environment config (prod vs staging) from the wrangler
    // `[vars]` binding. Everything channel-specific flows from cfg.
    const cfg = config(env);

    if (request.method !== "GET") {
      return new Response("Method Not Allowed", {
        status: 405,
        headers: {
          "Allow": "GET",
          "Content-Type": "text/plain; charset=utf-8",
          ...noindexHeader(cfg),
        },
      });
    }

    const url = new URL(request.url);
    // The route this Worker serves on ("/install" prod, "/staging" staging).
    // Chooser self-links use it so a staging chooser stays on the staging route.
    const selfPath = url.pathname;

    // Explicit ?platform=<p> — the chooser buttons use this. Resolve it through
    // the SAME per-file manifest lookup as UA detection (no baked URLs anywhere).
    const explicit = url.searchParams.get("platform");
    if (explicit) {
      if (KNOWN_PLATFORMS.includes(explicit)) {
        return redirectResponse(await resolveUrl(explicit, cfg), cfg);
      }
      // Unrecognised platform param → don't guess, show the chooser.
      return chooserResponse(cfg, selfPath);
    }

    // No explicit platform — sniff the User-Agent.
    const platform = detectPlatform(request.headers.get("User-Agent"));
    if (KNOWN_PLATFORMS.includes(platform)) {
      return redirectResponse(await resolveUrl(platform, cfg), cfg);
    }

    // Unknown / iPad / can't-tell → chooser.
    return chooserResponse(cfg, selfPath);
  },
};

// Exported for local testing harnesses (see README). Not used by the runtime.
export { detectPlatform, fetchManifest, resolveUrl, chooserItems, config, KNOWN_PLATFORMS, DEFAULT_URL };
