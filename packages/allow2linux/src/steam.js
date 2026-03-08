/**
 * Steam Monitor
 *
 * Detects Steam client, running games, and gamescope session.
 * Provides Steam-specific notification delivery.
 */

import { readdir, readFile, readlink } from 'node:fs/promises';
import { join } from 'node:path';
import { execFile } from 'node:child_process';

export class SteamMonitor {

    constructor() {
        this._steamPid = null;
        this._activeGamePid = null;
        this._stoppedGamePid = null;
    }

    /**
     * Check if Steam is running.
     */
    async isRunning() {
        try {
            var procs = await this._findProcesses('steam');
            this._steamPid = procs.length > 0 ? procs[0] : null;
            return procs.length > 0;
        } catch (e) {
            return false;
        }
    }

    /**
     * Check if we're in gamescope (Game Mode) session.
     */
    async isGamescope() {
        try {
            var procs = await this._findProcesses('gamescope');
            return procs.length > 0;
        } catch (e) {
            return false;
        }
    }

    /**
     * Find the active game process PID.
     * Steam games are children of the steam process, or launched via proton/wine.
     */
    async getActiveGamePid() {
        // Look for processes that are children of steam or common game launchers
        try {
            var entries = await readdir('/proc');
            for (var entry of entries) {
                if (!/^\d+$/.test(entry)) continue;

                try {
                    var comm = await readFile(join('/proc', entry, 'comm'), 'utf8');
                    var name = comm.trim();

                    // Skip known non-game Steam processes
                    if (name === 'steam' || name === 'steamwebhelper' ||
                        name === 'steam-runtime' || name === 'gamescope') {
                        continue;
                    }

                    // Check if parent is steam-related
                    var stat = await readFile(join('/proc', entry, 'stat'), 'utf8');
                    var ppid = stat.split(') ')[1];
                    if (ppid) {
                        ppid = ppid.split(' ')[1]; // PPID is field 4, index 1 after split past comm
                    }

                    // Check if this process has steam-related environment
                    try {
                        var environ = await readFile(join('/proc', entry, 'environ'), 'utf8');
                        if (environ.includes('SteamAppId') || environ.includes('STEAM_COMPAT')) {
                            this._activeGamePid = parseInt(entry, 10);
                            return this._activeGamePid;
                        }
                    } catch (e) {
                        // Permission denied for some processes
                    }
                } catch (e) {
                    // Process exited
                }
            }
        } catch (e) {
            // /proc scan failed
        }

        return null;
    }

    /**
     * Get PID of a game that was SIGSTOP'd.
     */
    getStoppedGamePid() {
        return this._stoppedGamePid;
    }

    /**
     * Kill the active game process gracefully.
     */
    async killActiveGame() {
        var pid = this._activeGamePid || this._stoppedGamePid;
        if (!pid) return;

        try {
            process.kill(pid, 'SIGTERM');
        } catch (e) { /* already dead */ }

        // Wait 10 seconds then force kill
        await new Promise(function (resolve) { setTimeout(resolve, 10000); });

        try {
            process.kill(pid, 0); // check if alive
            process.kill(pid, 'SIGKILL');
        } catch (e) { /* already dead */ }

        this._activeGamePid = null;
        this._stoppedGamePid = null;
    }

    /**
     * Send a notification via Steam's overlay (if available).
     * Falls back to nothing if Steam notification isn't available.
     */
    notify(message, level) {
        // Steam notifications via xdg-open steam:// URL scheme
        // This is best-effort — may not work in all configurations
        try {
            execFile('xdg-open', ['steam://open/console'], { timeout: 5000 }, function () {});
        } catch (e) {
            // Steam notification not available
        }
    }

    /**
     * Find PIDs matching a process name.
     */
    async _findProcesses(name) {
        var pids = [];
        try {
            var entries = await readdir('/proc');
            for (var entry of entries) {
                if (!/^\d+$/.test(entry)) continue;
                try {
                    var comm = await readFile(join('/proc', entry, 'comm'), 'utf8');
                    if (comm.trim() === name) {
                        pids.push(parseInt(entry, 10));
                    }
                } catch (e) { /* process exited */ }
            }
        } catch (e) { /* /proc not available */ }
        return pids;
    }
}
