# Debian / Ubuntu packaging (`.deb`)

The `.deb` is built by the shared, format-agnostic builder — **one spec → both
formats** via `fpm` — so there is no separate deb-only spec to drift:

```bash
packaging/build-package.sh \
  --format deb \
  --arch x86_64 \            # or aarch64
  --overlay path/to/prebuilt/allow2-lock-overlay \
  --version 1.0.0-alpha.1 \
  --out dist/
```

Output: `dist/allow2linux_<version>_<arch>.deb`.

## What it installs
See the header of [`../build-package.sh`](../build-package.sh) for the full
layout. In short: daemon → `/usr/lib/allow2linux`, overlay + assets →
`/usr/lib/allow2`, production-locked launcher → `/usr/bin/allow2linux`, systemd
`--user` unit → `/usr/lib/systemd/user`, plus desktop/metainfo/icons.

## Runtime dependencies (declared in the package)
`nodejs (>= 18.0.0)`, `libsdl2-2.0-0`, `libsdl2-ttf-2.0-0`, `libx11-6`.

## allow2 SDK resolution — FLAGGED
Same open decision as the Flatpak path (`flatpak/flathub/node-sources.json`):
`package.json` pins the published `allow2@^2.0.0-alpha.1`, but the monorepo
lockfile resolves `file:../../../sdk/node`. The builder `npm install`s from the
registry by default; if the SDK is not yet published, vendor it with
`VENDOR_SDK=/abs/path/to/sdk/node`. Decide published-vs-vendored once, apply to
Flatpak + deb + rpm identically.

## CI
Built per-arch by the `package-deb` job in `.github/workflows/release.yml` on a
semver `v*.*.*` tag. **[unverified-device]** — the real build runs on CI/Linux.
