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

- These fields have been verified on Kindle simplified pages:
- Reader navigation/state:
  - `reader.readerUrlParams`
  - `reader.curChapterUrlParam`
  - `reader.prevChapterUrlParam`
  - `reader.nextChapterUrlParam`
  - `reader.prevChaptersWordCount`
- Current chapter / current page context:
  - `reader.curChapter`
  - `reader.curChapterUrlParam`
  - `reader.chapterIndexes`
  - `reader.chapterInfoCount`
- Server-side last-known progress fallback:
  - `reader.progress.book.chapterUid`
  - `reader.progress.book.chapterIdx`
  - `reader.progress.book.chapterOffset`
  - `reader.progress.book.progress`
  - `reader.progress.book.summary`
- Book metadata:
  - `reader.bookInfo.bookId`
  - `reader.bookInfo.totalWords`
  - `reader.bookInfo.format`

- Important distinction:
  - `reader.curChapter*` describes the chapter currently being viewed.
  - `reader.progress.book.*` describes the server's last-known synced progress.
  - These are not interchangeable and must not be conflated in progress reporting.

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

- For page rendering and progress-report payloads, prefer these sources in order:
1. Resolved `reader.curChapter` object
2. `reader.curChapterUrlParam` and current catalog/url-param mappings
3. DOM-derived current-page offsets and current-page summary text
4. `reader.progress.book.*` only as fallback when current chapter fields are absent
5. DOM fallbacks only for rendered content and offsets

- Concretely:
  - `chapterUid`: prefer `reader.curChapter.chapterUid`; only fall back to `reader.progress.book.chapterUid` if current chapter data is missing
  - `chapterIdx`: prefer `reader.curChapter.chapterIdx`; only fall back to `reader.progress.book.chapterIdx` if current chapter data is missing
  - `chapterOffset`: prefer current-page offset samples (`wco`) or current-page derived offset; do not default to `reader.progress.book.chapterOffset`
  - `progress`: compute from current reading position plus `reader.prevChaptersWordCount`; only fall back to server-known progress when the current position cannot be computed
  - `summary`: prefer current-page text summary; only fall back to `reader.progress.book.summary` if no page summary is available
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
- Official simplified-page JS reports the currently viewed chapter, not the stale server progress chapter:
  - `c` / `ci` come from the current chapter (`reader.curChapter`)
  - `co` comes from the current page offset within that chapter
  - `sm` comes from current page text, capped to a short summary
  - `pr` is recomputed from the current reading position when current-position data is available
- `reader.progress.book.*` should be used for:
  - initial cloud-progress sync / deciding whether to jump to another chapter on open
  - fallback-only progress data when the current position cannot be computed
  - fallback summary/offset data only when current-page data is unavailable
- Prefer real content offsets from page `wco` samples over linear page guesses.
- Prefer current-page summary text over stale chapter-entry summaries.
- Mirror the simplified-page timing model:
  - initial report with `rt=0` and existing server progress
  - subsequent active-reading reports roughly every 30 seconds
  - pause reading after about 120 seconds of inactivity
- Mirror the simplified-page session behavior:
  - `sessionTimeout` from `/api/bookread` means the login state is stale
  - do not treat it as a normal transient post failure
  - refresh session / re-enter login flow before expecting cloud progress sync to recover

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

## Kindle Web Routing Notes

- Verified with the project Kindle UA from `src/api.h` plus real WeRead cookies:
  - `https://r.qq.com/` redirects to `https://weread.qq.com/`
  - Kindle UA traffic is then routed to `https://weread.qq.com/wrwebsimplenjlogic/shelf`
- Do not assume Kindle flows land on the modern `/web/...` Nuxt site.
- Under Kindle UA, `/web/reader/...`, `/web/category/...`, and `/web/search/...` requests can be collapsed by the server back to the simplified `wrwebsimplenjlogic` entry flow.
- For this project, treat `wrwebsimplenjlogic` as the canonical web surface for shelf/reader behavior.

## Simplified Web Pages

- Verified simplified routes:
  - `/wrwebsimplenjlogic/shelf`
  - `/wrwebsimplenjlogic/reader`
  - `/wrwebsimplenjlogic/mpdetail`
  - `/wrwebsimplenjlogic/buyreader`
  - `/wrwebsimplenjlogic/login`
  - `/wrwebsimplenjlogic/test`
- The SSR router bundle also defines test mirrors:
  - `/wrwebsimplenjlogic-test/shelf`
  - `/wrwebsimplenjlogic-test/reader`
  - `/wrwebsimplenjlogic-test/mpdetail`
  - `/wrwebsimplenjlogic-test/buyreader`
  - `/wrwebsimplenjlogic-test/login`
- With a valid logged-in cookie set, `/wrwebsimplenjlogic/login` redirects back to `/wrwebsimplenjlogic/shelf`.

## Simplified JS Asset Map

- Shared on most simplified pages:
  - `client/global.35483d.js`
  - `client/cookie.0e551c.js`
  - `static/es5-shim.min.js`
- Page-specific chunks:
  - `shelf` -> `client/shelf.fb74d4.js`
  - `reader` -> `client/reader.4efb56.js` and `client/collect_html.5c9780.js`
  - `mpdetail` -> `client/mpdetail.4304d7.js`
  - `buyreader` -> `client/buyreader.1241ae.js`
  - `test` -> no dedicated page chunk beyond shared assets
- When debugging page behavior, start from these chunks instead of the newer `wrweb-next/_nuxt/*` assets.

## Simplified JS Responsibilities

- `global.35483d.js`
  - Defines `window.k_utils`, `window.k_localStorage`, and `window.k_common`.
  - Owns the shared XHR wrapper, client log reporting, KV reporting, and viewport zoom logic.
  - All requests append `platform=desktop`.
- `cookie.0e551c.js`
  - Defines `window.cookie_utils` with simple `get/set/parse` helpers.
  - Used by global viewport scaling logic such as `wr_scaleRatio`.
- `shelf.fb74d4.js`
  - Reads SSR `window.__NUXT__.state.shelf`.
  - Renders shelf rows, handles prev/next paging, logout, and "buy reader" entry.
  - Uses SSR `bookReaderUrls` to map `bookId -> bc param` before entering reader.
  - `MP*` shelf entries first resolve `reviewId`, then jump to `mpdetail`.
- `reader.4efb56.js`
  - Implements the Kindle simplified reading UI: paging, catalog panel, note/bookmark UI, font size, shelf return, and buy-reader entry.
  - Reads SSR `window.__NUXT__.state.reader` and uses query form `reader?bc=...&fs=...`.
  - Reports reading progress to `/api/bookread`.
  - Lazy-loads additional catalog segments from `/api/catalogloadmore`.
- `collect_html.5c9780.js`
  - Exposes `window.k_collect`.
  - Traverses rendered reader DOM and converts it into content blocks, decoration layers, offsets, anchors, and selection metadata.
  - This is the key helper behind simplified-page pagination and note-range behavior.
- `mpdetail.4304d7.js`
  - Implements simplified article/MP detail reading.
  - Handles scroll paging, catalog, font size switching, image lazy loading, and reading-time reporting.
  - Also reports progress to `/api/bookread`.
- `buyreader.1241ae.js`
  - Very small behavior layer; mainly turns body click into `history.back()`.

## Simplified Navigation Rules

- Shelf reader entry is not the desktop `/web/reader/<id>` route.
- Shelf uses SSR `bookReaderUrls[*].param` and jumps to:
  - `/wrwebsimplenjlogic/reader?bc=<param>&fs=<font_size>`
- MP content jumps to:
  - `/wrwebsimplenjlogic/mpdetail?reviewId=<id>&fs=<font_size>`
- Reader font-size changes are implemented as full-page navigations back to:
  - `/wrwebsimplenjlogic/reader?bc=<current_param>&fs=<new_size>`
- When reproducing or debugging navigation, prefer these simplified URLs over newer `/web/...` routes.

## Simplified API Notes

- Shared infrastructure:
  - `POST /wrwebsimplenjlogic/api/kvlog`
  - `POST /wrwebsimplenjlogic/api/clog`
- Shelf:
  - `GET /wrwebsimplenjlogic/api/shelfloadmore`
  - `GET /wrwebsimplenjlogic/api/reviewId`
  - `GET /wrwebsimplenjlogic/api/logout`
- Reader:
  - `POST /wrwebsimplenjlogic/api/bookread`
  - `GET /wrwebsimplenjlogic/api/catalogloadmore`
- MP detail:
  - `POST /wrwebsimplenjlogic/api/bookread`
- Reader/reporting work should stay aligned with these simplified endpoints and payload semantics instead of inventing new web-only flows.
