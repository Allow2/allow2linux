# install-router — get.allow2.com/install (+ /staging)

A dependency-free Cloudflare Worker that powers the single, universal entry point
for **all** Allow2 installers, across every platform.

This is the cross-platform funnel entry, **not** a Steam-Deck-specific page. It
lives in the allow2linux repo for now only because the `get.allow2.com` tooling
(`scripts/publish.sh`, `flatpak/gen-install-page.sh`) already lives here.

## Co-hosted staging + production (same code, two envs)

The **same** Worker source runs as a **co-hosted staging+prod pair** on
get.allow2.com — mirroring how get.allow2.com already co-hosts
`steamdeck/stable` + `steamdeck/staging`. The two environments differ **only** by
their wrangler `[env.*.vars]`, never by hardcoded logic:

| | **production** | **staging** |
| --- | --- | --- |
| Deployed from branch | `main` | `staging` |
| Worker route (EXACT) | `get.allow2.com/install` | `get.allow2.com/staging` |
| Wrangler env | `--env production` | `--env staging` |
| Worker script name | `allow2-install-router` | `allow2-install-router-staging` |
| `CHANNEL` | `stable` | `staging` |
| `MANIFEST_BASE` (manifest reads) | `get.allow2.com/install/<p>.json` | `get.allow2.com/staging/install/<p>.json` |
| `DEFAULT_INSTALL` (deck fail-safe) | `get.allow2.com/steamdeck/stable/` | `get.allow2.com/steamdeck/staging/` |
| Sends visitors to | STABLE install pages | STAGING install pages |
| `NOINDEX` | `""` (indexable) | `"1"` → `X-Robots-Tag: noindex, nofollow` on **every** response |

**Routes are EXACT paths, not wildcards** (`get.allow2.com/install`, not
`/install*`). That is what lets each Worker read its own manifests without
looping: the prod Worker fetches `get.allow2.com/install/<p>.json` and the
staging Worker fetches `get.allow2.com/staging/install/<p>.json` — those are
**sub-paths** of the route, and an exact route does not match sub-paths, so the
manifest reads fall through to the R2 origin instead of re-entering the Worker.

Every default in the Worker's `config()` is **prod-safe** (stable pages, no
noindex), so a missing or mis-set env fails toward production behaviour and can
never accidentally deindex prod or point it at staging pages.

## What it does

On a `GET`, it reads the `User-Agent`, resolves the platform to a real install
URL **from an R2 manifest** (see below), and routes:

| Detected platform | Where it goes |
| --- | --- |
| SteamOS / Steam Deck / Valve markers, **or** generic desktop Linux | `302` -> `install/steamdeck.json` `.url` |
| Windows | `302` -> `install/windows.json` `.url` |
| macOS | `302` -> `install/mac.json` `.url` |
| Android | `302` -> `install/android.json` `.url` |
| Unknown, iPhone/iPad, or can't tell | `200` -> self-contained "choose your device" HTML page |

Any non-`GET` method returns `405`.

## Data-driven: per-platform manifests in R2

The per-platform install URLs are **not baked into the Worker**. At request time
the Worker fetches a tiny JSON manifest from R2, at the base for its env
(`MANIFEST_BASE`):

```
production:  https://get.allow2.com/install/<platform>.json
staging:     https://get.allow2.com/staging/install/<platform>.json
```

and `302`s to that manifest's `.url`. The fetch is **edge-cached for 5 minutes**
(`cf: { cacheTtl: 300, cacheEverything: true }`), so it does not hit R2 on every
request, and a freshly-published install URL goes live within the TTL (the
publisher also purges the manifest for instant pickup).

Each platform repo publishes its **own** manifest from its **own** release
pipeline. In this repo, `scripts/publish.sh` is **channel-aware**: publishing the
`production` channel writes `install/steamdeck.json` (→ `steamdeck/stable/`), and
publishing the `staging` channel writes `staging/install/steamdeck.json`
(→ `steamdeck/staging/`) — the two never clobber each other, and each env's
Worker reads the matching one. Shipping a new install page therefore requires
**no Worker redeploy** — only a manifest rewrite.

The **only** things baked into the Worker are (a) the **list of known platforms**
(`steamdeck`, `windows`, `mac`, `android`) and (b) the **UA→platform detection**
logic. That's genuine logic, not configuration. Everything URL-shaped lives in
the manifests.

### Manifest schema

`install/<platform>.json`:

```json
{
  "platform":  "steamdeck",
  "url":       "https://get.allow2.com/steamdeck/stable/",
  "label":     "Steam Deck / Linux",
  "sub":       "SteamOS or desktop Linux",
  "updatedAt": "2026-07-04T00:00:00Z"
}
```

| Field | Required | Used for |
| --- | --- | --- |
| `url` | **yes** | The `302` redirect target. A missing/blank `url` makes the manifest invalid (→ fail-safe). |
| `platform` | no | Informational / self-identifying. |
| `label` | no | Chooser button title. Falls back to the Worker's built-in default. |
| `sub` | no | Chooser button subtitle. Falls back to the built-in default. |
| `updatedAt` | no | Informational (ISO 8601). |

### Fail-safe (non-negotiable)

If a manifest 404s, errors, or is malformed JSON (or has no `url`), the Worker
**never dead-ends and never throws**:

- Redirect paths fall back to `DEFAULT_URL` (`https://allow2.com/`).
- The chooser always renders, using built-in labels when a manifest can't be read.

## The chooser page

The "choose your device" page is served for unknown / iPad / can't-tell UAs. Its
buttons carry **no baked URLs** — each links back to `/install?platform=<p>`, so
the Worker resolves the real target through the **same** manifest lookup as UA
detection. Button labels/subtitles come from each platform's manifest when
available, else the built-in defaults.

### The Steam Deck / Linux caveat

The stock Steam Deck browser User-Agent is **not** reliably distinctive. Some
builds report `SteamOS` or reference `Valve`, but many present as a plain
desktop Linux Chrome/Firefox UA with no Deck marker. Because the Deck is the
primary target of the Linux installer in this repo, the Worker deliberately
treats a **generic desktop Linux UA as the Steam Deck / Linux install** case.
Android is detected first and excluded, so this only catches desktop-class
Linux. See the comment block in `src/index.js` for the full rationale.

The chooser page also carries the hint *"Setting up a Steam Deck? Open this page
on the Deck itself."* for the common case of a parent browsing from a phone or
iPad that is off the target device.

## Deploy

Each route is bound **only** under its env in `wrangler.toml` (so a bare
`wrangler deploy` can't claim a path):

```sh
cd workers/install-router
wrangler deploy --env production   # binds get.allow2.com/install  (stable, indexable)
wrangler deploy --env staging      # binds get.allow2.com/staging  (staging, noindex)
```

Deploys are **branch-driven** (ecosystem convention; **`master` is being deleted
— never referenced**):

- **`.github/workflows/deploy-install-router.yml`** (paths-filtered to
  `workers/install-router/**`):
  - push to **`main`** → `deploy --env production`
  - push to **`staging`** → `deploy --env staging`
  - **pull_request** → `deploy --env production --dry-run` (validate only)
- It pins `cloudflare/wrangler-action@v3` with `wranglerVersion: '3.90.0'`
  (v4 defaults to Wrangler 4 — deliberately not adopted).
- Secrets: `CF_WORKERS_API_TOKEN` (an "Edit Cloudflare Workers"-template token,
  zone-scoped to `allow2.com` — **distinct** from the cache-purge-only
  `CLOUDFLARE_API_TOKEN` used by `publish.yml`) and `CLOUDFLARE_ACCOUNT_ID`.

The Worker only handles the exact `/install` and `/staging` paths; the rest of
`get.allow2.com` is untouched.

## Verify locally

```sh
node --check src/index.js
node test/harness.mjs   # UA -> target, manifest fetch, fail-safe, chooser, ?platform=, env-awareness
wrangler deploy --env production --dry-run   # optional, if wrangler is installed
wrangler deploy --env staging --dry-run      # optional
```

The harness mocks `fetch` to return a manifest / 404 / malformed JSON and
asserts the redirect target, the `DEFAULT_URL` fail-safe, the chooser render,
`?platform=` resolution, and the **env-awareness**: prod (stable pages, no
noindex, prod manifest base) vs staging (staging pages, `X-Robots-Tag:
noindex, nofollow` on every response, staging manifest base, `/staging`
self-links).
