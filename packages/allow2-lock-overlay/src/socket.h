/**
 * socket.h — Unix domain socket client for allow2-lock-overlay
 *
 * Non-blocking line-buffered reads for newline-delimited JSON.
 * Automatic reconnection every 2 seconds on disconnect.
 */

#ifndef SOCKET_H
#define SOCKET_H

#include <stddef.h>

/* Connect to the daemon Unix domain socket at the given path.
 * Returns 0 on success, -1 on failure (caller should retry). */
int socket_connect(const char *path);

/* Read one newline-delimited line from the socket (non-blocking).
 * Stores the line (without trailing newline) in buf, up to maxlen-1 chars.
 * Returns: number of chars read (>0) if a complete line is available,
 *          0 if no complete line yet,
 *         -1 on error / disconnect. */
int socket_read_line(char *buf, int maxlen);

/* Write data to the socket. Returns 0 on success, -1 on error. */
int socket_write(const char *data, int len);

/* Close the socket connection. */
void socket_disconnect(void);

/* Returns 1 if currently connected, 0 otherwise. */
int socket_is_connected(void);

/* Attempt reconnection if disconnected and enough time has passed (2s).
 * Call this periodically from the main loop.
 * Returns 1 if (re)connected, 0 otherwise. */
int socket_try_reconnect(const char *path);

#endif /* SOCKET_H */
