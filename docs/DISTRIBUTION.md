# allow2linux — Distribution channels (Phase-3)

How each channel installs, and which CI workflow builds it. The two workflows
have a **clean, non-overlapping division of labour**:

| Workflow | Owns | Triggers |
|---|---|---|
| **`publish.yml`** (do not edit) | **Flatpak → Cloudflare R2**, both channels | `staging` branch → staging prefix; `v*.*.*` tag → production prefix |
| **`release.yml`** | **Everything else** on a semver tag: the C overlay (x86_64 + aarch64), **deb + rpm**, the **curl\|bash installer + per-arch overlay assets**, and the **GitHub Release** | `v*.*.*` tag; `workflow_dispatch` |

A semver `v*.*.*` tag fires **both** workflows. The tag globs match exactly, and
`release.yml` no longer builds a Flatpak (that job was removed), so there is **no
Flatpak duplication**. AUR and AppImage are **dropped** from Phase-3.

---

## Channel matrix

| Channel | Tier | How it installs | Built by |
|---|---|---|---|
| **Flatpak** (self-repo → Flathub) | Primary | `.flatpakref` → `flatpak install` → auto-updates via `flatpak update` / timer | `publish.yml` (→ R2) |
| **curl \| bash installer** | Primary | `curl -sSL https://get.allow2.com/linux \| bash` — user-local, no root | `release.yml` (`installer-assets` → GitHub Release) |
| **deb** | Secondary | `apt install ./allow2linux_<v>_<arch>.deb` (x86_64 + aarch64) | `release.yml` (`package-deb`) |
| **rpm** | Secondary | `dnf install ./allow2linux_<v>_<arch>.rpm` (x86_64 + aarch64) | `release.yml` (`package-rpm`) |
| **Snap** | Optional (non-blocking) | `snap install --classic --dangerous allow2linux_*.snap` | `release.yml` (`package-snap`, manual/`continue-on-error`) |
| ~~AppImage~~ / ~~AUR~~ | Dropped | — | — |

---

## Primary: `curl | bash` installer

**`scripts/install.sh`** — parent-runnable, no root for the default user-local
install. It:

1. detects arch (x86_64 / aarch64),
2. bootstraps a Node ≥18 into `~/.local/share/allow2linux/node` if none present
   (no host Docker),
3. downloads the **daemon tarball** + the **per-arch overlay tarball** from the
   GitHub Release,
4. lays them out under `~/.local/share/allow2linux/packages/…` mirroring the repo
   `packages/` layout — so the daemon's `overlay-bridge.js` finds the overlay via
   its **existing** search paths (no daemon code change),
5. writes + enables the `systemd --user` unit, `enable-linger`, and starts the
   daemon so pairing begins.

**Production-locked:** the launcher `install.sh` writes bakes
`ALLOW2_PRODUCTION=1` + `NODE_ENV=production`, so the SDK guard pins to
`api.allow2.com` and this public installer **can never attach to staging**.
`install.sh` pre-creates the first-run marker (`~/.allow2/.setup-done`) so the
daemon's first-run won't overwrite the prod-locked unit; it performs the
enable + linger itself.

### The stable URL
`install.sh` and its payload are published as **GitHub Release assets**, reached
via GitHub's stable `…/releases/latest/download/<asset>` URLs. **`get.allow2.com/linux`
is a Cloudflare redirect** to `…/releases/latest/download/install.sh` (the only
piece hosted outside GitHub). Override the payload host with `ALLOW2_LINUX_BASE`.
(An R2 mirror under `get.allow2.com/linux/` is an equally valid host — `publish.yml`
already owns R2, so keep that separation if you mirror there.)

---

## Secondary: deb + rpm

Built by `packaging/build-package.sh` — **one spec → both formats** via `fpm`
(see `packaging/deb/README.md`, `packaging/rpm/README.md`). Installs:

- daemon → `/usr/lib/allow2linux` (src, config, systemd, package.json, node_modules)
- overlay + assets → `/usr/lib/allow2` (path matches `overlay-bridge.js`)
- production-locked launcher → `/usr/bin/allow2linux`
- `systemd --user` unit → `/usr/lib/systemd/user/allow2linux.service`
  (enabled for all users by the postinstall via `systemctl --global enable`)
- desktop entry, metainfo, icons

No Flatpak-style auto-update timer — deb/rpm updates come through apt/dnf.

Runtime deps declared: `nodejs ≥18`, SDL2 + SDL2_ttf + libX11 (distro-named).

> **[production-lock gap — FLAGGED]** On first launch the daemon's `first-run.js`
> writes a **per-user** copy of the unit to `~/.config/systemd/user/` with a
> node-direct `ExecStart` that does **not** re-export `ALLOW2_PRODUCTION`.
> `index.js` already defaults to the production vid/token, so the daemon still
> talks to production by default; the residual is that a user who sets
> `ALLOW2_ENV=staging` could reattach. Fully closing the lock for **packaged**
> installs needs a small `first-run.js` change (detect `/usr/bin/allow2linux` and
> use it as `ExecStart`, or copy the shipped unit verbatim). This touches shared
> daemon code — **flagged for a design decision, not silently changed.**
> `install.sh` sidesteps this cleanly via the first-run marker.

---

## Optional: Snap (non-blocking)

`snap/snapcraft.yaml` + the `package-snap` job (runs only on a manual dispatch
with `build_snap=true`, and is `continue-on-error` + not in the release job's
`needs`, so it **never blocks a release**).

> **Store-review caveat:** a parental-control agent needs `/proc` reads, session
> control (`loginctl`), and a fullscreen overlay — that requires **`classic`**
> confinement, which is gated behind **manual Snap Store review** (a human plus
> an automated/AI policy pass). Expect a justification request and rejection
> risk. Snap is deliberately the optional channel for this reason.

---

## allow2 SDK resolution (DECIDED — alpha phase)

**The `allow2` SDK is pulled from GIT, not npm, during alpha.**
`packages/allow2linux/package.json` declares:

```json
"allow2": "github:Allow2/allow2node#v2.0.0-alpha"
```

(committed `2d2bcd6`). The published `allow2@alpha` (2.0.0-alpha.6) predates the
staging guard + offline changes; the git branch has them.

Consequences for these channels:

- **deb / rpm / installer-assets** resolve daemon deps with **`npm install`**
  (never `npm ci`), against the git-ref in `package.json`. The stale monorepo
  `package-lock.json` (wrong `file:` path) is **never staged**.
- **Build env needs `git` + network** — npm clones the SDK repo. The user's
  machine does **not**: `install.sh` downloads a pre-built daemon tarball (deps
  already installed in CI).
- **Prerequisite:** the `Allow2/allow2node` `v2.0.0-alpha` branch must be pushed.
  **If that repo is private**, the git-install needs a token — the CI jobs read
  `secrets.SDK_GIT_TOKEN` and configure `git config url.insteadOf`. Set that
  secret if the SDK repo is private.
- **Future switch (one line):** when v2 is published to npm `latest`, flip the
  dep to `"allow2": "^2.0.0"` and the git prereqs/token become unnecessary.

---

## [unverified-device]

All builds here run on **CI/Linux**; they were authored, not executed, from the
Allow2 working environment. YAML/scripts were checked with `bash -n` / a YAML
lint. Validate the produced `.deb` / `.rpm` / `.snap` / installer on a real
x86_64 host and the Steam Deck (pairing, linger, Game-Mode overlay).
