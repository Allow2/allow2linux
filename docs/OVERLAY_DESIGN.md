# allow2-lock-overlay — Design Specification

## Overview

Fullscreen SDL2 overlay for Allow2 parental controls on Linux.
Renders 5 screens: pairing, child selector, PIN entry, lock, and warning/countdown.

Communicates with the allow2linux daemon via Unix domain socket (JSON protocol).
Long-running process — starts with daemon, sits hidden until commanded.

## Architecture

```
allow2linux daemon (Node.js)
        │
        │ Unix domain socket (JSON)
        │ /tmp/allow2-overlay.sock
        ▼
allow2-lock-overlay (C + SDL2)
        │
        ├── SDL2 window (fullscreen, always-on-top)
        ├── SDL_ttf (Inter font, bundled)
        ├── SDL_image (avatar PNGs, QR code)
        └── SDL_GameController (D-pad, A/B buttons)
```

### Process Lifecycle

- **Option A: Long-running** (chosen)
- Overlay binary starts with daemon, stays resident
- Hidden (no rendering) until it receives a command over the socket
- Hidden state: blocked on `SDL_WaitEvent`, zero CPU usage, ~5-10MB RSS
- Shows/hides screens as commanded — no startup latency when blocking urgently
- Daemon manages spawn/kill of the overlay process

### Dependencies

| Library | Purpose | Package |
|---------|---------|---------|
| SDL2 | Window, rendering, input | `libsdl2-dev` |
| SDL_ttf | Text rendering | `libsdl2-ttf-dev` |
| SDL_image | PNG loading (avatars, QR) | `libsdl2-image-dev` |

### Font

**Inter** — clean, readable at all sizes, ~200KB for Regular + Bold.
Bundled as `assets/Inter-Regular.ttf` and `assets/Inter-Bold.ttf`.
OFL license (free to bundle).

## Screens

### Screen 1: Pairing

Shown when device has no stored credentials.

```
┌─────────────────────────────────────────────┐
│                                             │
│                                             │
│         ┌───────────┐                       │
│         │           │    Enter this PIN     │
│         │  QR CODE  │    in the Allow2 app  │
│         │           │    on your phone       │
│         │           │                       │
│         └───────────┘    ┌─┐┌─┐┌─┐┌─┐┌─┐┌─┐│
│                          │8││4││7││2││9││1││
│                          └─┘└─┘└─┘└─┘└─┘└─┘│
│                                             │
│         ○ Waiting for confirmation...       │
│                                             │
└─────────────────────────────────────────────┘
```

- **QR code** on the left — encodes the pairing session URL for the Allow2 app to scan
- **6-digit PIN** on the right — large digits in styled boxes
- **Pulsing dot** or spinner below to indicate waiting
- Background: dark opaque (`rgba(20, 20, 30, 0.95)`)
- Auto-dismisses when daemon sends `{ "screen": "dismiss" }` after pairing completes

### Screen 2: Child Selector

Shown after pairing when child can't be auto-resolved (shared device, no OS username match).

```
┌─────────────────────────────────────────────┐
│                                             │
│            Who's playing?                   │
│                                             │
│     ┌──────────────────────────────┐        │
│     │  (E)  Emma                 ► │        │
│     ├──────────────────────────────┤        │
│     │  (J)  Jack                   │        │
│     ├──────────────────────────────┤        │
│     │  (S)  Sophie                 │        │
│     ├──────────────────────────────┤        │
│     │  ─────────────────────────── │        │
│     │  (P)  Parent                 │        │
│     └──────────────────────────────┘        │
│                                             │
│     D-pad: navigate    A: select            │
│                                             │
└─────────────────────────────────────────────┘
```

- **Vertical list**, centered, large rows (~100px tall)
- **Avatar**: colored circle with first letter of name (left side)
  - If `~/.allow2/avatars/{childId}.png` exists, load that instead
  - Avatar colors derived from childId (deterministic hash → hue)
- **Name** to the right of the avatar
- **Highlight bar** on focused row (arrow indicator `►`)
- **"Parent"** entry below a divider line, verified by parent PIN
- Navigation: D-pad up/down, A to select, mouse click
- Scrolls if > 5-6 children (unlikely)

### Screen 3: PIN Entry

Shown after selecting a child (if PIN verification required) or selecting "Parent".

```
┌─────────────────────────────────────────────┐
│                                             │
│           Enter PIN for Emma                │
│                                             │
│           ┌─┐ ┌─┐ ┌─┐ ┌─┐                  │
│           │3│ │7│ │ │ │ │                   │
│           └─┘ └─┘ └─┘ └─┘                  │
│                                             │
│     ┌───┬───┬───┐                           │
│     │ 1 │ 2 │ 3 │                           │
│     ├───┼───┼───┤     Or approve on the     │
│     │ 4 │ 5 │ 6 │     Allow2 app            │
│     ├───┼───┼───┤     ○ Waiting...          │
│     │ 7 │ 8 │ 9 │                           │
│     ├───┼───┼───┤                           │
│     │ ← │ 0 │ ✓ │                           │
│     └───┴───┴───┘                           │
│                                             │
│     D-pad: navigate    A: press digit       │
│                                             │
└─────────────────────────────────────────────┘
```

- **Digit display**: shows entered digits (not dots) for child PIN
  - Exception: parent PIN shows dots for security
- **Number pad**: 3x4 grid, controller-navigable
  - `←` = backspace, `✓` = confirm
  - Keyboard input also works (number keys + Enter)
- **App approval**: message on the right side with pulsing indicator
  - Whichever completes first (PIN or app approval) wins
- **Lockout**: after 5 failed attempts, shows "Try again in X:XX" countdown
  - PIN pad disabled during lockout
- **Attempts indicator**: "3 of 5 attempts remaining" below the digit display

### Screen 4: Lock Screen

Full opaque overlay. Blocks all interaction with the device underneath.

```
┌─────────────────────────────────────────────┐
│                                             │
│                                             │
│           Screen time is up                 │
│                                             │
│           Emma, your daily screen           │
│           time has been used up.            │
│                                             │
│                                             │
│     ┌──────────────────┐ ┌───────────────┐  │
│     │  Request More    │ │ Switch Child  │  │
│     │     Time         │ │               │  │
│     └──────────────────┘ └───────────────┘  │
│                                             │
│                                             │
│     D-pad: navigate    A: select            │
│                                             │
└─────────────────────────────────────────────┘
```

- **Reason text**: large, centered ("Screen time is up", "Gaming time is up", etc.)
- **Child name**: personalized message
- **Two action buttons**:
  - **"Request More Time"** → transitions to duration picker sub-screen
  - **"Switch Child"** → daemon emits child-select-required, overlay shows Screen 2
- Background: dark opaque (`rgba(20, 20, 30, 0.95)`)
- Game is SIGSTOP'd behind this in Game Mode

#### Sub-screen: Duration Picker (within Lock Screen)

```
┌─────────────────────────────────────────────┐
│                                             │
│           Request More Time                 │
│                                             │
│     ┌──────────┐ ┌──────────┐ ┌──────────┐ │
│     │  15 min  │ │  30 min  │ │  1 hour  │ │
│     └──────────┘ └──────────┘ └──────────┘ │
│                                             │
│           ← Back                            │
│                                             │
└─────────────────────────────────────────────┘
```

- Three preset buttons: 15 min, 30 min, 1 hour
- On selection: sends request to daemon, shows "Waiting for parent approval..." with spinner
- On approved: overlay dismisses
- On denied: returns to lock screen with "Request denied" message (brief)

### Screen 5: Warning / Countdown

Semi-transparent top bar. Game/app still visible and playable underneath.

```
┌─────────────────────────────────────────────┐
│ ▌ Gaming: 5 minutes remaining    [Request] │
├─────────────────────────────────────────────┤
│                                             │
│            (game visible underneath)        │
│                                             │
│                                             │
│                                             │
└─────────────────────────────────────────────┘
```

- **Top bar**: full width, ~60px tall
- **Left side**: activity name + time remaining
- **Right side**: "Request More Time" button (appears at `urgent` level and below)
- **Colored accent bar** on the left edge:
  - `info` (15 min): blue
  - `urgent` (5 min): amber/orange
  - `final` (1 min): red
  - `countdown` (30 sec): red, pulsing
- **Opacity**: bar background at ~85% opacity, rest of screen transparent
- **Auto-dismiss**: when time is extended or activity becomes unblocked
- Does NOT capture input — game controller still works underneath
  - "Request" button activated by mouse click or a specific key combo (e.g., F12)

## IPC Protocol

### Socket

Unix domain socket at `/tmp/allow2-overlay.sock` (configurable via `--socket` arg).

Daemon is the **server** (creates the socket). Overlay is the **client** (connects on startup).
If connection lost, overlay retries every 2 seconds.

### Messages (JSON, newline-delimited)

#### Daemon → Overlay

```json
{ "screen": "pairing", "pin": "847291", "qrData": "https://app.allow2.com/pair?session=abc123" }

{ "screen": "selector", "children": [
    { "id": 1, "name": "Emma", "avatarPath": "/home/deck/.allow2/avatars/1.png" },
    { "id": 2, "name": "Jack", "avatarPath": null }
]}

{ "screen": "pin-entry", "childId": 1, "childName": "Emma", "isParent": false, "maxDigits": 4 }

{ "screen": "lock", "reason": "Screen time is up", "childName": "Emma", "childId": 1 }

{ "screen": "warning", "activity": "Gaming", "activityId": 3, "remaining": 300, "level": "urgent" }

{ "screen": "pin-result", "success": false, "attemptsRemaining": 3, "lockedOut": false }
{ "screen": "pin-result", "success": false, "attemptsRemaining": 0, "lockedOut": true, "lockoutSeconds": 300 }
{ "screen": "pin-result", "success": true }

{ "screen": "request-status", "status": "pending" }
{ "screen": "request-status", "status": "approved" }
{ "screen": "request-status", "status": "denied" }

{ "screen": "denied" }

{ "screen": "dismiss" }
```

#### Overlay → Daemon

```json
{ "event": "child-selected", "childId": 1 }

{ "event": "pin-entered", "childId": 1, "pin": "1234" }

{ "event": "parent-selected" }

{ "event": "parent-pin-entered", "pin": "5678" }

{ "event": "request-more-time", "activityId": 3, "duration": 15 }

{ "event": "switch-child" }

{ "event": "ready" }
```

### Message Flow Examples

**Pairing:**
```
Daemon → { "screen": "pairing", "pin": "847291", "qrData": "..." }
  ... parent confirms on phone ...
Daemon → { "screen": "dismiss" }
```

**Child login (PIN):**
```
Daemon → { "screen": "selector", "children": [...] }
Overlay → { "event": "child-selected", "childId": 1 }
Daemon → { "screen": "pin-entry", "childId": 1, "childName": "Emma", "isParent": false, "maxDigits": 4 }
Overlay → { "event": "pin-entered", "childId": 1, "pin": "1234" }
Daemon → { "screen": "pin-result", "success": true }
Daemon → { "screen": "dismiss" }
```

**Child login (app approval):**
```
Daemon → { "screen": "selector", "children": [...] }
Overlay → { "event": "child-selected", "childId": 1 }
Daemon → { "screen": "pin-entry", "childId": 1, "childName": "Emma", "isParent": false, "maxDigits": 4 }
  ... child approves on Allow2 app ...
Daemon → { "screen": "pin-result", "success": true }
Daemon → { "screen": "dismiss" }
```

**Lock + request:**
```
Daemon → { "screen": "lock", "reason": "Screen time is up", "childName": "Emma", "childId": 1 }
Overlay → { "event": "request-more-time", "activityId": 8, "duration": 30 }
Daemon → { "screen": "request-status", "status": "pending" }
  ... parent approves ...
Daemon → { "screen": "request-status", "status": "approved" }
Daemon → { "screen": "dismiss" }
```

**Lock + switch child:**
```
Daemon → { "screen": "lock", "reason": "Gaming time is up", "childName": "Emma", "childId": 1 }
Overlay → { "event": "switch-child" }
Daemon → { "screen": "selector", "children": [...] }
```

## Visual Style

### Colors

| Element | Color | Hex |
|---------|-------|-----|
| Background (opaque screens) | Dark navy | `#14141E` at 95% opacity |
| Background (warning bar) | Dark navy | `#14141E` at 85% opacity |
| Primary text | White | `#FFFFFF` |
| Secondary text | Light gray | `#A0A0B0` |
| Accent (info) | Blue | `#667EEA` |
| Accent (urgent) | Amber | `#F6AD55` |
| Accent (blocked/final) | Soft red | `#FC8181` |
| Button background | Dark blue | `#2D3748` |
| Button highlight | Accent blue | `#667EEA` |
| Avatar fallback colors | Derived from childId | `hue = (childId * 137) % 360` |
| Divider | Subtle gray | `#2D2D3D` |

### Typography

| Element | Font | Size | Weight |
|---------|------|------|--------|
| Screen title | Inter | 36px | Bold |
| Child name (selector) | Inter | 28px | Regular |
| PIN digits | Inter | 48px | Bold |
| PIN pad buttons | Inter | 32px | Regular |
| Body text | Inter | 20px | Regular |
| Button label | Inter | 22px | Bold |
| Warning bar text | Inter | 20px | Bold |
| Helper text (controller hints) | Inter | 16px | Regular |
| Avatar initial | Inter | 36px | Bold |

### Layout (1280x800 — Steam Deck native)

- All screens centered vertically and horizontally
- Content area max width: 800px
- Buttons: 200x60px with 8px border radius
- Avatar circles: 64x64px
- Selector rows: 100px tall, full content width
- PIN digit boxes: 56x68px with 12px border radius
- PIN pad buttons: 80x80px with 8px border radius
- Warning bar: full width x 60px, pinned to top
- Padding: 24px between major elements, 12px between minor elements

### Controller Hints

Small text at bottom of interactive screens:
- `D-pad: navigate    A: select    B: back`
- Only shown when a game controller is detected
- Hidden when only mouse/keyboard input is active

## Source Structure

```
packages/allow2-lock-overlay/
├── Makefile
├── assets/
│   ├── Inter-Regular.ttf
│   └── Inter-Bold.ttf
└── src/
    ├── main.c              — Entry point, socket client, event loop, screen dispatch
    ├── socket.c / .h       — Unix domain socket client, JSON read/write, reconnect
    ├── render.c / .h       — Common rendering: fonts, colors, buttons, text, avatars
    ├── screen_pairing.c    — Screen 1: QR code + PIN display
    ├── screen_selector.c   — Screen 2: child selector list
    ├── screen_pin.c        — Screen 3: PIN entry pad
    ├── screen_lock.c       — Screen 4: lock screen + duration picker
    ├── screen_warning.c    — Screen 5: warning top bar
    └── qrencode.c / .h     — Minimal QR code generator (or use libqrencode)
```

## Build

```bash
# Dependencies (Debian/Ubuntu)
sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev

# Dependencies (Arch/SteamOS)
sudo pacman -S sdl2 sdl2_ttf sdl2_image

# Build
cd packages/allow2-lock-overlay
make

# Run (for testing — normally started by daemon)
./allow2-lock-overlay --socket /tmp/allow2-overlay.sock
```

## Gamescope (Game Mode) Notes

- gamescope is a nested Wayland compositor used by Steam Deck Gaming Mode
- SDL2 auto-detects Wayland and creates windows within gamescope
- `SDL_WINDOW_ALWAYS_ON_TOP` is respected by gamescope for overlay windows
- The overlay window renders above the game surface
- For semi-transparent warning: `SDL_SetWindowOpacity(window, 0.85)`
- For full lock: opacity 1.0, game process is SIGSTOP'd by the daemon
- gamescope passes controller input to the focused window — overlay receives it when visible

## Future Considerations

- **i18n**: all user-facing strings should be in a string table from the start, even if English-only initially
- **Theming**: colors defined as constants, easy to swap for light theme later
- **Resolution scaling**: render at logical 1280x800, SDL2 handles DPI scaling
- **Touch input**: SDL2 handles touch events — useful if Linux tablet support is added later
