#include "test_support.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

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

int main(void) {
    ApiContext ctx;
    char temp_dir[PATH_MAX];

    HOST_TEST_ASSERT_INT_EQ(host_test_init_api_context(&ctx, temp_dir, sizeof(temp_dir)), 0);

    assert_last_reader_fixture(&ctx);
    assert_reader_position_fixture(&ctx);
    assert_reader_state_round_trip(&ctx);

    host_test_cleanup_api_context(&ctx, temp_dir);
    return 0;
}
