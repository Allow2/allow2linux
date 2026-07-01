# allow2linux — Internal Beta Delivery (Steam Deck)

The end-to-end loop for shipping allow2linux to an internal tester and pushing
updates. Build once, host on Cloudflare R2, tester installs from a `.flatpakref`,
and every subsequent `publish-beta.sh` run is picked up automatically.

> Anything marked **[unverified-device]** was written but NOT run from the
> authoring environment — it needs a real x86_64 Linux host (the C overlay +
> Flatpak build) and the Steam Deck (pairing, linger, Game-Mode overlay).

---

## TL;DR — the one-command loop

```bash
# From examples/linux, with the R2 + Cloudflare secrets exported (see below):
./scripts/publish-beta.sh
```

That single command: builds the Flatpak → exports the ostree repo → refreshes the
summary → syncs `repo/` to R2 under `steamdeck/` → purges the Cloudflare cache for
the ostree metadata. The tester's device auto-updates within the timer window (or
on demand via `flatpak update`).

First-time tester install is a one-off (double-click the `.flatpakref`); after
that, **you** publish and **they** auto-update.

---

## Hosting: Cloudflare R2 at `https://get.allow2.com/steamdeck/`

- The ostree repo lives under the **`steamdeck/` prefix** of the R2 bucket and is
  served at `https://get.allow2.com/steamdeck/` (custom domain on the bucket).
- The `.flatpakref` `Url=` points at that path; `publish-beta.sh` syncs there.

### Secrets (never hardcode — export in your shell / CI secrets)

| Variable | What |
|---|---|
| `R2_ACCOUNT_ID` | Cloudflare account id (used to build the R2 S3 endpoint host) |
| `R2_BUCKET` | R2 bucket name (repo goes under `steamdeck/`) |
| `R2_ACCESS_KEY_ID` | R2 S3 access key id |
| `R2_SECRET_ACCESS_KEY` | R2 S3 secret access key |
| `CLOUDFLARE_API_TOKEN` | token with **Cache Purge** permission on the zone |
| `CLOUDFLARE_ZONE_ID` | zone id for `allow2.com` |

The R2 S3 endpoint is `https://<R2_ACCOUNT_ID>.r2.cloudflarestorage.com`.

### R2 sync tooling gotcha

Newer **aws-cli (≥2.23)** sends a streaming CRC checksum R2 rejects. Fixes:

- `aws s3 sync … --checksum-algorithm CRC32` (what `publish-beta.sh` does), or
- export `AWS_REQUEST_CHECKSUM_CALCULATION=WHEN_REQUIRED`, or
- use **rclone** instead: `SYNC_TOOL=rclone ./scripts/publish-beta.sh` (rclone's
  Cloudflare provider has no such quirk).

### Cloudflare cache rule (CRITICAL — do this once)

ostree `.tgz`/delta objects are content-addressed and immutable (cache them
freely), but **`summary`, `summary.sig`, `summary.idx` and `config` change every
publish**. If the CDN serves those stale, **clients never see the new build**.

1. Add a **Cache Rule**: for requests matching `*/steamdeck/summary*` (and
   `*/steamdeck/config`), set **Bypass cache**. This is the durable fix.
2. `publish-beta.sh` also **purges** those exact paths via the CF API after each
   sync as belt-and-braces. Both together = testers always see fresh metadata.

---

## Building the beta

Uses the **dev manifest** `flatpak/com.allow2.allow2linux.yml`.

```bash
cd examples/linux
# Prereqs on the build host (x86_64 Linux):
#   sudo apt-get install -y flatpak flatpak-builder
#   flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
#   flatpak install flathub org.freedesktop.Platform//23.08 org.freedesktop.Sdk//23.08
./scripts/publish-beta.sh
```

Notes / **[unverified-device]**:
- Target arch is **x86_64** (Steam Deck). A GitHub `ubuntu-latest` runner is a
  valid build host (see `.github/workflows/publish-beta.yml`) — you don't need
  the Deck to *build*, only to *test*.
- The dev manifest's daemon module now builds with `--share=network` so its
  `npm install` fetches the published `allow2` SDK + `ws` from the registry.
  (The Flathub manifest instead uses an offline yarn mirror.)
- The C overlay (`allow2-lock-overlay`) is compiled inside the manifest via
  `make`; that needs the SDL2 dev libs from the freedesktop SDK (already pulled).

CI: push a tag matching `v*-beta` (or run the workflow manually) to build +
publish with the secrets above.

---

## Tester: first-time install (manual, one-off)

On the Deck, in **Desktop Mode**:

1. **Install the app** — copy `com.allow2.allow2linux.flatpakref` to the Deck and
   double-click it (Discover/GNOME Software), or:
   ```bash
   flatpak install --from com.allow2.allow2linux.flatpakref
   ```
   This adds the `allow2-beta` remote (`https://get.allow2.com/steamdeck/`) and
   installs the app. The remote is added `gpg-verify=false` (unsigned beta over
   HTTPS — fine for internal testers; sign for production).
2. **Launch + pair** — run the app; it shows the pairing screen. In the Allow2
   parent app, scan the **QR** (or enter the **6-digit PIN**). Pair the child.
3. **Verify the service + linger** — the daemon auto-installs its `systemd --user`
   service, the auto-update timer, and enables linger on first run. Confirm:
   ```bash
   systemctl --user status allow2linux.service        # active (running)
   systemctl --user list-timers | grep allow2linux    # update timer scheduled
   loginctl show-user "$USER" | grep Linger            # Linger=yes
   ```
4. **If first-run couldn't set it up** (e.g. the Flatpak sandbox couldn't reach
   the host user manager) run the reliable fallback in Konsole:
   ```bash
   /path/to/examples/linux/scripts/install-service.sh
   # or, if you only have the installed app, the three manual steps:
   #   cp ~/.../allow2linux.service ~/.config/systemd/user/
   #   systemctl --user daemon-reload && systemctl --user enable --now allow2linux.service
   #   loginctl enable-linger "$USER"
   ```

After this, the tester does nothing further — updates arrive automatically.

---

## Pushing updates (the loop)

```bash
# make changes → bump the app → publish:
./scripts/publish-beta.sh
```

The tester's `allow2linux-update.timer` runs ~15 min after boot and every 6h
(with jitter, `Persistent=true` so a missed run fires on wake). To force it:

```bash
flatpak update -y com.allow2.allow2linux
systemctl --user try-restart allow2linux.service
# or trigger the unit directly:
systemctl --user start allow2linux-update.service
```

---

## Gotchas

- **x86_64 only.** The Deck is x86_64; don't ship an aarch64-only build.
- **Game-Mode overlay is dismissible.** In Gaming Mode (gamescope) there is no
  screen locker — the daemon SIGSTOPs the game and shows its own overlay, which a
  determined child can dismiss. Hard enforcement there is best-effort; the
  authority/quota is still enforced server-side. (Desktop Mode uses the real
  `loginctl` lock/terminate.)
- **SteamOS updates do NOT wipe the overlay/agent** — everything lives in
  `~/.var/app/com.allow2.allow2linux/` (Flatpak) and `~/.allow2/`, and **linger
  state lives under `/var` and survives SteamOS updates**. A SteamOS update won't
  remove a `--user` Flatpak or its linger.
- **`--user` services are user-killable.** `systemctl --user stop/mask
  allow2linux` or removing linger disables the agent without sudo. This is
  inherent to any `--user` daemon; managed/MDM enforcement is the answer for
  tamper-proofing, out of scope for this beta.
- **Metadata caching** (see the Cloudflare cache rule) is the #1 reason "I
  published but the tester didn't get it" — check `summary` freshness first.
- **Branch must match.** The `.flatpakref` `Branch=master` must equal the branch
  flatpak-builder exports (default `master`).

---

## Files in this loop

| File | Role |
|---|---|
| `com.allow2.allow2linux.flatpakref` | One-click tester install; `Url=https://get.allow2.com/steamdeck/` |
| `scripts/publish-beta.sh` | Build → export → R2 sync → CF purge (re-run to ship updates) |
| `scripts/install-service.sh` | Reliable manual service+timer+linger installer (first-run fallback) |
| `packages/allow2linux/systemd/allow2linux.service` | Canonical `systemd --user` daemon unit (Flatpak ExecStart) |
| `packages/allow2linux/systemd/allow2linux-update.{service,timer}` | Auto-update timer |
| `packages/allow2linux/src/first-run.js` | Auto-installs service + timer + linger on first launch |
| `.github/workflows/publish-beta.yml` | CI: build + publish on a `*-beta` tag |
| `flatpak/com.allow2.allow2linux.yml` | Dev build manifest (used by `publish-beta.sh`) |
