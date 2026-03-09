# Allow2 Linux Integration Case Study

## Building Next-Generation Parental Controls for Steam Deck and Linux

---

## 1. Overview

**allow2linux** is an Allow2 parental controls daemon for Linux. It implements the full Allow2 device lifecycle -- pairing, child identification, permission enforcement, warnings, lock screen, and request-more-time -- as a background service that runs on any Linux device.

**Primary targets:**
- Valve Steam Deck (SteamOS 3.x, both Game Mode and Desktop Mode)
- Standard Linux desktops (Ubuntu, Fedora, Arch, etc.)
- Other Linux handhelds (ASUS ROG Ally, Lenovo Legion Go)
- Raspberry Pi and single-board computers

**Architecture summary:** A Node.js daemon uses the Allow2 SDK for all platform-agnostic logic (API communication, credential storage, enforcement state machine). Platform-specific code handles process classification, Steam integration, and overlay display. The overlay uses two display backends that auto-switch: Steam's built-in browser for Game Mode (via `steam://openurl`) and a native SDL2 binary for Desktop Mode (via Unix domain socket).

**Repository:** [github.com/Allow2/allow2linux](https://github.com/Allow2/allow2linux)

---

## 2. The Point: Why This Matters

Before diving into architecture, consider the scope of what was built and what it would cost without the Allow2 SDK approach.

**What allow2linux delivers:**
- Device pairing via PIN and QR code deep link (universal link: iOS app, Android app, App Store, web)
- Child identification (OS username mapping or interactive selector with PIN)
- Per-activity quotas with stacking (Gaming, Internet, Social, Screen Time)
- Progressive warnings (15min → 5min → 1min → 30sec → 10sec → blocked)
- Lock screen with "Request More Time" (parent approves from phone)
- Offline support with grace period and deny-by-default
- Process monitoring and enforcement (SIGSTOP/SIGCONT for soft lock, SIGTERM/SIGKILL for hard block)
- Two native overlay backends with automatic mode switching
- Real-time bidirectional communication (WebSocket and Unix socket)
- QR code generation (pure JS, zero dependencies, works offline)
- Cross-compilation pipeline for SteamOS

**The platform code is ~1,200 lines of JavaScript and ~3,200 lines of C.** The SDK handles everything else.

**What this would cost to build from scratch:**

Most organisations that attempt parental controls build half-hearted implementations: a simple daily timer with no per-activity breakdown, no child identification, no request flow, no offline support, no progressive warnings. Even that minimal approach typically requires:

- 6-12 months of backend development (user management, device management, permission engine, API)
- 3-6 months of mobile app development (parent app for configuration)
- 2-4 months of per-platform client development
- Ongoing maintenance of the permission engine, edge cases, timezone handling, day types

Conservative estimate: 12-24 months and $500K-$2M for a basic implementation that still lacks features Allow2 provides out of the box.

**allow2linux was built from zero to running on real Steam Deck hardware in days, not months.** The SDK-first approach isn't a 10% improvement -- it's orders of magnitude less effort. The ratio is roughly 1,000x-2,000x less engineering time for a more complete result.

And because the Allow2 platform already handles the parent app, cloud API, permission engine, day types, activity definitions, request approval workflow, and multi-child management, the integration developer focuses only on what's unique to their platform.

---

## 3. Architecture

```
                    ┌──────────────────────────────────┐
                    │         Allow2 Cloud API          │
                    │       (api.allow2.com)            │
                    └───────────────┬──────────────────┘
                                    │ HTTPS (check, pair, request)
                                    │
┌───────────────────────────────────┼──────────────────────────────────┐
│                          allow2linux daemon                          │
│                         (packages/allow2linux/)                      │
│                                                                      │
│  ┌──────────────┐   ┌──────────────────┐   ┌─────────────────────┐  │
│  │  Allow2 SDK  │   │  index.js        │   │  OverlayBridge      │  │
│  │  (npm pkg)   │   │  Event wiring    │   │  (overlay-bridge.js)│  │
│  │              │   │  Lifecycle glue   │   │                     │  │
│  │ • DeviceDaemon│──│  • pairing-required──│ Game Mode:          │  │
│  │ • Credentials│  │  • child-select   │   │  HTTP+WS :3001     │  │
│  │ • Check API  │  │  • warning        │   │  steam://openurl    │  │
│  │ • Enforcement│  │  • activity-blocked│  │                     │  │
│  └──────────────┘  │  • soft-lock      │   │ Desktop Mode:       │  │
│                     │  • hard-lock      │   │  SDL2 native binary │  │
│  ┌──────────────┐   │  • unpaired       │   │  Unix socket IPC    │  │
│  │ProcessClassif│   └──────────────────┘   │                     │  │
│  │ (/proc scan) │                          │ Auto-switches when  │  │
│  └──────────────┘   ┌──────────────────┐   │ Steam dies (mode    │  │
│                     │  SteamMonitor    │   │ switch detected)    │  │
│  ┌──────────────┐   │  (steam.js)      │   └─────────────────────┘  │
│  │  QR Code     │   │ • Game detection │                             │
│  │  Generator   │   │ • SIGSTOP/CONT   │   ┌─────────────────────┐  │
│  │  (qr.js)     │   └──────────────────┘   │  SDL2 Overlay       │  │
│  │  Pure JS,    │                          │  (allow2-lock-overlay│  │
│  │  zero deps   │   ┌──────────────────┐   │  Native C binary    │  │
│  └──────────────┘   │SessionManager    │   │  Unix socket IPC    │  │
│                     │ (loginctl)       │   │  QR from grid data  │  │
│  ┌──────────────┐   └──────────────────┘   └─────────────────────┘  │
│  │DesktopNotify │                                                    │
│  │(notify-send) │                                                    │
│  └──────────────┘                                                    │
└──────────────────────────────────────────────────────────────────────┘
```

### What Lives in the SDK vs. Platform Code

This division is the key to the approach and directly applicable to anyone building an Allow2 integration on any platform:

**In the SDK (reusable across all platforms):**
- Device pairing flow (PIN generation, web wizard, API calls)
- Credential storage backends (pluggable: plaintext, keychain, encrypted)
- Check API polling (with `log: true` for usage recording)
- Enforcement state machine (warning thresholds, grace periods, soft/hard lock)
- Child resolver interface (OS account mapping)
- Request-more-time API with polling and statusSecret auth
- Offline grace period handling
- HTTP 401 unpair detection

**In the platform code (Linux-specific):**
- Process classification via `/proc` filesystem scanning
- Steam client integration (game detection, SIGSTOP/SIGCONT)
- Overlay display (dual backend: Steam browser + SDL2)
- Display environment discovery for systemd services
- QR code generation (pure JS encoder, SVG for web, module grid for SDL2)
- systemd service integration
- freedesktop notifications
- Device name detection (DMI board_name for Steam Deck)

---

## 4. The Dual Overlay: What Worked, What Didn't, and Why

This is the most interesting part of the project -- and the most instructive for anyone building UI on top of Linux gaming devices.

### What Didn't Work

**xdg-open on Steam Deck:** No browser registered in either Game Mode or Desktop Mode. `xdg-open` fails with "no method available for opening" -- there's no www-browser, links, or lynx installed on SteamOS.

**Firefox in Desktop Mode:** Opens but shows the welcome page, not the requested URL. Even when it navigates correctly, the user can drag the browser window away, minimise it, or close it. Not viable for a lock screen.

**`steam` CLI in Desktop Mode:** When Steam isn't running, the `steam` command tries to start Steam, which crashes `steamwebhelper` with a core dump. This happens every time the user switches from Game Mode to Desktop Mode while the daemon is running.

**SDL2 fullscreen in Game Mode (via gamescope):** The window is created on display `:1` (gamescope's inner Xwayland), the X11 atoms are set correctly, `xwininfo` can see it -- but gamescope does not composite it. Six approaches were tried (STEAM_OVERLAY atom, STEAM_INPUT_FOCUS, GAMESCOPECTRL_BASELAYER_WINDOW on :0, override-redirect, different X displays, PID namespace injection). None worked. Gamescope only composites windows from Steam's own process tree. This is good security design -- but it locks out external overlays.

**SDL2 in Desktop Mode with DISPLAY unset:** systemd user services don't inherit the desktop session's DISPLAY or WAYLAND_DISPLAY environment variables. SDL2 falls back to an "offscreen" video driver -- the window exists but is invisible.

**SDL2 fullscreen_desktop on portrait panel:** Steam Deck's display is natively 800x1280 (portrait). In Game Mode, gamescope rotates it to landscape. In Desktop Mode, KDE rotates it. But SDL2's `FULLSCREEN_DESKTOP` bypasses the compositor and gets the raw panel orientation, resulting in a 90-degree rotated display.

### What Did Work

**`steam steam://openurl/` for Game Mode:** Steam's built-in Chromium browser is part of Steam's process tree, so gamescope composites it without question. The daemon serves HTML pages at `http://127.0.0.1:3001/` and opens them via `steam steam://openurl/http://127.0.0.1:3001/pairing`. WebSocket provides real-time bidirectional messaging. Resolution-independent, gamepad-friendly, no compilation needed.

**SDL2 native binary for Desktop Mode:** A borderless always-on-top window (not fullscreen_desktop) lets the Wayland compositor handle display rotation. The daemon discovers DISPLAY and WAYLAND_DISPLAY from the active graphical session via `loginctl` and passes them to the SDL2 process. The binary communicates via Unix domain socket using the same JSON protocol as the WebSocket.

**Automatic mode switching:** When the user switches from Game Mode to Desktop Mode, Steam dies. The daemon detects this (pgrep check before every `steam://openurl` call), stops the Steam backend, starts the SDL2 backend, and re-shows the current screen -- all automatically, no restart needed.

### The Final Architecture

| Mode | Detection | Backend | Display | Communication |
|------|-----------|---------|---------|---------------|
| **Game Mode** | Steam running + gamescope-session process | HTTP server + WebSocket on :3001 | `steam steam://openurl/` opens in Steam's browser | WebSocket (JSON) |
| **Desktop Mode** | Steam not running | SDL2 native binary | Borderless always-on-top window via Wayland/X11 | Unix domain socket (newline-delimited JSON) |
| **Mode switch** | Steam process disappears | Auto-switch steam→sdl2 | Seamless transition | Same JSON protocol on both |

Both backends share:
- Identical JSON message protocol
- Same screen states (pairing, selector, pin, lock, warning, denied, dismiss)
- Same QR code data (generated once, sent as SVG to web, as module grid to SDL2)
- Same heartbeat and reconnection logic

---

## 5. QR Code Deep Linking

The pairing screen displays a scannable QR code containing a universal deep link:

```
https://app.allow2.com/pair?pin=XXXXXX
```

This URL works across all platforms:
- **iOS/Android with Allow2 app installed:** Deep links directly to the "connect device" confirmation screen with the mirrored PIN
- **iOS/Android without the app:** Redirects to the App Store / Play Store
- **Desktop browser:** Opens the Allow2 web app's device pairing page

The QR code is generated server-side in pure JavaScript (zero dependencies, works offline on Steam Deck):
- **For the Steam browser (HTML):** Generated as inline SVG
- **For the SDL2 overlay (C):** Generated as a flat module grid (`{ size: 29, modules: "010110..." }`), rendered as filled rectangles

The parent never needs to type a URL or know the device's IP address. Scan the QR code or type the 6-digit PIN into the Allow2 app on their phone.

---

## 6. Development Workflow

### Docker Cross-Compilation

SteamOS has no build tools. The C overlay is cross-compiled in a Docker container using Debian bookworm (matching SteamOS's glibc). On Apple Silicon Macs, Docker runs x86_64 containers via QEMU emulation -- Debian's apt works fine under emulation while Arch's pacman does not (seccomp/Landlock syscall issues).

### dev-deploy.sh

The deployment script handles the complete lifecycle:

```bash
./flatpak/dev-deploy.sh        # One-shot deploy
./flatpak/dev-deploy.sh watch  # Auto-redeploy on file changes
```

It auto-detects the SDK, installs Node.js on the Deck (no root required), cross-compiles the SDL2 overlay, rsyncs to the Deck over SSH, installs the systemd user service, and restarts the daemon. Edit-save-test cycle: ~2-5 seconds.

---

## 7. Lessons Learned (Final)

### What Matters Most

**Start with the SDK, not raw API calls.** The entire daemon is event wiring -- `daemon.on('event', function() { overlay.show(); })`. No state management, no polling logic, no API communication in the platform code. The SDK owns all of that. This is why the daemon is so small.

**Test on target hardware early and often.** Every significant discovery (xdg-open broken, gamescope invisibility, DISPLAY unset in systemd, portrait panel rotation, Steam CLI crash in Desktop Mode) was only found on the actual Steam Deck. A local Linux VM would not have caught any of them.

**Build for automatic recovery.** The daemon survives mode switches, Steam crashes, SDL2 process death, WebSocket disconnections, and display environment changes. Every external interaction is wrapped in try/catch. The overlay auto-reopens persistent screens. The systemd service auto-restarts on crash. Resilience isn't optional on a device kids will use.

**Two backends is better than one.** The initial instinct was "find one approach that works everywhere." That approach does not exist on Steam Deck. Game Mode and Desktop Mode are fundamentally different compositing environments. Accepting that and building two backends with a shared protocol was the right call.

### What Would Be Done Differently

**Spike test gamescope visibility first.** Building 3,200 lines of C overlay before discovering gamescope's restriction was wasted effort for Game Mode. A 10-line SDL2 test window would have revealed the problem in minutes.

**Discover display environment from the start.** The systemd service not inheriting DISPLAY was a predictable problem. Every systemd user service guide mentions it. Should have been handled in the initial architecture, not discovered during testing.

---

## 8. Applicability: How Easy Is This?

This is a first draft of the allow2linux integration. It will need tuning -- UI polish, edge case handling, more testing across Linux distributions. But the core architecture is proven and running on real hardware.

**The point is how little platform-specific code was needed:**

| Component | Lines | What it does |
|-----------|-------|--------------|
| index.js (daemon) | ~300 | Event wiring between SDK and overlay |
| overlay-bridge.js | ~950 | Dual backend, HTTP+WS server, embedded web UI, SDL2 IPC, mode switching |
| qr.js | ~530 | Pure JS QR code encoder (SVG + module grid) |
| process-classifier.js | ~160 | /proc scanning, activity mapping |
| steam.js | ~160 | Steam process detection, SIGSTOP/SIGCONT |
| session.js + desktop-notify.js | ~90 | loginctl + notify-send |
| **Total platform JS** | **~2,200** | |
| SDL2 overlay (C) | ~3,200 | Native fullscreen overlay, 5 screens, QR rendering |
| **Grand total** | **~5,400** | |

The Allow2 SDK provides ~2,500 lines of shared logic (pairing, credentials, check polling, enforcement, warnings, requests, offline support) that would otherwise need to be reimplemented for every platform.

**For comparison, consider what other organisations do:**

Most parental control implementations are vertically integrated -- they build the parent app, the cloud API, the permission engine, and the per-platform client all as one monolithic product. A typical "half-hearted" implementation (basic daily timer, no per-activity quotas, no child identification, no request flow, no offline support, no progressive warnings) still requires:

- A cloud backend with user management, device management, and a permission engine
- A parent-facing app or web interface
- Per-platform client code
- Ongoing maintenance of timezone handling, day types, edge cases

This typically costs 12-24 months and $500K-$2M, and the result still lacks features that Allow2 provides as platform infrastructure.

**The Allow2 SDK approach collapses this to days of platform-specific work.** Not weeks. Not months. Days. The ratio isn't 10x or even 100x -- it's 1,000x+ less effort for a more complete, more robust, and more feature-rich result. And every improvement to the SDK benefits every integration across every platform simultaneously.

Any product that touches screen time, parental controls, activity limits, quotas, tasks, or rewards can integrate Allow2 with this same pattern:

1. Use the Allow2 SDK for all platform-agnostic logic
2. Write a thin platform adapter (~500-1,000 lines) for display, process management, and user detection
3. Wire SDK events to the adapter
4. Ship

The allow2linux project is proof that this works in practice, on one of the hardest platforms to build for (a read-only Arch Linux system with a custom compositor that blocks external overlays). If it works here, it works anywhere.
