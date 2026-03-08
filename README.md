# allow2linux

Parental controls for Linux devices, powered by the [Allow2](https://allow2.com) platform.

allow2linux is a background daemon that enforces daily time quotas, allowed hours, activity-specific limits, and offline-safe approval workflows on any Linux device — Steam Deck, desktops, laptops, shared family PCs. Parents manage everything from the Allow2 app on their phone.

## How it works

The daemon runs as a systemd user service. It pairs with the Allow2 platform via a 6-digit PIN (parents never enter credentials on the child's device), identifies which child is using the device, then continuously enforces their configured limits.

```
Device boot → Child identification → Permission checks (every 30-60s)
                                          │
                              ┌───────────┼───────────┐
                              ▼           ▼           ▼
                           Allowed    Warning     Blocked
                          (continue)  (countdown)  (lock/terminate)
```

### Key features

- **Device pairing** — PIN or QR code, one-time setup
- **Child identification** — OS username mapping, or interactive "Who's playing?" selector with PIN verification
- **Activity enforcement** — per-activity quotas (Gaming, Internet, Social, Screen Time) with stacking
- **Progressive warnings** — 15min → 5min → 1min → 30sec → 10sec → blocked
- **Request More Time** — children can request extra time directly from the lock screen; parents approve/deny from their phone
- **Offline support** — cached permissions with grace period, deny-by-default when offline
- **Steam Deck support** — works in both Game Mode and Desktop Mode
- **Process monitoring** — scans `/proc` to detect and classify running applications by activity type

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                   allow2linux daemon                      │
│                  (Node.js, systemd --user)                │
│                                                           │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────────┐ │
│  │ Allow2 SDK  │  │ Overlay      │  │ Process         │ │
│  │ (DeviceDaemon│  │ Bridge       │  │ Classifier      │ │
│  │  pairing,   │  │ (HTTP+WS on  │  │ (/proc scanning │ │
│  │  checks,    │  │  localhost)   │  │  activity map)  │ │
│  │  requests)  │  │              │  │                 │ │
│  └──────┬──────┘  └──────┬───────┘  └────────┬────────┘ │
│         │                │                    │          │
│         ▼                ▼                    ▼          │
│    Allow2 cloud     Browser overlay     SIGTERM/SIGKILL  │
│   (api.allow2.com)  (pairing, selector,  (blocked apps)  │
│                      lock, warnings)                     │
└──────────────────────────────────────────────────────────┘
```

### Three packages

| Package | Purpose |
|---------|---------|
| **allow2linux** (`packages/allow2linux/`) | The daemon — wires the SDK to Linux-specific enforcement (process control, overlay, notifications) |
| **allow2** (SDK v2, separate repo) | The Allow2 Device SDK — pairing, permission checks, warnings, requests, offline support |
| **allow2-lock-overlay** (`packages/allow2-lock-overlay/`) | Native C + SDL2 fullscreen overlay for lock screens and child selection (WIP) |

### Overlay display

The overlay serves embedded HTML pages via a local HTTP + WebSocket server. Screens are opened in the available browser (Steam's browser in Game Mode, system browser in Desktop Mode). WebSocket provides real-time bidirectional communication for state updates and user input.

Screens: pairing, child selector, PIN entry, lock, warning bar, request more time.

## Not Allow2Automate

allow2linux talks **directly to the Allow2 cloud**. It does not require a parent app on the local network.

| | Allow2Automate | allow2linux |
|---|---|---|
| Communication | Agent pulls from parent app on LAN | Daemon calls Allow2 cloud directly |
| Network | Same local network required | Any internet connection |
| Parent device | Must be on network | Not needed — parent uses phone app |
| Use case | Managed home network | Any Linux device, anywhere |

## Development

### Prerequisites

- Node.js 18+
- The allow2 SDK v2 at `../../../sdk/node` (or set `SDK_ROOT`)

### Run locally

```bash
cd packages/allow2linux
npm install
node src/index.js
```

### Deploy to Steam Deck

```bash
# Fast dev deployment over SSH (syncs source + restarts daemon)
./flatpak/dev-deploy.sh

# Watch mode — auto-redeploy on file changes
./flatpak/dev-deploy.sh watch

# Custom IP
DECK_HOST=deck@192.168.100.2 ./flatpak/dev-deploy.sh
```

The deploy script handles: Node.js installation on the Deck, file sync, dependency install, systemd service setup, and daemon restart.

### Environment variables

```bash
ALLOW2_API_URL=https://staging-api.allow2.com  # API endpoint (default: production)
ALLOW2_VID=12345                                # Version ID (registered at developer.allow2.com)
ALLOW2_TOKEN=your-token-here                    # Version token
```

Place in `~/.allow2/.env` on the target device (the deploy script handles this if a `.env` file exists in the project root or `flatpak/` directory).

## Distribution (planned)

| Format | Target |
|--------|--------|
| Flatpak | Steam Deck, general Linux |
| Snap | Ubuntu and derivatives |
| deb | Debian, Ubuntu, Mint, Pop!_OS |
| rpm | Fedora, RHEL, openSUSE |
| AppImage | Any Linux (portable) |
| AUR | Arch, Manjaro |

## License

See [LICENSE](LICENSE) file.
