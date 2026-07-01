# Fedora / RHEL / openSUSE packaging (`.rpm`)

The `.rpm` is built by the same shared builder as the `.deb` — **one spec → both
formats** via `fpm`:

```bash
packaging/build-package.sh \
  --format rpm \
  --arch x86_64 \            # or aarch64
  --overlay path/to/prebuilt/allow2-lock-overlay \
  --version 1.0.0-alpha.1 \
  --out dist/
```

Output: `dist/allow2linux_<version>_<arch>.rpm`.

## What it installs
Identical layout to the `.deb` — see [`../build-package.sh`](../build-package.sh).

## Runtime dependencies (declared in the package)
`nodejs >= 18.0.0`, `SDL2`, `SDL2_ttf`, `libX11`.

## allow2 SDK resolution — FLAGGED
See [`../deb/README.md`](../deb/README.md#allow2-sdk-resolution--flagged) — same
published-vs-vendored decision, use `VENDOR_SDK=` to vendor.

## Building on Ubuntu CI
`fpm --output-type rpm` needs the `rpm` tool (`apt-get install -y rpm`) and Ruby.
The `package-rpm` job in `.github/workflows/release.yml` installs both. Building
an aarch64 `.rpm` on an x86_64 runner is fine — the payload is the pre-built
aarch64 overlay plus pure-JS daemon deps; fpm only stamps the arch metadata.
**[unverified-device]** — the real build runs on CI/Linux.
