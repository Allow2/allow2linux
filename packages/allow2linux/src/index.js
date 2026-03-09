#!/usr/bin/env node

/**
 * allow2linux — Allow2 Parental Freedom for Linux devices.
 *
 * Lifecycle:
 *   1. Unpaired  → dormant, no overlays. Wait for user to open Allow2 app.
 *   2. Pairing   → user opened app, show QR + PIN. Dismissible.
 *   3. Paired    → identify child (OS username match or interactive selector)
 *   4. Enforcing → check permissions, warn, block, lock
 *   5. Parent    → unrestricted, no enforcement
 */

import { DeviceDaemon, PlaintextBackend, resolveLinuxUser } from 'allow2';
import { ProcessClassifier } from './process-classifier.js';
import { SteamMonitor } from './steam.js';
import { DesktopNotifier } from './desktop-notify.js';
import { SessionManager } from './session.js';
import { OverlayBridge } from './overlay-bridge.js';
import { execSync } from 'node:child_process';
import { readFileSync, appendFileSync, mkdirSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';

const classifier = new ProcessClassifier();
const steam = new SteamMonitor();
const notifier = new DesktopNotifier();
const session = new SessionManager();
const overlay = new OverlayBridge();

const credentialBackend = new PlaintextBackend();

const daemon = new DeviceDaemon({
    deviceName: _getDeviceName(),
    activities: [{ id: 8 }, { id: 3 }, { id: 1 }, { id: 6 }],
    checkInterval: 60,
    credentialBackend: credentialBackend,
    childResolver: resolveLinuxUser,
    gracePeriod: 5 * 60,
    pairingPort: 3000,
    vid: parseInt(process.env.ALLOW2_VID, 10) || 21599,
    token: process.env.ALLOW2_TOKEN || 'x9AUeUPpiweHTNCR',
});

// --- Overlay events (from SDL2 binary) ---

overlay.on('child-selected', function (data) {
    daemon.selectChild(data.childId);
});

overlay.on('pin-entered', function (data) {
    // TODO: verify PIN via ChildShield, send result back to overlay
    // overlay.sendPinResult({ success, attemptsRemaining, lockedOut, lockoutSeconds })
});

overlay.on('parent-selected', function () {
    overlay.showPinEntry({ childId: 0, childName: 'Parent', isParent: true });
});

overlay.on('parent-pin-verified', function () {
    daemon.enterParentMode();
});

overlay.on('request-more-time', function (data) {
    daemon.requestMoreTime({
        duration: data.duration,
        activity: data.activityId,
    }).then(function (result) {
        overlay.showRequestStatus('pending');
        // Start polling for approval
        var pollCount = 0;
        var pollTimer = setInterval(function () {
            pollCount++;
            if (pollCount > 120) { // 10 min timeout
                clearInterval(pollTimer);
                overlay.showRequestStatus('denied');
                return;
            }
            daemon.pollRequestStatus(result.requestId, result.statusSecret).then(function (status) {
                if (status && status.status === 'approved') {
                    clearInterval(pollTimer);
                    overlay.showRequestStatus('approved');
                } else if (status && status.status === 'denied') {
                    clearInterval(pollTimer);
                    overlay.showRequestStatus('denied');
                }
            }).catch(function () { /* poll error, retry */ });
        }, 5000);
    }).catch(function (err) {
        console.error('Request More Time failed:', err.message);
    });
});

overlay.on('app-opened', function () {
    console.log('[app] User opened Allow2 app');
    daemon.openApp();
});

overlay.on('switch-child', function () {
    daemon.sessionTimeout();
});

overlay.on('report-issue', function () {
    if (daemon.canSubmitFeedback) {
        overlay.showFeedbackScreen();
    }
});

overlay.on('submit-feedback', function (data) {
    daemon.submitFeedback({
        category: data.category,
        message: data.message,
        deviceContext: {
            platform: 'linux',
            productName: 'allow2linux',
        },
    }).then(function (result) {
        console.log('[feedback] Submitted successfully');
        // Return to status screen
        daemon.openApp();
    }).catch(function (err) {
        console.error('[feedback] Submission failed:', err.message);
        // Return to status screen anyway
        daemon.openApp();
    });
});

overlay.on('feedback-cancel', function () {
    // Return to status screen
    daemon.openApp();
});

// --- Pairing events (Step 1) ---

daemon.on('pairing-required', function (info) {
    console.log('Device not paired. PIN: ' + info.pin);
    // Show pairing screen in the SDL2 overlay so the user sees the PIN/QR code
    // qrData is the deep link URL for the Allow2 app (or app store / website fallback)
    overlay.showPairingScreen({
        pin: info.pin,
        qrData: info.qrUrl || ('https://app.allow2.com/pair?pin=' + info.pin),
        pairingUrl: 'http://localhost:' + (info.port || 3000),
    });
});

daemon.on('paired', function (data) {
    console.log('Device paired! userId=' + data.userId + ', children=' + (data.children ? data.children.length : 0));
    overlay.dismiss();
    notifier.notify('Device paired with Allow2. Parental Freedom is now active.', 'info');
});

daemon.on('status-requested', function (data) {
    console.log('[app] Showing status screen');
    var isParent = daemon.isParentMode;

    // Find current child name
    var childName = '';
    var children = data.children || [];
    if (data.currentChildId) {
        for (var i = 0; i < children.length; i++) {
            var c = children[i];
            if ((c.id || c.childId) === data.currentChildId) {
                childName = c.name || c.firstName || '';
                break;
            }
        }
    }

    // Transform remaining from { activityId: { allowed, remaining } }
    // to [ { name, remaining } ] for the overlay
    var activities = [];
    if (data.remaining) {
        var activityIds = Object.keys(data.remaining);
        for (var j = 0; j < activityIds.length; j++) {
            var actId = activityIds[j];
            var info = data.remaining[actId];
            activities.push({
                name: classifier.getActivityName(Number(actId)),
                remaining: info.remaining || 0,
            });
        }
    }

    overlay.showStatusScreen({
        state: data.state,
        family: '', // TODO: populate from credentials when available
        childName: childName,
        currentChildId: data.currentChildId,
        remaining: activities,
        isParent: isParent,
        canSubmitFeedback: daemon.canSubmitFeedback,
    });
});

daemon.on('pairing-error', function (err) {
    console.error('Pairing error:', err.message);
});

// --- Child identification events (Step 2) ---

daemon.on('child-select-required', function (data) {
    var children = data.children || [];
    console.log('Child selection required. ' + children.length + ' children available.');

    if (children.length === 0) {
        console.log('No children configured. Device runs in parent mode (unrestricted).');
        return;
    }

    overlay.showChildSelector(children);
});

daemon.on('child-selected', function (data) {
    overlay.dismiss();
    console.log('Child selected: ' + (data.name || 'unknown') + ' (id=' + data.childId + ')');
});

daemon.on('parent-mode', function () {
    console.log('Parent mode — no restrictions');
    overlay.dismiss();
});

daemon.on('session-timeout', function () {
    console.log('Session timed out, re-identifying child...');
});

// --- Warning events (Step 3) ---

daemon.on('warning', function (data) {
    var minutes = Math.ceil(data.remaining / 60);
    var activityName = classifier.getActivityName(data.activity);
    var msg = activityName + ': ' + minutes + ' minute' + (minutes !== 1 ? 's' : '') + ' remaining';

    // Show overlay warning bar (semi-transparent top bar)
    overlay.showWarning({
        activity: activityName,
        activityId: data.activity,
        remaining: data.remaining,
        level: data.level,
    });

    // Also send desktop/Steam notification
    if (steam.isRunning()) {
        steam.notify(msg, data.level);
    } else {
        notifier.notify(msg, data.level);
    }
});

// --- Enforcement events ---

daemon.on('activity-blocked', async function (data) {
    var activityName = classifier.getActivityName(data.activity);
    var pids = await classifier.getProcessesForActivity(data.activity);

    console.log(activityName + ' blocked. Terminating ' + pids.length + ' processes.');

    for (var i = 0; i < pids.length; i++) {
        try { process.kill(pids[i], 'SIGTERM'); } catch (_e) { /* already dead */ }
    }

    // Force kill after 10 seconds
    setTimeout(function () {
        for (var j = 0; j < pids.length; j++) {
            try { process.kill(pids[j], 0); process.kill(pids[j], 'SIGKILL'); } catch (_e) { /* gone */ }
        }
    }, 10000);

    notifier.notify(activityName + ' time is up.', 'blocked');
});

daemon.on('soft-lock', async function (data) {
    console.log('Soft lock: ' + (data.reason || 'Screen time exhausted'));
    var gamePid = steam.getActiveGamePid();
    if (gamePid) {
        try { process.kill(gamePid, 'SIGSTOP'); } catch (_e) { /* */ }
    }
    await overlay.showLockScreen({ reason: data.reason, childId: data.childId });
});

daemon.on('unlock', async function () {
    console.log('Unlocked');
    var gamePid = steam.getStoppedGamePid();
    if (gamePid) {
        try { process.kill(gamePid, 'SIGCONT'); } catch (_e) { /* */ }
    }
    await overlay.dismiss();
});

daemon.on('hard-lock', async function () {
    console.log('Hard lock — terminating session');
    await overlay.dismiss();
    await steam.killActiveGame();
    await session.terminate();
});

// --- Request events ---

daemon.on('request-approved', async function () {
    await overlay.dismiss();
});

daemon.on('request-denied', async function () {
    await overlay.showDenied();
});

// --- Offline events ---

daemon.on('offline-grace', function () {
    notifier.notify('Checking connection...', 'info');
});

daemon.on('offline-deny', async function () {
    await overlay.showLockScreen({ reason: 'No internet connection. Device locked.' });
});

// --- Unpaired event (HTTP 401 during enforcement) ---

daemon.on('unpaired', function () {
    console.log('Device unpaired by server (HTTP 401). Going dormant.');
    overlay.dismiss();
    // Daemon already stopped enforcement internally
});

// --- Logging ---

var _logDir = join(homedir(), '.allow2');
try { mkdirSync(_logDir, { recursive: true }); } catch (_e) { /* */ }
var _logFile = join(_logDir, 'allow2linux.log');

function _log(msg) {
    var line = new Date().toISOString() + ' ' + msg;
    console.log(line);
    try { appendFileSync(_logFile, line + '\n'); } catch (_e) { /* */ }
}

function _logError(msg) {
    var line = new Date().toISOString() + ' ERROR ' + msg;
    console.error(line);
    try { appendFileSync(_logFile, line + '\n'); } catch (_e) { /* */ }
}

// Catch unhandled errors so the process doesn't die silently
process.on('uncaughtException', function (err) {
    _logError('Uncaught exception: ' + (err.stack || err.message || err));
});

process.on('unhandledRejection', function (reason) {
    _logError('Unhandled rejection: ' + (reason && reason.stack ? reason.stack : reason));
});

// --- Start ---

_log('allow2linux starting...');

// Start overlay, then daemon. Overlay failure is non-fatal — daemon can still
// pair via the SDK's HTTP pairing wizard (port 3000) without the SDL2 overlay.
overlay.start().then(function () {
    _log('Overlay started');
}).catch(function (err) {
    _logError('Overlay failed to start: ' + (err.message || err) + ' — continuing without overlay');
});

// Start daemon independently — don't chain on overlay
daemon.start().then(function () {
    _log('Daemon started');
    // Auto-open the app so the user sees a window immediately.
    // If unpaired → shows pairing screen. If paired → shows status.
    daemon.openApp();
}).catch(function (err) {
    _logError('Daemon failed to start: ' + (err.message || err));
    process.exit(1);
});

// Graceful shutdown
process.on('SIGTERM', function () {
    _log('Shutting down (SIGTERM)...');
    daemon.stop();
    overlay.stop();
    process.exit(0);
});

process.on('SIGINT', function () {
    _log('Shutting down (SIGINT)...');
    daemon.stop();
    overlay.stop();
    process.exit(0);
});

// --- Helpers ---

function _isGameMode() {
    // gamescope is Steam Deck's Gaming Mode compositor
    try {
        execSync('pgrep -x gamescope', { encoding: 'utf8' });
        return true;
    } catch (_err) {
        return false;
    }
}

function _getDeviceName() {
    var hostname;
    try {
        hostname = execSync('hostname', { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }).trim();
    } catch (_err) {
        try {
            hostname = readFileSync('/etc/hostname', 'utf8').trim();
        } catch (_err2) {
            hostname = '';
        }
    }

    // Detect Steam Deck specifically
    var isSteamDeck = false;
    try {
        var board = readFileSync('/sys/devices/virtual/dmi/id/board_name', 'utf8').trim();
        if (board === 'Jupiter' || board === 'Galileo') {
            isSteamDeck = true;
        }
    } catch (_e) { /* not available */ }

    if (isSteamDeck) {
        // Include hostname only if the user customized it
        if (hostname && hostname !== 'steamdeck' && hostname !== 'localhost') {
            return hostname + ' (Steam Deck)';
        }
        return 'Steam Deck';
    }

    return hostname || 'Linux PC';
}
