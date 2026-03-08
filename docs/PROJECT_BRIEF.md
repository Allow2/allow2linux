# allow2linux — Project Brief

**Version:** 6.0
**Date:** 8 March 2026
**Status:** Design

---

## 1. Project Overview

**allow2linux** is a standalone parental control daemon for Linux devices, backed by the Allow2 platform. It enforces daily time quotas, allowed hours, activity-specific limits, and offline-safe approval workflows — all manageable from the Allow2 parent app regardless of where the device is located.

This is **not** part of the Allow2Automate ecosystem. Allow2Automate requires a parent app on the local network. allow2linux talks directly to the Allow2 cloud via the Device API, working anywhere — home, school, travel, mobile data.

| | Allow2Automate | allow2linux |
|---|---|---|
| Communication | Agent pulls from parent app on LAN | Daemon calls Allow2 cloud directly |
| Network requirement | Same local network | Any internet connection |
| Parent device | Required on network | Not needed — parent uses phone app |
| Use case | Managed home network | Portable devices, shared consoles, any Linux device |

### 1.1 Packages

| Package | Type | Language | Purpose |
|---------|------|----------|---------|
| **`allow2linux`** | Installable product | Node.js + C | The thing families install (Flatpak, Snap, deb, etc.) |
| **`allow2`** (allow2node v2) | npm library | Node.js | The Allow2 Device SDK — evolved from existing `allow2` npm package |
| **`allow2-lock-overlay`** | Binary | C + SDL2 | Lock screen / child selector overlay for any Linux device |

Repository: `github.com/Allow2/allow2linux`
SDK repository: `github.com/Allow2/allow2node` (existing)

### 1.2 allow2node v2 — Clean Rewrite

The existing `allow2` npm package (v1.2.0, from `github.com/Allow2/allow2node`) is a bare API wrapper — ~110 lines, `pair()` and `check()` with callbacks, using the deprecated `request` library. It is being **rewritten from scratch** as v2.

No backward compatibility with v1. Clean break.

| Aspect | v2 |
|--------|-----|
| API | Event-driven `DeviceDaemon` class |
| Pairing | PIN/QR code (parent authenticates on their phone, never on device) |
| Child ID | ChildShield model (OS mapping + selector + PIN + push auth) |
| Offline | Cache + grace period + deny-by-default |
| Warnings | Configurable progressive warning scheduler |
| Requests | Request More Time flow |
| Updates | getUpdates polling |
| Credentials | Abstracted backends (plaintext, libsecret, extensible) |
| HTTP | Native `fetch` (Node 18+) |
| Style | EventEmitter + async/await |
| Module | ESM (`import`/`export`) |
| Node | 18+ required |

The npm name stays `allow2`. The repo stays `allow2node`.

### 1.3 VID/Token Configuration

Each Allow2 integration has a registered **Version ID (VID)** and **version token**. These identify the application (e.g., "allow2linux"), not the individual device instance. The per-device identity is a separate UUID generated at first run.

This follows the same pattern as the Brave browser integration (`kAllow2VersionId`, `kAllow2VersionToken` in `allow2_constants.h`).

| Source | Priority | Purpose |
|--------|----------|---------|
| Constructor option (`vid`, `token`) | Highest | Programmatic override |
| Environment variable (`ALLOW2_VID`, `ALLOW2_TOKEN`) | Medium | Testing / CI |
| Baked-in default | Lowest | Production — registered at developer.allow2.com |

The API base URL defaults to `https://api.allow2.com`. Override via `ALLOW2_API_URL` environment variable for testing.

No environment-specific URLs or tokens are baked into the codebase. All testing configuration is purely environment-driven.

### 1.4 Language & Runtime

| Component | Language | Rationale |
|-----------|----------|-----------|
| allow2 SDK | **Node.js** | Consistent with ecosystem. Existing npm presence. |
| Lock overlay + child selector | **C + SDL2** | Fullscreen Wayland/X11/framebuffer. Ships with most distros. |
| Pairing wizard | **Node.js + Express** | Local web UI. Part of SDK. |
| allow2linux glue | **Node.js** | Thin wrapper importing SDK. |

### 1.5 Distribution

| Format | Target | Auto-updates |
|--------|--------|-------------|
| **Flatpak** | Steam Deck, general Linux | Yes (Flathub) |
| **Snap** | Ubuntu, Ubuntu-derivatives | Yes (Snap Store) |
| **deb** | Debian, Ubuntu, Mint, Pop!_OS | Via apt repo |
| **rpm** | Fedora, RHEL, openSUSE | Via dnf/yum repo |
| **AppImage** | Any Linux (portable) | Manual |
| **AUR** | Arch, Manjaro, EndeavourOS | AUR helper |

All formats package the same thing: Node.js runtime + daemon + SDL2 overlay binary.

### 1.6 Flatpak-Ready Design

| Aspect | Design Decision |
|--------|-----------------|
| Base runtime | `org.freedesktop.Sdk` (includes SDL2) |
| Node.js | Bundled inside package |
| Credential storage | **Secret Service API** (`libsecret` / `org.freedesktop.secrets` portal) |
| D-Bus access | `org.freedesktop.login1` (session lock), `org.freedesktop.secrets` (credentials) |
| Network | Outbound only |
| App ID | `com.allow2.linux` |

---

## 2. Child Identification — Reconciled with Brave ChildShield

The child identification model is **already designed and implemented in the Brave browser** (`allow2_child_manager.cc`, `allow2_child_shield.cc`). allow2linux uses the identical model, with the SDL2 overlay as the rendering surface instead of Brave's native UI.

### 2.1 The Universal Pattern

```
Device boot / app launch / session resume
         │
         ▼
   Is OS account mapped to a child?
         │
    ┌────┴────┐
    YES       NO
    │         │
    ▼         ▼
 Implicit   Child Selector
 (no UI)    (gates entry)
    │         │
    │    ┌────┴────┐
    │    Child     Parent
    │    selected  selected
    │    │         │
    │    ▼         ▼
    │   PIN/Auth   Parent PIN
    │    │         │
    ▼    ▼         ▼
 Session active   Unrestricted
 (quotas enforced) (no Allow2 checks)
```

### 2.2 Three Device Modes (Auto-Detected)

| Mode | Scenario | Detection | Child identification |
|------|----------|-----------|---------------------|
| **Single child** | Child's own device | One child mapped during pairing | Automatic — no prompt |
| **Shared + profiles** | Family PC with per-child Linux logins | Multiple children + multiple Linux users mapped | Automatic — daemon reads `$USER` |
| **Shared + selector** | One login, kids take turns | Multiple children + unmapped/single Linux user | ChildShield selector gates entry |

**Critical rule:** Any OS account NOT mapped to a child triggers the child selector. No free passes for unmapped accounts. This closes the "create new Linux user to bypass" hole.

### 2.3 ChildShield Selector (from Brave, adapted for SDL2)

The selector appears as a fullscreen SDL2 overlay, controller-navigable:

```
┌───────────────────────────────────────┐
│                                       │
│           Who's playing?              │
│                                       │
│     ┌────────┐  ┌────────┐           │
│     │ avatar │  │ avatar │           │
│     │  Emma  │  │  Jack  │  Parent   │
│     │ #FF5733│  │ #3357FF│  🔓       │
│     └────────┘  └────────┘           │
│                                       │
│          Enter PIN: ____              │
│                                       │
└───────────────────────────────────────┘
```

**Triggers (same as Brave ChildShield):**
- Device boot / daemon start
- Previous child's time runs out ("Who's next?")
- Explicit "Switch child" (system tray / keyboard shortcut)
- Inactivity timeout (configurable, default 5 min)
- System sleep/wake
- Screen lock/unlock

### 2.4 Authentication (from Brave ChildManager)

The `allow2` SDK (v2) implements the same authentication model as Brave's `ChildManager`:

**Child data structure** (cached from server, identical to Brave):
```javascript
{
    id: 789,                    // Allow2 child ID
    name: "Emma",
    pinHash: "sha256...",       // SHA-256(pin + salt)
    pinSalt: "random...",
    avatarUrl: "https://...",   // Optional
    linkedAccountId: 0,         // If child has own Allow2 account
    color: "#FF5733",           // UI colour
    hasAccount: false           // Can use push auth if true
}
```

**Authentication methods** (in priority order):

| Method | When available | Flow |
|--------|---------------|------|
| **Push auth** | Child has `hasAccount: true` + network available | Notification sent to child's Allow2 app on their phone. Approve/deny. Fallback to PIN on timeout. |
| **PIN** (default) | Always | 4-6 digit PIN, validated locally against hash. |
| **Honour system** | Parent opts in (young kids) | Tap name, no verification. |
| **Parent-only** | Parent opts in (strict) | Only parent PIN can switch children. |

**PIN security** (from Brave implementation):
- SHA-256 hashing with per-child salt
- Constant-time comparison (prevents timing attacks)
- Rate limiting: **5 failed attempts → 5 minute lockout**
- PINs never logged or exposed
- Cached locally, refreshed from server on `getUpdates`

**Session management:**
- Session timeout after configurable idle period (default 5 min)
- `RecordActivity()` on user interaction updates last activity timestamp
- Session ends on: explicit logout, sleep/wake, screen lock, timeout, time exhausted

### 2.5 Verification Levels (Parent-Configurable)

| Level | Friction | Security | Best for |
|-------|----------|----------|----------|
| **Honour system** | Zero — tap name | Low | Young kids (under ~8) |
| **Child PIN** (default) | Low — 4-digit PIN | Medium | Most families |
| **Push auth** | Low — tap approve on phone | Medium-High | Kids with their own Allow2 account |
| **Parent PIN only** | Medium — parent must assign | High | Strict families |

### 2.6 Parent Guidance

| Their situation | What we tell them |
|----------------|-------------------|
| Child's own device | "Pair it to your child. Done." |
| Shared PC, kids have their own logins | "Each child logs into their own account. Allow2 tracks them automatically." |
| Shared device, one login | "When the device starts, each child taps their name and enters their PIN. Like choosing a profile on Netflix." |

---

## 3. Activity-Aware Enforcement

### 3.1 The Allow2 Activity Model

Allow2 activities are **independent quotas that stack**. The daemon checks ALL relevant activities and enforces based on what the child is actually doing.

Example check response:
```
Screen Time (8):  allowed=true,  remaining=45min
Gaming (3):       allowed=false, remaining=0
Internet (1):     allowed=true,  remaining=30min
Social (6):       allowed=true,  remaining=20min
```

| Child tries to... | Activities checked | Result |
|-------------------|-------------------|--------|
| Play a Steam game | Screen Time + Gaming | **BLOCKED** — Gaming is 0 |
| Open Firefox | Screen Time + Internet | Allowed — both have time |
| Watch Twitter in browser | Screen Time + Internet + Social | Allowed — all three have time |
| Watch a local movie | Screen Time only | Allowed — 45min left |
| Anything at all | Screen Time | **FULL DEVICE LOCK** when Screen Time hits 0 |

### 3.2 Enforcement Layers

```
┌───────────────────────────────────────────────────────┐
│  Screen Time (8) — master quota                        │
│  When 0: FULL DEVICE LOCK (overlay covers everything)  │
│                                                         │
│  ┌───────────────────────────────────────────────────┐ │
│  │  Gaming (3)                                        │ │
│  │  When 0: Kill game processes. Desktop still usable.│ │
│  ├───────────────────────────────────────────────────┤ │
│  │  Internet (1)                                      │ │
│  │  When 0: Kill browser processes. Games/offline     │ │
│  │  apps still usable.                                │ │
│  ├───────────────────────────────────────────────────┤ │
│  │  Social (6)                                        │ │
│  │  When 0: Kill social app processes. Other browsing │ │
│  │  still fine. In-browser social is Brave's job.     │ │
│  └───────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────┘
```

### 3.3 Process Classification

| Process | Activity | Detection |
|---------|----------|-----------|
| Steam game PIDs | Gaming (3) | Steam client IPC / `/proc` scan |
| Lutris, RetroArch, emulators | Gaming (3) | Known process name list |
| Firefox, Chrome, Brave, Chromium | Internet (1) | Known browser process names |
| Discord, Telegram, Signal | Social (6) | Known social app process names |
| Twitter/TikTok in browser | Social (6) | **Not daemon's job** — Brave handles URL-level |
| VLC, local movie | Screen Time (8) only | No additional activity matched |
| Desktop idle | Screen Time (8) only | Default |

Configurable + updatable process list. Ships with sensible defaults.

### 3.4 Interaction with Allow2 Brave

| Layer | Controls |
|-------|---------|
| **allow2linux** | Device-level: session lock, process blocking, activity logging |
| **Allow2 Brave** | URL-level: website filtering, in-browser social, per-site controls |

They complement each other. The daemon doesn't inspect browser content. Brave doesn't manage the session.

---

## 4. Warnings, Lock & Unlock

### 4.1 Progressive Warnings (Per-Activity)

| Time Remaining | SDK Event | Display |
|----------------|-----------|---------|
| 15 minutes | `warning('info', activity, 900)` | Desktop notification / Steam toast |
| 5 minutes | `warning('urgent', activity, 300)` | Notification + audible chime |
| 1 minute | `warning('final', activity, 60)` | "Save your game now!" |
| 30 seconds | `warning('countdown', activity, 30)` | Semi-transparent overlay countdown |
| 0 | `activity-blocked(activity)` or `soft-lock()` | Kill processes OR full lock |
| +5 min no approval | `hard-lock()` | Session terminated |

**Activity-specific vs device-wide:**

- Gaming runs out → kill game. Toast: "Gaming time is up. You can still browse or watch videos."
- Internet runs out → kill browsers. "Internet time is up."
- Screen Time runs out → **full device lock**. Overlay covers everything.

### 4.2 Full Device Lock (Screen Time Exhausted)

1. SDL2 overlay launches fullscreen, covering entire screen
2. All input captured by overlay
3. Shows: "Screen time is up for today" + usage summary + "Request More Time"
4. Parent approves → overlay dismisses
5. No approval after 5 min → `loginctl terminate-session`

### 4.3 Activity-Specific Block

When only one activity is blocked (e.g., Gaming):

1. `SIGTERM` game processes (10 second grace)
2. `SIGKILL` if still alive
3. Desktop notification explaining what happened
4. Prevent re-launch (watch for new game processes, kill immediately)
5. No overlay — desktop remains usable

### 4.4 Game Mode (gamescope)

In Game Mode there's no desktop to fall back to. When gaming time runs out:

1. `SIGSTOP` the game (preserve state in memory)
2. SDL2 overlay over gamescope
3. Options on overlay:
   - "Request More Time" (for gaming)
   - "Switch to Desktop Mode" (if Screen Time / Internet remains)
   - "Switch Child" (if shared device)
4. Parent approves → `SIGCONT`, dismiss overlay
5. Child switches to Desktop → exit gamescope
6. Screen Time also exhausted → full lock, only "Request More Time"

### 4.5 Request More Time

| Step | What Happens | Package |
|------|-------------|---------|
| 1. Child taps "Request More Time" | Duration picker (15min / 30min / 1hr) + message | Overlay |
| 2. Daemon sends request | `POST /api/request/createRequest` | allow2 SDK |
| 3. Parent gets push notification | Via Allow2 app | Server |
| 4. Daemon polls status | Using `statusSecret` | allow2 SDK |
| 5a. Approved | Overlay dismisses, activity resumes | SDK → Overlay |
| 5b. Denied | "Request denied", hard lock after 30sec | SDK → Overlay |
| 5c. Timeout (5 min) | Hard lock | SDK |

---

## 5. Offline & Adversarial Handling

| Scenario | Behaviour |
|----------|-----------|
| API succeeds | Update cache + `lastSuccessfulCheck` |
| API fails (network) | Use cached result. Start grace timer. |
| Grace < 5 min | Allow continued use. "Checking..." indicator. |
| Grace > 5 min | Deny-by-default. Full device lock. |
| WiFi restored | Immediate re-check. Unlock if permitted. |
| Daemon killed | systemd `Restart=always`. Immediately check on restart. |
| New unmapped Linux user | Child selector required (no free pass). |

---

## 6. Architecture

### 6.1 Package Relationship

```
┌──────────────────────────────────────────────────────┐
│  allow2linux (installable product — thin glue)        │
│  Process classification, Steam awareness,             │
│  desktop notifications, overlay bridge, systemd       │
├──────────────────────────────────────────────────────┤
│  allow2 v2 (npm — allow2node)                         │
│  DeviceDaemon, pairing (PIN/QR), check loop,          │
│  ChildShield (child ID + PIN + session), warnings,    │
│  credentials, offline, requests, getUpdates           │
├──────────────────────────────────────────────────────┤
│  allow2-lock-overlay (C + SDL2 binary)                │
│  Lock screen, child selector, countdown,              │
│  request UI, controller + keyboard input              │
└──────────────────────────────────────────────────────┘
```

### 6.2 allow2 v2 SDK — Event-Driven API

```javascript
import { DeviceDaemon } from 'allow2';

const daemon = new DeviceDaemon({
    deviceToken: 'your-device-token',
    deviceName: 'Living Room PC',
    activities: [8, 3, 1, 6],
    checkInterval: 60,
    credentialBackend: 'libsecret',
    childResolver: 'linux-user',  // or 'selector'
    gracePeriod: 5 * 60,
    warnings: [
        { remaining: 15 * 60, level: 'info' },
        { remaining: 5 * 60,  level: 'urgent' },
        { remaining: 60,      level: 'final' },
        { remaining: 30,      level: 'countdown' },
    ],
});

// Child identification events
daemon.on('child-select-required', (children) => { /* show selector overlay */ });
daemon.on('child-selected', (childId, childName) => { /* dismiss selector */ });
daemon.on('child-pin-failed', (attempts, maxAttempts) => { /* show error */ });
daemon.on('child-locked-out', (lockoutSeconds) => { /* show lockout timer */ });
daemon.on('session-timeout', () => { /* show selector again */ });

// Enforcement events
daemon.on('warning', (level, activity, remaining) => { /* notification */ });
daemon.on('activity-blocked', (activity, reason) => { /* kill processes */ });
daemon.on('soft-lock', (reason, childId) => { /* full overlay */ });
daemon.on('unlock', (childId) => { /* dismiss overlay */ });
daemon.on('hard-lock', (reason, childId) => { /* terminate session */ });

// Request events
daemon.on('request-approved', (childId, extension) => { /* resume */ });
daemon.on('request-denied', (childId) => { /* show denied */ });

// Offline events
daemon.on('offline-grace', (elapsed) => { /* show checking indicator */ });
daemon.on('offline-deny', () => { /* deny-by-default lock */ });

daemon.start();
```

**Clean rewrite.** No v1 API surface. `DeviceDaemon` is the only entry point.

### 6.3 allow2 v2 SDK — Reused By

| Integration | Notes |
|-------------|-------|
| **allow2linux** | This project |
| **allow2automate-ssh** | Existing — refactor to use SDK |
| **allow2automate-nintendo-switch** | Existing — refactor to use SDK |
| **Future Device API integrations** | Any community project |
| **Brave (reference)** | C++ implementation, but same model — SDK is the Node.js equivalent |

### 6.4 allow2-lock-overlay — IPC Protocol

Unix domain socket, JSON messages:

```
Daemon → Overlay:
  { "type": "child-select", "children": [
      { "id": 789, "name": "Emma", "color": "#FF5733", "avatarUrl": "..." },
      { "id": 790, "name": "Jack", "color": "#3357FF" }
    ], "verification": "pin", "showParent": true }
  { "type": "activity-blocked", "activity": "Gaming",
    "message": "Gaming time is up. You can still browse or watch videos." }
  { "type": "device-lock", "reason": "Screen time is up for today",
    "summary": "You used 2 hours of gaming and 30 minutes of browsing." }
  { "type": "countdown", "seconds": 30 }
  { "type": "request-status", "status": "pending" | "approved" | "denied" }
  { "type": "pin-error", "attempts": 3, "max": 5 }
  { "type": "lockout", "seconds": 300 }
  { "type": "dismiss" }

Overlay → Daemon:
  { "type": "child-selected", "childId": 789, "pin": "1234" }
  { "type": "parent-selected", "pin": "5678" }
  { "type": "request-time", "duration": 1800, "message": "Can I finish?" }
  { "type": "switch-child" }
  { "type": "switch-desktop" }
```

### 6.5 allow2linux — Platform Glue

| Feature | Implementation |
|---------|---------------|
| Process classification | Scan `/proc`, map to activities via configurable list |
| Steam awareness | Detect Steam, game PIDs, gamescope session |
| Steam notifications | `steam://` protocol / `steamclient.so` IPC |
| Desktop notifications | D-Bus `org.freedesktop.Notifications` |
| Session lock | `loginctl lock-session` / `loginctl terminate-session` |
| Overlay launch | Spawn `allow2-lock-overlay` with appropriate display flags |
| Process blocking | SIGSTOP/SIGCONT/SIGTERM/SIGKILL + re-launch prevention |
| systemd service | `allow2linux.service` (user) |

---

## 7. Repository Structure

```
allow2linux/                              # github.com/Allow2/allow2linux
├── packages/
│   ├── allow2-lock-overlay/              # C + SDL2 binary
│   │   ├── src/
│   │   │   ├── main.c                    # Entry + SDL2 init
│   │   │   ├── ipc.c                     # Unix domain socket
│   │   │   ├── render.c                  # UI rendering
│   │   │   ├── child_select.c            # Child selector screen
│   │   │   ├── lock_screen.c             # Lock / blocked screen
│   │   │   ├── request.c                 # Request More Time UI
│   │   │   ├── input.c                   # Controller + keyboard
│   │   │   └── assets/                   # Fonts, icons
│   │   ├── Makefile
│   │   └── tests/
│   │
│   └── allow2linux/                      # Node.js daemon (the product)
│       ├── package.json
│       ├── src/
│       │   ├── index.js                  # Entry — wires allow2 SDK + overlay
│       │   ├── process-classifier.js     # Running processes → activities
│       │   ├── steam.js                  # Steam detection + notifications
│       │   ├── desktop-notify.js         # freedesktop notifications
│       │   ├── session.js                # loginctl session management
│       │   └── overlay-bridge.js         # Launch/communicate with overlay
│       ├── config/
│       │   └── processes.json            # Default process → activity mappings
│       ├── systemd/
│       │   └── allow2linux.service
│       └── scripts/
│           └── install.sh                # Dev-only bash installer
│
├── flatpak/
│   ├── com.allow2.linux.yml
│   └── com.allow2.linux.desktop
├── snap/
│   └── snapcraft.yaml
└── docs/
    └── PROJECT_BRIEF.md                  # This document

allow2node/                               # github.com/Allow2/allow2node (existing repo)
├── package.json                          # npm: allow2 (v2.0.0), "type": "module"
├── src/
│   ├── index.js                          # Main export: DeviceDaemon
│   ├── daemon.js                         # DeviceDaemon class (event emitter)
│   ├── api.js                            # Allow2 API client (fetch-based)
│   ├── checker.js                        # Check loop + per-activity enforcement
│   ├── warnings.js                       # Warning scheduler
│   ├── child-shield.js                   # ChildShield (selection, PIN, session)
│   ├── pairing.js                        # PIN/QR wizard + pairDevice
│   ├── credentials/
│   │   ├── index.js                      # Backend interface
│   │   ├── plaintext.js                  # Dev: ~/.allow2/credentials.json
│   │   └── libsecret.js                  # Release: org.freedesktop.secrets
│   ├── child-resolver/
│   │   ├── linux-user.js                 # $USER → childId
│   │   └── selector.js                   # Interactive picker (emits events)
│   ├── offline.js                        # Cache + grace period
│   ├── request.js                        # Request More Time
│   ├── updates.js                        # getUpdates polling
│   └── wizard/
│       ├── server.js                     # Express setup wizard
│       └── public/                       # Static HTML/CSS/JS
└── tests/
```

---

## 8. Implementation Order

| Phase | Where | Deliverable |
|-------|-------|-------------|
| 1 | **allow2node** | v2 scaffold: DeviceDaemon class, Allow2Api (fetch-based, VID/token) |
| 2 | **allow2node** | Pairing wizard (PIN/QR, no credentials on device) |
| 3 | **allow2node** | ChildShield: child data, PIN hashing (SHA-256+salt), rate limiting, session timeout |
| 4 | **allow2node** | Check loop + per-activity enforcement + warning scheduler |
| 5 | **allow2node** | Offline handling + Request More Time |
| 6 | **allow2node** | Credential backends (plaintext + libsecret) |
| 7 | **allow2linux** | Process classifier + basic enforcement (kill blocked processes) |
| 8 | **allow2-lock-overlay** | Child selector screen (SDL2, controller-navigable) |
| 9 | **allow2-lock-overlay** | Lock screen + countdown + Request More Time UI |
| 10 | **allow2linux** | Wire everything: overlay bridge + Steam + desktop notifications |
| 11 | **allow2linux** | Flatpak + Snap packaging |
| 12 | All | Publish: npm (allow2 v2), Flathub, Snap Store |

Phases 1-6 (SDK) are independently useful for any Device API integration. The allow2linux-specific work starts at phase 7.

---

## 9. Key API Endpoints (Reference)

Abstracted by the `allow2` SDK. Listed for reference.

| Endpoint | Purpose | When |
|----------|---------|------|
| `POST /api/pairDevice` | One-time pairing. Returns `userId`, `pairToken`, children list. | Setup wizard |
| `POST /api/check` | Enforcement. Pass `childId` + activities. Returns per-activity `allowed` + remaining. Every 60s with `log: true`. | Continuous |
| `GET /api/getUpdates` | Extensions, day type changes, quota updates, bans, child data refresh. `timestampMillis` for delta. | Continuous |
| `POST /api/request/createRequest` | Child requests more time. Returns `statusSecret`. | Lock overlay |
| `GET /api/request/:id/status` | Poll approval status. | Lock overlay |
| `POST /api/logUsage` | Reconcile usage after offline period. | After reconnect |

Activity IDs (v1 hardcoded): Screen Time (8), Gaming (3), Internet (1), Social (6).

---

## 10. SteamOS-Specific Notes

These apply when allow2linux is installed on a Steam Deck. The same package handles all Linux devices; these are auto-detected behaviours.

| Consideration | Approach |
|---------------|----------|
| Immutable root | All in home dir or Flatpak sandbox |
| Game Mode (gamescope) | SDL2 overlay renders over gamescope; SIGSTOP/SIGCONT for game preservation |
| Desktop Mode | Standard loginctl + desktop notifications |
| Steam notifications | Via `steam://` protocol when Steam detected |
| "Switch to Desktop" | Offered when gaming blocked but Screen Time remains |

---

## 11. Parked (Future Work)

| Item | Notes |
|------|-------|
| Configurable activity IDs | Parent selects which activities to track |
| Custom process mappings | Parent adds app → activity mappings via Allow2 app |
| Tamper detection | Detect service disabled, notify parent |
| macOS / Windows credential backends | Keychain, Windows Credential Manager |
| Push auth for child verification | Child approves on their phone instead of PIN |
| Profile identity binding | OS profile switch triggers child switch (from Brave design) |
| Browser extension | Lightweight Allow2 extension for non-Brave browsers |
| Auto-updater (non-Flatpak) | For deb/rpm/AppImage installs without store updates |
| Additional distro packages | deb, rpm, AUR |

---

## 12. Resources

| Resource | Link |
|----------|------|
| Allow2 Developer Portal | https://developer.allow2.com |
| Allow2 GitHub | https://github.com/Allow2 |
| allow2node (SDK repo) | https://github.com/Allow2/allow2node |
| allow2 on npm | https://www.npmjs.com/package/allow2 |
| Brave ChildShield (reference) | `brave/src/brave/components/allow2/browser/allow2_child_shield.cc` |
| Brave ChildManager (reference) | `brave/src/brave/components/allow2/browser/allow2_child_manager.cc` |
| SDL2 Documentation | https://wiki.libsdl.org |
| Flatpak Documentation | https://docs.flatpak.org |
| Snapcraft Documentation | https://snapcraft.io/docs |
