# Changelog

## 0.1.9

`0.1.9` rebuilds catalog navigation around a fixed center cursor and hardens chapter and article transitions so browsing no longer drifts into the wrong entry or a blank reader page.

### Highlights

- Reworked catalog navigation so opening lands the current chapter on the center line, single taps and held repeat both keep the selected row locked to the middle, and background catalog hydration preserves the item you are actively browsing
- Made catalog confirm resolve the intended chapter more defensively: selected entries are validated against chapter ids and indexes, mismatched API targets fall back to the same prev/next chapter chain used by natural paging, and failed loads now stay on the current chapter instead of blanking the reader
- Made reader document adoption transactional and reject empty-content pages, preventing white screens when a target resolves to a shell page or malformed response
- Fixed shelf and article opening plumbing so article selections resolve through their real reader targets, while article HTML stripping now treats figures and images as block separators and keeps paragraph indentation after inline images
- Moved tg5040 haptic stop timing onto a worker thread so pulses no longer block the render loop, and added host coverage for mixed article/book shelf indexing plus article-with-image reader fixtures

### Notes

This release is mostly about navigation integrity and interaction feel. The visible changes are the centered catalog and more reliable chapter and article opens, while the loader, parser, and haptic changes are there to keep those interactions stable on device.

## 0.1.8

`0.1.8` finishes the公众号封面 path so article entries now look like proper shelf items instead of falling back to the generic article card whenever they are adjacent to the current book selection.

### Highlights

- Added cover URL extraction for article shelf entries and wired article slots into the same cover cache used by normal books
- Normalized `wx.qlogo.cn` article cover URLs from `http` to `https` before download so the device uses the modern endpoint consistently
- Added article-aware cover download polling and cache-path handling, letting visible article cards fetch and reuse real cached artwork
- Fixed shelf texture trimming so the article cover stays visible while the first normal book is selected, instead of disappearing whenever the article card is off-focus but still on screen

### Notes

This release is intentionally narrow: it keeps the mixed book/article shelf model from `0.1.7`, but closes the last obvious UI gap by making公众号文章 feel visually native on the shelf. The debugging logs used to verify the device-side behavior were removed before release, so runtime logging stays clean.

## 0.1.7

`0.1.7` finishes the mixed bookshelf/article reading flow so WeRead books and公众号文章 can coexist cleanly on TrimUI without regressing the normal book path.

### Highlights

- Added a dedicated article shelf flow that separates公众号文章 from normal books, keeps counts independent, and resolves article opens through the Kindle-style `reader -> mpdetail` path
- Fixed book opening and resume behavior when shelf entry order and `bookReaderUrls` order differ, so normal books no longer stall on chapter loading when article entries are present
- Added article-aware rendering with adjustable font size, Unicode text rendering, article catalogs from `mpChaptersInfo`, and article progress isolation from normal book resume state
- Restored continuous cross-chapter paging for both books and articles, including held page-turn behavior across chapter boundaries
- Moved full book catalog hydration off the first catalog-open action and into background chunk loading, so directory opening feels much faster while still filling in the full table of contents
- Reduced shelf idle heat by limiting background cover downloads to the currently visible neighborhood instead of walking the whole shelf queue

### Notes

This release is mainly about making公众号文章 a first-class reading target instead of a fragile compatibility path. The biggest user-facing changes are that books stay stable, articles now behave like real entries with working catalogs and font controls, and the device should spend less time doing unnecessary background work while sitting on the shelf screen.

## 0.1.6

`0.1.6` hardens the build, packaging, and release path so maintainers can ship TrimUI packages with higher confidence and clearer diagnostics.

### Highlights

- Added `make doctor-release` so missing compiler, SDK, libcurl, `zip`, and `libgcc_s.so.1` failures surface before packaging starts
- Added sha256 verification for tg5040 SDK and toolchain bootstrap downloads plus clearer missing-command diagnostics
- Added package archive audits for NextUI, Stock, and CrossMix that verify launcher contracts, packaged runtime assets, bundled libraries, and tg5040 ELF dependencies
- Added deterministic smoke coverage for the package audit helper itself with `make test-package-audit-smoke`
- Updated GitHub release automation to run the same `make doctor-release -> make test-host -> make test-smoke -> make package-all -> make package-audit-all` path used locally

### Notes

This release is about release reliability rather than user-facing UI changes. The app behavior stays the same, but the shipping path is now much easier to trust: prerequisites fail fast, package contents are audited statically before publication, and CI no longer uses a weaker parallel release sequence than maintainers do locally.

## 0.1.5

`0.1.5` focuses on reducing CPU usage and device temperature during long reading sessions.

### Highlights

- Switched the idle reader loop from fixed-interval polling to event-driven sleep, reducing wakeups from ~5.5/sec to near zero
- Replaced blocking haptic pulses (22-78ms usleep) with an async GPIO model that no longer stalls the render loop
- Added deadline-based sleep that calculates the precise next wake time from battery poll, clock tick, progress report, and haptic deadlines
- Extended render skipping to the shelf view so static screens no longer redraw every frame
- Cached the QR login texture instead of reloading the PNG from disk on every frame
- Guarded chapter prefetch updates behind a catalog-index check to skip redundant work
- Added active-work checks before polling prefetch and cover download threads

### Notes

The most common state for an ebook reader is sitting on a page of text with no user input. Before this release the app woke the CPU ~5.5 times per second in that state; after these changes it wakes roughly once every 30 seconds (for the battery poll). This should noticeably reduce device warmth and improve battery life during extended reading.

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
