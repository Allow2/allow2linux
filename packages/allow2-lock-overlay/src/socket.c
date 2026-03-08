/**
 * socket.c — Unix domain socket client for allow2-lock-overlay
 */

#include "socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

/* Internal state */
static int sock_fd = -1;

/* Line buffer for accumulating partial reads */
#define READ_BUF_SIZE 8192
static char read_buf[READ_BUF_SIZE];
static int read_buf_len = 0;

/* Reconnect timer */
static time_t last_reconnect_attempt = 0;
#define RECONNECT_INTERVAL_SEC 2

int socket_connect(const char *path) {
    struct sockaddr_un addr;
    int fd;

    if (!path) {
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "[overlay] socket(): %s\n", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[overlay] connect(%s): %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    /* Set non-blocking for reads */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    sock_fd = fd;
    read_buf_len = 0;

    fprintf(stderr, "[overlay] connected to %s (fd=%d)\n", path, fd);
    return 0;
}

int socket_read_line(char *buf, int maxlen) {
    if (sock_fd < 0 || !buf || maxlen < 2) {
        return -1;
    }

    /* First check if we already have a complete line in the buffer */
    char *nl = memchr(read_buf, '\n', read_buf_len);
    if (!nl) {
        /* Try to read more data */
        int space = READ_BUF_SIZE - read_buf_len - 1;
        if (space <= 0) {
            /* Buffer full with no newline — discard (malformed input) */
            fprintf(stderr, "[overlay] read buffer overflow, discarding\n");
            read_buf_len = 0;
            return 0;
        }

        int n = read(sock_fd, read_buf + read_buf_len, space);
        if (n > 0) {
            read_buf_len += n;
        } else if (n == 0) {
            /* EOF — daemon closed connection */
            fprintf(stderr, "[overlay] daemon disconnected (EOF)\n");
            socket_disconnect();
            return -1;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* No data available right now */
                return 0;
            }
            fprintf(stderr, "[overlay] read error: %s\n", strerror(errno));
            socket_disconnect();
            return -1;
        }

        nl = memchr(read_buf, '\n', read_buf_len);
    }

    if (!nl) {
        /* Still no complete line */
        return 0;
    }

    /* Extract the line */
    int line_len = (int)(nl - read_buf);
    int copy_len = line_len;
    if (copy_len >= maxlen) {
        copy_len = maxlen - 1;
    }
    memcpy(buf, read_buf, copy_len);
    buf[copy_len] = '\0';

    /* Remove the line (including newline) from the buffer */
    int consumed = line_len + 1;
    int remaining = read_buf_len - consumed;
    if (remaining > 0) {
        memmove(read_buf, read_buf + consumed, remaining);
    }
    read_buf_len = remaining;

    return copy_len;
}

int socket_write(const char *data, int len) {
    if (sock_fd < 0 || !data || len <= 0) {
        return -1;
    }

    int total = 0;
    while (total < len) {
        int n = write(sock_fd, data + total, len - total);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Brief spin — socket buffer should drain quickly */
                continue;
            }
            fprintf(stderr, "[overlay] write error: %s\n", strerror(errno));
            return -1;
        }
        total += n;
    }
    return 0;
}

void socket_disconnect(void) {
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }
    read_buf_len = 0;
}

int socket_is_connected(void) {
    return (sock_fd >= 0) ? 1 : 0;
}

int socket_try_reconnect(const char *path) {
    if (sock_fd >= 0) {
        return 1;  /* Already connected */
    }

    time_t now = time(NULL);
    if (now - last_reconnect_attempt < RECONNECT_INTERVAL_SEC) {
        return 0;  /* Too soon */
    }
    last_reconnect_attempt = now;

    fprintf(stderr, "[overlay] attempting reconnect to %s...\n", path);
    if (socket_connect(path) == 0) {
        return 1;
    }
    return 0;
}
