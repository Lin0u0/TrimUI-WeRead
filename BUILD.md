# Build Guide

## Assets

This repo expects the shared runtime assets to live in:

- `assets/fonts/SourceHanSerifSC-Regular.otf`
- `assets/icons/weread.png`

## Outputs

- `make macos-release`
  - Builds the native macOS test package at `dist/WeRead-macos.tar.gz`
- `make nextui-release`
  - Builds the NextUI packages at `dist/WeRead-nextui.tar.gz` and `dist/WeRead.pakz`
- `make stock-release`
  - Builds the TrimUI stock OS app package at `dist/WeRead-stock-app.tar.gz`
- `make package-all`
  - Builds all three distributable archives

## TG5040 prerequisites

1. Install an `aarch64-linux-gnu-gcc` cross compiler.
2. Run `make tg5040-bootstrap`
   - Downloads the official TrimUI SDK userland into `build/tg5040-sdk`
   - Builds a static `libcurl` into `third_party/tg5040/curl`

After that, `make nextui-release`, `make stock-release`, or `make package-all` can build the TrimUI targets end-to-end.
