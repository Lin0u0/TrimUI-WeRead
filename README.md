# TrimUI-WeRead

A native [WeChat Read (WeRead)](https://weread.qq.com) client for TrimUI handheld devices and macOS.

Read your WeRead library on the TrimUI Brick with a lightweight SDL2 interface -- QR code login, bookshelf browsing, paginated reader, and cloud progress sync.

## Overview

TrimUI-WeRead is a lightweight native reader for people who want to carry their WeRead library onto dedicated handheld hardware instead of staying inside a phone app or browser tab.

The project is built around a simple but complete reading loop:

- Sign in with a WeRead QR code
- Keep the session on device
- Browse your bookshelf with cover art
- Open a book in a paginated reader designed for buttons instead of touch gestures
- Jump between chapters, change font size, and continue where you left off
- Sync progress back to WeRead so the same title stays aligned across devices

The main target is the TrimUI Brick running NextUI, MinUI-style setups, Stock OS, or CrossMix-OS. A macOS build is also included so the app can be developed and tested on desktop.

## Features

- QR code login with persistent sessions
- Bookshelf with cover art
- Paginated reader with chapter navigation and font size control
- Reading progress synced to WeRead cloud
- Runs on TrimUI Brick (NextUI, Stock OS, CrossMix-OS) and macOS

## Supported Platforms

| Platform | Device | Notes |
|----------|--------|-------|
| **NextUI / MinUI** | TrimUI Brick (TG5040) | Primary target |
| **Stock OS** | TrimUI Brick (TG5040) | Official firmware |
| **CrossMix-OS** | TrimUI Brick (TG5040) | Community firmware |
| **macOS** | Desktop | Development and testing |

## First Release

Version `0.1.0` is the first public release of TrimUI-WeRead.

This release is meant to be a real, installable reading package rather than a technology preview. The focus of the first version is reliability, straightforward installation, and a reading experience that already feels useful on actual handheld hardware.

What this release brings together:

- End-to-end login flow with persistent sessions
- Native bookshelf browsing on device
- Paginated reading with hardware-friendly navigation
- Chapter switching and font size control
- Cloud progress synchronization back to WeRead
- Installable packages for all currently supported platforms
- A dedicated NextUI `.pakz` package for automatic import

## Installation

Download the latest release for your platform from the [Releases](https://github.com/Lin0u0/TrimUI-WeRead/releases) page.

### NextUI / MinUI

For NextUI, the recommended install is `WeRead.pakz`: copy it to the root of your SD card, reinsert the card, and let NextUI install it automatically.

For manual extraction, `WeRead-nextui.tar.gz` also works: extract it to the root of your SD card so `Tools/tg5040/WeRead.pak` appears under `Tools/`.

### Stock OS

Extract `WeRead-stock-app.tar.gz` to the root of your SD card. The app appears under `Apps/WeRead/`.

### CrossMix-OS

Extract `WeRead-crossmix.tar.gz` to the root of your SD card. The app appears under `Apps/WeRead/`.

### macOS

Extract `WeRead-macos.tar.gz` and run `./run.sh` from inside the extracted folder.

## Notes

- The project is currently centered on the TrimUI Brick (`tg5040`) family of environments.
- This release focuses on the core reading path first: login, bookshelf, reader, and progress sync.
- Release assets are split by platform so each system can use the package that matches its folder layout and launcher conventions.

## Building from Source

### macOS (native)

Install dependencies via Homebrew:

```sh
brew install sdl2 sdl2_ttf sdl2_image curl
```

Build and package:

```sh
make macos-release
```

### TrimUI (cross-compilation)

Install a compatible AArch64 Linux cross-compiler, then bootstrap the SDK and dependencies:

```sh
make tg5040-bootstrap
```

Build packages:

```sh
make nextui-release    # NextUI / MinUI
make stock-release     # Stock OS
make crossmix-release  # CrossMix-OS
make package-all       # All platforms
```

See [BUILD.md](BUILD.md) for details.

## Release Notes

Detailed `0.1.0` release notes are available in [CHANGELOG.md](CHANGELOG.md).

## License

MIT
