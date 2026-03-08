/**
 * Process Classifier
 *
 * Scans /proc to identify running processes and maps them to
 * Allow2 activity IDs based on a configurable process list.
 */

import { readdir, readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = fileURLToPath(new URL('.', import.meta.url));

// Activity IDs
const ACTIVITIES = {
    INTERNET: 1,
    GAMING: 3,
    SOCIAL: 6,
    SCREEN_TIME: 8,
};

const ACTIVITY_NAMES = {
    1: 'Internet',
    3: 'Gaming',
    6: 'Social',
    8: 'Screen Time',
};

// Default process → activity mappings
const DEFAULT_MAPPINGS = {
    // Gaming (3)
    'steam': ACTIVITIES.GAMING,
    'steamwebhelper': ACTIVITIES.GAMING,
    'lutris': ACTIVITIES.GAMING,
    'retroarch': ACTIVITIES.GAMING,
    'dosbox': ACTIVITIES.GAMING,
    'wine': ACTIVITIES.GAMING,
    'wine64': ACTIVITIES.GAMING,
    'proton': ACTIVITIES.GAMING,
    'mangohud': ACTIVITIES.GAMING,
    'gamemode': ACTIVITIES.GAMING,

    // Internet (1)
    'firefox': ACTIVITIES.INTERNET,
    'firefox-esr': ACTIVITIES.INTERNET,
    'chromium': ACTIVITIES.INTERNET,
    'chromium-browser': ACTIVITIES.INTERNET,
    'google-chrome': ACTIVITIES.INTERNET,
    'brave-browser': ACTIVITIES.INTERNET,
    'brave': ACTIVITIES.INTERNET,
    'epiphany': ACTIVITIES.INTERNET,
    'vivaldi': ACTIVITIES.INTERNET,
    'opera': ACTIVITIES.INTERNET,
    'midori': ACTIVITIES.INTERNET,

    // Social (6)
    'discord': ACTIVITIES.SOCIAL,
    'Discord': ACTIVITIES.SOCIAL,
    'telegram-desktop': ACTIVITIES.SOCIAL,
    'signal-desktop': ACTIVITIES.SOCIAL,
    'slack': ACTIVITIES.SOCIAL,
    'element-desktop': ACTIVITIES.SOCIAL,
    'teams': ACTIVITIES.SOCIAL,
    'zoom': ACTIVITIES.SOCIAL,
    'whatsapp-desktop': ACTIVITIES.SOCIAL,
};

export class ProcessClassifier {

    constructor(customMappings) {
        this.mappings = { ...DEFAULT_MAPPINGS };
        if (customMappings) {
            Object.assign(this.mappings, customMappings);
        }
    }

    /**
     * Load custom mappings from a JSON file.
     */
    async loadMappings(filePath) {
        try {
            const data = await readFile(filePath, 'utf8');
            var custom = JSON.parse(data);
            Object.assign(this.mappings, custom);
        } catch (err) {
            // File not found or invalid — use defaults
        }
    }

    /**
     * Get the human-readable name for an activity ID.
     */
    getActivityName(activityId) {
        return ACTIVITY_NAMES[activityId] || 'Activity ' + activityId;
    }

    /**
     * Scan /proc for running processes and return which activities are active.
     * @returns {Map<number, number[]>} Map of activityId → [pid, pid, ...]
     */
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

        // Screen Time is always active if ANY process is running (child is logged in)
        if (!result.has(ACTIVITIES.SCREEN_TIME)) {
            result.set(ACTIVITIES.SCREEN_TIME, []);
        }

        return result;
    }

    /**
     * Get PIDs for processes matching a specific activity.
     */
    async getProcessesForActivity(activityId) {
        var activities = await this.getActiveActivities();
        return activities.get(activityId) || [];
    }

    /**
     * Scan /proc for process names and PIDs.
     */
    async _scanProc() {
        var results = [];

        try {
            var entries = await readdir('/proc');
            for (var entry of entries) {
                // Only numeric directories (PIDs)
                if (!/^\d+$/.test(entry)) continue;

                try {
                    var comm = await readFile(join('/proc', entry, 'comm'), 'utf8');
                    results.push({
                        pid: parseInt(entry, 10),
                        name: comm.trim(),
                    });
                } catch (e) {
                    // Process may have exited between readdir and readFile
                }
            }
        } catch (e) {
            console.error('Failed to scan /proc:', e.message);
        }

        return results;
    }
}
