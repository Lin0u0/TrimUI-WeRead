# Maintainer Guide

This is the internal handoff entrypoint for TrimUI-WeRead maintainers. Start here for the current login, shelf, reader, verification, and release-adjacent paths, then use [MAINTAINER_BOUNDARIES.md](MAINTAINER_BOUNDARIES.md) for ownership lookup and [BUILD.md](BUILD.md) for build and packaging reference. The public-facing project overview stays in [README.md](README.md).

## Start Here

Begin with the host verification hard gates:

- `make test-host`
- `make test-smoke`

Then use the repo docs in this order:

- [MAINTAINER_BOUNDARIES.md](MAINTAINER_BOUNDARIES.md) for source ownership and protected runtime names.
- [BUILD.md](BUILD.md) for tg5040 build, package outputs, and launcher-sensitive paths.
- [README.md](README.md) for the outward-facing install, runtime, and command surface.

Keep these persisted runtime files stable unless you are intentionally adding compatibility work:

- `cookies.txt`
- `state/shelf.json`
- `state/last-reader.json`
- `state/reader-positions.json`
- `state/preferences.json`

## Common Tasks

### Login and Session Issues

Start with `src/ui_flow_startup_login.c` and `src/session_service.c`.

- `src/main.c` routes CLI session checks through `session_service_require_valid_session()`, and the SDL startup/login flow in `src/ui_flow_startup_login.c` uses the same service layer for startup refresh, QR generation, and polling.
- `src/session_service.c` is the maintainer seam for session validation, startup refresh, and background worker contexts.
- `src/auth.c` owns the QR/session transport details and talks to the WeRead auth endpoints.
- `cookies.txt` is the persisted cookie jar. If login appears broken after a working session, treat expired or stale cookies as the first failure path.

Use this path when:

- startup drops to login unexpectedly
- QR generation or poll-confirm never completes
- CLI commands such as `shelf`, `reader`, or `resume` fail session validation before doing useful work

### Shelf and Cache Issues

Start with `src/ui_flow_shelf.c`, `src/shelf_service.c`, and `src/shelf_state.c`.

- `src/ui_flow_shelf.c` owns UI-side cover download and book-open preparation.
- `src/shelf_service.c` owns cached shelf fallback, resume/open preparation, and the shelf-facing service seam used by both CLI and UI paths.
- `src/shelf.c` still owns the underlying shelf fetch/parse behavior and reader-target lookup.
- `src/shelf_state.c` owns persisted shelf cache reads and writes for `state/shelf.json`.
- `state/shelf.json` is the local fallback when live shelf refresh is unavailable or poor-network behavior kicks in.

Check this area when:

- the shelf is empty online but cached data still exists
- selected-book open preparation fails before the reader starts loading
- covers or cached shelf content behave differently between a fresh refresh and fallback reads

### Reader and Resume Issues

Start with `src/ui_flow_reader.c`, `src/reader_service.c`, and `src/reader_state.c`.

- `src/ui_flow_reader.c` owns UI-side reader open, prefetch, and progress-report worker lifecycle.
- `src/reader_service.c` is the maintainer seam for reader open/resume orchestration, including the cloud-progress-versus-local-position decision boundary.
- `src/reader.c` owns chapter loading, parsing integration, catalog expansion, and progress reporting primitives.
- `src/ui_reader_view.c` owns reader-view pagination, page offsets, and the UI-side local-position helpers.
- `src/reader_state.c` owns persisted resume state in `state/last-reader.json` and `state/reader-positions.json`.
- `state/last-reader.json` stores the last reader target and font sizing.
- `state/reader-positions.json` stores per-book local position state.

Maintainer rule of thumb:

- cloud progress wins when the loaded document exposes a newer remote chapter/progress target
- local resume state matters when cloud progress is absent or older, especially when `state/last-reader.json` or `state/reader-positions.json` points at a newer saved chapter or page

Also keep `state/preferences.json` in mind for reader-facing behavior such as brightness and dark mode, which flows through `src/preferences_state.c`.

### Verification and Regression Checks

Phase 03 established a hard-gate versus advisory split. Keep that split explicit.

Hard gates:

- `make test-host`
- `make test-smoke`

Advisory follow-up:

- `make tg5040`
- `make nextui-release stock-release crossmix-release`
- manual tg5040 smoke for login, shelf refresh, reader open/resume, and progress sync

Use the hard gates for normal maintainer iteration first. Use the advisory checks when a change touches tg5040 packaging, launcher-sensitive paths, or release confidence matters.

### Release-Adjacent Prep

Start with [BUILD.md](BUILD.md), `Makefile`, and the launcher scripts:

- `packaging/nextui/launch.sh`
- `packaging/stock/launch.sh`
- `packaging/crossmix/launch.sh`

Current package outputs:

- `dist/WeRead-nextui.tar.gz`
- `dist/WeRead.pakz`
- `dist/WeRead-stock-app.tar.gz`
- `dist/WeRead-crossmix.tar.gz`

Release-adjacent work in this repo is still mostly about understanding current inputs and outputs, not running a large release workflow. `Makefile` is the source of truth for target names, artifact staging, and package assembly, while the launcher scripts define runtime roots, CA bundle wiring, and package-sensitive startup behavior.

## Module Index

- Login and session: `src/main.c`, `src/ui_flow_startup_login.c`, `src/session_service.c`, `src/auth.c`
- Shelf and cache: `src/ui_flow_shelf.c`, `src/shelf_service.c`, `src/shelf.c`, `src/shelf_state.c`
- Reader and resume: `src/ui_flow_reader.c`, `src/reader_service.c`, `src/reader.c`, `src/ui_reader_view.c`, `src/reader_state.c`
- Persisted state: `src/reader_state.c`, `src/shelf_state.c`, `src/preferences_state.c`, plus the shared persistence layer in `src/state.c`
- UI flow ownership: `src/ui_flow_startup_login.c`, `src/ui_flow_shelf.c`, `src/ui_flow_reader.c`
- Build and packaging: `Makefile`, [BUILD.md](BUILD.md), `packaging/nextui/launch.sh`, `packaging/stock/launch.sh`, `packaging/crossmix/launch.sh`

For the full ownership map and protected runtime contract, use [MAINTAINER_BOUNDARIES.md](MAINTAINER_BOUNDARIES.md).

## Frequent Failure Paths

- Expired or stale session state in `cookies.txt`, usually showing up as session validation failures or startup refresh dropping back to login.
- Stale `state/shelf.json`, where cached shelf behavior masks a live fetch failure or old shelf data keeps reappearing.
- Mismatched resume state in `state/last-reader.json` or `state/reader-positions.json`, especially when local state and cloud progress disagree about the right chapter or page.
- Failing host verification commands, starting with `make test-host` and `make test-smoke`, which should be treated as real regressions before doing package or device follow-up.
- tg5040 and package-sensitive paths, where `make tg5040` or `make nextui-release stock-release crossmix-release` exposes launcher, archive, asset, or cross-build assumptions that host checks do not cover.

This guide intentionally stays focused on the highest-frequency real-world paths: session/cookie handling, shelf cache behavior, resume/local reader state, host verification commands, and tg5040/package-sensitive issues. It does not try to document every possible failure tree or future release hardening flow.
