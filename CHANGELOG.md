# Changelog

## 0.1.4

`0.1.4` improves reading flow responsiveness and prepares the codebase for faster iteration.

### Highlights

- Added confirm-to-exit behavior when leaving the reader, reducing accidental returns to the shelf
- Smoothed catalog open/close motion and selection movement for a more polished navigation feel
- Preloaded nearby shelf covers more aggressively to reduce visible loading while browsing
- Continued splitting reader and UI internals into dedicated modules to simplify future maintenance

### Notes

This release focuses on interaction polish rather than new platform support. The biggest user-facing changes are in the reader and shelf browsing experience, while the internal refactors reduce duplication and move catalog, platform, and parsing helpers into clearer modules.

## 0.1.0

`0.1.0` is the first public release of TrimUI-WeRead.

This version turns the project into a complete reading package instead of a prototype. The goal of the first release is clear: let a user sign in to WeRead, browse their existing library, open books comfortably on TrimUI hardware, and keep reading progress synchronized with the cloud.

### Highlights

- Native WeRead reading experience for TrimUI handheld devices
- QR code login with persistent local session support
- Bookshelf browsing with cover art
- Paginated reader with chapter navigation and font size control
- WeRead cloud progress synchronization
- Platform packages for NextUI, Stock OS, CrossMix-OS, and macOS
- NextUI `.pakz` release asset for automatic installation

### Included Packages

- `WeRead.pakz`
  NextUI automatic installer package. Copy it to the SD card root and let NextUI import it.
- `WeRead-nextui.tar.gz`
  Manual extraction package for NextUI and MinUI-style setups.
- `WeRead-stock-app.tar.gz`
  Package for the TrimUI stock operating system.
- `WeRead-crossmix.tar.gz`
  Package for CrossMix-OS.
- `WeRead-macos.tar.gz`
  Native macOS build for development and testing.

### Core Experience

This first version is built around the main handheld reading loop:

- Authenticate once with WeRead QR login
- Reopen the app without repeating the login flow every session
- Browse your bookshelf directly on-device
- Open a title and read through discrete pages with hardware controls
- Navigate chapters without leaving the reading context
- Adjust text size for screen comfort
- Push progress back to WeRead so reading state stays consistent

### Release Scope

The intent of `0.1.0` is to establish a stable baseline:

- A native SDL2-based app that runs on the TrimUI Brick target environment
- A packaging pipeline that can produce installable archives for all supported platforms
- A release process that can publish GitHub artifacts for end users

This release is the foundation for future improvements in UI polish, packaging convenience, compatibility, and long-session reading comfort.

### Current Focus

The first release prioritizes:

- Fast startup into a usable bookshelf
- Predictable button-based navigation
- Lightweight packaging and simple installation
- Compatibility across the main TrimUI software variants

### Upgrade Direction

This baseline makes room for later work in areas such as:

- Better onboarding and visual presentation
- More refined reader interactions for longer sessions
- Platform-specific polish and packaging improvements
- More complete release automation and distribution metadata
