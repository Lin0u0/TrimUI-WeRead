# Test Fixtures

`tests/fixtures/` holds deterministic host-side inputs for Phase 03 verification work.

- `tests/fixtures/state/` is for persisted JSON and state-tree samples that mirror files under `ctx->state_dir`.
- `tests/fixtures/parser/` is for captured HTML, inline JSON, and parser-focused payloads that should stay stable across host runs.
- `tests/fixtures/service/` is for higher-level service inputs and cross-module fixture sets that exercise non-UI workflows without needing live WeRead traffic.

Keep this tree lightweight and repo-local. Prefer minimal realistic samples over large snapshots.
