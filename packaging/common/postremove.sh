#!/bin/sh
# postremove — runs as root after the .deb/.rpm removes its files.
# Disables the globally-enabled user service. Per-user copies written by the
# daemon's first-run (~/.config/systemd/user/allow2linux.service) and linger are
# left in place — removing them would require touching every user's home.
set -eu

if command -v systemctl >/dev/null 2>&1; then
    systemctl --global disable allow2linux.service 2>/dev/null || true
fi

exit 0
