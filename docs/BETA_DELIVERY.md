# allow2linux — Channel Delivery (Steam Deck)

The end-to-end loop for shipping allow2linux over Flatpak. **Two channels, two
independent Cloudflare R2 prefixes**, one parameterized publish script. Build once,
host on R2, users install from a `.flatpakref`, and every subsequent publish is
picked up automatically (`flatpak update` / the auto-update timer).

> Anything marked **[unverified-device]** was written but NOT run from the
> authoring environment — it needs a real x86_64 Linux host (the C overlay +
> Flatpak build) and the Steam Deck (pairing, linger, Game-Mode overlay).

---

## The two channels

| | **staging** (BETA) | **production** (STABLE) |
|---|---|---|
| Audience | internal tester only | public / prod |
| Endpoint | `staging-api.allow2.com` | `api.allow2.com` |
| Launcher bakes | `ALLOW2_ENV=staging`, **omits** prod flag | `ALLOW2_PRODUCTION=1` + `NODE_ENV=production` |
| SDK guard | honours `ALLOW2_ENV` → staging | prod flag → staging **unreachable** |
| Flatpak `Branch` | `beta` | `stable` |
| R2 prefix | `get.allow2.com/steamdeck/staging/` | `get.allow2.com/steamdeck/stable/` |
| `.flatpakref` | `com.allow2.allow2linux-beta.flatpakref` | `com.allow2.allow2linux.flatpakref` |
| Publish | `./scripts/publish.sh staging` | `./scripts/publish.sh production` |
| CI trigger | push to `staging` branch | push a `vX.Y.Z` tag |
| vid/token | `ALLOW2_STAGING_VID/TOKEN` (21341) | `ALLOW2_PROD_VID/TOKEN` (21599) |

### Why two independent repos/prefixes (not one repo, two branches)

An ostree repo's `summary` is regenerated from **only the builds present in that
repo at publish time**. A fresh CI run that built just one channel would drop the
other channel from the summary and break its clients. So each channel is a fully
independent ostree repo synced (`--delete`) to its **own** R2 prefix. They never
share ostree state (separate `flatpak/build-<sub>/` + `flatpak/repo-<sub>/` dirs).

---

## The channel mechanism (single manifest, no drift)

The **only** per-channel artifact is the launcher script. `flatpak/gen-launcher.sh`
writes `flatpak/allow2linux.launcher.sh` (gitignored) for the requested channel;
the single manifest `flatpak/com.allow2.allow2linux.yml` includes it as a
`type: file` source. Every build entry point (`build.sh`, `publish.sh`, CI) runs
the generator first. There are **no divergent manifests**.

- `flatpak/gen-launcher.sh` — the generator. `CHANNEL=staging|production`.
- `flatpak/com.allow2.allow2linux.yml` — the single dev manifest (launcher =
  `type: file` → `allow2linux.launcher.sh`).
- Flatpak branch is a build parameter: `flatpak-builder --default-branch=beta|stable`.

> A naked `flatpak-builder` run without generating the launcher first fails
> (missing `allow2linux.launcher.sh`) **by design** — better than silently baking
> the wrong endpoint. Use `build.sh` / `publish.sh`, which generate it for you.

The Flathub submission manifest `flatpak/flathub/com.allow2.allow2linux.yml` is a
**separate, self-contained, production-locked** manifest (offline yarn mirror, git
sources) — it bakes `ALLOW2_PRODUCTION=1` inline and is the public production path.
It is never a staging build.

---

## TL;DR — the one-command loops

```bash
# From examples/linux, with the R2 + Cloudflare secrets exported (see below):

./scripts/publish.sh staging      # → beta build → /steamdeck/staging/ (internal testers)
./scripts/publish.sh production    # → stable build → /steamdeck/stable/ (public/prod)
```

Each command: bakes the channel launcher → builds the Flatpak → exports that
channel's ostree repo → refreshes the summary → syncs `repo-<sub>/` to R2 under the
channel prefix → purges the Cloudflare cache for that prefix's ostree metadata. The
user's device auto-updates within the timer window (or on demand via
`flatpak update`).

Local `.env`-less builds work: `gen-launcher.sh` defaults to the known type ids
(staging 21341 / production 21599).

---

## Hosting: Cloudflare R2 under `https://get.allow2.com/steamdeck/`

- `…/steamdeck/staging/` and `…/steamdeck/stable/` are two independent ostree
  repos under the same R2 bucket (custom domain on the bucket).
- Each `.flatpakref`'s `Url=` points at its channel's prefix; `publish.sh` syncs
  there with `--delete`.

### Secrets (never hardcode — export in your shell / CI **Secrets**)

| Variable | What |
|---|---|
| `R2_ACCOUNT_ID` | Cloudflare account id (used to build the R2 S3 endpoint host) |
| `R2_BUCKET` | R2 bucket name (repos go under `steamdeck/staging/` + `steamdeck/stable/`) |
| `R2_ACCESS_KEY_ID` | R2 S3 access key id |
| `R2_SECRET_ACCESS_KEY` | R2 S3 secret access key |
| `CLOUDFLARE_API_TOKEN` | token with **Cache Purge** permission on the zone |
| `CLOUDFLARE_ZONE_ID` | zone id for `allow2.com` |

The R2 S3 endpoint is `https://<R2_ACCOUNT_ID>.r2.cloudflarestorage.com`.

### vid/token (CI **Variables**, NOT Secrets)

vid/token are **type identifiers** (which integration this is), not secrets — put
them in Actions **Variables**. `gen-launcher.sh` falls back to the known ids if a
Variable is unset, so local builds and un-configured CI still work.

| Variable | Channel | Default |
|---|---|---|
| `ALLOW2_STAGING_VID` | staging | `21341` |
| `ALLOW2_STAGING_TOKEN` | staging | `QhkiFFPVfMkUjLUR` |
| `ALLOW2_PROD_VID` | production | `21599` |
| `ALLOW2_PROD_TOKEN` | production | `x9AUeUPpiweHTNCR` |

### R2 sync tooling gotcha

Newer **aws-cli (≥2.23)** sends a streaming CRC checksum R2 rejects. Fixes:

- `aws s3 sync … --checksum-algorithm CRC32` (what `publish.sh` does), or
- export `AWS_REQUEST_CHECKSUM_CALCULATION=WHEN_REQUIRED`, or
- use **rclone** instead: `SYNC_TOOL=rclone ./scripts/publish.sh staging`.

### Cloudflare cache rule (CRITICAL — do this once, per channel)

ostree `.tgz`/delta objects are content-addressed and immutable (cache them
freely), but **`summary`, `summary.sig`, `summary.idx` and `config` change every
publish**. If the CDN serves those stale, **clients never see the new build**.

1. Add a **Cache Rule**: for requests matching `*/steamdeck/staging/summary*` and
   `*/steamdeck/stable/summary*` (and the two `…/config`), set **Bypass cache**.
   This is the durable fix.
2. `publish.sh` also **purges** those exact paths (for the channel it published)
   via the CF API after each sync as belt-and-braces.

---

## CI: two triggers, two jobs (`.github/workflows/publish.yml`)

| Trigger | Channel | Prefix | Version |
|---|---|---|---|
| push to `staging` branch | `staging` | `/steamdeck/staging/` | rolling (`staging-<short-sha>`) |
| push a `vX.Y.Z` tag | `production` | `/steamdeck/stable/` | the semver tag |
| `workflow_dispatch` (choice) | either | matching | `manual-<short-sha>` |

The workflow resolves the channel from the trigger, then runs
`./scripts/publish.sh <channel>`, passing the R2/CF **Secrets** and the per-channel
vid/token **Variables**.

> `ubuntu-latest` is a valid x86_64 build host — you don't need the Deck to
> *build*, only to *test*. **[unverified-device]** the actual flatpak-builder run.

---

## Building a channel locally

Uses the **dev manifest** `flatpak/com.allow2.allow2linux.yml`.

```bash
cd examples/linux
# Prereqs on the build host (x86_64 Linux):
#   sudo apt-get install -y flatpak flatpak-builder
#   flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
#   flatpak install flathub org.freedesktop.Platform//23.08 org.freedesktop.Sdk//23.08

./scripts/publish.sh staging          # build + publish the beta
# or, to build a local sideload bundle without publishing:
CHANNEL=staging ./flatpak/build.sh    # beta bundle
./flatpak/build.sh                    # production/stable bundle (default)
```

Notes / **[unverified-device]**:
- Target arch is **x86_64** (Steam Deck).
- The dev manifest's daemon module builds with `--share=network` so its
  `npm install` fetches the published `allow2` SDK + `ws` from the registry. (The
  Flathub manifest instead uses an offline yarn mirror.)
- The C overlay (`allow2-lock-overlay`) is compiled inside the manifest via `make`.

---

## Tester: first-time install (BETA / staging, one-off)

On the Deck, in **Desktop Mode**:

1. **Install the beta** — copy `com.allow2.allow2linux-beta.flatpakref` to the Deck
   and double-click it (Discover/GNOME Software), or:
   ```bash
   flatpak install --from com.allow2.allow2linux-beta.flatpakref
   ```
   This adds the staging remote (`https://get.allow2.com/steamdeck/staging/`,
   `Branch=beta`) and installs the beta. The remote is `gpg-verify=false` (unsigned
   over HTTPS — fine for internal testers; sign for a wider rollout).
2. **Launch + pair** — run the app; it shows the pairing screen. In the Allow2
   parent app, scan the **QR** (or enter the **6-digit PIN**). Pair the child.
3. **Verify the service + linger** — the daemon auto-installs its `systemd --user`
   service, the auto-update timer, and enables linger on first run. Confirm:
   ```bash
   systemctl --user status allow2linux.service        # active (running)
   systemctl --user list-timers | grep allow2linux    # update timer scheduled
   loginctl show-user "$USER" | grep Linger            # Linger=yes
   ```
4. **If first-run couldn't set it up**, run `scripts/install-service.sh` (or the
   three manual `systemctl --user` / `loginctl enable-linger` steps).

After this the tester does nothing further — beta updates arrive automatically
whenever CI publishes to the `staging` prefix.

The **production** install is the same flow with `com.allow2.allow2linux.flatpakref`
(`Branch=stable`, `…/stable/`).

---

## Pushing updates (the loop)

```bash
# staging: merge/push to the `staging` branch → CI publishes the beta, or locally:
./scripts/publish.sh staging

# production: push a semver tag → CI publishes the stable, or locally:
./scripts/publish.sh production
```

The device's `allow2linux-update.timer` runs ~15 min after boot and every 6h (with
jitter, `Persistent=true`). To force it:

```bash
flatpak update -y com.allow2.allow2linux
systemctl --user try-restart allow2linux.service
```

---

## SECURITY NOTE — the beta is deliberately staging; keep it there

- The **beta/staging build intentionally targets `staging-api.allow2.com`** so the
  internal tester exercises staging. Its launcher **omits** `ALLOW2_PRODUCTION` and
  bakes `ALLOW2_ENV=staging`.
- **This build must stay on the beta channel (`Branch=beta`, `/steamdeck/staging/`)
  and must NEVER be promoted to stable or submitted to Flathub.** It is for the
  internal tester only.
- The **stable/production build stays production-locked**: its launcher bakes
  `ALLOW2_PRODUCTION=1` + `NODE_ENV=production`, so the Allow2 SDK's hard guard
  ignores `ALLOW2_ENV` / `ALLOW2_API_URL` entirely and **staging is unreachable by
  any runtime input**. The public release (stable channel + the Flathub manifest)
  can therefore never attach to staging — the whole point of the SDK guard.
- vid/token are type identifiers, not secrets — safe to keep in CI **Variables**
  and in these docs.

---

## Gotchas

- **x86_64 only.** The Deck is x86_64; don't ship an aarch64-only build.
- **Game-Mode overlay is dismissible.** In Gaming Mode (gamescope) there is no
  screen locker — the daemon SIGSTOPs the game and shows its own overlay, which a
  determined child can dismiss. Hard enforcement there is best-effort; the
  authority/quota is still enforced server-side.
- **SteamOS updates do NOT wipe the overlay/agent** — everything lives in
  `~/.var/app/com.allow2.allow2linux/` (Flatpak) and `~/.allow2/`, and linger state
  lives under `/var` and survives SteamOS updates.
- **`--user` services are user-killable.** Inherent to any `--user` daemon;
  managed/MDM enforcement is the answer for tamper-proofing (out of scope here).
- **Metadata caching** (see the Cloudflare cache rule) is the #1 reason "I
  published but the device didn't get it" — check `summary` freshness first.
- **Branch must match.** Each `.flatpakref`'s `Branch` must equal the branch
  `publish.sh` exports for that channel (`beta` / `stable`).

---

## Files in this loop

| File | Role |
|---|---|
| `flatpak/gen-launcher.sh` | Generates the channel-specific launcher (env + vid/token). The channel mechanism. |
| `flatpak/com.allow2.allow2linux.yml` | Single dev manifest; launcher = `type: file` → `allow2linux.launcher.sh`. |
| `scripts/publish.sh` | `publish.sh <staging\|production>` — bake → build → export → R2 sync → CF purge. |
| `com.allow2.allow2linux-beta.flatpakref` | BETA/staging tester install; `Branch=beta`, `…/steamdeck/staging/`. |
| `com.allow2.allow2linux.flatpakref` | STABLE/production install; `Branch=stable`, `…/steamdeck/stable/`. |
| `.github/workflows/publish.yml` | CI: `staging` branch → beta, `vX.Y.Z` tag → stable. |
| `flatpak/build.sh` | Local sideload bundle build (`CHANNEL=` selects channel). |
| `scripts/install-service.sh` | Reliable manual service+timer+linger installer (first-run fallback). |
| `flatpak/flathub/com.allow2.allow2linux.yml` | Separate production-locked Flathub submission manifest. |
