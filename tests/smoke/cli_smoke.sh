#!/bin/sh

set -eu

bin_path="${1:-build/host/bin/weread}"
tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/weread-smoke.XXXXXX")"

cleanup() {
    rm -rf "$tmpdir"
}

trap cleanup EXIT INT TERM

run_capture() {
    output_file="$1"
    shift

    if "$@" >"$output_file" 2>&1; then
        return 0
    fi
    return 1
}

assert_contains() {
    needle="$1"
    file="$2"

    if ! grep -Fq "$needle" "$file"; then
        printf '%s\n' "smoke assertion failed: expected '$needle' in $file" >&2
        cat "$file" >&2
        exit 1
    fi
}

help_output="$tmpdir/help.txt"
missing_arg_output="$tmpdir/reader-missing-arg.txt"
cache_missing_output="$tmpdir/shelf-cache-missing.txt"
cache_success_output="$tmpdir/shelf-cache-success.txt"
resume_missing_output="$tmpdir/resume-missing.txt"
resume_success_output="$tmpdir/resume-success.txt"
resume_logged_out_output="$tmpdir/resume-logged-out.txt"
shelf_logged_out_output="$tmpdir/shelf-logged-out.txt"
data_dir="$tmpdir/data"
state_dir="$data_dir/state"

mkdir -p "$state_dir"

run_capture "$help_output" "$bin_path" --help
assert_contains "shelf-cache" "$help_output"
assert_contains "resume" "$help_output"
assert_contains "reader <reader-url-or-bc> [font-size]" "$help_output"

if run_capture "$missing_arg_output" "$bin_path" reader; then
    printf '%s\n' "smoke assertion failed: reader without args should fail" >&2
    exit 1
fi
assert_contains "Usage:" "$missing_arg_output"

if run_capture "$cache_missing_output" "$bin_path" --data "$data_dir" shelf-cache; then
    printf '%s\n' "smoke assertion failed: shelf-cache without cache should fail" >&2
    exit 1
fi
assert_contains "No cached shelf data found" "$cache_missing_output"

cat >"$state_dir/shelf.json" <<'EOF'
{"state":{"shelf":{"books":[{"bookId":"book-1","title":"Fixture Book","author":"Fixture Author"}],"bookReaderUrls":["https://weread.qq.com/web/reader/book-1"]}}}
EOF

run_capture "$cache_success_output" "$bin_path" --data "$data_dir" shelf-cache
assert_contains "Shelf contains 1 books" "$cache_success_output"
assert_contains "Fixture Book - Fixture Author [bookId=book-1]" "$cache_success_output"
assert_contains "reader=https://weread.qq.com/web/reader/book-1" "$cache_success_output"

if WEREAD_TEST_SESSION_STATUS=1 \
    run_capture "$resume_missing_output" "$bin_path" --data "$data_dir" resume; then
    printf '%s\n' "smoke assertion failed: resume without saved state should fail" >&2
    exit 1
fi
assert_contains "No saved reader state found" "$resume_missing_output"

cat >"$state_dir/last-reader.json" <<'EOF'
{"target":"https://weread.qq.com/web/reader/book-1","fontSize":4,"contentFontSize":38}
EOF

WEREAD_TEST_SESSION_STATUS=1 WEREAD_TEST_READER_OUTPUT="fixture resume output" \
    run_capture "$resume_success_output" "$bin_path" --data "$data_dir" resume
assert_contains "fixture resume output" "$resume_success_output"

if WEREAD_TEST_SESSION_STATUS=0 \
    run_capture "$resume_logged_out_output" "$bin_path" --data "$data_dir" resume; then
    printf '%s\n' "smoke assertion failed: resume should fail when the session is invalid" >&2
    exit 1
fi
assert_contains "Session expired or not logged in. Run \`weread login\` first." "$resume_logged_out_output"

if WEREAD_TEST_SESSION_STATUS=0 \
    run_capture "$shelf_logged_out_output" "$bin_path" --data "$data_dir" shelf; then
    printf '%s\n' "smoke assertion failed: shelf should fail when the session is invalid" >&2
    exit 1
fi
assert_contains "Session expired or not logged in. Run \`weread login\` first." "$shelf_logged_out_output"

printf '%s\n' "[test-smoke] help, shelf-cache, resume, and logged-out session checks passed"
