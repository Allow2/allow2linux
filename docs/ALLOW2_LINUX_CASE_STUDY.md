# Allow2 Linux Integration Case Study

## Building Parental Controls for Steam Deck and Linux Desktops

---

## 1. Overview

**allow2linux** is an Allow2 parental controls daemon for Linux. It implements the full Allow2 device lifecycle -- pairing, child identification, permission enforcement, warnings, lock screen, and request-more-time -- as a background service that runs on any Linux device.

**Primary targets:**
- Valve Steam Deck (SteamOS 3.x, both Game Mode and Desktop Mode)
- Standard Linux desktops (Ubuntu, Fedora, Arch, etc.)
- Other Linux handhelds (ASUS ROG Ally, Lenovo Legion Go)
- Raspberry Pi and single-board computers

**Architecture summary:** A Node.js daemon uses the Allow2 SDK for all platform-agnostic logic (API communication, credential storage, enforcement state machine). Platform-specific code handles process classification, Steam integration, and overlay display. The overlay uses two display strategies: a web-based approach for Steam Deck Game Mode (via `steam://openurl`) and an SDL2 native binary for Desktop Mode.

**Repository:** [github.com/Allow2/allow2linux](https://github.com/Allow2/allow2linux)

---

## 2. Architecture

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
│  │ • DeviceDaemon│──│  • pairing-required──│ • HTTP server :3001 │  │
│  │ • Credentials│  │  • child-select   │   │ • WebSocket IPC     │  │
│  │ • Check API  │  │  • warning        │   │ • Screen rendering  │  │
│  │ • Enforcement│  │  • activity-blocked│  │ • Auto-reopen       │  │
│  └──────────────┘  │  • soft-lock      │   └────────┬────────────┘  │
│                     │  • hard-lock      │            │               │
│  ┌──────────────┐   │  • unpaired       │   ┌────────┴────────────┐  │
│  │ProcessClassif│   └──────────────────┘   │  Display Strategy   │  │
│  │ (/proc scan) │                          │                     │  │
│  └──────────────┘   ┌──────────────────┐   │ Game Mode:          │  │
│                     │  SteamMonitor    │   │  steam://openurl    │  │
│  ┌──────────────┐   │  (steam.js)      │   │  → Steam browser    │  │
│  │SessionManager│   │ • Game detection │   │                     │  │
│  │ (loginctl)   │   │ • SIGSTOP/CONT   │   │ Desktop Mode:       │  │
│  └──────────────┘   └──────────────────┘   │  xdg-open           │  │
│                                             │  → System browser   │  │
│  ┌──────────────┐                          └─────────────────────┘  │
│  │DesktopNotify │                                                    │
│  │(notify-send) │   ┌──────────────────────────────────────────┐    │
│  └──────────────┘   │  SDL2 Overlay (packages/allow2-lock-overlay)│  │
│                     │  Native C binary for Desktop Mode          │  │
│                     │  17 source files, ~3200 lines              │  │
│                     └──────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────┘
```

### Component Breakdown

| Component | Location | Lines | Role |
|-----------|----------|-------|------|
| **Allow2 SDK** | External npm package | ~2000 | API communication, pairing, credentials, enforcement state machine |
| **index.js** | `packages/allow2linux/src/` | 332 | Event wiring between SDK and overlay |
| **overlay-bridge.js** | `packages/allow2linux/src/` | 491 | HTTP + WebSocket server, screen rendering, display strategy |
| **process-classifier.js** | `packages/allow2linux/src/` | 159 | Maps running processes to Allow2 activity IDs via /proc |
| **steam.js** | `packages/allow2linux/src/` | 157 | Steam client detection, game process management |
| **session.js** | `packages/allow2linux/src/` | 48 | loginctl session management |
| **desktop-notify.js** | `packages/allow2linux/src/` | 43 | freedesktop notification delivery |
| **SDL2 overlay** | `packages/allow2-lock-overlay/src/` | 3168 | Native fullscreen overlay for Desktop Mode |
| **dev-deploy.sh** | `flatpak/` | 308 | Development deployment pipeline |

### What Lives in the SDK vs. Platform Code

The division is deliberate and important for anyone building an Allow2 integration on any platform:

**In the SDK (reusable across all platforms):**
- Device pairing flow (PIN generation, web wizard, API calls)
- Credential storage backends (pluggable: plaintext, keychain, encrypted)
- Check API polling (with `log: true` for usage recording)
- Enforcement state machine (warning thresholds, grace periods, soft/hard lock)
- Child resolver interface (OS account mapping)
- Request-more-time API
- Offline grace period handling
- HTTP 401 unpair detection

**In the platform code (Linux-specific):**
- Process classification via `/proc` filesystem scanning
- Steam client integration (game detection, SIGSTOP/SIGCONT)
- Overlay display (gamescope workaround, desktop browser, SDL2)
- systemd service integration
- freedesktop notifications
- Device name detection (DMI board_name for Steam Deck)
- Session management via loginctl

---

## 3. Device Lifecycle Implementation

Every Allow2 integration follows the same lifecycle. Here is how each stage maps to code in allow2linux.

### Stage 1: Pairing

When the daemon starts with no stored credentials, the SDK emits `pairing-required` with a 6-digit PIN and a web wizard URL.

```javascript
// packages/allow2linux/src/index.js, lines 97-129
daemon.on('pairing-required', function (info) {
    console.log('Device not paired. PIN: ' + info.pin);
    console.log('Pairing wizard running at: ' + info.url);

    if (overlay.isAvailable()) {
        overlay.showPairingScreen({
            pin: info.pin,
            message: 'Open the Allow2 app on your phone and enter this PIN',
        });
    } else {
        try {
            execSync('xdg-open ' + info.url + ' 2>/dev/null &');
        } catch (_err) { }
        notifier.notify('Allow2 setup: enter PIN ' + info.pin, 'info');
    }
});
```

The parent enters the PIN in the Allow2 app on their phone. The SDK handles the API handshake. On success, the SDK emits `paired` with the userId and children list, and stores credentials via the configured backend.

**Key design decision:** The parent never enters their Allow2 credentials on the child's device. Only a 6-digit PIN is exchanged. This is a core Allow2 security principle.

### Stage 2: Child Identification

After pairing, the SDK needs to know which child is using the device. Two strategies are supported:

1. **OS account mapping** (preferred for PCs): The SDK's `resolveLinuxUser` maps the current Linux username to a child entity in the controller's Allow2 account. If the mapping exists, no interactive selection is needed.

2. **Interactive selector**: When no mapping exists, the SDK emits `child-select-required` and the overlay shows a child picker with avatars. The child taps their name, then enters their PIN.

```javascript
// packages/allow2linux/src/index.js, lines 143-158
daemon.on('child-select-required', function (data) {
    var children = data.children || [];
    if (children.length === 0) {
        // No children configured = parent mode (unrestricted)
        return;
    }
    overlay.showChildSelector(children);
});
```

If no OS user is mapped to any child (e.g., the parent is logged in), the daemon operates in parent mode with no restrictions.

### Stage 3: Enforcement (Check API Polling)

Once a child is identified, the SDK polls the Allow2 Check API every 60 seconds with `log: true` to both check permissions and record usage. The response includes remaining time, daily limits, time blocks, day type, and bans for each activity.

The process classifier scans `/proc` to determine which activities are currently active:

```javascript
// packages/allow2linux/src/process-classifier.js, lines 101-121
async getActiveActivities() {
    var result = new Map();
    var procs = await this._scanProc();

    for (var proc of procs) {
        var activity = this.mappings[proc.name];
        if (activity) {
            if (!result.has(activity)) {
                result.set(activity, []);
            }
            result.get(activity).push(proc.pid);
        }
    }
    // Screen Time is always active if child is logged in
    if (!result.has(ACTIVITIES.SCREEN_TIME)) {
        result.set(ACTIVITIES.SCREEN_TIME, []);
    }
    return result;
}
```

Process-to-activity mappings are configurable via `config/processes.json` and include sensible defaults for gaming (Steam, Lutris, RetroArch, Wine/Proton), internet (Firefox, Chrome, Brave), and social (Discord, Telegram, Signal).

### Stage 4: Warnings

The SDK emits `warning` events at progressive thresholds (configurable, but typically 15 min, 5 min, 1 min, 30 sec). Each warning includes a `level` field: `info`, `urgent`, `final`, or `countdown`.

```javascript
// packages/allow2linux/src/index.js, lines 166-185
daemon.on('warning', function (data) {
    var minutes = Math.ceil(data.remaining / 60);
    var activityName = classifier.getActivityName(data.activity);

    overlay.showWarning({
        activity: activityName,
        activityId: data.activity,
        remaining: data.remaining,
        level: data.level,
    });

    if (steam.isRunning()) {
        steam.notify(msg, data.level);
    } else {
        notifier.notify(msg, data.level);
    }
});
```

The warning overlay is a semi-transparent bar at the top of the screen. In Game Mode, it opens as a Steam browser page. At `urgent` and `final` levels, a "Request More Time" button appears.

### Stage 5: Lock and Enforcement

When time runs out, the SDK emits either `soft-lock` (pause the activity) or `activity-blocked` (terminate processes). For gaming on Steam Deck, soft-lock uses SIGSTOP to freeze the game process, and unlock uses SIGCONT to resume it -- the game is paused, not killed.

```javascript
// packages/allow2linux/src/index.js, lines 209-232
daemon.on('soft-lock', async function (data) {
    var gamePid = steam.getActiveGamePid();
    if (gamePid) {
        try { process.kill(gamePid, 'SIGSTOP'); } catch (_e) { }
    }
    await overlay.showLockScreen({ reason: data.reason, childId: data.childId });
});

daemon.on('unlock', async function () {
    var gamePid = steam.getStoppedGamePid();
    if (gamePid) {
        try { process.kill(gamePid, 'SIGCONT'); } catch (_e) { }
    }
    await overlay.dismiss();
});
```

The lock screen offers two actions: "Request More Time" and "Switch Child."

### Stage 6: Request More Time

From any lock screen or urgent warning, the child can request more time. The request goes to the Allow2 API, which notifies the parent. The daemon polls for approval:

```javascript
// packages/allow2linux/src/index.js, lines 61-89
overlay.on('request-more-time', function (data) {
    daemon.requestMoreTime({
        duration: data.duration,
        activity: data.activityId,
    }).then(function (result) {
        overlay.showRequestStatus('pending');
        var pollCount = 0;
        var pollTimer = setInterval(function () {
            pollCount++;
            if (pollCount > 120) { // 10 min timeout
                clearInterval(pollTimer);
                overlay.showRequestStatus('denied');
                return;
            }
            daemon.pollRequestStatus(result.requestId, result.statusSecret)
                .then(function (status) {
                    if (status && status.status === 'approved') {
                        clearInterval(pollTimer);
                        overlay.showRequestStatus('approved');
                    } else if (status && status.status === 'denied') {
                        clearInterval(pollTimer);
                        overlay.showRequestStatus('denied');
                    }
                }).catch(function () { /* retry */ });
        }, 5000);
    });
});
```

The `statusSecret` is returned on request creation and used for polling authentication -- the child never sees or needs the parent's credentials.

---

## 4. Platform-Specific Considerations

### Steam Deck Detection

The Steam Deck is detected via DMI board name, not hostname or OS version:

```javascript
// packages/allow2linux/src/index.js, lines 314-331
function _getDeviceName() {
    var isSteamDeck = false;
    try {
        var board = readFileSync('/sys/devices/virtual/dmi/id/board_name', 'utf8').trim();
        if (board === 'Jupiter' || board === 'Galileo') {
            isSteamDeck = true;
        }
    } catch (_e) { }

    if (isSteamDeck) {
        if (hostname && hostname !== 'steamdeck' && hostname !== 'localhost') {
            return hostname + ' (Steam Deck)';
        }
        return 'Steam Deck';
    }
    return hostname || 'Linux PC';
}
```

`Jupiter` is the LCD Steam Deck, `Galileo` is the OLED model.

### SteamOS Quirks

| Issue | Cause | Solution |
|-------|-------|----------|
| `hostname` command not found | SteamOS minimal install | Read `/etc/hostname` as fallback |
| No build tools (gcc, make) | Read-only filesystem | Docker cross-compilation from dev machine |
| No development headers | Immutable OS image | Debian bookworm Docker image (matching glibc) |
| Missing libSDL2_image | SteamOS ships SDL2 but not SDL2_image | Removed SDL2_image dependency, use SDL2_ttf only |
| Node.js not installed | Not in SteamOS base | dev-deploy.sh downloads to ~/node (no root) |
| gamescope ignores external windows | Security design | `steam://openurl` workaround (see Section 5) |

### Gamescope Detection

Game Mode is detected by checking if gamescope is running:

```javascript
// packages/allow2linux/src/index.js, lines 292-300
function _isGameMode() {
    try {
        execSync('pgrep -x gamescope', { encoding: 'utf8' });
        return true;
    } catch (_err) {
        return false;
    }
}
```

### Desktop Linux Differences

On standard Linux desktops (GNOME, KDE, XFCE, etc.):
- Overlay uses `xdg-open` to open pages in the default browser, or the SDL2 native overlay
- Warnings use freedesktop `notify-send` via D-Bus
- Session management uses `loginctl` (systemd-logind)
- No gamescope workarounds needed -- normal window management applies

---

## 5. The Overlay Problem: Gamescope and steam://openurl

This section documents the most technically interesting challenge in the project and the solution that emerged. It is directly relevant to anyone building an overlay or notification system for Steam Deck Game Mode.

### What Gamescope Is

Gamescope is Valve's micro-compositor for Steam Deck. In Game Mode, it runs as the sole Wayland compositor. Inside it, an Xwayland server (display `:1`) hosts Steam and all games. Gamescope also exposes a control display (`:0`) for setting properties like resolution scaling and frame rate limits.

Gamescope's compositing pipeline is deliberately restrictive: it composites windows from Steam's process tree and nothing else. This is a security feature -- it prevents games from overlaying arbitrary content on each other, and it prevents malware from displaying fake UI.

### Why External Windows Are Invisible

When the allow2linux daemon spawns an SDL2 overlay process, that process creates a window on display `:1` (the Xwayland server inside gamescope). The window exists in X11. It has the correct properties. But gamescope does not include it in the composite output because the process is not part of Steam's process tree.

The window is literally there -- `xwininfo` can see it, `xdotool` can interact with it -- but it is not rendered to the screen.

### What Was Tried

Six approaches were attempted, all implemented in the SDL2 overlay's `set_gamescope_overlay()` function:

**1. STEAM_OVERLAY atom**

```c
Atom steam_overlay = XInternAtom(dpy, "STEAM_OVERLAY", False);
XChangeProperty(dpy, xwin, steam_overlay, XA_CARDINAL, 32,
                PropModeReplace, (unsigned char *)&val, 1);
```

Gamescope checks this atom, but only trusts it from Steam's own overlay process (SteamOverlayHelper). External processes setting it are ignored.

**2. STEAM_INPUT_FOCUS atom**

```c
Atom steam_input = XInternAtom(dpy, "STEAM_INPUT_FOCUS", False);
XChangeProperty(dpy, xwin, steam_input, XA_CARDINAL, 32,
                PropModeReplace, (unsigned char *)&val, 1);
```

This controls which window receives input in the gamescope model. Setting it alone has no effect on visibility.

**3. GAMESCOPECTRL_BASELAYER_WINDOW on :0**

```c
Display *ctrl = XOpenDisplay(":0");
Window root = DefaultRootWindow(ctrl);
Atom overlay_window = XInternAtom(ctrl, "GAMESCOPECTRL_BASELAYER_WINDOW", False);
uint32_t wid = (uint32_t)xwin;
XChangeProperty(ctrl, root, overlay_window, XA_CARDINAL, 32,
                PropModeReplace, (unsigned char *)&wid, 1);
```

This is the most promising approach in theory -- writing to the gamescope control display to inject a window into the composite layer. In practice, gamescope validates the source of these properties and does not honor them from arbitrary processes.

**4. Override-redirect**

```c
XSetWindowAttributes attrs;
attrs.override_redirect = True;
XChangeWindowAttributes(dpy, xwin, CWOverrideRedirect, &attrs);
```

Override-redirect windows bypass the X11 window manager. But gamescope's compositing decision is made at a layer below WM management -- it is about whether to include the window at all, not how to decorate it.

**5. Different X displays**

Attempted creating the window on `:0` (gamescope control display) instead of `:1` (inner Xwayland). The control display is not a compositing target.

**6. PID namespace injection**

Using `nsenter` to run the overlay inside Steam's cgroup, hoping gamescope would identify it as a Steam child process. This was fragile and unreliable -- gamescope appears to track the process tree at a level that is not easily spoofed.

### The Solution: steam://openurl

Steam supports a URL scheme for controlling the client. The command:

```bash
steam -ifrunning steam://openurl/http://localhost:3001/pairing
```

tells the already-running Steam client to open a URL in its built-in Chromium browser. This browser is part of Steam itself, so gamescope composites it without question. It renders on top of games. It accepts gamepad input. It works.

### The Web-Based Overlay Architecture

The overlay bridge (`packages/allow2linux/src/overlay-bridge.js`) implements:

1. **HTTP server** on `127.0.0.1:3001` that serves screen pages:
   - `/pairing` -- shows PIN and QR code
   - `/selector` -- child picker with avatars
   - `/pin` -- PIN entry with number pad
   - `/lock` -- lock screen with "Request More Time"
   - `/warning` -- semi-transparent warning bar

2. **WebSocket server** on the same port for real-time bidirectional messaging

3. **Display strategy** that tries Steam first, falls back to system browser:

```javascript
// packages/allow2linux/src/overlay-bridge.js, lines 204-213
_openUrl(url) {
    execFile('steam', ['-ifrunning', 'steam://openurl/' + url], {
        timeout: 3000,
    }, function (err) {
        if (err) {
            execFile('xdg-open', [url], { timeout: 5000 }, function () {});
        }
    });
}
```

4. **Auto-reopen for persistent screens**: When the WebSocket disconnects (user closed the browser tab or navigated away), the daemon re-opens the current screen after 1 second:

```javascript
// packages/allow2linux/src/overlay-bridge.js, lines 59-71
ws.on('close', function () {
    if (self._ws === ws) {
        self._ws = null;
        if (self._currentScreen && self._currentScreen !== 'warning') {
            console.log('Overlay closed, re-opening ' + self._currentScreen);
            setTimeout(function () {
                if (self._currentScreen) {
                    var url = 'http://127.0.0.1:' + OVERLAY_PORT + '/' + self._currentScreen;
                    self._openUrl(url);
                }
            }, 1000);
        }
    }
});
```

This makes the lock screen inescapable -- pressing Back or closing the tab just reopens it. The warning bar does not auto-reopen (it is transient by nature).

5. **State synchronization**: When the WebSocket connects, the server immediately sends the current screen state. This handles the case where the daemon sets state before the browser page has loaded:

```javascript
// packages/allow2linux/src/overlay-bridge.js, lines 76-78
if (self._currentScreen) {
    ws.send(JSON.stringify(self._screenData));
}
```

### Why This Works Well

The `steam://openurl` approach has several advantages beyond just "gamescope composites it":

- **No special permissions needed** -- the Steam client is already running, the command just talks to it
- **Gamepad input works** -- Steam's browser handles gamepad-to-mouse translation
- **Resolution-independent** -- HTML/CSS scales to any resolution (1280x800, 1920x1080, etc.)
- **Fast iteration** -- change HTML, reload page, see results immediately
- **No native compilation** -- the web UI runs in Steam's Chromium, no cross-compilation needed
- **Same protocol** -- the JSON/WebSocket protocol is identical to what the SDL2 overlay used

---

## 6. Development Workflow

### dev-deploy.sh

The deployment script (`flatpak/dev-deploy.sh`) handles the full lifecycle:

```bash
# One-shot deploy
./flatpak/dev-deploy.sh

# Continuous deploy on file changes
./flatpak/dev-deploy.sh watch

# Custom Steam Deck IP
DECK_HOST=deck@192.168.100.2 ./flatpak/dev-deploy.sh
```

The script:
1. Auto-detects the Allow2 SDK path (checks `../sdk/node` and `../../sdk/node`, or uses `SDK_ROOT` env var)
2. Checks for Node.js on the Deck, installs to `~/node` if missing (no root required)
3. Downloads Inter font files for the SDL2 overlay (one-time, cached locally)
4. Cross-compiles the C overlay via Docker if source has changed
5. Rsyncs SDK and daemon to `~/allow2/` on the Deck
6. Deploys `.env` to `~/.allow2/.env` with secure permissions
7. Installs systemd user service
8. Restarts the daemon

### Docker Cross-Compilation

SteamOS has no build tools. The C overlay is cross-compiled in a Docker container:

```bash
# Dockerfile (embedded in dev-deploy.sh)
FROM debian:bookworm-slim
RUN apt-get update -qq && \
    apt-get install -y -qq gcc make libsdl2-dev libsdl2-ttf-dev libx11-dev && \
    rm -rf /var/lib/apt/lists/*
```

Debian bookworm is used instead of Arch because:
- Its glibc version matches SteamOS 3.x
- `apt-get` works under QEMU emulation on Apple Silicon (Arch's `pacman` uses seccomp/Landlock syscalls that QEMU does not support)
- The Docker image is built once and cached, making subsequent builds take seconds

### systemd Integration

The daemon runs as a systemd user service:

```ini
[Unit]
Description=Allow2 Parental Controls for Linux
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
EnvironmentFile=-%h/.allow2/.env
Environment=PATH=%h/node/bin:/usr/bin:/bin
ExecStart=%h/node/bin/node %h/allow2/allow2linux/packages/allow2linux/src/index.js
Restart=always
RestartSec=5

[Install]
WantedBy=default.target
```

Notable details:
- `EnvironmentFile=-` (with minus prefix) means missing .env is not an error
- `%h` expands to the user's home directory
- `PATH` includes `~/node/bin` for the dev-deploy-installed Node.js
- `Restart=always` ensures the daemon comes back after crashes
- User service (not system service) so it runs in the user's session and can access the display

### Rapid Iteration Cycle

With `dev-deploy.sh watch` running:

1. Edit a source file on the development machine
2. fswatch (macOS) or inotifywait (Linux) detects the change
3. Docker rebuilds the C overlay if C sources changed (incremental, seconds)
4. rsync pushes changed files to the Deck (delta transfer, fast)
5. systemd restarts the daemon
6. Total time from save to running: ~2-5 seconds

---

## 7. Code Organization

```
allow2linux/
├── docs/
│   ├── OVERLAY_DESIGN.md          # Visual design spec for all 5 screens
│   ├── PROJECT_BRIEF.md           # Original project requirements
│   ├── DEVELOPMENT_TIMELINE.md    # Narrative development story
│   └── ALLOW2_LINUX_CASE_STUDY.md # This document
├── packages/
│   ├── allow2linux/               # Main daemon (Node.js)
│   │   ├── src/
│   │   │   ├── index.js           # 332 lines — lifecycle event wiring
│   │   │   ├── overlay-bridge.js  # 491 lines — HTTP/WS server + embedded web UI
│   │   │   ├── process-classifier.js  # 159 lines — /proc scanner
│   │   │   ├── steam.js           # 157 lines — Steam client integration
│   │   │   ├── session.js         # 48 lines  — loginctl wrapper
│   │   │   └── desktop-notify.js  # 43 lines  — freedesktop notifications
│   │   ├── config/
│   │   │   └── processes.json     # Custom process→activity mappings
│   │   ├── systemd/
│   │   │   └── allow2linux.service
│   │   └── package.json
│   └── allow2-lock-overlay/       # SDL2 native overlay (C)
│       ├── src/
│       │   ├── main.c             # 542 lines — event loop, message dispatch
│       │   ├── socket.c/h         # 216 lines — Unix domain socket IPC
│       │   ├── render.c/h         # 481 lines — font loading, UI primitives
│       │   ├── json.c/h           # 423 lines — minimal JSON parser
│       │   ├── screen_pairing.c/h # 234 lines — PIN display screen
│       │   ├── screen_selector.c/h# 249 lines — child picker
│       │   ├── screen_pin.c/h     # 420 lines — PIN entry with numpad
│       │   ├── screen_lock.c/h    # 389 lines — lock screen + request flow
│       │   └── screen_warning.c/h # 214 lines — warning bar
│       ├── assets/
│       │   ├── Inter-Regular.ttf
│       │   └── Inter-Bold.ttf
│       └── Makefile
├── flatpak/
│   ├── dev-deploy.sh              # 308 lines — deployment pipeline
│   ├── build.sh                   # Flatpak build script
│   └── com.allow2.allow2linux.yml # Flatpak manifest
├── scripts/
│   └── register-steam-shortcut.py # Register as non-Steam game
└── .env.example
```

### What Is Reusable

| Component | Reusable? | Notes |
|-----------|-----------|-------|
| Allow2 SDK integration pattern | Yes | Same event-driven approach works on any platform |
| Overlay web UI | Partially | HTML/CSS/JS is portable, display strategy is platform-specific |
| Process classifier | Linux-only | `/proc` filesystem is Linux-specific |
| dev-deploy.sh | Adaptable | SSH+rsync pattern works for any remote target |
| Docker cross-compile | Adaptable | Change base image for different targets |

---

## 8. Key Code Patterns

### Event-Driven Lifecycle

The core pattern is simple: the SDK emits events, the daemon reacts. No polling, no state management in the daemon.

```javascript
// The daemon is essentially just this pattern repeated:
daemon.on('event-name', function (data) {
    overlay.showSomething(data);
});

overlay.on('user-action', function (data) {
    daemon.doSomething(data);
});
```

The entire `index.js` is event wiring. There is no `while` loop, no polling timer, no state variable in the daemon. The SDK owns the state machine.

### WebSocket Overlay Bridge

The overlay bridge serves both HTTP and WebSocket on the same port:

```javascript
// packages/allow2linux/src/overlay-bridge.js, lines 39-87
self._httpServer = createServer(function (req, res) {
    self._handleHttp(req, res);
});

self._wss = new WebSocketServer({ server: self._httpServer });

self._wss.on('connection', function (ws) {
    self._ws = ws;

    ws.on('message', function (data) {
        var msg = JSON.parse(data.toString());
        self._handleMessage(msg);
    });

    // Send current state on connect (solves startup race)
    if (self._currentScreen) {
        ws.send(JSON.stringify(self._screenData));
    }
});
```

The web pages use the same WebSocket for sending user actions back:

```javascript
// Embedded in overlay-bridge.js client-side JavaScript
function send(msg) {
    if (ws && ws.readyState === 1) ws.send(JSON.stringify(msg));
}

window.selectChild = function(id) {
    send({ event: 'child-selected', childId: id });
};
```

### Steam Deck Hardware Detection

```javascript
// packages/allow2linux/src/index.js, lines 314-331
var board = readFileSync('/sys/devices/virtual/dmi/id/board_name', 'utf8').trim();
if (board === 'Jupiter' || board === 'Galileo') {
    isSteamDeck = true;
}
```

### Steam Game Process Detection

Games launched by Steam have `SteamAppId` or `STEAM_COMPAT` in their environment:

```javascript
// packages/allow2linux/src/steam.js, lines 73-79
var environ = await readFile(join('/proc', entry, 'environ'), 'utf8');
if (environ.includes('SteamAppId') || environ.includes('STEAM_COMPAT')) {
    this._activeGamePid = parseInt(entry, 10);
    return this._activeGamePid;
}
```

This is more reliable than checking process names or parent PIDs, because Proton games run through Wine with arbitrary process names, but they all have Steam environment variables.

### Hostname Fallback

SteamOS does not always have the `hostname` command:

```javascript
// packages/allow2linux/src/index.js, lines 303-312
try {
    hostname = execSync('hostname', { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }).trim();
} catch (_err) {
    try {
        hostname = readFileSync('/etc/hostname', 'utf8').trim();
    } catch (_err2) {
        hostname = '';
    }
}
```

---

## 9. Lessons Learned

### What Worked Well

**Starting with the MCP knowledge base.** Having the device lifecycle, SDK philosophy, and integration patterns available before writing any code eliminated false starts and ensured the architecture was correct from the beginning.

**Pushing everything generic into the SDK.** The daemon is thin because the SDK handles pairing, credentials, check polling, enforcement logic, and the state machine. This means every bug fix and improvement in the SDK benefits all platforms.

**Event-driven architecture.** The daemon has no state management. It reacts to SDK events and overlay events. This makes the code easy to understand and hard to get wrong.

**Docker cross-compilation.** Building on the development machine and deploying via rsync is faster and more reliable than trying to set up build tools on SteamOS.

**The `steam://openurl` discovery.** Instead of fighting gamescope, working with Steam produced a solution that is more robust than native overlays would have been (resolution-independent, gamepad-friendly, no compilation needed).

### What Would Be Done Differently

**Start with the web overlay, not SDL2.** The SDL2 overlay is technically excellent and works well on Desktop Mode, but building 3,200 lines of C before discovering the gamescope limitation was wasted effort for Game Mode. A spike test (blank SDL2 window, check visibility in gamescope) would have revealed the problem in 10 minutes.

**Test on target hardware earlier.** Several issues (missing hostname, missing libSDL2_image, gamescope invisibility) were only discovered on the actual Steam Deck. A deploy-and-test cycle earlier in development would have caught them sooner.

**Consider Electron or CEF for Desktop Mode.** The SDL2 overlay is performant and dependency-light, but the web overlay is easier to maintain and update. For Desktop Mode, a persistent local web page may be sufficient without needing the native binary at all.

---

## 10. Applicability to Other Platforms

The allow2linux architecture is directly applicable to other Linux-based devices:

### Raspberry Pi / Single-Board Computers

- Same daemon, same SDK, same overlay bridge
- Process classifier works identically (Linux `/proc` filesystem)
- No gamescope -- desktop overlay strategy only
- Lower-resolution screens may need CSS adjustments
- ARM cross-compilation: change Docker base image to `arm64v8/debian:bookworm-slim`

### ChromeOS (Crostini / Linux container)

- Daemon runs in the Linux container
- Overlay opens in Chrome via `xdg-open` (Chrome is always available)
- Process classification limited to Linux container processes
- Cannot monitor Android apps running outside the container
- ChromeOS's own parental controls (Family Link) may conflict

### Other Handhelds (ROG Ally, Legion Go, etc.)

- These typically run Windows or SteamOS
- SteamOS devices: identical to Steam Deck (same gamescope, same `steam://openurl`)
- Windows devices: different project entirely (Win32 APIs for overlay, different process monitoring)
- ChimeraOS / Bazzite / HoloISO: gamescope-based, same solution applies

### Standard Linux Desktops

- Simplest case: no gamescope, use SDL2 overlay or system browser
- All desktop environments supported via freedesktop standards (notify-send, xdg-open, loginctl)
- Could optionally use a persistent browser tab instead of SDL2 overlay
- Wayland desktops: SDL2 overlay may need layer-shell protocol for proper overlay behavior

### General Pattern for Any Platform

The pattern that makes allow2linux work is applicable far beyond Linux:

1. **Use the Allow2 SDK** for all platform-agnostic logic
2. **Write a thin platform adapter** that handles:
   - How to display the overlay (native UI, web view, system notification)
   - How to classify running processes/apps into activities
   - How to enforce blocks (kill processes, lock session, etc.)
   - How to detect the current user/child
3. **Wire SDK events to the adapter** -- this is the daemon
4. **The adapter should be <500 lines** -- if it is larger, you are probably reimplementing something the SDK should handle

The allow2linux daemon is 332 lines. The overlay bridge is 491 lines (most of which is embedded HTML/CSS/JS). Together they are under 1,000 lines of platform-specific code. Everything else is the SDK.

That ratio -- 1,000 lines of platform code to 2,000+ lines of shared SDK -- is the goal for any Allow2 integration.
