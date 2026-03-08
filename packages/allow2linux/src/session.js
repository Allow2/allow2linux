/**
 * Session Manager
 *
 * Manages Linux user sessions via loginctl (systemd-logind).
 */

import { execFile } from 'node:child_process';

export class SessionManager {

    /**
     * Lock the current session (shows lock screen).
     */
    async lock() {
        return this._exec('loginctl', ['lock-session']);
    }

    /**
     * Terminate the current session (logs user out).
     */
    async terminate() {
        return this._exec('loginctl', ['terminate-session', '']);
    }

    /**
     * Get the current session ID.
     */
    async getSessionId() {
        try {
            var result = await this._exec('loginctl', ['show-session', '--property=Id', '--value']);
            return result.trim();
        } catch (e) {
            return null;
        }
    }

    _exec(command, args) {
        return new Promise(function (resolve, reject) {
            execFile(command, args, { timeout: 10000 }, function (err, stdout, stderr) {
                if (err) {
                    reject(err);
                } else {
                    resolve(stdout);
                }
            });
        });
    }
}
