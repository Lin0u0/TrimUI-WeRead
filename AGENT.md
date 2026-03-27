# minui-weread Agent Notes

## Reader Scope

- All reading behavior must stay on WeRead's Kindle simplified page.
- Do not introduce desktop reader pages or non-Kindle UA handling into the reader flow.
- Reader URLs should stay under `https://weread.qq.com/wrwebsimplenjlogic/reader?bc=...&fs=...`.

## Login Scope

- Login flow should stay on the WeRead web/Kindle-compatible auth path already used by this project.
- Do not replace QR login with desktop-only or browser-automation-only flows unless explicitly requested.
- Session state is cookie-driven and must be persisted under the configured data directory.
- Startup should validate whether the current cookie session is still usable before entering normal shelf/reader flow.

## Verified Login Flow

- The current login flow is:
1. Start QR login and fetch the QR image.
2. Poll `getlogininfo`.
3. When the ticket is ready, complete `weblogin`.
4. Persist returned cookies for subsequent shelf/reader requests.

- The important behavior constraints are:
  - `getlogininfo` may long-poll or timeout.
  - Timeout should be treated as "keep waiting", not immediate failure.
  - `weblogin` success is determined by returned tokens/cookies, not by assuming one field is always a string.
  - Login completion must refresh shelf/session state after cookies are updated.

## Login Parsing Notes

- `getlogininfo` responses may contain fields such as `vid`, `skey`, and `code`.
- `vid` must not be assumed to always be a string.
- Real-world responses have included numeric `vid`; parsing must accept both numeric and string representations.
- Auth-state bugs should be debugged from the actual response body written to disk, not from UI symptoms alone.

## Login UI Rules

- QR generation and login polling must never block the SDL UI thread.
- UI flow should use background threads for:
  - QR generation
  - polling `getlogininfo`
  - waiting for final confirmation / `weblogin`
- The UI should remain responsive while login is in progress.
- Status text should distinguish at least:
  - QR generation
  - waiting for scan
  - scanned / waiting for confirmation
  - login success
  - login failure / expired session

## Session Validation

- Before entering shelf or reader flow, always check whether the current session is still valid.
- Invalid or expired cookies should send the app back into login flow rather than failing later inside reader loading.
- Session-check failures and reader-content failures are different classes of errors and should not be conflated.

## Reader Page Shapes

- There are at least two Kindle simplified reader shapes in the wild:
- `txt`-like books
  - Often expose familiar reader DOM and straightforward chapter fields.
- `epub`-like books such as `南方高速`
  - Still use the Kindle simplified page and `window.__NUXT__`.
  - May have different alias/layout patterns.
  - Must not be treated as unsupported just because initial field probing differs.

## Verified Kindle Fields

- These fields have been verified on Kindle simplified pages and should be preferred:
- Reader navigation/state:
  - `reader.readerUrlParams`
  - `reader.curChapterUrlParam`
  - `reader.prevChapterUrlParam`
  - `reader.nextChapterUrlParam`
  - `reader.prevChaptersWordCount`
- Progress:
  - `reader.progress.book.chapterUid`
  - `reader.progress.book.chapterIdx`
  - `reader.progress.book.chapterOffset`
  - `reader.progress.book.progress`
  - `reader.progress.book.summary`
- Book metadata:
  - `reader.bookInfo.bookId`
  - `reader.bookInfo.totalWords`
  - `reader.bookInfo.format`
- Chapter metadata:
  - `reader.curChapter`
  - `reader.chapterIndexes`
  - `reader.chapterInfoCount`

## Alias Resolution Rules

- `window.__NUXT__` uses function-parameter aliasing heavily.
- When resolving aliases, always:
  - Find the `(function(...){...})(...)` wrapper.
  - Skip past the full function body before locating the invocation argument list.
  - Resolve aliases from the invocation arguments, not by naive global string guesses.
- For complex objects like `reader.curChapter`, prefer:
  - Read the alias from the `reader:{...}` block.
  - Resolve that alias back to its invocation literal.
  - Then extract fields from the resolved object slice.

## Parsing Priority

- For progress/reporting-critical metadata, prefer these sources in order:
1. `reader.progress.book.*`
2. `reader.readerUrlParams` and `reader.curChapterUrlParam/prevChapterUrlParam/nextChapterUrlParam`
3. Resolved `reader.curChapter` object
4. DOM fallbacks only for rendered content and offsets

- Concretely:
  - `chapterUid`: prefer `reader.progress.book.chapterUid`
  - `chapterIdx`: prefer `reader.progress.book.chapterIdx`
  - `chapterOffset`: prefer `reader.progress.book.chapterOffset`
  - `progress`: prefer `reader.progress.book.progress`
  - `summary`: prefer `reader.progress.book.summary`
  - `prevChaptersWordCount`: use the verified `reader.prevChaptersWordCount`

## Chapter Navigation

- Do not assume missing `prev/next` on the entry `bc` means the book cannot switch chapters.
- Some books must first resolve from the shelf entry target to the current chapter target.
- After loading a reader page, if `doc.target` differs from the requested target, treat `doc.target` as the canonical current chapter target.

## Local Position Safety

- Never mix reading positions across books.
- Local saved position must be keyed by:
  - `bookId`
  - the original shelf/source target used to open that book
- Do not reuse a saved local page position when either of those identifiers differs.

## Reporting Notes

- `POST /wrwebsimplenjlogic/api/bookread` is the Kindle simplified progress-report endpoint.
- The current implementation should stay aligned with Kindle payload semantics:
  - `b`, `c`, `ci`, `co`, `sm`, `pr`, `rt`, `ts`, `rn`, `tk`, `ct`
- Prefer real content offsets from page `wco` samples over linear page guesses.
- Prefer current-page summary text over stale chapter-entry summaries.

## Things To Avoid

- Do not add desktop UA branches as a shortcut.
- Do not parse reader metadata with broad global `strstr(... "chapterIdx:" ...)` style matching when a scoped reader/progress block is available.
- Do not assume all books share the same Kindle page layout.

---

## Analysis Session — 2026-03-27

### Reader page URL format (verified)

```
GET https://weread.qq.com/wrwebsimplenjlogic/reader?bc=<chapter_target>&fs=<font_size>
```

Returns HTTP 200, 15–70 KB HTML. The `chapter_target` is the opaque string from `reader-positions.json`.

### Progress sync endpoint (verified working)

```
POST https://weread.qq.com/wrwebsimplenjlogic/api/bookread
Content-Type: application/json

{
  "b":  "<book_id>",         // e.g. "26150754"
  "c":  "<chapter_uid>",     // numeric string, e.g. "1144"
  "ci": <chapter_idx>,
  "co": <chapter_offset>,    // byte offset within chapter
  "sm": "<page_summary>",    // ~20-char summary of current page text
  "pr": <progress_0_100>,
  "rt": <reading_seconds>,   // 0–60, capped
  "ts": <timestamp_ms>,
  "rn": <random_0_999>,
  "tk": "<token>",           // from reader:{token:"..."} in the page NUXT data
  "ct": <timestamp_seconds>
}
```

Success response: `{"data":{"succ":1},"isTimeValid":1}`. The `token` field is always a **literal string** in the NUXT call args, never an alias.

### NUXT alias scale

- Small books (~20 chapters): ~109 parameters, single-char aliases only (`a`–`z`, `A`–`D`).
- Large books (1000+ chapters): up to 2346 parameters, two-char aliases (`aa`–`Qx`). All chapter titles and params are inlined as string literals; structural arrays use `Array(N)`.

### `chapterIndexes` / `readerUrlParams` — large book behavior

For large books, the NUXT call args pass `Array(N)` (e.g. `Array(60)`) for `chapterIndexes` and `readerUrlParams`. `parse_js_literal_for_alias` converts any `Array(...)` to `"[]"`, so resolving these aliases yields an empty array — the actual chapter list is NOT available in the top-level reader state.

**Solution**: `firstPageCatalogs` and `lastPageCatalogs` always contain inline data:

```
firstPageCatalogs:{
  indexes: [{chapterUid:X, displayTitle:"第1章 ...", isCurrent:Y, isLock:Z, ...}, ...],
  readerUrlParams: [{cUid:X, param:"<chapter_target>"}, ...],
}
lastPageCatalogs:{ ... }  // same shape, last ~20 chapters
```

Fall back to these when top-level arrays resolve to `"[]"`.

### Progress fields are nested, not top-level

The outer progress block is `progress:{bookId:X, book:{...}, canFreeRead:Y, ...}`.
All chapter-specific fields (`chapterUid`, `chapterIdx`, `chapterOffset`, `progress`, `summary`) are inside the inner `book:{}`, NOT at depth=1 of the outer block.

`find_top_level_field` at depth=1 of the outer block will NOT find them.

**Correct extraction**: Use `extract_container_from_slice(prog_start, prog_end, "book:", '{', '}')` to isolate the inner block, then search it.

### `extract_literal_from_slice` — array content loss

`extract_literal_from_slice` calls `parse_js_literal_for_alias` on the value. For any inline array `[{...}]`, `parse_js_literal_for_alias` returns `"[]"`, discarding all content. This broke catalog parsing for both small and large books.

**Fix**: `extract_array_from_slice` — when the value starts with `[`, use `find_matching_pair` to extract the full array text directly instead of going through `parse_js_literal_for_alias`.

## Build & Release

### Canonical build assets

- Shared packaging assets live in:
  - `assets/fonts/SourceHanSerifSC-Regular.otf`
  - `assets/icons/weread.png`
- Do not point build scripts at ad-hoc files under `Downloads/`; update the checked-in assets instead.

### Canonical build entrypoints

- Use the repo `Makefile` as the single entrypoint for builds and packaging.
- Main release commands:
  - `make macos-release`
  - `make nextui-release`
  - `make stock-release`
  - `make package-all`

### Expected distributables

- `make macos-release`
  - Produces `dist/WeRead-macos.tar.gz`
  - Intended for local macOS testing
- `make nextui-release`
  - Produces `dist/WeRead-nextui.tar.gz`
  - Installs under `Tools/tg5040/WeRead.pak` plus `Tools/tg5040/.media/WeRead.png`
- `make stock-release`
  - Produces `dist/WeRead-stock-app.tar.gz`
  - Installs under `Apps/WeRead`

### TG5040 / TrimUI build rules

- Cross-compilation requires an `aarch64-linux-gnu-gcc` toolchain on macOS.
- The official TrimUI SDK userland is bootstrapped by:
  - `make tg5040-sdk`
- The tg5040 `libcurl` dependency is built against the official SDK by:
  - `make tg5040-libcurl`
- For a fresh machine, use:
  - `make tg5040-bootstrap`
- After bootstrap, `nextui-release` and `stock-release` should work end-to-end.

### Packaging templates

- NextUI launch template:
  - `packaging/nextui/launch.sh`
- Stock OS launch/config templates:
  - `packaging/stock/launch.sh`
  - `packaging/stock/config.json`
- macOS runner template:
  - `packaging/macos/run.sh`

### Maintenance notes

- Keep `Makefile`, `scripts/bootstrap_tg5040_sdk.sh`, and `scripts/build_tg5040_libcurl.sh` aligned; do not change one build path in isolation.
- Keep TrimUI runtime libs sourced from the official SDK userland under `build/tg5040-sdk/sdk_usr/usr/lib`.
- If `curl` is already present at `third_party/tg5040/curl`, the helper script should reuse it instead of rebuilding unnecessarily.
- If a TrimUI package build starts using host `cc`/clang instead of `aarch64-linux-gnu-gcc`, treat that as a regression in the recursive `make` context.
