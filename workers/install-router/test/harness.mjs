// Tiny local sanity harness for the UA routing logic. No test framework.
//   node test/harness.mjs
import worker, { detectPlatform } from "../src/index.js";

let failures = 0;
function assertEq(label, actual, expected) {
  const ok = actual === expected;
  if (!ok) failures++;
  console.log(`${ok ? "PASS" : "FAIL"}  ${label}  (got ${actual}, want ${expected})`);
}

// --- platform detection ---
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

// --- fetch handler behaviour ---
async function statusAndLocation(ua, method = "GET") {
  const res = await worker.fetch(new Request("https://get.allow2.com/install", {
    method,
    headers: ua ? { "User-Agent": ua } : {},
  }));
  return { status: res.status, location: res.headers.get("Location") };
}

const deck = await statusAndLocation("Mozilla/5.0 (X11; Linux x86_64) Firefox");
assertEq("deck fetch status", deck.status, 302);
assertEq("deck fetch target", deck.location, "https://get.allow2.com/steamdeck/stable/");

const win = await statusAndLocation("Mozilla/5.0 (Windows NT 10.0) Chrome");
assertEq("windows fetch status", win.status, 302);

const chooser = await statusAndLocation("Mozilla/5.0 (iPad; CPU OS 17_0) Safari");
assertEq("chooser fetch status", chooser.status, 200);
assertEq("chooser has no redirect", chooser.location, null);

const post = await statusAndLocation("Mozilla/5.0 (Windows NT 10.0)", "POST");
assertEq("POST rejected", post.status, 405);

console.log(failures === 0 ? "\nALL PASS" : `\n${failures} FAILURE(S)`);
process.exit(failures === 0 ? 0 : 1);
