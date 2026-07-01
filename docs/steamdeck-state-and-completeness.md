# allow2linux on Steam Deck — State, Completeness & Industry Assessment

**Date:** 2026-06-29
**Scope:** Code-grounded completeness audit of `allow2linux` (Steam Deck / SteamOS / Linux on-device daemon) plus industry research on Steam Deck parental control / kiosk / bypass realities.
**Method:** Source read of `examples/linux/packages/` and repo packaging (trust code over docs, per operating principle §10); live web research for industry sections. Citations in **Sources**. Unconfirmed claims flagged `[unverified]`.

> **Headline verdict:** The earlier "~30% skeleton" estimate is a substantial undercount. This is a *deployed, debugged, integration-complete alpha* for the core control loop and Desktop-Mode UI. Corrected overall completeness toward a usable commercial alpha→release: **~60–65% (medium-high confidence)**. The genuine weaknesses are (a) the **Game-Mode enforcement surface is platform-limited, not just unfinished**, and (b) **distribution/packaging (Snap, deb, rpm, AUR, the promised one-line installer) is largely absent (~15–20%)**.

---

## Part A — Documented approach & roadmap

### A.1 The original Project Brief (`Allow2_SteamDeck_ProjectBrief.docx`, 8 Mar 2026)

Intended architecture and roadmap as written:

- **Decision:** on-device daemon using the Allow2 **Device API** (outbound calls), explicitly *rejecting* Allow2Automate (which needs inbound LAN access) so the device works "anywhere — home, school, travel, 4G hotspot."
- **Four components to build:** (1) Node.js daemon — `POST /api/check` every 60s for Gaming (3) + Screen Time (8), lock via `loginctl lock-session`, poll `GET /api/getUpdates` for extension approvals, 5-minute offline grace then deny-by-default, run as `systemd --user`; (2) lock-screen overlay with reason + "Request More Time"; (3) pairing wizard in Desktop Mode serving `http://localhost:3000`, parent enters Allow2 email/password once, `POST /api/pairDevice`, saves to `~/.allow2/credentials.json`, maps each child to a Linux username; (4) one-line installer `curl -sSL https://get.allow2.com/steamdeck | bash` that installs Node via nvm into home, drops daemon/overlay/wizard into `~/.allow2/`, registers the service, launches the wizard.
- **Multi-child:** one Linux user per child; daemon reads active Linux user → maps to Allow2 `childId`; mapping stored in shared credentials file.
- **Offline table:** succeed→cache; fail→cached result + grace timer; <5min→allow with "checking"; >5min→lock deny-by-default; WiFi back→re-check.
- **SteamOS notes:** immutable root → everything in `~/.allow2/`; Node via nvm in home; `systemd --user` in `~/.config/systemd/user/`; overlay must work in Game Mode (gamescope) and Desktop Mode; **brief itself flags that `loginctl lock-session` works in Desktop Mode but "Game Mode may require a different approach (e.g. overlay via gamescope layer or simply killing the game process)."**
- **Publish target:** npm `allow2automate-steam-deck` + PR to the registry.

### A.2 The evolved design (`docs/PROJECT_BRIEF.md` v6.0) & journey (`docs/DEVELOPMENT_TIMELINE.md`)

The shipped design diverged from the docx in important ways (the repo is the newer truth):

- **Three packages**, not one npm plugin: `allow2linux` (Node daemon, the product), `allow2` (SDK v2 / allow2node, rewritten clean ESM), `allow2-lock-overlay` (C + SDL2 binary). Product name became **"Allow2 Parental Freedom for Linux"**, app-id `com.allow2.allow2linux`.
- **Pairing redesigned away from email/password on device** → PIN/QR; parent authenticates on their phone; device never sees credentials. Two lifecycle modes: **Unpaired (dormant — no overlays/enforcement)** and **Paired (full enforcement, released only by parent deleting the device → HTTP 401)**.
- **ChildShield** model reused from the Brave browser integration: OS-user mapping → selector → PIN (SHA-256+salt, 5-attempt → 5-min lockout) → optional push auth.
- **The "Gamescope Wall" (`DEVELOPMENT_TIMELINE.md` Phase 3):** the team tried six ways to composite an external SDL2 overlay over a game in Game Mode (`STEAM_OVERLAY` atom, `GAMESCOPE_EXTERNAL_OVERLAY`, `GAMESCOPECTRL_BASELAYER_WINDOW` on `:0`, override-redirect, display switching, PID-namespace injection) — **all failed**, because gamescope only composites windows from Steam's own process tree. **Documented pivot (Phase 4): use Steam's built-in Chromium browser via `steam steam://openurl/...`** as the Game-Mode display surface (it *is* in Steam's process tree, so gamescope composites it). Desktop Mode keeps the native SDL2 overlay. The daemon auto-switches steam→sdl2 when Steam dies.
- **Roadmap (PROJECT_BRIEF §8):** 12 phases (SDK phases 1–6, then process classifier, overlay screens, wiring, Flatpak+Snap packaging, publish). **§11 "Parked":** configurable activity IDs, custom process mappings, **tamper detection**, macOS/Windows credential backends, push auth, profile-identity binding, browser extension, non-Flatpak auto-updater, deb/rpm/AUR.
- **Distribution promised (§1.5):** Flatpak, Snap, deb, rpm, AppImage, AUR.

### A.3 Stated vs. actual status

`metainfo.xml` declares release **1.0.0-alpha.1 (2026-03-08)**; `package.json` is `1.0.0-alpha.1`. There is no CHANGELOG/TODO/ROADMAP file beyond the docs above. The `ALLOW2_LINUX_CASE_STUDY.md` and `DEVELOPMENT_TIMELINE.md` present the work as a finished alpha ("a complete parental controls system… built on one of the hardest platforms"). The code review below largely supports the *core* of that claim but corrects several "done" implications (Snap, installer, logUsage, mid-session getUpdates, Game-Mode un-dismissability).

---

## Part B — Implementation completeness audit (line-grounded)

Repo layout audited: `packages/allow2linux/src/*.js` (daemon), `packages/allow2-lock-overlay/` (C overlay), `flatpak/`, `data/`, `scripts/`, `.github/workflows/`, and the SDK at `/workspace/ai/allow2/sdk/node/` (separate package, but it owns all API wiring).

### B.1 Daemon entry — `src/index.js` (598 lines, 20 KB) — **COMPLETE (functional), ~85%**

Wires every SDK event to a Linux action. Solid, defensive code:
- **Single-instance lock** with PID + `/proc/<pid>/cmdline` verification to survive Flatpak PID-namespace recycling; SIGUSR1 to an existing instance to re-open the window instead of double-launching (`index.js:25–66, 551–554`).
- Constructs `DeviceDaemon` with activities `[8,3,1,6]`, `checkInterval:60`, `gracePeriod:300`, `childResolver: resolveLinuxUser`, and **hardcoded production-style `vid:21599` / `token:'x9AUeUPpiweHTNCR'` fallbacks** (`index.js:76–86`). Note: secrets in source — acceptable for a "version token" (identifies the app, not the device) but should move to build-time injection.
- ChildShield PIN flows fully wired (`child-selected`→PIN gate→`selectChild`; parent PIN→`enterParentMode`) (`index.js:116–186`).
- Enforcement handlers: `activity-blocked` → SIGTERM all classified PIDs, SIGKILL after 10 s (`index.js:401–419`); `soft-lock` → SIGSTOP the active game + show lock overlay (`421–428`); `unlock` → SIGCONT (`430–437`); `hard-lock` → kill game + `session.terminate()` (`439–444`); offline-grace/deny, unpaired (401), children-updated, feedback all handled.
- Logging to `~/.allow2/allow2linux.log`, uncaught-exception/rejection guards (`483–506`).

**Gaps / bugs:**
- `hard-lock` calls `session.terminate()` which runs `loginctl terminate-session ''` with an **empty session id** (`session.js:22`) — will not terminate the right session; effectively a no-op/error. (Root-cause fix: resolve session id first via `getSessionId()` and pass it.)
- `_isGameMode()` helper (`558–566`) is defined but unused; mode detection lives in the overlay bridge.
- Status screen `family` field is a `// TODO` (`index.js:338`).

### B.2 Overlay bridge — `src/overlay-bridge.js` (1203 lines, 48 KB) — **COMPLETE but architecturally constrained, ~80%**

The most sophisticated file. Two auto-detected backends sharing one JSON protocol:
- **Game Mode → Steam Chromium browser.** HTTP+WebSocket server on `127.0.0.1:3001`; screens rendered by **embedded HTML/CSS/JS** (`OVERLAY_CSS`/`OVERLAY_JS`, ~220 lines, lines 986–1203); opened via `steam steam://openurl/http://127.0.0.1:3001/<screen>` (`_openSteamUrl`, 448–478). Heartbeat monitor (500 ms) + aggressive **re-open logic** (`_scheduleReopen`, 519–546) that re-launches the page if the WebSocket drops or the browser is closed — this is the mechanism that *tries* to make the Game-Mode lock "sticky."
- **Desktop Mode → native SDL2 binary** over Unix socket `/tmp/allow2-overlay.sock`. On-demand spawn, app-mode vs overlay-mode (`_showScreen`, 224–266), restart-on-crash for overlay mode but not app mode (`_scheduleRestartSdl`, 723–746).
- **Auto mode-switch** steam→sdl2 when Steam dies mid-session (`_switchToSdl2`, 480–492).
- **Display-env discovery** for systemd `--user` services that don't inherit `DISPLAY`/`WAYLAND_DISPLAY`: reads the graphical session leader's `/proc/<pid>/environ` via `loginctl`, plus Xwayland auth-file fallback (`_discoverDisplayEnv`, 888–980). This is real, hard-won integration code.
- QR stripped from SDL2 messages to avoid the C binary's 8 KB buffer overflow (`_sendSdl`, 770–785).

**Key limitation (platform, not code quality):** the Game-Mode "lock" is a **Steam browser page, which is dismissible** — the child can press B / close it. The re-open loop fights this (re-launch within 200–500 ms, backoff after 5 attempts/30 s), but it is a race, not a true modal lock. There is no un-bypassable lock surface in Game Mode (confirmed by Part C: gamescope's single external-overlay slot is held by mangoapp and not available to external processes). Warning bar uses `body{background:transparent}` so the game shows through — correct for warnings, but underlines that Game-Mode enforcement is "nudge + kill/suspend process," not "cover the screen."

### B.3 Native SDL2 overlay — `packages/allow2-lock-overlay/` (~4,900 lines C, 11 `.c` files) — **COMPLETE for Desktop Mode, ~85%; Game-Mode-native path DEAD**

- `main.c` (801 lines): SDL2 init, 7 screens (pairing, selector, PIN, lock, warning, status, feedback, denied), game-controller input, custom JSON parser (`json.c`), Unix-socket client (`socket.c`), deferred fullscreen window creation, ESC blocked on the lock screen, 60 s watchdog that exits a stale overlay so it can't lock the screen forever (`main.c:603–612`). Per-screen modules each 167–613 lines and complete.
- **`set_gamescope_overlay()` (`main.c:93–155`) sets `STEAM_OVERLAY`, `STEAM_INPUT_FOCUS`, override-redirect, and `GAMESCOPECTRL_BASELAYER_WINDOW` on `:0`** — this is exactly the approach `DEVELOPMENT_TIMELINE.md` Phase 3 documents as **tried and failed**. It remains in the binary but is *not* the Game-Mode path (the JS bridge routes Game Mode to the Steam browser). Effectively dead/aspirational code; harmless but misleading.
- Binary `allow2-lock-overlay` is **committed pre-built (64 KB, x86_64 only)** — no aarch64 build committed; CI/Flatpak build it per-arch.
- `register-steam-shortcut.py` (148 lines): hand-rolls Valve's binary VDF to register the overlay as a hidden non-Steam game so gamescope *would* composite it — an alternative Game-Mode route. Complete but fragile (binary-VDF surgery) and **not wired into any install flow**.

### B.4 QR generator — `src/qr.js` (540 lines, 17 KB) — **COMPLETE for its use case, ~80%**

Full pure-JS QR encoder: GF(256) Reed–Solomon, byte mode, all 8 masks with penalty scoring, SVG output (Steam browser) and flat module grid (SDL2). **Caveat:** `CAPACITIES` table covers v1–20 but `EC_TABLE`/`TOTAL_CODEWORDS`/`ALIGNMENTS` only cover v1–10; for data >213 bytes it falls back to `ecCW = floor(total*0.3)` which is **not spec-correct and may yield unscannable codes**. Irrelevant in practice — pairing URLs (`https://app.allow2.com/pair?pin=XXXXXX`, ~40 chars) fit in v3 — but a latent bug if payloads grow.

### B.5 Steam monitor — `src/steam.js` (157 lines) — **PARTIAL, ~60%**

- `/proc` scan for `steam`/`gamescope`; `getActiveGamePid()` detects games by `SteamAppId`/`STEAM_COMPAT` in `/proc/<pid>/environ` (151–92) — reasonable but permission-limited (can't read other users' environ; fine for single-user Deck).
- PPID parse (`steam.js:67–71`) is computed but unused/buggy.
- `notify()` (128–136) is effectively a **no-op** — it just `xdg-open steam://open/console`; there is no real Steam toast integration despite docs claiming "Steam notifications via `steam://`."
- `killActiveGame()` SIGTERM→SIGKILL works.

### B.6 Session manager — `src/session.js` (48 lines) — **PARTIAL, ~50%**

`lock()` = `loginctl lock-session` (works in Desktop Mode only — see Part C; no locker in Game Mode). `terminate()` = `loginctl terminate-session ''` — **empty session arg bug** (see B.1). `getSessionId()` exists but isn't used to fix it.

### B.7 Process classifier — `src/process-classifier.js` (159 lines) + `config/processes.json` — **COMPLETE for scope, ~80%**

`/proc` scan mapping `comm` → activity (Gaming 3 / Internet 1 / Social 6); Screen Time 8 always active. Solid. **Limits:** matches `/proc/<pid>/comm` which is **truncated to 15 chars** by the kernel (e.g. `chromium-browser`→`chromium-browse`) and ignores `cmdline`; mapping is static (no server-driven custom maps — that's "parked"). Default map duplicated in both the JS constants and `config/processes.json` (drift risk).

### B.8 Desktop notifier — `src/desktop-notify.js` (43 lines) — **COMPLETE (minimal), ~75%**

`notify-send` wrapper with urgency mapping. Comment admits "In production, we'd use D-Bus directly" — no direct D-Bus path, so it depends on `notify-send` being present.

### B.9 systemd unit — `systemd/allow2linux.service` — **PARTIAL/inconsistent, ~50%**

The committed unit uses `ExecStart=/usr/bin/node %h/.allow2/allow2linux/src/index.js` — but `dev-deploy.sh` deploys to `~/allow2/allow2linux/...` and Node to `~/node/bin/node`, and **generates a different, better unit inline** (with `EnvironmentFile=-%h/.allow2/.env`, correct PATH, `Restart=always`). So the checked-in unit is a placeholder that doesn't match the real deploy. **No `loginctl enable-linger`** anywhere → the service won't start at boot before the user logs into a graphical session (Part C: linger is required and is the documented Deck pattern).

### B.10 Packaging & distribution

- **Flatpak — MOSTLY COMPLETE, ~75%.** Two manifests: `flatpak/com.allow2.allow2linux.yml` (dev, installs deps via `npm install` at build — won't work on Flathub's offline builders) and the more complete `flatpak/flathub/com.allow2.allow2linux.yml` (yarn **offline mirror** via `node-sources.json`, git sources, Inter font module, multi-res icons, Node 20 x86_64+aarch64). `finish-args` are sane (wayland/x11/dri, network, `/proc:ro`, `home`, session+system bus for notifications/login1, `org.freedesktop.secrets`, `/tmp`). **TODOs:** both flathub source entries say `# TODO: pin to a release tag` (currently `branch: main` — not Flathub-acceptable); not yet submitted to Flathub.
- **Snap — ABSENT.** Brief/PROJECT_BRIEF promised Snap; there is **no `snap/snapcraft.yaml`**.
- **deb / rpm / AUR — ABSENT.** `.github/workflows/release.yml` has matrix jobs for deb/rpm/aur/release that are all **`echo "TODO"`** stubs; only the Flatpak build job is real. No `packaging/` directory exists.
- **One-line installer — ABSENT.** No `install.sh`, no `get.allow2.com/steamdeck` script. `flatpak/dev-deploy.sh` (308 lines) is the de-facto installer but is **developer/SSH-oriented**: it bootstraps Node into `~/node`, rsyncs SDK+daemon, cross-compiles the overlay **in Docker on the host**, deploys `.env`, writes+enables the systemd unit, restarts. Robust for devs; **not** a parent-runnable installer, and it still requires the host to have Docker for the overlay build.
- **CI — `ci.yml` real** (npm test [`|| true`], overlay build, flatpak offline-source generation+validation). **`release.yml` mostly stubs.**
- **Data:** desktop entry, AppStream metainfo (well-formed, OARS rating, screenshots referencing `main`), SVG+PNG icons, three screenshots present.

### B.11 Which Allow2 Device API calls are actually wired (SDK at `sdk/node/src/`)

| Call | Wired? | How / evidence |
|---|---|---|
| **Pairing** | ✅ | `/api/pair/qr/init`, `/api/pair/pin/init`, `/api/pair/status/:id` (`api.js:95–136`). Note: PIN/QR flow, **not** the docx's `pairDevice` email/password. |
| **check** | ✅ | `POST /serviceapi/check` (**not** `/api/check` as docs say), every **60 s**, `log:true`, with `userId/pairId/pairToken/deviceToken/tz/childId/activities` (`api.js:142–156`, `checker.js:153–169`). |
| **getUpdates** | ⚠️ Partial | `GET /api/getUpdates` (`api.js:161–172`). **Only polled by the heartbeat (every 60 s) when paired-but-no-child-selected** (`daemon.js:599–635`). When a child is actively enforcing, the checker runs but the heartbeat is **stopped** (`daemon.js:655`), so the dedicated getUpdates poll for extensions/bans/daytype/quota changes **does not run mid-session**. (Request approvals still arrive via the request-status poll; and `check` returns live `remaining`, so quota changes are felt — but the brief's "poll getUpdates for extension approvals" during use is effectively not implemented as designed.) The standalone `UpdatesPoller` in `updates.js` (30 s default) is exported but **unused** by the daemon. |
| **createRequest + status** | ✅ | `/api/request/createRequest` then polls `/api/request/:id/status` with `X-Status-Secret`; `index.js:188–216` polls every 5 s for 10 min. |
| **logUsage** | ❌ Not wired | `logUsage()` defined in `api.js:288–300` but **never called anywhere** — the brief's "reconcile usage after offline" is not implemented. |
| **feedback** | ✅ | submit/load/reply wired (`api.js:221–279`, `index.js:227–255`). |

**Offline handling:** `checker.js` does its own **in-memory** grace (`_offlineSince`, 5 min → `offline-deny`, `checker.js:264–294`); it retains the last in-memory `_state` during grace. The disk-cache `OfflineHandler` (`offline.js`, writes `~/.allow2/cache.json`) **exists but is not used by the daemon** → a cached "allowed" result is **not persisted across daemon restarts**, so a restart during an outage falls back to deny-after-grace rather than "use last cached result." Partial vs. the brief's offline table.

**Multi-child:** `resolveLinuxUser` maps `$USER`→`childId`; otherwise the ChildShield selector gates entry; PIN per child. Matches the design. (The docx's "one Linux user per child" is supported by the resolver but is itself off the supported SteamOS path — see Part C §5.)

### B.12 Documented (brief) vs. built

| Brief promised | Built? |
|---|---|
| On-device daemon, check every 60s (Gaming+Screen Time) | ✅ (checks 8,3,1,6) |
| Lock via `loginctl lock-session` | ⚠️ Desktop only; Game Mode uses SIGSTOP+browser overlay; terminate has empty-id bug |
| Poll getUpdates for extensions | ⚠️ only when idle, not mid-session |
| 5-min offline grace, deny-by-default | ⚠️ in-memory only; disk cache unused |
| Request More Time + remote approve | ✅ |
| systemd `--user` service surviving updates | ⚠️ unit inconsistent; no enable-linger |
| Pairing wizard `localhost:3000` | ✅ (SDK pairing on port 3000) — but PIN/QR, no email/password |
| `pairDevice` saving credentials | ✅ (PIN/QR variant; PlaintextBackend → `~/.allow2/`) |
| Multi-child via active Linux user | ✅ resolver + selector |
| Overlay works in Game **and** Desktop Mode | ✅ via dual backend (Game Mode = dismissible Steam browser, not a true lock) |
| `curl \| bash` installer | ❌ absent (dev SSH script only) |
| Snap / deb / rpm / AUR / AppImage | ❌ absent (Flatpak only; release CI stubbed) |
| logUsage reconcile | ❌ not wired |
| Tamper detection | ❌ parked |

### B.13 Per-component completeness (corrected)

| Component | State | % | Confidence | Evidence |
|---|---|---|---|---|
| `index.js` daemon entry | Complete (functional) | 85% | High | full event wiring; minor terminate bug |
| `overlay-bridge.js` | Complete, platform-constrained | 80% | High | dual backend, heartbeat/reopen, mode-switch, env discovery |
| `allow2-lock-overlay` (C) | Complete (Desktop); Game-native dead | 85% | High | 7 screens, JSON/socket/controller, watchdog; gamescope atoms unused |
| `qr.js` | Complete for short payloads | 80% | High | full encoder; v11–20 EC fallback non-spec |
| `steam.js` | Partial | 60% | Med | detect+kill ok; notify no-op; ppid dead code |
| `session.js` | Partial | 50% | High | lock ok (Desktop); terminate empty-id bug |
| `process-classifier.js` | Complete for scope | 80% | High | /proc map; comm 15-char truncation |
| `desktop-notify.js` | Complete (minimal) | 75% | High | notify-send only |
| `systemd/` unit | Partial/inconsistent | 50% | High | mismatched paths; no linger |
| Flatpak packaging | Mostly complete | 75% | Med | flathub manifest good; tags unpinned, not submitted |
| Snap / deb / rpm / AUR / installer | Absent | 15% | High | no files; release CI stubbed |
| API wiring (SDK) | Mostly wired | 75% | High | check/pair/request/feedback ✅; getUpdates partial; logUsage unused; offline disk-cache unused |
| **Overall (toward usable alpha→release)** | **Substantial alpha** | **~60–65%** | **Med-High** | core loop + Desktop UI done; Game-Mode lock weak by platform; distribution thin |

The "~30% skeleton" figure was wrong: ~7,500+ lines of working, deployed, debugged JS+C across daemon, dual overlay, QR, child-shield wiring, Flatpak offline build, and a real (if dev-only) installer is well past skeleton. It is held back from "release" by distribution gaps and the inherent Game-Mode/tamper ceiling, not by missing core function.

---

## Part C — Industry best practices & reviews

### C.1 Doing time-limiting / kiosk / parental control on SteamOS — the technical reality

- **Immutable, atomic A/B rootfs.** SteamOS 3 replaces the root filesystem atomically on update (writes to the inactive partition, reboots, A/B fallback). Anything written to `/usr` or installed via pacman is **wiped on update**; pacman is unsupported and its keyring ships broken (`steamos-readonly disable` required, then keyring repair). **Therefore the design's "everything in `~/.allow2` + `systemd --user`" is exactly right and is the only update-safe, root-free path.** The home partition and `~/.config/systemd/user/` are writable and untouched by image swaps. [Sources 1,2,3,4]
- **Boot-at-power-on requires lingering:** `sudo loginctl enable-linger deck` starts the user manager at boot without a login; linger state lives under `/var` and **is not reset by SteamOS updates**. **allow2linux does not set this** → gap (B.9). [Source 5]
- **Game Mode = gamescope micro-compositor; Desktop Mode = KDE Plasma.** In KDE, normal always-on-top / layer-shell windows work (the SDL2 overlay is legitimate there). In gamescope, **an external process cannot reliably draw an always-on-top surface over a game.** gamescope exposes a **single external-overlay slot** (atom-driven, `GAMESCOPE_EXTERNAL_OVERLAY` / `STEAM_OVERLAY`), and **mangoapp now grabs it on startup and never releases it**, so other apps can't draw there; even Valve's own overlay is fragile under gamescope WSI/HDR. This is precisely the "Gamescope Wall" the repo hit. The only sanctioned in-Game-Mode UI routes are (a) win that contended overlay atom, (b) inject into the Steam UI layer via **Decky**, or (c) register your binary as a Steam shortcut so it's in Steam's process tree (what `register-steam-shortcut.py` does) — or the project's chosen route, **the Steam Chromium browser via `steam://openurl`.** [Sources 6,7,8,9]
- **`loginctl lock-session`:** works in Desktop Mode (KDE locker; caveat: needs a real/BT keyboard, virtual keyboard issues). **Game Mode has no screen locker**, so session-lock yields no usable lock there — matching the brief's own warning and the daemon's choice to SIGSTOP the game + show a browser overlay instead. [Sources 10,11]
- **Decky Loader** injects React UI into the Steam Game-Mode UI without touching system files; it's the only mainstream way to put custom UI in Game Mode. But it's user-installed/user-removable, runs in user space, and is **not a trust anchor** for enforcement — fine as a parent-facing convenience, not an enforcement boundary. There are **no mature parental-control enforcement plugins** on Decky as of mid-2026 (a Playtime *tracker* exists). [Sources 12,13]
- **Single-user OS.** SteamOS uses one Linux user (`deck`); Steam-account switching shares the same home and does **not** isolate Linux state. Adding per-child Linux users is possible via `useradd`/SDDM but is **off the supported path** (Game Mode, auto-login, updates assume `deck`). So the docx's "one Linux user per child" multi-child model is technically doable but **not realistic/supported on a Deck** — the in-app ChildShield selector is the pragmatic mechanism, which the SDK already favours. [Sources 14,15]

### C.2 Existing solutions & community consensus

- **Steam Families (2024–2025 revamp)** replaced Family View. Per-child **daily/weekly playtime limits, scheduled access windows, per-game allow-listing, purchase approval, community-feature toggles**; role-based (adult vs child) rather than a local 4-digit PIN. **Limits that matter for positioning allow2linux:** (1) **Steam-only scope** — does *not* cover desktop apps, browsers, emulators, or non-Steam content; (2) **per-account, not per-device** — a different/new/offline account sidesteps it; (3) **reset/escape via the child's password or either account's email**; (4) assumes the Steam client is intact. [Sources 16,17,18]
- **SteamOS has no OS-level screen-time** feature (unlike Switch/iOS); the only built-ins are inside Steam. (Aside: GNOME 50 is adding desktop screen-time controls in 2026, but SteamOS ships **KDE**, so it doesn't apply.) [Sources 13,19,20]
- **Third-party on-device tools:** none robust. Generic Linux options (Timekpr-nExT, ArchWiki hosts/dnsmasq/timekpr stack) all carry the explicit caveat that **the restricted user must not have sudo, and a live USB/SD boot defeats them**. **Community consensus is that real control is network/router/DNS-level** (Gryphon, NETGEAR, Bark Home) *because it can't be disabled on-device*; common parent playbook = child-role Steam account + keep all credentials/email + set a sudo/desktop password + lock or withhold Desktop Mode + push time/content enforcement to the router. [Sources 21,22,23,24]

### C.3 Bypass landscape on the Deck — the honest tamper-resistance ceiling

The Deck is an open Linux PC; **physical possession + the `deck` user ≈ root-capable**, and a user-space agent runs *as* the user it's trying to police. Escape vectors, roughly by effort:

1. **Escape to Desktop Mode** → full KDE + Konsole as `deck`. [Source 25]
2. **Disable/kill the agent with no sudo:** `systemctl --user stop/disable/mask allow2linux`, `kill`, or delete the unit — the user owns their own session services. **This is the central, unavoidable problem for any `--user` daemon.** [Source 5 + systemd `--user` semantics; no Deck-specific writeup — *largely `[unverified]` as a published Deck article*]
3. **sudo password is unset by default;** until a parent sets it, `sudo` is free. And it's **resettable with only physical access** — boot to a TTY (`Ctrl+Alt+F2`) or the Boot Manager (Vol-Down+Power) → recovery → `passwd deck`. So a parent-set sudo password is **not a hard root of trust**. [Sources 26,27]
4. **Boot another OS / live image from SD/USB** (Boot Manager) and mount the **unencrypted-by-default** internal partition → read/modify/disable the agent and config. [Sources 22,28]
5. **Dual boot** a second OS — clean environment, no agent.
6. **Re-image with Valve's official recovery image** (balenaEtcher → boot → "Re-image") → wipes everything, **no credential required**. [Sources 28,29,30]

**Ceiling:** *No user-space agent on a stock Deck can be made tamper-proof.* It can be stopped, masked, edited, starved of config, booted around, or factory-reset — most without any credential. Genuine resistance needs **off-device** controls (router/DNS) or the **full kiosk-hardening stack** the kiosk literature prescribes — immutable rootfs (already present on SteamOS) **plus** full-disk encryption (block offline tamper), **UEFI/BIOS password + Secure Boot** (block external-media/recovery boot), **disabled TTYs/terminal**, and **password-protected sudo the child lacks** — and **even then recovery-reflash and physical disassembly remain residual escapes**. On a stock Deck these mitigations are absent or defeatable. [Sources 31,32,33,34]

### C.4 Published research

No Deck-specific academic literature on screen-time/kiosk enforcement. The directly-applicable practitioner corpus is the **kiosk-lockdown** body: `ikarus23/kiosk-mode-breakout` (escape catalogue: terminal hotkeys, TTY switching, kill-X, **unlocked UEFI + unencrypted disk → live-boot breakout**); GNOME `org.gnome.desktop.lockdown` single-app mode; Hexnode/kiosk-distro hardening (disable TTY switching, kernel `lockdown` LSM, restrict shell, FDE). Consistent expert conclusion: a fullscreen app is not a kiosk; real lockdown is the layered set above. [Sources 31,32,33,34]

---

## Part D — Synthesis

### D.1 Completeness table

See **B.13** — overall **~60–65%** toward a usable commercial alpha→release (medium-high confidence). Core control loop + Desktop-Mode UI: ~80–85%. Game-Mode enforcement: functional but platform-capped (dismissible browser overlay + process suspend, no true lock). Distribution/installer: ~15–20%.

### D.2 Gap list to reach usable alpha → release

**Correctness / robustness (do first):**
1. Fix `session.terminate()` empty-session-id bug (resolve id via `getSessionId()` first). (`session.js`)
2. Wire **`enable-linger`** in the installer and ship one canonical, path-correct systemd unit (reconcile committed unit with `dev-deploy.sh`'s inline unit).
3. Decide and implement **mid-session getUpdates** (extensions/bans/daytype) — currently only polled when idle; either run a low-rate getUpdates alongside the checker or fold updates into the check response handling.
4. Actually use the disk **`OfflineHandler`** (or persist last-good check) so "use last cached result" survives daemon restart during outages; wire **`logUsage`** for post-offline reconciliation (or delete it and the brief claim).
5. Fix `qr.js` v11–20 EC tables or cap/validate payload length so a longer pairing URL can't produce an unscannable code.

**Game-Mode vs Desktop-Mode overlay coverage:**
6. Accept and document that Game-Mode enforcement = **suspend/kill the game process + best-effort Steam-browser overlay**, not a screen lock. Harden the "sticky" behaviour: keep the game SIGSTOP'd while the lock is owed (so closing the browser doesn't resume play), rather than relying on the re-open race.
7. Make the `register-steam-shortcut.py` route a first-class, installer-driven option (overlay as a non-Steam game gives a more reliable Game-Mode surface than `steam://openurl`), and remove or clearly comment the dead `set_gamescope_overlay()` path.

**Distribution / installer (largest gap to "release"):**
8. Ship a **parent-runnable installer** (the promised `curl | bash`) that bootstraps Node into `$HOME`, installs daemon+overlay (pre-built per-arch binary, no host Docker), writes the unit, `enable-linger`, and launches pairing — replacing the SSH/dev `dev-deploy.sh` for end users.
9. **Submit to Flathub** (pin git sources to release tags; the flathub manifest is otherwise close). Flatpak is the best Deck fit.
10. Deliver the promised **Snap / deb / rpm / AUR / AppImage** (or formally de-scope them) and implement the stubbed `release.yml` jobs.

**Tamper mitigation (manage expectations, then layer):**
11. Implement the parked **tamper detection** (service disabled/stopped/credentials removed → notify parent) — turn an unwinnable "prevent" into a strong "detect + report," which is the honest best-practice posture.
12. Provide **parent setup guidance** for the off-device/hardening layers that actually bite: child-role Steam account + parent keeps credentials/email, set sudo password, withhold or PIN-gate Desktop Mode, and **router/DNS time+content controls** as the durable backstop. Optionally document UEFI password + FDE for high-assurance setups.

### D.3 Best-practice recommendations to align with what works on the Deck

- **Lead with Desktop-coverage + telemetry, not "lock the screen in Game Mode."** allow2linux's real differentiators vs Steam Families are: it covers **non-Steam apps/browsers/emulators**, it's **per-device + per-child (ChildShield)**, it does **cross-device activity quotas via the Allow2 cloud**, and **remote approval works anywhere**. Steam Families is Steam-only and per-account. Position there.
- **Treat the Deck as one lever in a layered system, not the enforcement boundary** (see D.4) — because the tamper ceiling is real.
- **Use `systemd --user` + linger + home-dir install** (already the plan) and **never** depend on pacman or rootfs writes.
- **Detect-and-report** beats brittle self-defence on an unlocked handheld.

### D.4 Fit with Allow2's combinatory "controller-of-controllers" model

allow2linux is best understood as **one on-device agent contributing several layered levers**, not a standalone lock:

- **On-device process control** (SIGSTOP/SIGTERM/SIGKILL of classified PIDs, session lock in Desktop Mode) — immediate, but defeatable by a determined holder.
- **Steam telemetry / Steam Families** as a complementary platform lever (per-game, purchase, schedule — Steam-scoped, account-bound).
- **Off-device levers** (router/DNS time+content) as the **tamper-resistant backstop** the device agent can't be.
- **The Allow2 cloud as the controller-of-controllers:** unified per-child quotas (Day Types, stacking activities), cross-device accounting (Deck gaming time counts against the same quota as console/phone), and remote approve/extend. The Deck agent feeds usage in and enforces what it can; the cloud reconciles across all devices and lets the parent approve from their phone.

So the correct framing for allow2linux on the Deck is: **a high-signal on-device enforcement + telemetry node that is strongest in Desktop Mode and as a "detect, suspend, report, and request-approval" agent — combined with Steam-account and network-level controllers for the coverage and tamper-resistance no on-device user-space agent can provide alone.**

---

## Sources

SteamOS / gamescope / persistence (Part C.1, C.3, C.4):
1. SteamOS + systemd-sysext (immutable rootfs, A/B, home writable) — https://blogs.igalia.com/berto/2022/09/13/adding-software-to-the-steam-deck-with-systemd-sysext/
2. SteamOS & OverlayFS — https://blog.setale.me/2022/06/27/Steam-Deck-and-Overlay-FS/
3. `steamos-readonly` man — https://linuxcommandlibrary.com/man/steamos-readonly
4. Valve SteamOS issue #2151 (what survives updates / `/etc` overlay) — https://github.com/valvesoftware/steamos/issues/2151 ; pacman keyring — https://steamcommunity.com/app/1675200/discussions/0/7529517132619672170/ ; https://christitus.com/unlock-steam-deck/
5. `systemd --user` + enable-linger persistence — https://medium.com/@alexeypetrenko/systemd-user-level-persistence-25eb562d2ea8 ; https://gist.github.com/simons-public/6a2dc641ee3cdc8caa00efe1dcf35250 ; https://blog.ihaveahax.net/2023/09/17/adventures-of-installing-third-party-software-on-steam-deck/
6. gamescope — https://github.com/ValveSoftware/gamescope
7. gamescope external-overlay slot held by mangoapp; `GAMESCOPE_EXTERNAL_OVERLAY`/`STEAM_OVERLAY` atoms — https://github.com/flightlessmango/MangoHud/issues/775  `[atom name/semantics unverified against shipping gamescope source]`
8. gamescope overlay fragility — https://github.com/ValveSoftware/gamescope/issues/1537 ; https://github.com/ValveSoftware/gamescope/issues/835
9. gamescope overview — https://steamcommunity.com/app/221410/discussions/0/803471225741509907/
10. `loginctl lock-session` / KDE locker on Deck — https://steamcommunity.com/app/1675200/discussions/1/598526768326922466/
11. lock-screen keyboard caveats — https://www.makeuseof.com/complete-guide-to-privacy-on-steam-deck/ ; https://github.com/ValveSoftware/SteamOS/issues/825  `["no locker in Game Mode" is well-supported inference, partially unverified]`
12. Decky Loader — https://github.com/SteamDeckHomebrew/decky-loader ; https://decky.xyz/
13. Decky / child-proofing & "no built-in screen time" — https://decky.net/en/blogs/news-en/child-proof-your-steam-deck ; https://steamdeckhq.com/tips-and-guides/enhance-your-steam-deck-with-plugins-from-decky-loader/
14. Single-user model — https://pimylifeup.com/steam-deck-multiple-users/ ; https://gamerant.com/steam-deck-add-switch-multiple-users/
15. Shared-state leakage — https://steamcommunity.com/app/1675200/discussions/1/4038102936190852716/

Steam Families / parental landscape (Part C.2):
16. Steam Families FAQ — https://help.steampowered.com/en/faqs/view/054C-3167-DD7F-49D4  `[Valve page returned only nav chrome to fetch; behaviour corroborated by secondary sources — partially unverified]`
17. Steam Families setup/limits/PIN deprecation — https://www.therundown.today/guides/steam-families-parental-controls-complete-setup-guide ; https://www.internetmatters.org/parental-controls/gaming-consoles/steam/
18. Steam-only scope / per-account / reset vectors — https://impulsec.com/parental-control-software/parental-controls-steam-deck/ ; https://www.playbite.com/q/does-the-steam-deck-have-parental-controls ; https://steamcommunity.com/app/1675200/discussions/2/3824161508156491069/ ; https://steamcommunity.com/app/1675200/discussions/0/3490878556691806560/
19. (context) GNOME 50 screen-time — https://ubuntuhandbook.org/index.php/2026/01/gnome-50-will-support-bedtime-daily-screen-time-parental-controls/
20. Blocking browsers on Deck (community) — https://steamcommunity.com/app/1675200/discussions/0/597402866249958367/
21. Timekpr-nExT / ArchWiki parental stack — https://wiki.archlinux.org/title/Parental_control ; https://www.linuxuprising.com/2019/11/timekpr-next-is-linux-parental-control.html ; https://www.baeldung.com/linux/limit-user-computer-time
22. Live-boot defeats on-device control — https://wiki.archlinux.org/title/Parental_control
23. Network-level recommendation — https://impulsec.com/parental-control-software/parental-controls-steam-deck/
24. Parent playbook / withhold Desktop Mode — https://stories.truple.io/posts/1f33294e-b756-4e26-b39b-016a7e157b9b/1749176527323

Bypass (Part C.3):
25. Konsole access — https://gamerant.com/steam-deck-how-access-konsole-terminal/
26. sudo unset by default — https://www.gamingonlinux.com/guides/view/how-to-set-change-and-reset-your-steamos-steam-deck-desktop-sudo-password/ ; https://www.dexerto.com/tech/how-to-set-a-sudo-2031183/
27. Reset sudo via TTY / Boot Manager — https://www.techbloat.com/how-to-reset-sudo-password-on-steam-deck-2-easy-methods.html
28. Boot Manager / recovery — https://help.steampowered.com/en/faqs/view/1B71-EDF2-EB6D-2BB3
29. Recovery re-image — https://pimylifeup.com/steam-deck-recovery/
30. Restore SteamOS — https://www.makeuseof.com/how-to-restore-steamos-on-steam-deck/

Kiosk lockdown (Part C.4):
31. kiosk-mode-breakout (escape catalogue) — https://github.com/ikarus23/kiosk-mode-breakout
32. GNOME single-app lockdown — https://help.gnome.org/admin/system-admin-guide/stable/lockdown-single-app-mode.html.en
33. Linux kiosk hardening — https://www.hexnode.com/blogs/what-is-linux-kiosk-mode/ ; https://forums.raspberrypi.com/viewtopic.php?t=155994
34. Kiosk distro roundup / "fullscreen app ≠ kiosk" — https://techyorker.com/9-free-open-source-linux-kiosk-distros-and-browsers-tools/

Repo (Parts A, B): `Allow2_SteamDeck_ProjectBrief.docx`; `docs/PROJECT_BRIEF.md`; `docs/OVERLAY_DESIGN.md`; `docs/DEVELOPMENT_TIMELINE.md`; `docs/ALLOW2_LINUX_CASE_STUDY.md`; `packages/allow2linux/src/*.js`; `packages/allow2-lock-overlay/src/*.c`; `flatpak/*`, `scripts/register-steam-shortcut.py`, `.github/workflows/*`; SDK at `sdk/node/src/*.js`.

### `[unverified]` / low-confidence items to spot-check
- Exact current `GAMESCOPE_EXTERNAL_OVERLAY` / `STEAM_OVERLAY` atom names & contention behaviour in the shipping gamescope build (confirm in gamescope `steamcompmgr.cpp`).
- "No screen locker in Game Mode" — strong inference from sources, not a single explicit Valve statement.
- Whether specific `/etc`-overlay writes survive a *major* SteamOS image bump.
- Supportability/viability of extra Linux users per child on a Deck (off documented path).
- `systemctl --user stop/mask` as the agent-kill vector — sound from systemd semantics but no Deck-specific published article cited.
- Steam Families specifics (PIN-unlock deprecation, exact reset flows) rest on secondary guides — Valve's FAQ didn't return body text to fetch; verify against the live FAQ before publishing externally.
- Hardcoded `vid:21599`/`token` in `index.js` and `staging-api` default in `.env.example` — confirm these are intended (token in source; staging URL as the example default).
