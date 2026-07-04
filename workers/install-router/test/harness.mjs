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
globalThis.fetch = async (input) => {
  const urlStr = typeof input === "string" ? input : input.url;
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
assertEq("resolveUrl valid -> manifest url", await resolveUrl("steamdeck"), "https://get.allow2.com/steamdeck/stable/");

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
async function call(path, { ua, method = "GET" } = {}) {
  const res = await worker.fetch(new Request(`https://get.allow2.com${path}`, {
    method,
    headers: ua ? { "User-Agent": ua } : {},
  }));
  const body = res.headers.get("Content-Type")?.includes("text/html") ? await res.text() : null;
  return { status: res.status, location: res.headers.get("Location"), body };
}

// Deck UA → 302 to the manifest url.
setManifests({ steamdeck: { status: 200, body: jsonBody({ platform: "steamdeck", url: "https://get.allow2.com/steamdeck/stable/" }) } });
const deck = await call("/install", { ua: "Mozilla/5.0 (X11; Linux x86_64) Firefox" });
assertEq("deck fetch status", deck.status, 302);
assertEq("deck fetch target (from manifest)", deck.location, "https://get.allow2.com/steamdeck/stable/");

// Windows UA with NO manifest stubbed → fail-safe 302 to DEFAULT_URL.
setManifests({});
const win = await call("/install", { ua: "Mozilla/5.0 (Windows NT 10.0) Chrome" });
assertEq("windows fetch status", win.status, 302);
assertEq("windows fail-safe -> DEFAULT_URL", win.location, DEFAULT_URL);

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

// ?platform= resolves through the same per-file lookup.
setManifests({ windows: { status: 200, body: jsonBody({ platform: "windows", url: "https://get.allow2.com/windows/stable/" }) } });
const q = await call("/install?platform=windows", { ua: "Mozilla/5.0 (iPad; CPU OS 17_0) Safari" });
assertEq("?platform=windows status", q.status, 302);
assertEq("?platform=windows -> manifest url", q.location, "https://get.allow2.com/windows/stable/");

// ?platform= with a missing manifest → fail-safe DEFAULT_URL.
setManifests({});
const qfail = await call("/install?platform=mac", {});
assertEq("?platform=mac missing manifest status", qfail.status, 302);
assertEq("?platform=mac -> DEFAULT_URL fail-safe", qfail.location, DEFAULT_URL);

// ?platform= with an unknown platform → chooser, never a bad redirect.
const qbad = await call("/install?platform=blackberry", {});
assertEq("?platform=blackberry -> chooser", qbad.status, 200);
assertEq("?platform=blackberry no redirect", qbad.location, null);

// Non-GET → 405.
const post = await call("/install", { ua: "Mozilla/5.0 (Windows NT 10.0)", method: "POST" });
assertEq("POST rejected", post.status, 405);

console.log(failures === 0 ? "\nALL PASS" : `\n${failures} FAILURE(S)`);
process.exit(failures === 0 ? 0 : 1);
