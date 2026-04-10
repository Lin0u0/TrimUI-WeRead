# Maintainer Boundaries

This document is the Phase 1 ownership map for TrimUI-WeRead. It is meant to help a new maintainer find the right file family quickly, without tracing the whole application from `main.c`.

## Ownership Map

### Login

- `src/auth.c` / `src/auth.h`: QR login start, polling, and session validation against WeRead.
- `src/ui_flow_startup_login.c`: startup refresh, login UI worker lifecycle, and QR flow coordination inside the SDL app.

### Shelf

- `src/shelf.c` / `src/shelf.h`: shelf fetch, SSR/API pagination, and book selection data.
- `src/shelf_service.c` / `src/shelf_service.h`: shelf-domain orchestration, resume/open preparation, and shelf worker entrypoints.
- `src/shelf_state.h` / `src/shelf_state.c`: persisted shelf cache ownership for `state/shelf.json`.
- `src/ui_flow_shelf.c`: cover download lifecycle and shelf-specific UI-side work.

### Reader

- `src/reader.c` / `src/reader.h`: reader-domain loading, progress sync, chapter prefetch, and resume semantics.
- `src/reader_service.c` / `src/reader_service.h`: reader-open, resume, and progress orchestration above transport and local state.
- `src/reader_state.h` / `src/reader_state.c`: last-reader and per-book local-position persistence.
- `src/catalog.c`: chapter catalog merge/focus helpers used by the reader.
- `src/ui_flow_reader.c`: reader-open lifecycle, background progress/prefetch coordination, and chapter adoption into the UI.
- `src/ui_reader_view.c`: reader-view pagination, page offsets, catalog selection, local-position persistence, and progress helper logic used by the UI.

### Parsing

- `src/parser_common.c` / `src/parser_internal.h`: shared parsing primitives with the `parser_` naming family.
- `src/js_parser.c`: generic NUXT / JS literal parsing built on the shared parser helpers.
- `src/reader_parser.c` / `src/reader_internal.h`: reader-specific parsing and alias resolution built on the shared parser helpers.

### State

- `src/state.c` / `src/state.h`: generic JSON persistence helpers and protected runtime-contract filenames only.
- `src/reader_state.h` / `src/reader_state.c`, `src/shelf_state.h` / `src/shelf_state.c`, and `src/preferences_state.h` / `src/preferences_state.c`: typed persisted-state ownership by domain.

### UI

- `src/ui.c`: SDL scheduler, render composition, view switching, and top-level orchestration.
- `src/ui_internal.h`: shared internal UI types and non-public helper contracts.
- `src/ui_input.c`: input mapping.
- `src/ui_platform.c`: tg5040-specific device behavior and brightness application, with persistence delegated to preferences-state helpers.
- `src/ui_text.c`: text and UTF-8 helpers.

### Packaging / Build

- `Makefile`: canonical build and packaging entry point.
- `BUILD.md`: maintainer-facing build notes.
- `packaging/`: launcher layouts and platform package contents for NextUI, Stock OS, and CrossMix-OS.

## Quick Lookup by Task

| Task | Start here | Why these files |
|---|---|---|
| `Login and session` | `src/auth.c`, `src/ui_flow_startup_login.c`, `src/session_service.c` | Login QR flow, startup session validation, and session-oriented orchestration live across these three owners. |
| `Shelf and cache` | `src/shelf.c`, `src/ui_flow_shelf.c`, `src/shelf_service.c`, `src/shelf_state.c` | Shelf fetch, cached shelf state, cover-download UI work, and shelf-to-open preparation converge here. |
| `Reader and resume` | `src/reader.c`, `src/ui_flow_reader.c`, `src/ui_reader_view.c`, `src/reader_service.c`, `src/reader_state.c`, `src/preferences_state.c` | Reader loading, resume/open orchestration, local position persistence, and reader preference state are split across these files. |
| `Verification` | `Makefile`, `tests/host/`, `tests/smoke/cli_smoke.sh`, `BUILD.md` | `Makefile` owns `test-host` and `test-smoke`, `tests/host/` owns focused C checks, and `BUILD.md` explains the hard-gate versus advisory split. |
| `Packaging and release` | `Makefile`, `BUILD.md`, `packaging/nextui/launch.sh`, `packaging/stock/launch.sh`, `packaging/crossmix/launch.sh` | Package targets, artifact outputs, launcher env setup, and platform-specific runtime differences are release-sensitive here. |

## Protected Runtime Contract

These names are runtime-facing and should stay stable unless a later phase adds explicit compatibility or migration work.

### Persisted data

- `cookies.txt`
- `state/shelf.json`
- `state/last-reader.json`
- `state/reader-positions.json`
- `state/preferences.json`

### Binary and commands

- Binary name: `weread`
- Commands: `login`, `shelf`, `shelf-cache`, `reader`, `resume`, `ui`

### Packaging / launcher expectations

- Package/install name family: `WeRead`
- Runtime data roots:
  - NextUI: `$SHARED_USERDATA_PATH/WeRead`
  - Stock OS: `SDCARD/Data/WeRead`
  - CrossMix-OS: `/mnt/SDCARD/Data/WeRead`
- Launcher-related environment names used by packaging/runtime scripts: `HOME`, `LD_LIBRARY_PATH`, `CURL_CA_BUNDLE`, `SHARED_USERDATA_PATH`, `PLATFORM`, `LOGS_PATH`

## Release-Sensitive Surfaces

- Binary and command contract: keep the binary name `weread` and the user-visible commands `login`, `shelf`, `shelf-cache`, `reader`, `resume`, and `ui` stable unless compatibility work is added deliberately.
- Package/install contract: keep the package and install name family `WeRead` stable across NextUI, Stock OS, and CrossMix-OS surfaces.
- Launcher/runtime contract: treat `$SHARED_USERDATA_PATH/WeRead`, `SDCARD/Data/WeRead`, `/mnt/SDCARD/Data/WeRead`, `HOME`, `LD_LIBRARY_PATH`, `CURL_CA_BUNDLE`, `SHARED_USERDATA_PATH`, `PLATFORM`, and `LOGS_PATH` as protected release-sensitive names because package launchers depend on them directly.
- Package outputs and launcher differences: use [BUILD.md](BUILD.md) as the canonical quick reference for current package outputs, install roots, launcher/runtime differences, and common release-adjacent failure points.

## Safe Internal Rename Space

These are good Phase 1 cleanup targets, as long as behavior stays stable:

- Internal helper names and file-local utility names
- Non-public structs and function families in `_internal.h` headers
- Module/file ownership comments and maintainer docs
- Shared helper extraction inside the current flat `src/` layout
