# allow2linux Development Journey

## Architecture Evolution and Lessons Learned

---

## Phase 1: SDK Rewrite and Initial Architecture

The Allow2 Node SDK (v1) had issues that made it unsuitable for a device daemon: optional chaining syntax, baked-in staging references, mismatched method names, and a PIN bypass vulnerability in the pairing callback. Fourteen source files were reviewed and rewritten as SDK v2 — a pure ESM, Node 18+ package with a clean event-driven lifecycle (`DeviceDaemon`, `ChildShield`, `PairingWizard`).

The key architectural decision was making the SDK own all platform-agnostic logic: pairing, credential storage, check polling, enforcement state machine, warnings, requests, offline support, HTTP 401 handling. The platform code (allow2linux) only wires SDK events to Linux-specific implementations.

---

## Phase 2: SDL2 Overlay — 3,200 Lines of C

The overlay was implemented as a native C + SDL2 binary: 17 source files covering five screens (pairing, child selector, PIN entry, lock, warning), a custom JSON parser, Unix domain socket IPC, gamepad support, and animated transitions. Cross-compiled via Docker (Debian bookworm, matching SteamOS glibc) from Apple Silicon Macs.

---

## Phase 3: The Gamescope Wall

Deploying to the Steam Deck revealed that gamescope (the Game Mode compositor) only composites windows from Steam's own process tree. Six approaches were tried and all failed:

1. **STEAM_OVERLAY X11 atom** — gamescope only trusts this from Steam's overlay process
2. **GAMESCOPE_EXTERNAL_OVERLAY env var** — exists in source but did not work on SteamOS 3.x
3. **GAMESCOPECTRL_BASELAYER_WINDOW on :0** — property set, but gamescope ignored it from non-Steam processes
4. **Override-redirect window** — gamescope still ignored it
5. **Display switching (:0 vs :1)** — the control display does not composite arbitrary windows
6. **PID namespace injection** — fragile, didn't work

This is actually good security design — games can't overlay content on each other. But it blocks external parental control overlays.

---

## Phase 4: Dual Backend Architecture

### The Breakthrough: `steam://openurl`

Steam's built-in Chromium browser IS part of Steam's process tree, so gamescope composites it. The command `steam steam://openurl/http://127.0.0.1:3001/pairing` opens a URL in Steam's browser, rendered on top of games, accepting input.

### What Didn't Work

| Approach | Mode | Problem |
|----------|------|---------|
| `xdg-open` | Both | No browser registered on SteamOS |
| Firefox | Desktop | User can drag/minimize the window — not a lock screen |
| `steam` CLI | Desktop | Steam not running → crashes steamwebhelper with core dump |
| SDL2 fullscreen | Game Mode | Gamescope ignores non-Steam windows |
| SDL2 `FULLSCREEN_DESKTOP` | Desktop | Bypasses compositor → 90-degree rotation on portrait panel |
| SDL2 with DISPLAY unset | Desktop | systemd services don't inherit display env → invisible "offscreen" window |

### What Worked

| Mode | Backend | Communication | Display |
|------|---------|---------------|---------|
| **Game Mode** | HTTP server + WebSocket on :3001 | `steam steam://openurl/` | Steam's Chromium browser |
| **Desktop Mode** | SDL2 native binary | Unix domain socket | Borderless always-on-top window |
| **Mode switch** | Auto-detect Steam death | Seamless steam→sdl2 transition | Same JSON protocol both sides |

### Key Technical Solutions

- **Game Mode detection**: Requires BOTH `pgrep -x steam` AND `pgrep -f gamescope-session` — gamescope-session lingers after mode switch
- **Auto mode switching**: When Steam dies mid-session, daemon detects this, stops Steam backend, starts SDL2, re-shows current screen
- **Display env discovery**: systemd services don't inherit DISPLAY/WAYLAND_DISPLAY — daemon reads from `loginctl` session leader's `/proc/PID/environ`
- **SDL2 rotation fix**: Use borderless sized window (not `FULLSCREEN_DESKTOP`) so Wayland compositor handles rotation
- **QR dual format**: SVG for Steam browser, module grid for SDL2 — stripped from SDL2 messages to avoid 8KB buffer overflow

---

## Phase 5: Pairing Flow Redesign

The architecture evolved from "daemon auto-shows pairing overlay" to a deliberate two-mode design:

**Unpaired (dormant):** Daemon starts, detects no credentials, sits idle. No overlays, no enforcement, no visible presence. The only entry point is the user deliberately launching the Allow2 app (Flatpak on Steam Deck, desktop app on Linux).

**Paired (controlled):** Full enforcement — child identification, permission checks, warnings, blocks, requests. Only released by parent deleting the device from their Allow2 account (HTTP 401).

The SDL2 binary serves dual purpose:
- **App mode** (`--mode=app`): User-launched, normal window, closeable. Shows pairing screen when unpaired, status/info screen when paired.
- **Overlay mode** (`--mode=overlay`): Daemon-spawned, fullscreen, always-on-top, not dismissible. Used for enforcement screens (child selector, lock, warnings, requests).

Non-Steam games launched through Steam ARE composited by gamescope — so the Allow2 app in Game Mode works naturally as a "non-Steam game" entry in the Steam library.

The child selector was enhanced with search/filter, sorted by last-used (most recent first, tracked by the SDK), and parent access pinned to the bottom.

---

## Architecture Summary

```
┌──────────────────────────────────────────────────────────────┐
│                        allow2linux                            │
│                                                              │
│  Unpaired (dormant)           Paired (controlled)            │
│  ├─ No overlays               ├─ Child identification        │
│  ├─ No enforcement            ├─ Permission checks (30-60s)  │
│  └─ Wait for user to          ├─ Progressive warnings        │
│     launch Allow2 app         ├─ Lock screen + requests      │
│                               ├─ Process monitoring           │
│  User launches app:           └─ SIGSTOP/SIGTERM enforcement │
│  ├─ SDL2 app mode (Desktop)                                  │
│  └─ Non-Steam game (Game Mode)                               │
│                                                              │
│  Enforcement overlays:                                       │
│  ├─ Steam browser (Game Mode) — steam://openurl/             │
│  └─ SDL2 overlay mode (Desktop) — Unix socket IPC            │
└──────────────────────────────────────────────────────────────┘
```

---

## What This Demonstrates

The platform-specific code is ~5,400 lines (JS + C). The SDK handles ~2,500 lines of shared logic. A complete parental controls system — pairing, child identification, per-activity quotas, progressive warnings, lock screen, request more time, offline support, process monitoring, dual overlay backends, automatic mode switching — built on one of the hardest platforms (read-only Arch Linux with a custom compositor that blocks external overlays).

Conservative estimate for building this from scratch without the SDK: 12-24 months, $500K-$2M for a basic implementation that still lacks features Allow2 provides. The SDK approach reduces this by 1,000x+ in engineering effort for a more complete result.
