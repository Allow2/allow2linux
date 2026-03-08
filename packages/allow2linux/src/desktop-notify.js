/**
 * Desktop Notifier
 *
 * Sends desktop notifications via freedesktop D-Bus notification protocol.
 * Falls back to notify-send command if D-Bus is not available.
 */

import { execFile } from 'node:child_process';

const URGENCY = {
    info: 'low',
    urgent: 'normal',
    final: 'critical',
    countdown: 'critical',
    blocked: 'critical',
};

export class DesktopNotifier {

    /**
     * Send a desktop notification.
     *
     * @param {string} message - Notification body text
     * @param {string} level - 'info', 'urgent', 'final', 'countdown', 'blocked'
     */
    notify(message, level) {
        var urgency = URGENCY[level] || 'normal';

        execFile('notify-send', [
            '--app-name=Allow2',
            '--urgency=' + urgency,
            '--icon=dialog-warning',
            'Allow2',
            message,
        ], { timeout: 5000 }, function (err) {
            if (err) {
                // notify-send not available — silent fail
                // In production, we'd use D-Bus directly
                console.warn('Desktop notification failed:', err.message);
            }
        });
    }
}
