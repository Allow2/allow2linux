#!/bin/sh
# postinstall — runs as root after the .deb/.rpm unpacks its files.
#
# Enables the systemd --user service for ALL users (applied at their next login;
# `systemctl --global` writes the enable symlinks under /etc/systemd/user/). Per
# user linger is enabled on first launch by the daemon, or manually.
set -eu

# Enable the user service globally (best-effort; no-op on system-less containers).
if command -v systemctl >/dev/null 2>&1; then
    systemctl --global enable allow2linux.service 2>/dev/null || true
fi

# Refresh desktop + icon caches (best-effort).
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications 2>/dev/null || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t /usr/share/icons/hicolor 2>/dev/null || true
fi

cat <<'EOF'

allow2linux installed (production channel).

Start it now for your user:
  systemctl --user daemon-reload
  systemctl --user enable --now allow2linux.service
  loginctl enable-linger "$USER"

Then launch the app and pair with the Allow2 phone app (scan the QR or enter the
6-digit PIN). The daemon also self-installs its user service + linger on first
launch — see docs/DISTRIBUTION.md.
EOF

exit 0
