# Build Guide

## Assets

This repo expects the shared runtime assets to live in:

- `assets/icons/weread.png`
- `assets/icons/weread-icontop.png`
- `assets/cacert.pem`

## Outputs

- `make nextui-release`
  - Builds the NextUI packages at `dist/WeRead-nextui.tar.gz` and `dist/WeRead.pakz`
- `make stock-release`
  - Builds the TrimUI stock OS app package at `dist/WeRead-stock-app.tar.gz`
- `make crossmix-release`
  - Builds the CrossMix-OS app package at `dist/WeRead-crossmix.tar.gz`
- `make package-all`
  - Builds all TrimUI distributable archives

## TG5040 prerequisites

1. Install an `aarch64-linux-gnu-gcc` cross compiler.
2. Run `make tg5040-bootstrap`
   - Downloads the official TrimUI SDK userland into `build/tg5040-sdk`
   - Builds a static `libcurl` into `third_party/tg5040/curl`

After that, `make nextui-release`, `make stock-release`, `make crossmix-release`, or `make package-all` can build the TrimUI targets end-to-end.
