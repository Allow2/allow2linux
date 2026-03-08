#!/usr/bin/env python3
"""
Register allow2-lock-overlay as a non-Steam game shortcut.
Steam will launch it, so gamescope composites it in Game Mode.

Usage: python3 register-steam-shortcut.py [overlay-binary-path] [socket-path]
"""

import struct
import os
import sys
import glob
import zlib

def make_shortcut_entry(app_name, exe_path, launch_options, app_id):
    """Build a single shortcut entry in Valve's binary VDF format."""
    entry = b''

    def add_string(key, val):
        return b'\x01' + key.encode() + b'\x00' + val.encode() + b'\x00'

    def add_int(key, val):
        return b'\x02' + key.encode() + b'\x00' + struct.pack('<I', val)

    entry += add_int('appid', app_id)
    entry += add_string('AppName', app_name)
    entry += add_string('Exe', exe_path)
    entry += add_string('StartDir', os.path.dirname(exe_path))
    entry += add_string('icon', '')
    entry += add_string('ShortcutPath', '')
    entry += add_string('LaunchOptions', launch_options)
    entry += add_int('IsHidden', 1)  # Hidden from library
    entry += add_int('AllowDesktopConfig', 1)
    entry += add_int('AllowOverlay', 1)
    entry += add_int('OpenVR', 0)
    entry += add_int('Devkit', 0)
    entry += add_string('DevkitGameID', '')
    entry += add_int('DevkitOverrideAppID', 0)
    entry += add_int('LastPlayTime', 0)
    entry += add_string('FlatpakAppID', '')
    # Tags sub-object (empty)
    entry += b'\x00tags\x00\x08'
    entry += b'\x08'  # End of entry

    return entry


def generate_app_id(exe_path, app_name):
    """Generate a stable non-Steam app ID from exe path and name."""
    key = exe_path + app_name
    crc = zlib.crc32(key.encode()) & 0xFFFFFFFF
    # Non-Steam app IDs use top bit set
    return crc | 0x80000000


def find_userdata_dir():
    """Find the Steam userdata config directory."""
    patterns = [
        os.path.expanduser('~/.steam/steam/userdata/*/config'),
        os.path.expanduser('~/.local/share/Steam/userdata/*/config'),
    ]
    for pattern in patterns:
        dirs = glob.glob(pattern)
        if dirs:
            return dirs[0]
    return None


def read_existing_shortcuts(path):
    """Read existing shortcuts.vdf to preserve other entries."""
    if not os.path.exists(path):
        return b''
    with open(path, 'rb') as f:
        return f.read()


def has_allow2_shortcut(data):
    """Check if Allow2 overlay is already registered."""
    return b'Allow2 Overlay' in data


def write_shortcuts_vdf(path, exe_path, socket_path):
    """Write or update shortcuts.vdf with the Allow2 overlay entry."""
    existing = read_existing_shortcuts(path)

    if has_allow2_shortcut(existing):
        print('Allow2 overlay shortcut already registered')
        return True

    app_id = generate_app_id(exe_path, 'Allow2 Overlay')
    launch_opts = '--socket ' + socket_path

    entry = make_shortcut_entry('Allow2 Overlay', exe_path, launch_opts, app_id)

    if existing:
        # Existing file: insert our entry before the final \x08\x08
        # Strip trailing terminators
        base = existing.rstrip(b'\x08')
        # Find highest index
        idx = 0
        for i in range(100):
            tag = b'\x00' + str(i).encode() + b'\x00'
            if tag in base:
                idx = i + 1

        new_entry = b'\x00' + str(idx).encode() + b'\x00' + entry
        data = base + new_entry + b'\x08\x08'
    else:
        # New file
        data = b'\x00shortcuts\x00'
        data += b'\x00' + b'0' + b'\x00' + entry
        data += b'\x08\x08'

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)

    print('Registered Allow2 overlay as non-Steam shortcut (appid=%u)' % app_id)
    return True


def main():
    overlay_path = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.expanduser('~/allow2/allow2linux/packages/allow2-lock-overlay/allow2-lock-overlay')
    socket_path = sys.argv[2] if len(sys.argv) > 2 else '/tmp/allow2-overlay.sock'

    if not os.path.exists(overlay_path):
        print('ERROR: overlay binary not found at ' + overlay_path)
        sys.exit(1)

    config_dir = find_userdata_dir()
    if not config_dir:
        print('ERROR: Steam userdata directory not found')
        sys.exit(1)

    vdf_path = os.path.join(config_dir, 'shortcuts.vdf')
    print('Overlay: ' + overlay_path)
    print('VDF: ' + vdf_path)

    if write_shortcuts_vdf(vdf_path, overlay_path, socket_path):
        print('Done. Restart Steam for the shortcut to take effect.')
    else:
        print('Failed to write shortcuts.vdf')
        sys.exit(1)


if __name__ == '__main__':
    main()
