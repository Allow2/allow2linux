# Building Parental Controls for Steam Deck in One Night

## How an MCP Knowledge Base, AI Pair Programming, and Creative Problem-Solving Produced a Working Linux Daemon From Scratch

---

The Steam Deck sits in a weird place. It is a full Linux PC that runs Arch, but it pretends to be a console. Valve designed it so that parents can hand it to a kid who will never see a terminal. That is exactly the kind of device that needs parental controls -- and exactly the kind of device where building them is surprisingly hard.

This is the story of building [allow2linux](https://github.com/Allow2/allow2linux), an Allow2 parental controls daemon for Linux, from zero to running on real Steam Deck hardware in a single session. It covers architecture decisions, SDK rewrites, a brick wall with gamescope, and the elegant hack that got around it.

## Starting From the MCP

The Allow2 platform has existed for years. It has SDKs, APIs, a device lifecycle model, integration patterns. The problem is that all of this knowledge lives spread across repositories, documentation, and the heads of people who have built integrations before.

Before writing any code, the first step was querying the Allow2 MCP Server -- a Model Context Protocol server that serves structured knowledge about the platform. The MCP provided the device lifecycle (pairing, child identification, enforcement, warnings, lock screen, request more time), the SDK philosophy (always use or create an SDK, never raw API calls), and the architecture patterns that every Allow2 integration should follow.

This turned out to be the single best investment of the entire session. Instead of guessing at how pairing works or what events a daemon needs to handle, the MCP laid out the complete integration contract. Every screen, every state transition, every edge case like HTTP 401 unpair events and offline grace periods -- all documented and immediately available.

## The Pivot: Not Just Steam Deck

The original brief said "Steam Deck parental controls." Within the first hour of design work, it became clear that almost nothing about the problem is Steam-Deck-specific. A Linux parental controls daemon needs to: pair with a parent's phone, identify which child is using the device, check permissions against the Allow2 API, warn when time is running out, and lock the screen when time is up.

All of that is generic Linux. The only Steam-Deck-specific pieces are: detecting gamescope (the compositor), detecting the hardware via DMI board names (Jupiter for LCD, Galileo for OLED), and the overlay display mechanism for Game Mode.

The project was restructured as `allow2linux` -- a general-purpose Linux parental controls daemon that happens to have first-class Steam Deck support. The same binary runs on Ubuntu desktops, Raspberry Pi, the ASUS ROG Ally, or any other Linux device.

## Rewriting the SDK

The Allow2 Node SDK existed but had problems. It used optional chaining (`?.`), which the project's Babel and Node configuration does not support. It had references to staging endpoints baked in. The `pairDevice()` method called a function that did not exist. The `save()` and `store()` methods were mismatched. And there was a PIN bypass vulnerability in the pairing callback where an attacker could complete pairing without the correct PIN by sending a crafted request.

Fourteen files were reviewed and fixed. The most important change was rewriting `daemon.js` -- the core lifecycle manager. The original version crashed on startup if no device token existed (the unpaired state). The rewrite implements the full lifecycle as an event-driven state machine: unpaired leads to pairing-required, which leads to paired, which leads to child-select-required, which leads to enforcing. Each transition emits an event. The consuming application (allow2linux in this case) just listens for events and reacts.

This matters beyond Linux. Every Allow2 integration on every platform benefits from a daemon that handles its own lifecycle cleanly. The SDK rewrite is upstream and shared.

## Five Screens, Two Display Modes

The overlay system needed five screens: pairing (shows a 6-digit PIN), child selector (who is playing?), PIN entry (verify identity), lock screen (time is up, request more time), and warning bar (5 minutes remaining). Each screen needed to work in two modes: Steam Deck Game Mode (where gamescope controls what is visible) and standard Linux Desktop Mode (where a normal window manager is running).

The visual design was documented first in OVERLAY_DESIGN.md, then implemented. The IPC protocol between daemon and overlay uses JSON messages over a socket -- simple, debuggable, and the same protocol regardless of which display technology renders the screens.

## The SDL2 Overlay: 17 Files of C

For the overlay, the initial approach was a native SDL2 application. A swarm of AI agents was spawned to implement it in parallel: main.c (event loop, message dispatch), socket.c (Unix domain socket IPC), render.c (font loading, text rendering, UI primitives), json.c (a minimal JSON parser -- no external dependencies), and five screen modules (screen_pairing.c, screen_selector.c, screen_pin.c, screen_lock.c, screen_warning.c).

The total came to 17 source files, about 3,200 lines of C. Cross-file review caught real bugs: activity_id not being passed through the lock screen's "request more time" flow, a `strlen` narrowing conversion, and a static `pulse_phase` variable that should have been per-screen. The SDL2 overlay compiled, linked, ran, received messages from the daemon, and rendered every screen correctly.

Then it was deployed to the Steam Deck. And it was invisible.

## The Gamescope Wall

Gamescope is Steam Deck's compositor. In Game Mode, it runs as the only Wayland compositor, with an internal Xwayland server on display :1 where Steam and its games run. Gamescope composites exactly the windows it knows about -- which means windows from Steam's process tree. An external process creating an SDL2 window on :1 is simply ignored.

Six approaches were tried:

1. **STEAM_OVERLAY atom** -- Setting the `STEAM_OVERLAY` X11 atom on the window. Gamescope reads this but only trusts it from Steam's own overlay process.

2. **GAMESCOPE_EXTERNAL_OVERLAY** -- An environment variable that should enable external overlays. It exists in gamescope's source but did not work in practice on SteamOS 3.x.

3. **GAMESCOPECTRL_BASELAYER_WINDOW** -- Writing to the gamescope control display (:0) to inject the window into the composite stack. The property was set, but gamescope did not pick it up from a non-Steam process.

4. **Override-redirect** -- Bypassing the window manager entirely with an override-redirect window. Gamescope still ignored it.

5. **Display switching** -- Creating the window on :0 instead of :1. The gamescope control display does not composite arbitrary windows.

6. **PID namespace injection** -- Using nsenter to run the overlay inside Steam's cgroup/PID namespace, hoping gamescope would treat it as a Steam child. This was getting into increasingly fragile territory.

None of them worked. Gamescope is specifically designed so that only Steam's process tree gets composited. This is actually good security design -- it means games cannot overlay arbitrary content on top of each other. But it means parental control overlays from a separate daemon are locked out.

## The Breakthrough: steam://openurl

The solution came from thinking about what gamescope *does* composite: Steam itself. Steam has a built-in Chromium-based browser. And Steam supports a URL scheme: `steam://openurl/http://example.com` opens a URL in that browser.

The command `steam -ifrunning steam://openurl/http://localhost:3001/pairing` opens the pairing page in Steam's own browser. And because that browser *is* Steam, gamescope composites it perfectly. It renders on top of games. It accepts input. It works.

The entire overlay architecture was rewritten in about an hour. The SDL2 binary approach was replaced (for Game Mode) with an HTTP server and WebSocket communication layer. The daemon serves web pages for each screen at `http://127.0.0.1:3001/pairing`, `http://127.0.0.1:3001/selector`, etc. It opens them via `steam -ifrunning steam://openurl/...`. The web pages connect back via WebSocket for real-time bidirectional messaging -- the same JSON protocol the SDL2 overlay used.

The key insight that makes this robust: when the WebSocket connection closes (user navigated away from the page), the daemon automatically re-opens persistent screens (lock, selector, pairing) after a one-second delay. A child cannot escape the lock screen by pressing Back -- it reopens.

The SDL2 overlay was not thrown away. It still works perfectly for Desktop Mode on standard Linux, where there is no gamescope to fight with. The overlay bridge detects which mode to use and acts accordingly.

## Docker Cross-Compilation

SteamOS is Arch Linux, but the filesystem is read-only and there are no build tools installed. You cannot `pacman -S gcc` without disabling the read-only filesystem, and even then, the SteamOS base image does not include development headers.

The solution is Docker cross-compilation from the development Mac. A Debian bookworm container (which matches SteamOS's glibc version) installs gcc, make, and the SDL2 development headers, then builds the overlay binary. The container image is persistent -- the first build takes a minute to create the image, subsequent builds take seconds.

There was an interesting wrinkle: on Apple Silicon Macs, Docker runs x86_64 containers via QEMU emulation. Arch Linux's pacman uses seccomp and Landlock syscalls that QEMU does not support. This is why the Docker image uses Debian instead of Arch -- Debian's apt does not use those syscalls, so it works fine under emulation while producing binaries compatible with SteamOS's glibc.

## The Dev-Deploy Pipeline

The `dev-deploy.sh` script handles the entire development workflow in about 230 lines of bash:

1. Detects the Allow2 SDK location automatically
2. Downloads and installs Node.js on the Steam Deck if it is not present (to ~/node, no root needed)
3. Downloads Inter font files for the SDL2 overlay
4. Cross-compiles the C overlay via Docker
5. Rsyncs the SDK and daemon to the Deck over SSH
6. Deploys the .env file securely (chmod 600)
7. Installs a systemd user service
8. Restarts the daemon

In watch mode (`./dev-deploy.sh watch`), it monitors source files and re-deploys on every change. Edit a file on the Mac, save, and two seconds later the daemon restarts on the Deck with the new code.

## What the Numbers Look Like

The daemon itself -- `index.js` -- is 332 lines. It is almost entirely event wiring: listen for SDK events, show the appropriate overlay screen, handle overlay responses. The overlay bridge is 491 lines, including all the embedded HTML, CSS, and JavaScript for the web-based UI. The process classifier, Steam monitor, session manager, and desktop notifier add another 407 lines.

The C overlay is 3,168 lines across 17 files, with full gamepad support, animated transitions, and a custom JSON parser. The deploy script is 308 lines.

Total: about 4,700 lines of code for a complete parental controls system that runs on any Linux device with first-class Steam Deck support. The low line count is a direct result of pushing generic functionality into the Allow2 SDK -- the daemon does not implement pairing, API communication, credential storage, or the enforcement state machine. It just wires events to a display.

## What This Means

The allow2linux project demonstrates a pattern that applies to any Allow2 integration: keep the platform-specific layer thin, push everything generic into the SDK, and focus your effort on the one hard problem unique to your platform (in this case: gamescope visibility).

The MCP knowledge base eliminated hours of research time. AI pair programming handled the parallel implementation of 17 C source files and caught cross-file bugs that a single developer might have missed. Docker cross-compilation solved the "no build tools on target" problem elegantly. And the `steam://openurl` discovery turned an impossible gamescope limitation into a clean, robust solution.

The code is open source at [github.com/Allow2/allow2linux](https://github.com/Allow2/allow2linux). The SDK improvements are upstream. The gamescope overlay pattern is documented for anyone else who needs to display content on top of Steam Deck games from an external process.

From zero to running on real hardware, in one session. That is what happens when you start with good architecture documentation, have the right tools, and are willing to throw away an approach (SDL2 in Game Mode) when a better one (`steam://openurl`) presents itself.
