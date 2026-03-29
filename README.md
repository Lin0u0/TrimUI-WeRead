# TrimUI-WeRead

A native [WeChat Read (WeRead)](https://weread.qq.com) client for TrimUI handheld devices and macOS.

Read your WeRead library on the TrimUI Brick with a lightweight SDL2 interface -- QR code login, bookshelf browsing, paginated reader, and cloud progress sync.

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

## Installation

Download the latest release for your platform from the [Releases](https://github.com/Lin0u0/TrimUI-WeRead/releases) page.

### NextUI / MinUI

Extract `WeRead-nextui.tar.gz` to the root of your SD card. The `Tools/tg5040/WeRead.pak` folder should appear under `Tools/`.

### Stock OS

Extract `WeRead-stock-app.tar.gz` to the root of your SD card. The app appears under `Apps/WeRead/`.

### CrossMix-OS

Extract `WeRead-crossmix.tar.gz` to the root of your SD card. The app appears under `Apps/WeRead/`.

### macOS

Extract `WeRead-macos.tar.gz` and run `./run.sh` from inside the extracted folder.

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

Install an `aarch64-linux-gnu-gcc` cross-compiler, then bootstrap the SDK and dependencies:

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

## License

MIT
