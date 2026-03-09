/**
 * Overlay Bridge
 *
 * Two display backends (auto-detected):
 *   1. Game Mode (gamescope running): Steam browser via steam://openurl/
 *      Communication: HTTP server + WebSocket (same JSON protocol)
 *   2. Desktop Mode: SDL2 native overlay binary (allow2-lock-overlay)
 *      Communication: Unix domain socket (newline-delimited JSON)
 *
 * Both backends use the same JSON message protocol.
 */

import { EventEmitter } from 'node:events';
import { createServer } from 'node:http';
import { execFile, spawn, execSync } from 'node:child_process';
import { createConnection } from 'node:net';
import { existsSync, unlinkSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';
import { generateQrSvg, generateQrGrid } from './qr.js';

var OVERLAY_PORT = 3001;
var SOCKET_PATH = '/tmp/allow2-overlay.sock';

export class OverlayBridge extends EventEmitter {

    constructor() {
        super();
        // Common state
        this._currentScreen = null;
        this._screenData = {};
        this._mode = null; // 'steam' or 'sdl2', detected at start

        // Steam browser backend (Game Mode)
        this._httpServer = null;
        this._wss = null;
        this._ws = null;
        this._lastHeartbeat = 0;
        this._heartbeatTimer = null;
        this._lastReopen = 0;
        this._reopenCount = 0;
        this._reopenTimer = null;

        // SDL2 backend (Desktop Mode)
        this._sdlProcess = null;
        this._sdlSocket = null;
        this._sdlBuffer = '';
        this._sdlReconnectTimer = null;
        this._sdlAppMode = false; // true = windowed app, false = fullscreen overlay
    }

    // ── Lifecycle ──────────────────────────────────────────────

    async start() {
        this._mode = _detectGameMode() ? 'steam' : 'sdl2';
        console.log('[overlay] mode: ' + this._mode + ' (DISPLAY=' + (process.env.DISPLAY || 'unset') + ')');

        if (this._mode === 'steam') {
            await this._startSteamBackend();
        } else {
            await this._startSdl2Backend();
        }
    }

    async stop() {
        if (this._mode === 'steam') {
            this._stopSteamBackend();
        } else {
            this._stopSdl2Backend();
        }
        this._currentScreen = null;
        this._screenData = {};
    }

    isAvailable() {
        if (this._mode === 'steam') {
            return this._httpServer !== null;
        }
        return this._sdlProcess !== null;
    }

    // ── Screen Commands (shared interface) ────────────────────

    showPairingScreen(params) {
        var qrSvg = '';
        var qrGrid = null;
        if (params.qrData) {
            try {
                qrSvg = generateQrSvg(params.qrData, 3);
                qrGrid = generateQrGrid(params.qrData);
            } catch (err) {
                console.error('[overlay] QR generation failed:', err.message);
            }
        }
        this._showScreen('pairing', {
            screen: 'pairing',
            pin: params.pin,
            qrData: params.qrData || '',
            qrSvg: qrSvg,
            qrSize: qrGrid ? qrGrid.size : 0,
            qrModules: qrGrid ? qrGrid.modules : '',
            message: params.message || '',
        });
    }

    showChildSelector(children) {
        this._showScreen('selector', {
            screen: 'selector',
            children: (children || []).map(function (c) {
                return {
                    id: c.id || c.childId,
                    name: c.name || c.firstName || 'Child',
                    avatarPath: c.avatarPath || '',
                    lastUsedAt: c.lastUsedAt || null,
                };
            }),
        });
    }

    showPinEntry(params) {
        this._showScreen('pin', {
            screen: 'pin-entry',
            childId: params.childId,
            childName: params.childName,
            isParent: !!params.isParent,
            maxDigits: params.maxDigits || 4,
        });
    }

    sendPinResult(params) {
        this._send({
            screen: 'pin-result',
            success: !!params.success,
            attemptsRemaining: params.attemptsRemaining || 0,
            lockedOut: !!params.lockedOut,
            lockoutSeconds: params.lockoutSeconds || 0,
        });
    }

    showLockScreen(params) {
        this._showScreen('lock', {
            screen: 'lock',
            reason: params.reason || 'Screen time is up',
            childName: params.childName || '',
            childId: params.childId || 0,
            activityId: params.activityId || 0,
        });
    }

    showWarning(params) {
        this._showScreen('warning', {
            screen: 'warning',
            activity: params.activity || '',
            activityId: params.activityId || 0,
            remaining: params.remaining || 0,
            level: params.level || 'info',
        });
    }

    showStatusScreen(data) {
        this._showScreen('status', {
            screen: 'status',
            family: data.family || '',
            childName: data.childName || '',
            childId: data.currentChildId || 0,
            isParent: data.isParent ? 1 : 0,
            activities: data.remaining || [],
            canSubmitFeedback: data.canSubmitFeedback ? 1 : 0,
        });
    }

    showFeedbackScreen() {
        this._showScreen('feedback', {
            screen: 'feedback',
        });
    }

    showFeedbackConfirmation() {
        /* Return to status screen after brief confirmation.
         * The daemon will call openApp() which triggers status-requested. */
    }

    showRequestStatus(status) {
        this._send({ screen: 'request-status', status: status });
    }

    showDenied() {
        this._send({ screen: 'denied' });
    }

    dismiss() {
        this._currentScreen = null;
        this._screenData = {};
        this._reopenCount = 0;
        if (this._reopenTimer) {
            clearTimeout(this._reopenTimer);
            this._reopenTimer = null;
        }
        this._send({ screen: 'dismiss' });
        // In SDL2 overlay mode, kill the binary after dismiss — it will be
        // respawned on-demand when the next screen is needed.
        // In app mode, the binary stays alive (shows idle background).
        if (this._mode === 'sdl2' && this._sdlProcess && !this._sdlAppMode) {
            console.log('[overlay] killing SDL2 overlay binary after dismiss');
            try { this._sdlProcess.kill('SIGTERM'); } catch (_e) { /* */ }
            this._sdlProcess = null;
        }
    }

    // ── Unified send/show ─────────────────────────────────────

    _showScreen(screenName, data) {
        this._currentScreen = screenName;
        this._screenData = data;

        if (this._mode === 'steam') {
            this._send(data);
            var url = 'http://127.0.0.1:' + OVERLAY_PORT + '/' + screenName;
            this._openSteamUrl(url);
        } else {
            // Desktop Mode: non-blocking screens use windowed app mode,
            // blocking screens (lock, warning) use fullscreen overlay mode.
            var wantAppMode = (screenName === 'pairing' || screenName === 'selector'
                || screenName === 'status' || screenName === 'feedback'
                || screenName === 'pin');
            var needsRespawn = this._sdlProcess && (wantAppMode !== this._sdlAppMode);

            if (needsRespawn) {
                console.log('[overlay] switching SDL2 mode (' + (this._sdlAppMode ? 'app' : 'overlay')
                    + ' → ' + (wantAppMode ? 'app' : 'overlay') + ')');
                try { this._sdlProcess.kill('SIGTERM'); } catch (_e) { /* */ }
                this._sdlProcess = null;
                this._sdlSocket = null;
            }

            if (!this._sdlProcess) {
                var binaryPath = this._findOverlayBinary();
                if (binaryPath) {
                    console.log('[overlay] spawning SDL2 on-demand for screen: ' + screenName
                        + ' (mode: ' + (wantAppMode ? 'app' : 'overlay') + ')');
                    this._sdlAppMode = wantAppMode;
                    this._spawnSdl2(binaryPath, wantAppMode);
                    // Binary will connect and receive current screen via _sdlServer 'connection' handler
                } else {
                    console.error('[overlay] SDL2 binary not found, cannot show screen');
                }
            } else {
                this._sendSdl(data);
            }
        }
    }

    _send(message) {
        try {
            if (this._mode === 'steam') {
                this._sendWs(message);
            } else {
                this._sendSdl(message);
            }
        } catch (err) {
            console.error('[overlay] send error:', err.message);
        }
    }

    // ── Message handling (same for both backends) ─────────────

    _handleMessage(msg) {
        if (!msg || !msg.event) return;

        switch (msg.event) {
            case 'child-selected':
                this.emit('child-selected', { childId: msg.childId });
                break;
            case 'pin-entered':
                this.emit('pin-entered', { childId: msg.childId, pin: msg.pin });
                break;
            case 'parent-selected':
                this.emit('parent-selected');
                break;
            case 'parent-pin-entered':
                this.emit('parent-pin-entered', { pin: msg.pin });
                break;
            case 'parent-pin-verified':
                this.emit('parent-pin-verified');
                break;
            case 'request-more-time':
                this.emit('request-more-time', {
                    activityId: msg.activityId,
                    duration: msg.duration,
                });
                break;
            case 'switch-child':
                this.emit('switch-child');
                break;
            case 'app-opened':
                this.emit('app-opened');
                break;
            case 'app-close':
                this.emit('app-closed');
                break;
            case 'report-issue':
                this.emit('report-issue');
                break;
            case 'submit-feedback':
                this.emit('submit-feedback', {
                    category: msg.category,
                    message: msg.message,
                });
                break;
            case 'feedback-cancel':
                this.emit('feedback-cancel');
                break;
            case 'ready':
                // SDL2 overlay connected and ready
                console.log('[overlay] SDL2 overlay ready');
                if (this._currentScreen) {
                    this._sendSdl(this._screenData);
                }
                break;
            default:
                console.warn('[overlay] unknown event:', msg.event);
        }
    }

    // ══════════════════════════════════════════════════════════
    // STEAM BROWSER BACKEND (Game Mode)
    // ══════════════════════════════════════════════════════════

    async _startSteamBackend() {
        if (this._httpServer) return;

        var self = this;

        return new Promise(function (resolve, reject) {
            self._httpServer = createServer(function (req, res) {
                try {
                    self._handleHttp(req, res);
                } catch (err) {
                    console.error('[overlay] HTTP handler error:', err.message);
                    try { res.writeHead(500); res.end('Internal error'); } catch (_e) { /* */ }
                }
            });

            self._wss = new WebSocketServer({ server: self._httpServer });

            self._wss.on('connection', function (ws) {
                self._ws = ws;
                self._lastHeartbeat = Date.now();
                self._reopenCount = 0;
                console.log('[overlay] WebSocket connected');

                self._startHeartbeatMonitor();

                ws.on('message', function (data) {
                    try {
                        var msg = JSON.parse(data.toString());
                        if (msg.event === 'heartbeat') {
                            self._lastHeartbeat = Date.now();
                            return;
                        }
                        if (msg.event === 'page-closing') {
                            self._scheduleReopen('beforeunload');
                            return;
                        }
                        self._handleMessage(msg);
                    } catch (_e) {
                        console.warn('[overlay] invalid WebSocket message');
                    }
                });

                ws.on('close', function () {
                    if (self._ws === ws) {
                        self._ws = null;
                        self._scheduleReopen('ws-close');
                    }
                });

                ws.on('error', function (err) {
                    console.error('[overlay] WebSocket error:', err.message);
                });

                if (self._currentScreen) {
                    try { ws.send(JSON.stringify(self._screenData)); } catch (_e) { /* */ }
                }
            });

            self._wss.on('error', function (err) {
                console.error('[overlay] WebSocketServer error:', err.message);
            });

            self._httpServer.listen(OVERLAY_PORT, '127.0.0.1', function () {
                console.log('[overlay] web server on http://127.0.0.1:' + OVERLAY_PORT);
                resolve();
            });

            self._httpServer.on('error', function (err) {
                console.error('[overlay] HTTP server error:', err.message);
                reject(err);
            });
        });
    }

    _stopSteamBackend() {
        this._stopHeartbeatMonitor();
        if (this._reopenTimer) {
            clearTimeout(this._reopenTimer);
            this._reopenTimer = null;
        }
        if (this._ws) {
            try { this._ws.close(); } catch (_e) { /* */ }
            this._ws = null;
        }
        if (this._wss) {
            try { this._wss.close(); } catch (_e) { /* */ }
            this._wss = null;
        }
        if (this._httpServer) {
            try { this._httpServer.close(); } catch (_e) { /* */ }
            this._httpServer = null;
        }
    }

    _sendWs(message) {
        try {
            if (this._ws && this._ws.readyState === 1) {
                this._ws.send(JSON.stringify(message));
            }
        } catch (err) {
            console.error('[overlay] WebSocket send error:', err.message);
        }
    }

    _openSteamUrl(url) {
        var self = this;

        // Verify Steam is still running before calling steam CLI
        // (mode switch from Game→Desktop kills Steam)
        try {
            execSync('pgrep -x steam', { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] });
        } catch (_e) {
            console.log('[overlay] Steam no longer running — switching to SDL2 mode');
            this._switchToSdl2();
            return;
        }

        var steamUrl = 'steam://openurl/' + url;
        console.log('[overlay] opening: ' + steamUrl);
        try {
            execFile('steam', [steamUrl], { timeout: 5000 }, function (err) {
                if (err) {
                    var msg = (err.message || '').split('\n')[0];
                    console.log('[overlay] steam open failed: ' + msg);
                    // If steam died mid-call, switch to SDL2
                    if (msg.indexOf('not running') !== -1 || msg.indexOf('ENOENT') !== -1) {
                        self._switchToSdl2();
                    }
                }
            });
        } catch (err) {
            console.error('[overlay] execFile steam error:', err.message);
            this._switchToSdl2();
        }
    }

    async _switchToSdl2() {
        if (this._mode === 'sdl2') return; // already switched

        console.log('[overlay] switching from steam → sdl2');
        this._stopSteamBackend();
        this._mode = 'sdl2';
        await this._startSdl2Backend();

        // Re-show current screen on new backend
        if (this._currentScreen) {
            this._sendSdl(this._screenData);
        }
    }

    // ── Steam: Heartbeat + re-open ───────────────────────────

    _startHeartbeatMonitor() {
        var self = this;
        if (this._heartbeatTimer) clearInterval(this._heartbeatTimer);

        this._heartbeatTimer = setInterval(function () {
            if (self._currentScreen && self._currentScreen !== 'warning' && self._currentScreen !== 'pairing') {
                var elapsed = Date.now() - self._lastHeartbeat;
                if (elapsed > 1500 && self._ws) {
                    console.log('[overlay] heartbeat lost (' + elapsed + 'ms), re-opening');
                    self._ws = null;
                    self._scheduleReopen('heartbeat-timeout');
                }
            }
        }, 500);
    }

    _stopHeartbeatMonitor() {
        if (this._heartbeatTimer) {
            clearInterval(this._heartbeatTimer);
            this._heartbeatTimer = null;
        }
    }

    _scheduleReopen(reason) {
        var self = this;

        if (!this._currentScreen || this._currentScreen === 'warning' || this._currentScreen === 'pairing') return;

        var now = Date.now();
        if (now - this._lastReopen < 2000) return;

        if (this._reopenCount >= 5) {
            console.log('[overlay] re-open limit reached, backing off');
            setTimeout(function () { self._reopenCount = 0; }, 30000);
            return;
        }

        if (this._reopenTimer) clearTimeout(this._reopenTimer);

        var delay = reason === 'beforeunload' ? 200 : 500;

        this._reopenTimer = setTimeout(function () {
            if (self._currentScreen && self._currentScreen !== 'warning' && self._currentScreen !== 'pairing') {
                self._reopenCount++;
                self._lastReopen = Date.now();
                console.log('[overlay] re-opening ' + self._currentScreen + ' (reason: ' + reason + ', attempt: ' + self._reopenCount + ')');
                var url = 'http://127.0.0.1:' + OVERLAY_PORT + '/' + self._currentScreen;
                self._openSteamUrl(url);
            }
        }, delay);
    }

    // ── Steam: HTTP handler ──────────────────────────────────

    _handleHttp(req, res) {
        var path = req.url.split('?')[0];

        if (path === '/api/state') {
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify(this._screenData || {}));
            return;
        }

        res.writeHead(200, {
            'Content-Type': 'text/html; charset=utf-8',
            'Cache-Control': 'no-cache',
        });
        res.end(this._renderPage(path));
    }

    _renderPage(path) {
        var stateJson = JSON.stringify(this._screenData || {});
        var screenName = path.replace(/^\//, '') || 'index';

        return '<!DOCTYPE html>'
            + '<html><head>'
            + '<meta name="viewport" content="width=device-width,initial-scale=1">'
            + '<title>Allow2</title>'
            + '<style>' + OVERLAY_CSS + '</style>'
            + '</head><body>'
            + '<div id="app"></div>'
            + '<script>'
            + 'var INITIAL_STATE=' + stateJson + ';'
            + 'var SCREEN="' + screenName + '";'
            + OVERLAY_JS
            + '</script>'
            + '</body></html>';
    }

    // ══════════════════════════════════════════════════════════
    // SDL2 BACKEND (Desktop Mode)
    // ══════════════════════════════════════════════════════════

    async _startSdl2Backend() {
        var self = this;
        var binaryPath = this._findOverlayBinary();

        if (!binaryPath) {
            console.error('[overlay] SDL2 binary not found, falling back to steam backend');
            this._mode = 'steam';
            return this._startSteamBackend();
        }

        // Clean up stale socket
        try {
            if (existsSync(SOCKET_PATH)) unlinkSync(SOCKET_PATH);
        } catch (_e) { /* */ }

        // Create Unix socket server for the SDL2 binary to connect to
        var net = await import('node:net');
        this._sdlServer = net.createServer(function (conn) {
            console.log('[overlay] SDL2 binary connected');
            self._sdlSocket = conn;
            self._sdlBuffer = '';

            conn.on('data', function (chunk) {
                self._sdlBuffer += chunk.toString();
                var lines = self._sdlBuffer.split('\n');
                self._sdlBuffer = lines.pop(); // keep incomplete line
                for (var i = 0; i < lines.length; i++) {
                    if (lines[i].trim()) {
                        try {
                            var msg = JSON.parse(lines[i]);
                            self._handleMessage(msg);
                        } catch (_e) {
                            console.warn('[overlay] invalid SDL2 message:', lines[i].substring(0, 80));
                        }
                    }
                }
            });

            conn.on('close', function () {
                console.log('[overlay] SDL2 binary disconnected');
                self._sdlSocket = null;
                // Restart if it died unexpectedly
                if (self._sdlProcess) {
                    self._scheduleRestartSdl();
                }
            });

            conn.on('error', function (err) {
                console.error('[overlay] SDL2 socket error:', err.message);
            });

            // Send current state if any
            if (self._currentScreen) {
                self._sendSdl(self._screenData);
            }
        });

        this._sdlServer.on('error', function (err) {
            console.error('[overlay] SDL2 server error:', err.message);
        });

        return new Promise(function (resolve) {
            self._sdlServer.listen(SOCKET_PATH, function () {
                console.log('[overlay] Unix socket listening at ' + SOCKET_PATH);
                // Don't spawn binary yet — it will be spawned on-demand
                // when the first screen needs to be shown (deferred start).
                // This prevents a fullscreen overlay from blocking the desktop
                // when the device is unpaired or idle.
                console.log('[overlay] SDL2 binary deferred until first screen');
                resolve();
            });
        });
    }

    _spawnSdl2(binaryPath, appMode) {
        var self = this;

        // Build environment for SDL2 binary.
        // systemd user services don't inherit DISPLAY/WAYLAND_DISPLAY,
        // so we discover them from the active graphical session.
        var sdlEnv = Object.assign({}, process.env);
        if (!sdlEnv.DISPLAY && !sdlEnv.WAYLAND_DISPLAY) {
            var sessionEnv = _discoverDisplayEnv();
            if (sessionEnv.DISPLAY) sdlEnv.DISPLAY = sessionEnv.DISPLAY;
            if (sessionEnv.WAYLAND_DISPLAY) sdlEnv.WAYLAND_DISPLAY = sessionEnv.WAYLAND_DISPLAY;
            if (sessionEnv.XDG_RUNTIME_DIR) sdlEnv.XDG_RUNTIME_DIR = sessionEnv.XDG_RUNTIME_DIR;
            if (sessionEnv.XAUTHORITY) sdlEnv.XAUTHORITY = sessionEnv.XAUTHORITY;
            console.log('[overlay] discovered display env: DISPLAY=' + (sdlEnv.DISPLAY || 'unset')
                + ' WAYLAND_DISPLAY=' + (sdlEnv.WAYLAND_DISPLAY || 'unset')
                + ' XAUTHORITY=' + (sdlEnv.XAUTHORITY || 'unset'));
        }

        var args = ['--socket', SOCKET_PATH];
        if (appMode) {
            args.push('--mode', 'app');
        }

        console.log('[overlay] spawning SDL2 binary: ' + binaryPath + ' ' + args.join(' '));
        try {
            this._sdlProcess = spawn(binaryPath, args, {
                stdio: ['ignore', 'pipe', 'pipe'],
                env: sdlEnv,
            });

            this._sdlProcess.stdout.on('data', function (data) {
                var lines = data.toString().split('\n');
                for (var i = 0; i < lines.length; i++) {
                    if (lines[i].trim()) console.log('[sdl2] ' + lines[i].trim());
                }
            });

            this._sdlProcess.stderr.on('data', function (data) {
                var lines = data.toString().split('\n');
                for (var i = 0; i < lines.length; i++) {
                    if (lines[i].trim()) console.log('[sdl2] ' + lines[i].trim());
                }
            });

            this._sdlProcess.on('exit', function (code, signal) {
                console.log('[overlay] SDL2 binary exited (code=' + code + ', signal=' + signal + ')');
                self._sdlProcess = null;
                self._sdlSocket = null;
            });

            this._sdlProcess.on('error', function (err) {
                console.error('[overlay] SDL2 spawn error:', err.message);
                self._sdlProcess = null;
            });
        } catch (err) {
            console.error('[overlay] failed to spawn SDL2:', err.message);
            this._sdlProcess = null;
        }
    }

    _scheduleRestartSdl() {
        var self = this;
        if (this._sdlReconnectTimer) return;

        this._sdlReconnectTimer = setTimeout(function () {
            self._sdlReconnectTimer = null;
            if (!self._sdlProcess && self._currentScreen) {
                var binaryPath = self._findOverlayBinary();
                if (binaryPath) {
                    self._spawnSdl2(binaryPath, self._sdlAppMode);
                }
            }
        }, 2000);
    }

    _stopSdl2Backend() {
        if (this._sdlReconnectTimer) {
            clearTimeout(this._sdlReconnectTimer);
            this._sdlReconnectTimer = null;
        }
        if (this._sdlSocket) {
            try { this._sdlSocket.destroy(); } catch (_e) { /* */ }
            this._sdlSocket = null;
        }
        if (this._sdlProcess) {
            try { this._sdlProcess.kill('SIGTERM'); } catch (_e) { /* */ }
            this._sdlProcess = null;
        }
        if (this._sdlServer) {
            try { this._sdlServer.close(); } catch (_e) { /* */ }
            this._sdlServer = null;
        }
        try {
            if (existsSync(SOCKET_PATH)) unlinkSync(SOCKET_PATH);
        } catch (_e) { /* */ }
    }

    _sendSdl(message) {
        try {
            if (this._sdlSocket && !this._sdlSocket.destroyed) {
                // Strip qrSvg from SDL2 messages — the C binary can't render SVG
                // and the large SVG string overflows the 8KB read buffer
                var msg = message;
                if (msg.qrSvg) {
                    msg = Object.assign({}, msg);
                    delete msg.qrSvg;
                }
                this._sdlSocket.write(JSON.stringify(msg) + '\n');
            }
        } catch (err) {
            console.error('[overlay] SDL2 send error:', err.message);
        }
    }

    _findOverlayBinary() {
        // Look relative to this module, then in common install paths
        var __dirname;
        try {
            __dirname = dirname(fileURLToPath(import.meta.url));
        } catch (_e) {
            __dirname = '.';
        }

        var candidates = [
            '/app/bin/allow2-lock-overlay',  // Flatpak
            join(__dirname, '..', '..', 'allow2-lock-overlay', 'allow2-lock-overlay'),
            join(__dirname, '..', '..', '..', 'packages', 'allow2-lock-overlay', 'allow2-lock-overlay'),
            '/usr/lib/allow2/allow2-lock-overlay',
            '/usr/local/bin/allow2-lock-overlay',
        ];

        // Also check deployed path on Steam Deck
        var home = process.env.HOME || '';
        if (home) {
            candidates.push(join(home, 'allow2', 'allow2linux', 'packages', 'allow2-lock-overlay', 'allow2-lock-overlay'));
        }

        for (var i = 0; i < candidates.length; i++) {
            if (existsSync(candidates[i])) {
                console.log('[overlay] found SDL2 binary: ' + candidates[i]);
                return candidates[i];
            }
        }

        console.log('[overlay] SDL2 binary not found, checked: ' + candidates.join(', '));
        return null;
    }
}

// ── Mode detection ────────────────────────────────────────────

function _detectGameMode() {
    // Detect Game Mode: need BOTH gamescope AND Steam actually running.
    // gamescope-session can linger after switching to Desktop Mode,
    // so we must verify Steam is responsive too.
    var checks = [];
    var steamRunning = false;

    try {
        // Check if Steam process is running (required for steam:// URLs)
        try {
            execSync('pgrep -x steam', { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] });
            steamRunning = true;
            checks.push('steam-running');
        } catch (_e) { /* Steam not running */ }

        if (!steamRunning) {
            console.log('[overlay] gameMode detection: Steam not running → sdl2');
            return false;
        }

        // Check for gamescope (the Game Mode compositor)
        var gamescopeFound = false;

        try {
            execSync('pgrep -f gamescope-session', { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] });
            checks.push('gamescope-session');
            gamescopeFound = true;
        } catch (_e) { /* */ }

        if (!gamescopeFound) {
            try {
                execSync('pgrep -x gamescope', { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] });
                checks.push('gamescope-exact');
                gamescopeFound = true;
            } catch (_e) { /* */ }
        }

        if (!gamescopeFound) {
            try {
                var result = execSync('pgrep -af gamescope', { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }).trim();
                if (result && result.indexOf('pgrep') === -1) {
                    checks.push('gamescope-partial');
                    gamescopeFound = true;
                }
            } catch (_e) { /* */ }
        }

        // Also check for gamescope env vars
        if (process.env.GAMESCOPE_WAYLAND_DISPLAY) {
            checks.push('gamescope-wayland');
            gamescopeFound = true;
        }

    } catch (_e) {
        console.error('[overlay] mode detection error:', _e.message);
    }

    var gameMode = checks.length >= 2; // need steam + at least one gamescope indicator
    console.log('[overlay] gameMode detection: checks=[' + checks.join(',') + '] → ' + gameMode);
    return gameMode;
}

// ── Display environment discovery ────────────────────────────

function _discoverDisplayEnv() {
    var result = {};

    // Try to read from the active graphical session via loginctl
    try {
        var sessions = execSync(
            'loginctl list-sessions --no-legend --no-pager 2>/dev/null',
            { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }
        ).trim().split('\n');

        for (var i = 0; i < sessions.length; i++) {
            var parts = sessions[i].trim().split(/\s+/);
            var sid = parts[0];
            if (!sid) continue;

            try {
                var stype = execSync(
                    'loginctl show-session ' + sid + ' -p Type --value 2>/dev/null',
                    { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }
                ).trim();

                if (stype === 'wayland' || stype === 'x11') {
                    // Found a graphical session — read its DISPLAY/WAYLAND_DISPLAY
                    try {
                        var display = execSync(
                            'loginctl show-session ' + sid + ' -p Display --value 2>/dev/null',
                            { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }
                        ).trim();
                        if (display) result.DISPLAY = display;
                    } catch (_e) { /* */ }

                    // For Wayland sessions, try reading from the session leader's environment
                    if (stype === 'wayland') {
                        try {
                            var leader = execSync(
                                'loginctl show-session ' + sid + ' -p Leader --value 2>/dev/null',
                                { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }
                            ).trim();
                            if (leader) {
                                var env = execSync(
                                    'cat /proc/' + leader + '/environ 2>/dev/null',
                                    { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'], maxBuffer: 1024 * 64 }
                                );
                                var vars = env.split('\0');
                                for (var v = 0; v < vars.length; v++) {
                                    if (vars[v].indexOf('WAYLAND_DISPLAY=') === 0) {
                                        result.WAYLAND_DISPLAY = vars[v].split('=')[1];
                                    }
                                    if (vars[v].indexOf('XDG_RUNTIME_DIR=') === 0) {
                                        result.XDG_RUNTIME_DIR = vars[v].split('=')[1];
                                    }
                                    if (!result.DISPLAY && vars[v].indexOf('DISPLAY=') === 0) {
                                        result.DISPLAY = vars[v].split('=')[1];
                                    }
                                    if (!result.XAUTHORITY && vars[v].indexOf('XAUTHORITY=') === 0) {
                                        result.XAUTHORITY = vars[v].split('=')[1];
                                    }
                                }
                            }
                        } catch (_e) { /* */ }
                    }

                    break; // use first graphical session
                }
            } catch (_e) { /* */ }
        }
    } catch (_e) { /* */ }

    // Fallback: find XAUTHORITY from Mutter Xwayland auth files (KDE/GNOME on Wayland)
    if (!result.XAUTHORITY) {
        try {
            var xauthGlob = execSync(
                'ls /run/user/*/.[mM]utter-Xwaylandauth.* 2>/dev/null | head -1',
                { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }
            ).trim();
            if (xauthGlob) result.XAUTHORITY = xauthGlob;
        } catch (_e) { /* */ }
    }

    // Fallback: try common defaults
    if (!result.DISPLAY && !result.WAYLAND_DISPLAY) {
        result.DISPLAY = ':0';
    }
    if (!result.XDG_RUNTIME_DIR) {
        var uid;
        try {
            uid = execSync('id -u', { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }).trim();
        } catch (_e) { uid = '1000'; }
        result.XDG_RUNTIME_DIR = '/run/user/' + uid;
    }

    return result;
}

// ══════════════════════════════════════════════════════════════
// Embedded CSS + JS for Steam browser backend
// ══════════════════════════════════════════════════════════════

var OVERLAY_CSS = ''
    + '* { margin:0; padding:0; box-sizing:border-box; }'
    + 'body { background:#14141E; color:#fff; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;'
    + '  display:flex; align-items:center; justify-content:center; min-height:100vh; }'
    + '#app { text-align:center; padding:2rem; max-width:800px; width:100%; }'
    + '.pin-digits { font-size:4rem; letter-spacing:1.2rem; font-weight:700; color:#667eea; margin:1.5rem 0; }'
    + '.subtitle { color:#a0a0b0; font-size:1.1rem; margin:0.8rem 0; }'
    + '.child-list { display:flex; flex-wrap:wrap; gap:1.2rem; justify-content:center; margin:1.5rem 0; }'
    + '.child-btn { background:#2d3748; border:2px solid transparent; border-radius:12px; padding:1.2rem 2rem;'
    + '  color:#fff; font-size:1.1rem; cursor:pointer; min-width:140px; transition:all 0.2s; }'
    + '.child-btn:hover,.child-btn:focus { border-color:#667eea; background:#3d4758; outline:none; }'
    + '.child-btn .avatar { width:48px; height:48px; border-radius:50%; margin:0 auto 0.5rem;'
    + '  display:flex; align-items:center; justify-content:center; font-size:1.5rem; font-weight:700; }'
    + '.btn { background:#667eea; color:#fff; border:none; border-radius:8px; padding:0.8rem 2rem;'
    + '  font-size:1rem; cursor:pointer; margin:0.5rem; transition:all 0.2s; }'
    + '.btn:hover { background:#5a6fd6; }'
    + '.btn-secondary { background:#2d3748; }'
    + '.btn-secondary:hover { background:#3d4758; }'
    + '.lock-reason { font-size:2rem; font-weight:700; margin-bottom:0.5rem; }'
    + '.warning-bar { position:fixed; top:0; left:0; right:0; padding:0.8rem 1.5rem;'
    + '  display:flex; align-items:center; justify-content:space-between; z-index:999; }'
    + '.warning-bar.info { background:rgba(102,126,234,0.9); }'
    + '.warning-bar.urgent { background:rgba(246,173,85,0.9); }'
    + '.warning-bar.final,.warning-bar.countdown { background:rgba(252,129,129,0.9); }'
    + '.pin-input { display:flex; gap:0.8rem; justify-content:center; margin:1.5rem 0; }'
    + '.pin-dot { width:48px; height:48px; border-radius:50%; border:2px solid #667eea;'
    + '  display:flex; align-items:center; justify-content:center; font-size:1.5rem; background:#2d3748; }'
    + '.pin-dot.filled { background:#667eea; }'
    + '.pin-pad { display:grid; grid-template-columns:repeat(3,80px); gap:0.8rem; justify-content:center; margin:1rem 0; }'
    + '.pin-key { background:#2d3748; border:none; color:#fff; font-size:1.5rem; padding:1rem;'
    + '  border-radius:8px; cursor:pointer; transition:all 0.15s; }'
    + '.pin-key:hover { background:#3d4758; }'
    + '.duration-btns { display:flex; gap:1rem; justify-content:center; margin:1.5rem 0; }'
    + '.pending-dot { display:inline-block; width:10px; height:10px; border-radius:50%;'
    + '  background:#667eea; animation:pulse 1.5s ease-in-out infinite; }'
    + '@keyframes pulse { 0%,100%{opacity:0.4} 50%{opacity:1} }'
    + '.denied-msg { color:#fc8181; font-size:2rem; font-weight:700; }'
    + '.qr-placeholder { width:160px; height:160px; background:#2d3748; border-radius:12px;'
    + '  margin:1rem auto; display:flex; align-items:center; justify-content:center; color:#a0a0b0; }';

var OVERLAY_JS = ''
    + '(function(){'
    + 'var app=document.getElementById("app");'
    + 'var ws;'
    + 'var state=INITIAL_STATE;'
    + 'var pinDigits="";'
    + ''
    + 'var hbTimer=null;'
    + 'function connect(){'
    + '  ws=new WebSocket("ws://"+location.host);'
    + '  ws.onopen=function(){'
    + '    if(hbTimer)clearInterval(hbTimer);'
    + '    hbTimer=setInterval(function(){send({event:"heartbeat"});},500);'
    + '  };'
    + '  ws.onmessage=function(e){'
    + '    var msg=JSON.parse(e.data);'
    + '    if(msg.screen==="dismiss"){window.close();return;}'
    + '    if(msg.screen==="pin-result"){handlePinResult(msg);return;}'
    + '    if(msg.screen==="request-status"){handleRequestStatus(msg);return;}'
    + '    state=msg;render();'
    + '  };'
    + '  ws.onclose=function(){if(hbTimer){clearInterval(hbTimer);hbTimer=null;}setTimeout(connect,2000);};'
    + '}'
    + ''
    + 'window.addEventListener("beforeunload",function(){'
    + '  if(ws&&ws.readyState===1)ws.send(JSON.stringify({event:"page-closing"}));'
    + '});'
    + 'document.addEventListener("visibilitychange",function(){'
    + '  if(document.hidden&&ws&&ws.readyState===1)ws.send(JSON.stringify({event:"page-closing"}));'
    + '});'
    + ''
    + 'function send(msg){if(ws&&ws.readyState===1)ws.send(JSON.stringify(msg));}'
    + ''
    + 'function render(){'
    + '  if(!state||!state.screen){app.innerHTML="<p class=subtitle>Waiting...</p>";return;}'
    + '  switch(state.screen){'
    + '    case"pairing":renderPairing();break;'
    + '    case"selector":renderSelector();break;'
    + '    case"pin-entry":renderPin();break;'
    + '    case"lock":renderLock();break;'
    + '    case"warning":renderWarning();break;'
    + '    default:app.innerHTML="<p class=subtitle>"+state.screen+"</p>";'
    + '  }'
    + '}'
    + ''
    + 'function renderPairing(){'
    + '  var pin=state.pin||"------";'
    + '  var digits=pin.split("").join(" ");'
    + '  var qrHtml="<div class=qr-placeholder>QR</div>";'
    + '  if(state.qrSvg){'
    + '    qrHtml="<div style=\\"margin:1rem auto;width:200px;height:200px;background:#fff;border-radius:12px;display:flex;align-items:center;justify-content:center;overflow:hidden\\">"+state.qrSvg+"</div>";'
    + '  }'
    + '  app.innerHTML='
    + '    "<h1>Set Up Allow2</h1>"'
    + '    +qrHtml'
    + '    +"<p class=subtitle>"+(state.message||"Open the Allow2 app and enter this PIN")+"</p>"'
    + '    +"<div class=pin-digits>"+digits+"</div>"'
    + '    +"<p class=subtitle>PIN Code</p>";'
    + '}'
    + ''
    + 'function renderSelector(){'
    + '  var kids=state.children||[];'
    + '  var html="<h1>Who is playing?</h1><div class=child-list>";'
    + '  for(var i=0;i<kids.length;i++){'
    + '    var c=kids[i];'
    + '    var hue=(c.id*137)%360;'
    + '    var initial=c.name?c.name[0].toUpperCase():"?";'
    + '    html+="<button class=child-btn onclick=\\"selectChild("+c.id+")\\">"'
    + '      +"<div class=avatar style=\\"background:hsl("+hue+",60%,50%)\\">"+initial+"</div>"'
    + '      +c.name+"</button>";'
    + '  }'
    + '  html+="<button class=child-btn onclick=\\"selectParent()\\">"'
    + '    +"<div class=avatar style=\\"background:#4a5568\\">P</div>Parent</button>";'
    + '  html+="</div>";'
    + '  app.innerHTML=html;'
    + '}'
    + ''
    + 'window.selectChild=function(id){send({event:"child-selected",childId:id});};'
    + 'window.selectParent=function(){send({event:"parent-selected"});};'
    + ''
    + 'function renderPin(){'
    + '  var name=state.childName||"";'
    + '  var max=state.maxDigits||4;'
    + '  var dots="";'
    + '  for(var i=0;i<max;i++){'
    + '    dots+="<div class=\\"pin-dot"+(i<pinDigits.length?" filled":"")+"\\">"+('
    + '      i<pinDigits.length?"\\u25CF":"")+"</div>";'
    + '  }'
    + '  var pad="";'
    + '  for(var n=1;n<=9;n++)pad+="<button class=pin-key onclick=\\"pinKey("+n+"\\">"+n+"</button>";'
    + '  pad+="<button class=pin-key onclick=\\"pinClear()\\">C</button>";'
    + '  pad+="<button class=pin-key onclick=\\"pinKey(0)\\">0</button>";'
    + '  pad+="<button class=pin-key onclick=\\"pinBack()\\">\\u2190</button>";'
    + '  app.innerHTML='
    + '    "<h1>Enter PIN</h1>"'
    + '    +"<p class=subtitle>"+name+"</p>"'
    + '    +"<div class=pin-input>"+dots+"</div>"'
    + '    +"<div class=pin-pad>"+pad+"</div>";'
    + '}'
    + ''
    + 'window.pinKey=function(n){'
    + '  var max=state.maxDigits||4;'
    + '  if(pinDigits.length>=max)return;'
    + '  pinDigits+=n;'
    + '  render();'
    + '  if(pinDigits.length===max){'
    + '    var ev=state.isParent?"parent-pin-entered":"pin-entered";'
    + '    send({event:ev,childId:state.childId,pin:pinDigits});'
    + '    pinDigits="";'
    + '  }'
    + '};'
    + 'window.pinClear=function(){pinDigits="";render();};'
    + 'window.pinBack=function(){pinDigits=pinDigits.slice(0,-1);render();};'
    + ''
    + 'function handlePinResult(msg){'
    + '  if(msg.success){app.innerHTML="<h1>\\u2713</h1><p class=subtitle>Verified</p>";}'
    + '  else if(msg.lockedOut){app.innerHTML="<div class=denied-msg>Locked out</div>"'
    + '    +"<p class=subtitle>Try again in "+msg.lockoutSeconds+"s</p>";}'
    + '  else{app.innerHTML="<div class=denied-msg>Wrong PIN</div>"'
    + '    +"<p class=subtitle>"+msg.attemptsRemaining+" attempts remaining</p>";'
    + '    setTimeout(render,2000);}'
    + '}'
    + ''
    + 'function renderLock(){'
    + '  app.innerHTML='
    + '    "<div class=lock-reason>"+(state.reason||"Screen time is up")+"</div>"'
    + '    +"<p class=subtitle>"+(state.childName?state.childName+", your":"Your")+" daily screen time has been used up.</p>"'
    + '    +"<div style=\\"margin-top:2rem\\">"'
    + '    +"<button class=btn onclick=\\"requestTime()\\">Request More Time</button>"'
    + '    +"<button class=\\"btn btn-secondary\\" onclick=\\"switchChild()\\">Switch Child</button>"'
    + '    +"</div>";'
    + '}'
    + ''
    + 'window.requestTime=function(){'
    + '  app.innerHTML='
    + '    "<h1>Request More Time</h1>"'
    + '    +"<div class=duration-btns>"'
    + '    +"<button class=btn onclick=\\"doRequest(15)\\">15 min</button>"'
    + '    +"<button class=btn onclick=\\"doRequest(30)\\">30 min</button>"'
    + '    +"<button class=btn onclick=\\"doRequest(60)\\">1 hour</button>"'
    + '    +"</div>"'
    + '    +"<button class=\\"btn btn-secondary\\" onclick=\\"render()\\">\\u2190 Back</button>";'
    + '};'
    + ''
    + 'window.doRequest=function(mins){'
    + '  send({event:"request-more-time",activityId:state.activityId||0,duration:mins});'
    + '};'
    + ''
    + 'window.switchChild=function(){send({event:"switch-child"});};'
    + ''
    + 'function handleRequestStatus(msg){'
    + '  if(msg.status==="pending"){'
    + '    app.innerHTML="<p class=subtitle>Waiting for parent approval...</p>"'
    + '      +"<div class=pending-dot></div>";'
    + '  }else if(msg.status==="denied"){'
    + '    app.innerHTML="<div class=denied-msg>Request denied</div>";'
    + '    setTimeout(render,3000);'
    + '  }else if(msg.status==="approved"){'
    + '    app.innerHTML="<h1>Approved!</h1>";'
    + '  }'
    + '}'
    + ''
    + 'function renderWarning(){'
    + '  var secs=state.remaining||0;'
    + '  var time=secs>=60?Math.ceil(secs/60)+" min":secs+"s";'
    + '  var level=state.level||"info";'
    + '  document.body.style.background="transparent";'
    + '  app.style.textAlign="left";'
    + '  app.innerHTML='
    + '    "<div class=\\"warning-bar "+level+"\\">"'
    + '    +"<span>"+(state.activity||"")+" \\u2014 "+time+" remaining</span>"'
    + '    +(level!=="info"?"<button class=btn onclick=\\"doRequest(15)\\">Request More Time</button>":"")'
    + '    +"</div>";'
    + '}'
    + ''
    + 'connect();'
    + 'render();'
    + '})();';
