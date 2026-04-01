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
- Stock OS / CrossMix-OS: `SDCARD/Data/WeRead`

This data includes:

- `cookies.txt`
- `state/shelf.json`
- `state/last-reader.json`
- `state/reader-positions.json`
- `state/preferences.json`
- launch logs on TrimUI builds

## Controls

### TrimUI

- `D-pad`: move selection, turn pages, navigate catalog
- `A`: confirm, open book, next page
- `B`: back, close overlays, previous page in some contexts
- `X`: resume the last opened book from the shelf
- `Y`: cycle reader font size
- `L1/R1`: page up and page down in catalog / reader
- `L3`: toggle dark mode
- `R3`: rotate the UI
- `Power/Lock`: suspend the device from inside the app

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
