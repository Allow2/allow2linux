/**
 * First-run setup.
 *
 * Ensures the daemon survives reboots WITHOUT an interactive graphical login,
 * which is the whole point of a parental-control agent on a shared device.
 * Two things are required and were previously left as manual steps:
 *
 *   1. `loginctl enable-linger <user>` — starts the user's systemd manager at
 *      boot, so `systemd --user` services run before (and without) anyone
 *      logging into a graphical session. Linger state lives under /var and is
 *      NOT reset by SteamOS updates.
 *   2. The `systemd --user` unit installed + enabled.
 *
 * This runs once (guarded by a marker file) and is entirely best-effort: any
 * failure is logged and non-fatal — the daemon still runs for the current
 * session, and the manual installer (scripts/install-service.sh) is the
 * reliable fallback documented in docs/BETA_DELIVERY.md.
 *
 * Sandbox awareness: inside Flatpak the host's systemd user manager is not
 * directly reachable, so host commands are routed through `flatpak-spawn
 * --host`. If that portal is unavailable the step is skipped with a clear
 * instruction to run the manual installer.
 */

import { execFile } from 'node:child_process';
import { existsSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { homedir, userInfo } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const MARKER = join(homedir(), '.allow2', '.setup-done');

function _run(command, args) {
    return new Promise(function (resolve) {
        execFile(command, args, { timeout: 15000 }, function (err, stdout, stderr) {
            resolve({ ok: !err, stdout: stdout || '', stderr: stderr || '', err: err });
        });
    });
}

function _inFlatpak() {
    return !!process.env.FLATPAK_ID || existsSync('/.flatpak-info');
}

/**
 * Build a runner that executes on the host. Inside Flatpak we prefix with
 * `flatpak-spawn --host`; otherwise we run directly.
 */
function _hostRunner() {
    if (_inFlatpak()) {
        return function (cmd, args) {
            return _run('flatpak-spawn', ['--host', cmd].concat(args));
        };
    }
    return _run;
}

/**
 * The canonical unit contents for the current layout.
 * Flatpak → `flatpak run <app-id>`. Direct Node → resolved node + index.js.
 */
function _unitContents() {
    const flatpakId = process.env.FLATPAK_ID || 'com.allow2.allow2linux';
    let execStart;
    if (_inFlatpak()) {
        execStart = '/usr/bin/flatpak run ' + flatpakId;
    } else {
        // Resolve the actual node binary + this package's entry point so the
        // dev/direct install has a correct ExecStart (no hardcoded ~/.allow2).
        const nodeBin = process.execPath;
        const here = dirname(fileURLToPath(import.meta.url)); // .../src
        const indexJs = join(here, 'index.js');
        execStart = nodeBin + ' ' + indexJs;
    }

    return [
        '[Unit]',
        'Description=Allow2 Parental Freedom for Linux',
        'Documentation=https://github.com/Allow2/allow2linux',
        'After=network-online.target graphical-session.target',
        'Wants=network-online.target',
        '',
        '[Service]',
        'Type=simple',
        'ExecStart=' + execStart,
        'Restart=always',
        'RestartSec=5',
        'TimeoutStopSec=10',
        '',
        '[Install]',
        'WantedBy=default.target',
        '',
    ].join('\n');
}

async function _lingerEnabled(user) {
    // Fast path: the linger marker file under /var (host path). Inside Flatpak
    // /var is the host's, so this check is still valid.
    if (existsSync('/var/lib/systemd/linger/' + user)) {
        return true;
    }
    return false;
}

/**
 * @param {(msg: string) => void} [log]
 * @returns {Promise<{ linger: string, service: string }>} status per step
 */
export async function ensureFirstRunSetup(log) {
    const logFn = typeof log === 'function' ? log : function () {};
    const status = { linger: 'skipped', service: 'skipped' };

    // Idempotency guard.
    try {
        if (existsSync(MARKER)) {
            return status;
        }
    } catch (_e) { /* proceed */ }

    const user = (userInfo().username) || process.env.USER || '';
    const host = _hostRunner();

    // ── 1. Linger ────────────────────────────────────────────────
    try {
        if (await _lingerEnabled(user)) {
            status.linger = 'already-enabled';
        } else {
            const r = await host('loginctl', ['enable-linger', user]);
            if (r.ok) {
                status.linger = 'enabled';
                logFn('[first-run] enable-linger succeeded for ' + user);
            } else {
                status.linger = 'failed';
                logFn('[first-run] enable-linger failed: ' + (r.stderr || (r.err && r.err.message) || 'unknown')
                    + ' — run: loginctl enable-linger ' + user);
            }
        }
    } catch (e) {
        status.linger = 'error';
        logFn('[first-run] enable-linger error: ' + (e.message || e));
    }

    // ── 2. Install + enable the user unit ────────────────────────
    // Inside Flatpak this must target the HOST user manager. We can only write
    // the host unit file via flatpak-spawn (sh -c), and reliability there is
    // device-specific; the manual installer remains the documented fallback.
    try {
        const unit = _unitContents();
        if (_inFlatpak()) {
            // Write the unit on the host then reload/enable, all via the portal.
            const script =
                'set -e; ' +
                'mkdir -p "$HOME/.config/systemd/user"; ' +
                'cat > "$HOME/.config/systemd/user/allow2linux.service" <<\'A2UNIT\'\n' +
                unit +
                'A2UNIT\n' +
                'systemctl --user daemon-reload; ' +
                'systemctl --user enable allow2linux.service';
            const r = await host('sh', ['-c', script]);
            if (r.ok) {
                status.service = 'installed';
                logFn('[first-run] user service installed + enabled (via flatpak-spawn)');
            } else {
                status.service = 'failed';
                logFn('[first-run] service install via flatpak-spawn failed — '
                    + 'run scripts/install-service.sh in Desktop Mode. Detail: '
                    + (r.stderr || (r.err && r.err.message) || 'unknown'));
            }
        } else {
            // Direct install into the real user home.
            const unitDir = join(homedir(), '.config', 'systemd', 'user');
            mkdirSync(unitDir, { recursive: true });
            writeFileSync(join(unitDir, 'allow2linux.service'), unit);
            const reload = await _run('systemctl', ['--user', 'daemon-reload']);
            const enable = await _run('systemctl', ['--user', 'enable', 'allow2linux.service']);
            if (reload.ok && enable.ok) {
                status.service = 'installed';
                logFn('[first-run] user service installed + enabled');
            } else {
                status.service = 'failed';
                logFn('[first-run] systemctl enable failed: '
                    + (enable.stderr || reload.stderr || 'unknown'));
            }
        }
    } catch (e) {
        status.service = 'error';
        logFn('[first-run] service install error: ' + (e.message || e));
    }

    // Only write the marker if BOTH steps reached a terminal good/known state.
    // If something genuinely failed we leave the marker off so the next launch
    // retries (e.g. linger requires a polkit prompt that will be granted later).
    try {
        mkdirSync(dirname(MARKER), { recursive: true });
        const done = (status.linger === 'enabled' || status.linger === 'already-enabled')
            && (status.service === 'installed');
        if (done) {
            writeFileSync(MARKER, new Date().toISOString() + '\n');
        }
    } catch (_e) { /* non-fatal */ }

    return status;
}
