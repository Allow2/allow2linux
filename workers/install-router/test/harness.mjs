// Tiny local sanity harness for the install-router Worker. No test framework.
//   node test/harness.mjs
//
// Covers: UA detection, the manifest-driven redirect (fetch mocked to return a
// manifest / 404 / malformed JSON), the DEFAULT_URL fail-safe, the chooser
// render, and the ?platform= per-file resolution.
import worker, {
  detectPlatform,
  fetchManifest,
  resolveUrl,
  withIndex,
  config,
  DEFAULT_URL,
} from "../src/index.js";

let failures = 0;
function assertEq(label, actual, expected) {
  const ok = actual === expected;
  if (!ok) failures++;
  console.log(`${ok ? "PASS" : "FAIL"}  ${label}  (got ${JSON.stringify(actual)}, want ${JSON.stringify(expected)})`);
}
function assertMatch(label, actual, re) {
  const ok = typeof actual === "string" && re.test(actual);
  if (!ok) failures++;
  console.log(`${ok ? "PASS" : "FAIL"}  ${label}  (got ${JSON.stringify(actual)}, want match ${re})`);
}

// ---------------------------------------------------------------------------
// Mock global fetch. Behaviour is switched by a module-level MODE var so each
// test can control what the manifest read returns. Cloudflare's `cf` option is
// ignored here (it only matters in the real edge runtime).
// ---------------------------------------------------------------------------
let MANIFESTS = {}; // platform -> { status, body } | undefined
let FETCHED = []; // record every manifest URL fetched (to assert the env's base)
globalThis.fetch = async (input) => {
  const urlStr = typeof input === "string" ? input : input.url;
  FETCHED.push(urlStr);
  const m = urlStr.match(/\/install\/([a-z0-9]+)\.json$/i);
  const platform = m ? m[1] : null;
  const entry = platform ? MANIFESTS[platform] : undefined;

  if (!entry) {
    // Simulate a 404 for anything we didn't explicitly stub.
    return new Response("not found", { status: 404 });
  }
  return new Response(entry.body, {
    status: entry.status ?? 200,
    headers: { "Content-Type": "application/json" },
  });
};

function setManifests(map) {
  MANIFESTS = map;
}
function jsonBody(obj) {
  return JSON.stringify(obj);
}

// ---------------------------------------------------------------------------
// platform detection (pure)
// ---------------------------------------------------------------------------
const cases = [
  ["SteamOS marker", "Mozilla/5.0 (X11; Linux x86_64; SteamOS) Chrome/113", "steamdeck"],
  ["Valve marker", "Mozilla/5.0 (Valve Steam Gaming) Chrome", "steamdeck"],
  ["generic desktop Linux -> deck", "Mozilla/5.0 (X11; Linux x86_64) Firefox/126", "steamdeck"],
  ["Windows", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120", "windows"],
  ["macOS", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Safari", "mac"],
  ["Android excluded from Linux", "Mozilla/5.0 (Linux; Android 14; Pixel) Chrome", "android"],
  ["iPad -> unknown/chooser", "Mozilla/5.0 (iPad; CPU OS 17_0 like Mac OS X) Safari", "unknown"],
  ["iPhone -> unknown/chooser", "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0) Safari", "unknown"],
  ["empty UA -> unknown", "", "unknown"],
];
for (const [label, ua, expected] of cases) assertEq(label, detectPlatform(ua), expected);

// ---------------------------------------------------------------------------
// fetchManifest / resolveUrl — the manifest lookup + fail-safe
// ---------------------------------------------------------------------------

// Happy path: a valid manifest resolves to its .url.
setManifests({
  steamdeck: { status: 200, body: jsonBody({ platform: "steamdeck", url: "https://get.allow2.com/steamdeck/stable/", label: "Steam Deck / Linux", sub: "SteamOS or desktop Linux" }) },
});
assertEq("fetchManifest valid returns url", (await fetchManifest("steamdeck"))?.url, "https://get.allow2.com/steamdeck/stable/");
// resolveUrl appends index.html to the directory-style get.allow2.com install page.
assertEq("resolveUrl valid -> manifest url + index.html", await resolveUrl("steamdeck"), "https://get.allow2.com/steamdeck/stable/index.html");

// ---------------------------------------------------------------------------
// withIndex — directory-style get.allow2.com install pages get index.html;
// everything else (external hosts, already-file URLs) is untouched. No throws,
// no double-append.
// ---------------------------------------------------------------------------
assertEq("withIndex appends index.html to get.allow2.com dir", withIndex("https://get.allow2.com/steamdeck/staging/"), "https://get.allow2.com/steamdeck/staging/index.html");
assertEq("withIndex leaves an existing index.html unchanged (no double-append)", withIndex("https://get.allow2.com/steamdeck/staging/index.html"), "https://get.allow2.com/steamdeck/staging/index.html");
assertEq("withIndex leaves a concrete file url unchanged", withIndex("https://get.allow2.com/steamdeck/staging/Allow2.flatpak"), "https://get.allow2.com/steamdeck/staging/Allow2.flatpak");
assertEq("withIndex leaves the external DEFAULT_URL fail-safe EXACTLY unchanged", withIndex(DEFAULT_URL), "https://allow2.com/");
assertEq("withIndex leaves DEFAULT_URL === constant (no index.html)", withIndex(DEFAULT_URL), DEFAULT_URL);
assertEq("withIndex leaves a non-get.allow2.com dir url unchanged", withIndex("https://example.com/foo/"), "https://example.com/foo/");
assertEq("withIndex returns a malformed url untouched (never throws)", withIndex("not a url"), "not a url");

// 404 → null → DEFAULT_URL.
setManifests({}); // nothing stubbed => 404
assertEq("fetchManifest 404 -> null", await fetchManifest("windows"), null);
assertEq("resolveUrl 404 -> DEFAULT_URL", await resolveUrl("windows"), DEFAULT_URL);

// Malformed JSON → null → DEFAULT_URL.
setManifests({ mac: { status: 200, body: "{ this is not json " } });
assertEq("fetchManifest malformed -> null", await fetchManifest("mac"), null);
assertEq("resolveUrl malformed -> DEFAULT_URL", await resolveUrl("mac"), DEFAULT_URL);

// Valid JSON but missing url → null → DEFAULT_URL.
setManifests({ android: { status: 200, body: jsonBody({ platform: "android", label: "Android" }) } });
assertEq("fetchManifest no-url -> null", await fetchManifest("android"), null);
assertEq("resolveUrl no-url -> DEFAULT_URL", await resolveUrl("android"), DEFAULT_URL);

// Unknown platform never fetches → null → DEFAULT_URL.
assertEq("fetchManifest unknown platform -> null", await fetchManifest("blackberry"), null);
assertEq("resolveUrl unknown platform -> DEFAULT_URL", await resolveUrl("blackberry"), DEFAULT_URL);

// ---------------------------------------------------------------------------
// fetch handler behaviour
// ---------------------------------------------------------------------------
async function call(path, { ua, method = "GET", env } = {}) {
  const res = await worker.fetch(new Request(`https://get.allow2.com${path}`, {
    method,
    headers: ua ? { "User-Agent": ua } : {},
  }), env);
  const body = res.headers.get("Content-Type")?.includes("text/html") ? await res.text() : null;
  return {
    status: res.status,
    location: res.headers.get("Location"),
    robots: res.headers.get("X-Robots-Tag"),
    body,
  };
}

// Deck UA → 302 to the manifest url.
setManifests({ steamdeck: { status: 200, body: jsonBody({ platform: "steamdeck", url: "https://get.allow2.com/steamdeck/stable/" }) } });
const deck = await call("/install", { ua: "Mozilla/5.0 (X11; Linux x86_64) Firefox" });
assertEq("deck fetch status", deck.status, 302);
assertEq("deck fetch target (manifest url + index.html)", deck.location, "https://get.allow2.com/steamdeck/stable/index.html");

// Windows UA (KNOWN platform) with NO manifest stubbed → chooser, NOT a bounce to
// allow2.com. Keeps the visitor in the funnel to pick a supported platform.
setManifests({});
const win = await call("/install", { ua: "Mozilla/5.0 (Windows NT 10.0) Chrome" });
assertEq("windows missing-manifest -> chooser status", win.status, 200);
assertEq("windows missing-manifest -> no redirect", win.location, null);
assertMatch("windows missing-manifest -> chooser renders", win.body, /Pick the device/);
assertEq("windows missing-manifest -> never DEFAULT_URL redirect", win.location === DEFAULT_URL, false);

// iPad UA → chooser (200 HTML, no redirect).
const chooser = await call("/install", { ua: "Mozilla/5.0 (iPad; CPU OS 17_0) Safari" });
assertEq("chooser fetch status", chooser.status, 200);
assertEq("chooser has no redirect", chooser.location, null);
assertMatch("chooser renders buttons back to /install?platform=", chooser.body, /\/install\?platform=steamdeck/);
assertMatch("chooser carries no baked steamdeck url", chooser.body, /Pick the device/);

// Chooser prefers manifest label/sub when present.
setManifests({
  steamdeck: { status: 200, body: jsonBody({ platform: "steamdeck", url: "https://get.allow2.com/steamdeck/stable/", label: "MANIFEST DECK LABEL", sub: "manifest deck sub" }) },
});
const chooser2 = await call("/install", { ua: "Mozilla/5.0 (iPad; CPU OS 17_0) Safari" });
assertMatch("chooser uses manifest label when present", chooser2.body, /MANIFEST DECK LABEL/);
assertMatch("chooser uses manifest sub when present", chooser2.body, /manifest deck sub/);

// Chooser falls back to built-in defaults when manifests 404.
setManifests({});
const chooser3 = await call("/install", { ua: "Mozilla/5.0 (iPad; CPU OS 17_0) Safari" });
assertMatch("chooser default windows label on 404", chooser3.body, /Windows 10 and 11/);

// ?platform= resolves through the same per-file lookup (+ index.html correction).
setManifests({ windows: { status: 200, body: jsonBody({ platform: "windows", url: "https://get.allow2.com/windows/stable/" }) } });
const q = await call("/install?platform=windows", { ua: "Mozilla/5.0 (iPad; CPU OS 17_0) Safari" });
assertEq("?platform=windows status", q.status, 302);
assertEq("?platform=windows -> manifest url + index.html", q.location, "https://get.allow2.com/windows/stable/index.html");

// A manifest url that is already a concrete file is NOT double-suffixed.
setManifests({ windows: { status: 200, body: jsonBody({ platform: "windows", url: "https://get.allow2.com/windows/stable/setup.exe" }) } });
const qfile = await call("/install?platform=windows", {});
assertEq("?platform=windows file url unchanged (no index.html)", qfile.location, "https://get.allow2.com/windows/stable/setup.exe");

// KNOWN platform (mac) with a MISSING manifest → chooser, NOT a DEFAULT_URL redirect.
setManifests({});
const qfail = await call("/install?platform=mac", {});
assertEq("?platform=mac missing manifest -> chooser status", qfail.status, 200);
assertEq("?platform=mac missing manifest -> no redirect", qfail.location, null);
assertMatch("?platform=mac missing manifest -> chooser renders", qfail.body, /Pick the device/);

// Same for a UA-DETECTED known platform (mac) with no manifest → chooser, 200 HTML.
setManifests({});
const macUa = await call("/install", { ua: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Safari" });
assertEq("mac UA missing manifest -> chooser status", macUa.status, 200);
assertEq("mac UA missing manifest -> no redirect (not allow2.com)", macUa.location, null);
assertMatch("mac UA missing manifest -> text/html chooser", macUa.body, /Pick the device/);

// ?platform= with an unknown platform → chooser, never a bad redirect.
const qbad = await call("/install?platform=blackberry", {});
assertEq("?platform=blackberry -> chooser", qbad.status, 200);
assertEq("?platform=blackberry no redirect", qbad.location, null);

// Non-GET → 405.
const post = await call("/install", { ua: "Mozilla/5.0 (Windows NT 10.0)", method: "POST" });
assertEq("POST rejected", post.status, 405);

// ---------------------------------------------------------------------------
// ENV-AWARENESS — the co-hosted prod + staging pair
// ---------------------------------------------------------------------------
const PROD_ENV = {
  CHANNEL: "stable",
  MANIFEST_BASE: "https://get.allow2.com/install",
  DEFAULT_INSTALL: "https://get.allow2.com/steamdeck/stable/",
  NOINDEX: "",
};
const STAGING_ENV = {
  CHANNEL: "staging",
  MANIFEST_BASE: "https://get.allow2.com/staging/install",
  DEFAULT_INSTALL: "https://get.allow2.com/steamdeck/staging/",
  NOINDEX: "1",
};

// config() defaults are prod-safe when env is missing/partial.
assertEq("config() default channel", config().channel, "stable");
assertEq("config() default manifestBase", config().manifestBase, "https://get.allow2.com/install");
assertEq("config() default noindex is false", config().noindex, false);
assertEq("config({}) missing NOINDEX -> false (prod-safe)", config({}).noindex, false);
assertEq("config(prod) noindex false", config(PROD_ENV).noindex, false);
assertEq("config(staging) noindex true", config(STAGING_ENV).noindex, true);

// PROD deck: stable manifest url, NO noindex header, fetched the PROD base.
FETCHED = [];
setManifests({ steamdeck: { status: 200, body: jsonBody({ platform: "steamdeck", url: "https://get.allow2.com/steamdeck/stable/" }) } });
const pDeck = await call("/install", { ua: "Mozilla/5.0 (X11; Linux x86_64) Firefox", env: PROD_ENV });
assertEq("prod deck -> stable page (index.html)", pDeck.location, "https://get.allow2.com/steamdeck/stable/index.html");
assertEq("prod deck NO noindex header", pDeck.robots, null);
assertMatch("prod fetched the prod manifest base", FETCHED.join("|"), /^https:\/\/get\.allow2\.com\/install\/steamdeck\.json$/m);

// STAGING deck: staging manifest url, noindex header PRESENT, fetched the STAGING base.
FETCHED = [];
setManifests({ steamdeck: { status: 200, body: jsonBody({ platform: "steamdeck", url: "https://get.allow2.com/steamdeck/staging/" }) } });
const sDeck = await call("/staging", { ua: "Mozilla/5.0 (X11; Linux x86_64) Firefox", env: STAGING_ENV });
assertEq("staging deck -> staging page (index.html)", sDeck.location, "https://get.allow2.com/steamdeck/staging/index.html");
assertEq("staging deck noindex header", sDeck.robots, "noindex, nofollow");
assertMatch("staging fetched the staging manifest base", FETCHED.join("|"), /https:\/\/get\.allow2\.com\/staging\/install\/steamdeck\.json/);

// Deck fail-safe is CHANNEL-AWARE: manifest 404 → DEFAULT_INSTALL for that env,
// index.html-corrected (the deck ALWAYS resolves to a live install page).
setManifests({});
const sFail = await call("/staging", { ua: "Mozilla/5.0 (X11; Linux x86_64) Firefox", env: STAGING_ENV });
assertEq("staging deck fail-safe -> staging DEFAULT_INSTALL (index.html)", sFail.location, "https://get.allow2.com/steamdeck/staging/index.html");
assertEq("staging deck fail-safe still noindex", sFail.robots, "noindex, nofollow");
const pFail = await call("/install", { ua: "Mozilla/5.0 (X11; Linux x86_64) Firefox", env: PROD_ENV });
assertEq("prod deck fail-safe -> stable DEFAULT_INSTALL (index.html)", pFail.location, "https://get.allow2.com/steamdeck/stable/index.html");
assertEq("prod deck fail-safe NO noindex header", pFail.robots, null);

// Non-deck known platform with no manifest → CHOOSER (not a DEFAULT_URL bounce);
// staging chooser still carries the noindex header.
const sWin = await call("/staging", { ua: "Mozilla/5.0 (Windows NT 10.0) Chrome", env: STAGING_ENV });
assertEq("staging windows missing-manifest -> chooser status", sWin.status, 200);
assertEq("staging windows missing-manifest -> no redirect", sWin.location, null);
assertMatch("staging windows missing-manifest -> chooser renders", sWin.body, /Pick the device/);
assertEq("staging windows missing-manifest chooser noindex header", sWin.robots, "noindex, nofollow");

// Chooser self-links track the ROUTE: staging stays on /staging, prod on /install.
const sChooser = await call("/staging", { ua: "Mozilla/5.0 (iPad; CPU OS 17_0) Safari", env: STAGING_ENV });
assertEq("staging chooser status", sChooser.status, 200);
assertEq("staging chooser noindex header", sChooser.robots, "noindex, nofollow");
assertMatch("staging chooser self-links to /staging?platform=", sChooser.body, /\/staging\?platform=steamdeck/);
assertEq("staging chooser does NOT self-link to /install", /\/install\?platform=/.test(sChooser.body), false);
const pChooser = await call("/install", { ua: "Mozilla/5.0 (iPad; CPU OS 17_0) Safari", env: PROD_ENV });
assertMatch("prod chooser self-links to /install?platform=", pChooser.body, /\/install\?platform=steamdeck/);
assertEq("prod chooser NO noindex header", pChooser.robots, null);

// 405 also carries the noindex header on staging, never on prod.
const s405 = await call("/staging", { ua: "x", method: "POST", env: STAGING_ENV });
assertEq("staging 405 status", s405.status, 405);
assertEq("staging 405 noindex header", s405.robots, "noindex, nofollow");
const p405 = await call("/install", { ua: "x", method: "POST", env: PROD_ENV });
assertEq("prod 405 NO noindex header", p405.robots, null);

console.log(failures === 0 ? "\nALL PASS" : `\n${failures} FAILURE(S)`);
process.exit(failures === 0 ? 0 : 1);
