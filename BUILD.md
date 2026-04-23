# Build Guide

This document covers the build and packaging boundary only. For module ownership and protected runtime names, see [MAINTAINER_BOUNDARIES.md](MAINTAINER_BOUNDARIES.md).

## Assets

This repo expects the shared runtime assets to live in:

- `assets/icons/weread.png`
- `assets/icons/weread-icontop.png`
- `assets/cacert.pem`

## Package Matrix

| Maintainer command | Underlying target | Outputs | Install root |
|---|---|---|---|
| `make nextui-release` | `nextui-release` | `dist/WeRead-nextui.tar.gz`, `dist/WeRead.pakz` | `Tools/tg5040/WeRead.pak` |
| `make stock-release` | `stock-release` | `dist/WeRead-stock-app.tar.gz` | `Apps/WeRead` |
| `make crossmix-release` | `crossmix-release` | `dist/WeRead-crossmix.tar.gz` | `Apps/WeRead` |
| `make package-all` | `package-all` | Builds all distributable archives | All package layouts above |

## TG5040 prerequisites

1. Install an `aarch64-linux-gnu-gcc` cross compiler.
2. Run `make tg5040-bootstrap`
   - Downloads the official TrimUI SDK userland into `build/tg5040-sdk`
   - Builds a static `libcurl` into `third_party/tg5040/curl`

After that, `make nextui-release`, `make stock-release`, `make crossmix-release`, or `make package-all` can build the TrimUI targets end-to-end.

## Launcher Runtime Differences

| Surface | NextUI / MinUI-style | Stock OS | CrossMix-OS |
|---|---|---|---|
| Data root | `$SHARED_USERDATA_PATH/WeRead` | `SDCARD/Data/WeRead` | `/mnt/SDCARD/Data/WeRead` |
| Log path | `$LOGS_PATH/WeRead.txt` | `SDCARD/Data/WeRead/logs/launch.txt` | `/mnt/SDCARD/Data/WeRead/logs/launch.txt` |
| Runtime env highlights | Exports `PATH`, `LD_LIBRARY_PATH`, and `CURL_CA_BUNDLE` | Sets `HOME` and `LD_LIBRARY_PATH` | Sets `HOME`, extends `LD_LIBRARY_PATH` with `/mnt/SDCARD/System/lib`, and exports `CURL_CA_BUNDLE` |
| Binary launch behavior | Executes `weread` directly from the package | Copies `weread` to `/tmp/weread` before launch because the SD card is vfat | Copies `weread` to `/tmp/weread` before launch |

Notes:

- NextUI runs the packaged binary in place and logs through `LOGS_PATH`, so missing log output usually points to launcher env setup rather than `weread` itself.
- Stock OS uses `SDCARD/Data/WeRead` as the runtime home, logs under `logs/launch.txt`, and depends on the `/tmp/weread` copy behavior to avoid executing directly from vfat.
- CrossMix-OS uses `/mnt/SDCARD/Data/WeRead`, logs under `logs/launch.txt`, and adds `/mnt/SDCARD/System/lib` to `LD_LIBRARY_PATH` on top of the app-local libraries.

## Common Failure Points

- Missing `assets/icons/weread.png` breaks every package target that stages the shared icon asset.
- Missing `assets/icons/weread-icontop.png` breaks the Stock OS package, which is the only package that stages the top icon asset.
- Missing `assets/cacert.pem` breaks packaged TLS setup because launchers point `CURL_CA_BUNDLE` at the packaged CA bundle.
- Missing `zip` breaks NextUI packaging because `dist/WeRead.pakz` is created with the external `zip` command.
- Missing `libgcc_s.so.1` breaks package staging because the Makefile copies that runtime library into each package layout.
- Missing `aarch64-none-linux-gnu-gcc` or `aarch64-linux-gnu-gcc` breaks tg5040 builds before packaging can begin.
- Unprepared tg5040 dependencies break all package targets until `make tg5040-bootstrap` has populated the SDK userland and static curl tree.
- Stock OS and CrossMix-OS launchers always copy the binary to `/tmp/weread`, so debugging the packaged path directly can hide runtime behavior differences.
- Launcher-specific surprises usually come from `LOGS_PATH`, `/tmp/weread`, or platform-specific library paths rather than from the shared `weread` command surface.

## Boundary Notes

- `Makefile` is the canonical entry point for build, package, and release layout decisions.
- `packaging/nextui`, `packaging/stock`, and `packaging/crossmix` own launcher-facing package structure.
- The runtime binary name remains `weread`.
- The persisted runtime data names documented in `README.md` and `MAINTAINER_BOUNDARIES.md` should stay stable unless a later phase adds explicit migration work.

## Verification Contract

CLI host gates:

- `make doctor-release`
- `make test-host`
- `make test-smoke`

Package and launcher gates:

- `make test-package-audit-smoke`
- `make package-all`
- `make package-audit-all`

Advisory checks:

- Manual tg5040 smoke for login, shelf refresh, reader open/resume, and progress sync on real hardware
- Optional `make tg5040` when maintainers want a binary-only compile check without building archives

Recommended Phase 05 sign-off flow:

1. Run `make doctor-release`
2. Run `make test-host`
3. Run `make test-smoke`
4. Run `make test-package-audit-smoke`
5. Run `make package-all`
6. Run `make package-audit-all`
7. Run optional real-device smoke on tg5040 hardware before tagging

GitHub Actions `workflow_dispatch` now runs the same release verification sequence without publishing a tag, while tag pushes still publish the release artifacts after the same hard gates pass.
