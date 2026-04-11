# TrimUI-WeRead

A native [WeRead](https://weread.qq.com) client for TrimUI handhelds.

It brings the core WeRead flow to button-driven devices: QR code login, bookshelf browsing, paginated reading, chapter navigation, local resume state, and cloud progress sync.

## What It Does

TrimUI-WeRead is aimed at the TrimUI Brick (`tg5040`) family first.

Current capabilities:

- WeRead QR code login with persistent local session
- Bookshelf browsing with cached cover art
- Paginated reader tuned for hardware buttons
- Chapter catalog and quick resume
- Reader font size adjustment, dark mode, and screen rotation
- Reading progress sync back to WeRead cloud
- Packaged builds for NextUI / MinUI-style setups, Stock OS, and CrossMix-OS

## Supported Platforms

| Platform | Device | Notes |
|----------|--------|-------|
| NextUI / MinUI-style launchers | TrimUI Brick (`tg5040`) | Primary target |
| Stock OS | TrimUI Brick (`tg5040`) | Official firmware layout |
| CrossMix-OS | TrimUI Brick (`tg5040`) | Community firmware layout |
## Installation

Download the latest release from [Releases](https://github.com/Lin0u0/TrimUI-WeRead/releases).

### NextUI / MinUI

Recommended:

- Copy `WeRead.pakz` to the root of the SD card
- Reinsert the SD card and let NextUI import it automatically

Manual install:

- Extract `WeRead-nextui.tar.gz` to the SD card root
- Confirm the app ends up at `Tools/tg5040/WeRead.pak`

### Stock OS

- Extract `WeRead-stock-app.tar.gz` to the SD card root
- The app will be available under `Apps/WeRead/`

### CrossMix-OS

- Extract `WeRead-crossmix.tar.gz` to the SD card root
- The app will be available under `Apps/WeRead/`

## First Run

On first launch, open the login flow and scan the generated QR code with WeRead on your phone. After login succeeds, the app keeps its cookies and reader state locally so later launches can go straight back to the shelf or the last book.

Runtime data is stored per platform:

- NextUI: `$SHARED_USERDATA_PATH/WeRead`
- Stock OS: `SDCARD/Data/WeRead`
- CrossMix-OS: `/mnt/SDCARD/Data/WeRead`

This data includes:

- `cookies.txt`
- `state/shelf.json`
- `state/last-reader.json`
- `state/reader-positions.json`
- `state/preferences.json`
- launch logs on TrimUI builds

These filenames are part of the current runtime contract and should stay stable unless compatibility work is added on purpose.

## Controls

### TrimUI

- `D-pad`: move selection, turn pages, navigate catalog
- `A`: confirm, open book, next page
- `B`: back, close overlays, previous page in some contexts
- `X`: resume the last opened book from the shelf
- `Y`: open settings from shelf or reader
- `L1/R1`: page up and page down in catalog / reader
- `Power/Lock`: suspend the device from inside the app

Reader font size, dark mode, brightness, and rotation now live inside the in-app settings page.

`Menu`, `Select`, and `Start` are intentionally left to the system so firmware hotkeys keep working.

## Command-Line Usage

The binary also supports non-UI commands:

```sh
weread [--data DIR] [--font FILE] [--platform NAME] [--cafile FILE] [command]
```

Available commands:

- `login [qr.png]`
- `shelf`
- `shelf-cache`
- `reader <reader-url-or-bc> [font-size]`
- `resume`
- `ui`

Notes:

- With no explicit command, the program starts `ui` when a font is available.
- Without SDL UI support, the default falls back to `shelf`.
- `shelf-cache` prints the cached shelf when network access is unavailable.

## Maintainer Map

The repo keeps a flat `src/` layout, so ownership is easier to follow by file family than by folder depth:

- `src/ui.c`: SDL scheduler, render composition, and view switching
- `src/ui_flow_startup_login.c`, `src/ui_flow_shelf.c`, `src/ui_flow_reader.c`: UI-side flow ownership
- `src/ui_reader_view.c`: reader-view pagination, page offsets, local-position, and progress helpers
- `src/parser_common.c`, `src/parser_internal.h`: shared parser primitives
- `src/state.c`, `src/state.h`: persisted runtime state
- `Makefile` and `packaging/`: build outputs and launcher packaging

The full maintainer-facing ownership and protected-name reference is in [MAINTAINER_BOUNDARIES.md](MAINTAINER_BOUNDARIES.md).

## Maintainer Docs

- [MAINTAINER_GUIDE.md](MAINTAINER_GUIDE.md): main internal handoff entrypoint for onboarding, common maintenance tasks, verification flow, and frequent failure paths.
- [MAINTAINER_BOUNDARIES.md](MAINTAINER_BOUNDARIES.md): source ownership map and protected runtime-contract reference.
- [BUILD.md](BUILD.md): build, package, artifact, and release-surface reference.

## Building from Source

### Required assets

The repo expects these runtime assets:

- `assets/icons/weread.png`
- `assets/icons/weread-icontop.png`
- `assets/cacert.pem`

### TrimUI cross-build

Install an AArch64 Linux cross compiler first. Then bootstrap the TrimUI SDK userland and static `libcurl`:

```sh
make tg5040-bootstrap
```

Build packages:

```sh
make nextui-release
make stock-release
make crossmix-release
make package-all
```

Useful helper targets:

```sh
make tg5040
make print-config
make clean
make clean-tg5040
make clean-dist
```

Outputs:

- `dist/WeRead-nextui.tar.gz`
- `dist/WeRead.pakz`
- `dist/WeRead-stock-app.tar.gz`
- `dist/WeRead-crossmix.tar.gz`

More build details are in [BUILD.md](BUILD.md).

## Verification

Phase 03 separates required host checks from broader confirmation work.

Required host gates:

- `make test-host`
- `make test-smoke`

Advisory checks:

- `make tg5040`
- `make nextui-release stock-release crossmix-release`
- Manual tg5040 smoke for login, shelf refresh, reader open/resume, and progress sync

The smoke script intentionally stays thin and host-native. It verifies stable CLI-visible paths like `--help`, cached shelf output, and resume command behavior without turning device validation into automation in this phase.

## Known Scope

This project currently focuses on the core reading path:

- login
- bookshelf
- reader
- resume state
- progress sync

It is intentionally centered on TrimUI Brick packaging and launcher conventions rather than being a generic Linux WeRead client.

## Release Notes

Detailed release notes are available in [CHANGELOG.md](CHANGELOG.md).

## License

MIT
