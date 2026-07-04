# install-router — get.allow2.com/install

A dependency-free Cloudflare Worker that powers **`get.allow2.com/install`** as
the single, universal entry point for **all** Allow2 installers, across every
platform.

This is the cross-platform funnel entry, **not** a Steam-Deck-specific page. It
lives in the allow2linux repo for now only because the `get.allow2.com` tooling
(`scripts/publish.sh`, `flatpak/gen-install-page.sh`) already lives here.

## What it does

On a `GET`, it reads the `User-Agent` and routes:

| Detected platform | Where it goes |
| --- | --- |
| SteamOS / Steam Deck / Valve markers, **or** generic desktop Linux | `302` -> Steam Deck / Linux install page (real) |
| Windows | `302` -> `WINDOWS_INSTALL_URL` (placeholder) |
| macOS | `302` -> `MAC_INSTALL_URL` (placeholder) |
| Android | `302` -> `ANDROID_INSTALL_URL` (placeholder) |
| Unknown, iPhone/iPad, or can't tell | `200` -> self-contained "choose your device" HTML page |

Any non-`GET` method returns `405`.

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

## Configure the target URLs

All per-platform targets are plain constants at the top of `src/index.js`:

- `STEAMDECK_INSTALL_URL` — **real** (`https://get.allow2.com/steamdeck/stable/`).
- `WINDOWS_INSTALL_URL`, `MAC_INSTALL_URL`, `ANDROID_INSTALL_URL` —
  **placeholders** that currently default to `https://allow2.com/` so the funnel
  never dead-ends. Replace each with the real install page as it ships, then
  redeploy. No env vars or secrets involved.

## Deploy

```sh
cd workers/install-router
wrangler deploy
```

Then, in `wrangler.toml` (or the Cloudflare dashboard), **add the route** and
confirm the zone:

```toml
[[routes]]
pattern = "get.allow2.com/install"
zone_name = "allow2.com"
```

The route is commented out in `wrangler.toml` so a blind `wrangler deploy`
doesn't claim the path before the operator has reviewed it. The Worker only
handles `/install`; the rest of `get.allow2.com` is untouched.

## Verify locally

```sh
node --check src/index.js
node test/harness.mjs   # asserts UA -> target routing
wrangler deploy --dry-run   # optional, if wrangler is installed
```
