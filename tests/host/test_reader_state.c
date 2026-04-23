#include "test_support.h"

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include "json.h"
#include "reader_state.h"
#include "state.h"

static void assert_last_reader_fixture(ApiContext *ctx) {
    char target[256];
    int font_size = 0;
    int content_font_size = 0;
    char *fixture = host_test_load_fixture("state/last-reader.json");

    HOST_TEST_ASSERT(fixture != NULL);
    HOST_TEST_ASSERT_INT_EQ(host_test_seed_state_file(ctx, STATE_FILE_LAST_READER, fixture), 0);
    free(fixture);

    HOST_TEST_ASSERT_INT_EQ(
        reader_state_load_last_reader(ctx, target, sizeof(target), &font_size, &content_font_size),
        0
    );
    HOST_TEST_ASSERT(strcmp(target, "https://weread.qq.com/web/reader/abc123") == 0);
    HOST_TEST_ASSERT_INT_EQ(font_size, 4);
    HOST_TEST_ASSERT_INT_EQ(content_font_size, 40);
}

static void assert_reader_position_fixture(ApiContext *ctx) {
    char target[256];
    char source_target[256];
    int font_size = 0;
    int content_font_size = 0;
    int current_page = 0;
    int current_offset = 0;
    char *fixture = host_test_load_fixture("state/reader-positions.json");

    HOST_TEST_ASSERT(fixture != NULL);
    HOST_TEST_ASSERT_INT_EQ(host_test_seed_state_file(ctx, STATE_FILE_READER_POSITIONS, fixture), 0);
    free(fixture);

    HOST_TEST_ASSERT_INT_EQ(
        reader_state_load_position(
            ctx,
            "book-1",
            "https://weread.qq.com/web/bookDetail/book-1",
            target,
            sizeof(target),
            &font_size,
            &content_font_size,
            &current_page,
            &current_offset
        ),
        0
    );
    HOST_TEST_ASSERT(strcmp(target, "https://weread.qq.com/web/reader/book-1-chapter-7") == 0);
    HOST_TEST_ASSERT_INT_EQ(font_size, 5);
    HOST_TEST_ASSERT_INT_EQ(content_font_size, 38);
    HOST_TEST_ASSERT_INT_EQ(current_page, 12);
    HOST_TEST_ASSERT_INT_EQ(current_offset, 3456);

    HOST_TEST_ASSERT_INT_EQ(
        reader_state_load_position(
            ctx,
            "book-1",
            "https://weread.qq.com/web/bookDetail/book-1?mismatch=1",
            target,
            sizeof(target),
            NULL,
            NULL,
            NULL,
            NULL
        ),
        -1
    );

    memset(source_target, 0, sizeof(source_target));
    HOST_TEST_ASSERT_INT_EQ(
        reader_state_load_position_by_book_id(
            ctx,
            "book-1",
            source_target,
            sizeof(source_target),
            target,
            sizeof(target),
            &font_size,
            &content_font_size,
            &current_page,
            &current_offset
        ),
        0
    );
    HOST_TEST_ASSERT(strcmp(source_target, "https://weread.qq.com/web/bookDetail/book-1") == 0);
}

static void assert_reader_position_missing_source_target(ApiContext *ctx) {
    char target[256];
    char source_target[256];
    int font_size = 0;
    int content_font_size = 0;
    int current_page = 0;
    int current_offset = 0;

    HOST_TEST_ASSERT_INT_EQ(
        host_test_seed_state_file(
            ctx,
            STATE_FILE_READER_POSITIONS,
            "{\"book-2\":{\"target\":\"https://weread.qq.com/web/reader/book-2-chapter-1\","
            "\"fontSize\":6,\"contentFontSize\":39,\"currentPage\":8,\"currentOffset\":123}}"
        ),
        0
    );

    memset(source_target, 'x', sizeof(source_target));
    source_target[sizeof(source_target) - 1] = '\0';
    HOST_TEST_ASSERT_INT_EQ(
        reader_state_load_position_by_book_id(
            ctx,
            "book-2",
            source_target,
            sizeof(source_target),
            target,
            sizeof(target),
            &font_size,
            &content_font_size,
            &current_page,
            &current_offset
        ),
        0
    );
    HOST_TEST_ASSERT(strcmp(source_target, "") == 0);
    HOST_TEST_ASSERT(strcmp(target, "https://weread.qq.com/web/reader/book-2-chapter-1") == 0);
    HOST_TEST_ASSERT_INT_EQ(font_size, 6);
    HOST_TEST_ASSERT_INT_EQ(content_font_size, 39);
    HOST_TEST_ASSERT_INT_EQ(current_page, 8);
    HOST_TEST_ASSERT_INT_EQ(current_offset, 123);
}

static void assert_reader_state_round_trip(ApiContext *ctx) {
    char target[256];
    char persisted_path[PATH_MAX];
    char *persisted_text;
    int font_size = 0;
    int content_font_size = 0;
    int current_page = 0;
    int current_offset = 0;

    HOST_TEST_ASSERT_INT_EQ(
        reader_state_save_last_reader(ctx, "https://weread.qq.com/web/reader/new-target", 6, 42),
        0
    );
    HOST_TEST_ASSERT_INT_EQ(
        reader_state_load_last_reader(ctx, target, sizeof(target), &font_size, &content_font_size),
        0
    );
    HOST_TEST_ASSERT(strcmp(target, "https://weread.qq.com/web/reader/new-target") == 0);
    HOST_TEST_ASSERT_INT_EQ(font_size, 6);
    HOST_TEST_ASSERT_INT_EQ(content_font_size, 42);

    HOST_TEST_ASSERT_INT_EQ(
        reader_state_save_position(
            ctx,
            "book-2",
            "https://weread.qq.com/web/bookDetail/book-2",
            "https://weread.qq.com/web/reader/book-2-chapter-1",
            4,
            36,
            3,
            99
        ),
        0
    );
    HOST_TEST_ASSERT_INT_EQ(
        reader_state_load_position(
            ctx,
            "book-2",
            "https://weread.qq.com/web/bookDetail/book-2",
            target,
            sizeof(target),
            &font_size,
            &content_font_size,
            &current_page,
            &current_offset
        ),
        0
    );
    HOST_TEST_ASSERT(strcmp(target, "https://weread.qq.com/web/reader/book-2-chapter-1") == 0);
    HOST_TEST_ASSERT_INT_EQ(font_size, 4);
    HOST_TEST_ASSERT_INT_EQ(content_font_size, 36);
    HOST_TEST_ASSERT_INT_EQ(current_page, 3);
    HOST_TEST_ASSERT_INT_EQ(current_offset, 99);

    HOST_TEST_ASSERT(
        snprintf(persisted_path, sizeof(persisted_path), "%s/%s", ctx->state_dir, STATE_FILE_READER_POSITIONS) <
        (int)sizeof(persisted_path)
    );
    persisted_text = host_test_read_file(persisted_path);
    HOST_TEST_ASSERT(persisted_text != NULL);
    HOST_TEST_ASSERT(strstr(persisted_text, "\"book-2\"") != NULL);
    HOST_TEST_ASSERT(strstr(persisted_text, "\"sourceTarget\":\"https://weread.qq.com/web/bookDetail/book-2\"") != NULL);
    free(persisted_text);

    HOST_TEST_ASSERT_INT_EQ(reader_state_save_last_reader(ctx, NULL, 1, 1), -1);
    HOST_TEST_ASSERT_INT_EQ(
        reader_state_save_position(ctx, "book-3", "", "https://weread.qq.com/web/reader/book-3", 1, 1, 0, 0),
        -1
    );
}

static void assert_state_write_json_atomic(ApiContext *ctx) {
    char path[PATH_MAX];
    char *before;
    char *after;
    cJSON *json;
    struct rlimit old_limit;
    struct rlimit limited;
    void (*old_sigxfsz)(int);

    HOST_TEST_ASSERT_INT_EQ(host_test_seed_state_file(ctx, "atomic.json", "{\"keep\":true}"), 0);
    HOST_TEST_ASSERT(
        snprintf(path, sizeof(path), "%s/%s", ctx->state_dir, "atomic.json") < (int)sizeof(path)
    );
    before = host_test_read_file(path);
    HOST_TEST_ASSERT(before != NULL);

    json = cJSON_CreateObject();
    HOST_TEST_ASSERT(json != NULL);
    cJSON_AddStringToObject(json, "payload",
                            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");

    HOST_TEST_ASSERT_INT_EQ(getrlimit(RLIMIT_FSIZE, &old_limit), 0);
    limited = old_limit;
    limited.rlim_cur = 1;
    old_sigxfsz = signal(SIGXFSZ, SIG_IGN);
    HOST_TEST_ASSERT(old_sigxfsz != SIG_ERR);
    HOST_TEST_ASSERT_INT_EQ(setrlimit(RLIMIT_FSIZE, &limited), 0);

    HOST_TEST_ASSERT_INT_EQ(state_write_json(ctx, "atomic.json", json), -1);

    HOST_TEST_ASSERT_INT_EQ(setrlimit(RLIMIT_FSIZE, &old_limit), 0);
    HOST_TEST_ASSERT(signal(SIGXFSZ, old_sigxfsz) != SIG_ERR);

    after = host_test_read_file(path);
    HOST_TEST_ASSERT(after != NULL);
    HOST_TEST_ASSERT(strcmp(before, after) == 0);

    free(after);
    free(before);
    cJSON_Delete(json);
}

int main(void) {
    ApiContext ctx;
    char temp_dir[PATH_MAX];

    HOST_TEST_ASSERT_INT_EQ(host_test_init_api_context(&ctx, temp_dir, sizeof(temp_dir)), 0);

    assert_last_reader_fixture(&ctx);
    assert_reader_position_fixture(&ctx);
    assert_reader_position_missing_source_target(&ctx);
    assert_reader_state_round_trip(&ctx);
    assert_state_write_json_atomic(&ctx);

    host_test_cleanup_api_context(&ctx, temp_dir);
    return 0;
}
