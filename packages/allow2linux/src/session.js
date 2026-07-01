/**
 * Session Manager
 *
 * Manages Linux user sessions via loginctl (systemd-logind).
 *
 * IMPORTANT: allow2linux runs as a `systemd --user` service (and/or inside a
 * Flatpak sandbox). Such a process is NOT itself a login/graphical session, so
 * `loginctl lock-session` / `loginctl terminate-session` with no session id
 * (or an empty one) do nothing useful — they must be given the id of the
 * graphical session we want to act on. We therefore resolve the active
 * graphical session id first and pass it explicitly.
 */

import { execFile } from 'node:child_process';

export class SessionManager {

    /**
     * Lock the active graphical session (shows lock screen).
     * Desktop Mode only — Game Mode (gamescope) has no screen locker, so the
     * daemon handles that case by SIGSTOP-ing the game + showing the overlay.
     */
    async lock() {
        const id = await this._resolveSessionId();
        if (!id) {
            throw new Error('lock: could not resolve an active session id');
        }
        return this._exec('loginctl', ['lock-session', id]);
    }

    /**
     * Terminate the active graphical session (logs the user out).
     * This is the hard-lock enforcement escalation.
     */
    async terminate() {
        const id = await this._resolveSessionId();
        if (!id) {
            throw new Error('terminate: could not resolve an active session id');
        }
        return this._exec('loginctl', ['terminate-session', id]);
    }

    /**
     * Resolve the session id to act on. Order of preference:
     *   1. XDG_SESSION_ID env (set when the daemon inherits a login session)
     *   2. The active graphical session (Type=wayland|x11|mir) for this user
     *   3. Any active session for this user
     *
     * Returns null if nothing suitable is found.
     */
    async _resolveSessionId() {
        // 1. Environment (cheapest, works when launched from a login session)
        const envId = (process.env.XDG_SESSION_ID || '').trim();
        if (envId) {
            return envId;
        }

        // 2/3. Enumerate this user's sessions and pick the best candidate.
        let listing;
        try {
            // Columns: SESSION UID USER SEAT TTY (varies by systemd version) —
            // we only need the first column (session id).
            listing = await this._exec('loginctl', ['list-sessions', '--no-legend', '--no-pager']);
        } catch (_e) {
            return null;
        }

        const uid = String(process.getuid ? process.getuid() : '');
        const lines = listing.split('\n').map(function (l) { return l.trim(); }).filter(Boolean);

        let graphical = null;
        let anyActive = null;

        for (let i = 0; i < lines.length; i++) {
            const sid = lines[i].split(/\s+/)[0];
            if (!sid) continue;

            // Scope to our own uid where we can determine it.
            let sessionUid = null;
            let type = '';
            let state = '';
            try {
                const props = await this._exec('loginctl', [
                    'show-session', sid,
                    '-p', 'User', '-p', 'Type', '-p', 'State', '-p', 'Active',
                    '--value', '--no-pager',
                ]);
                // --value prints one value per requested property, in order.
                const vals = props.split('\n').map(function (v) { return v.trim(); });
                sessionUid = vals[0];
                type = (vals[1] || '').toLowerCase();
                state = (vals[2] || '').toLowerCase();
            } catch (_e) {
                continue;
            }

            if (uid && sessionUid && sessionUid !== uid) continue;

            const isActive = state === 'active' || state === 'online';
            if (type === 'wayland' || type === 'x11' || type === 'mir') {
                // Prefer an active graphical session; fall back to any graphical.
                if (isActive) return sid;
                if (!graphical) graphical = sid;
            }
            if (isActive && !anyActive) anyActive = sid;
        }

        return graphical || anyActive;
    }

    /**
     * Get the current session ID (best-effort). Retained for compatibility;
     * delegates to the robust resolver.
     */
    async getSessionId() {
        try {
            return await this._resolveSessionId();
        } catch (_e) {
            return null;
        }
    }

    _exec(command, args) {
        return new Promise(function (resolve, reject) {
            execFile(command, args, { timeout: 10000 }, function (err, stdout, _stderr) {
                if (err) {
                    reject(err);
                } else {
                    resolve(stdout);
                }
            });
        });
    }
}
