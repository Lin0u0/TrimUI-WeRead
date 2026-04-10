#include "test_support.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "shelf_state.h"
#include "state.h"

static void assert_shelf_fixture_load(ApiContext *ctx) {
    cJSON *json;
    cJSON *books;
    cJSON *first_book;
    char *fixture = host_test_load_fixture("state/shelf.json");

    HOST_TEST_ASSERT(fixture != NULL);
    HOST_TEST_ASSERT_INT_EQ(host_test_seed_state_file(ctx, STATE_FILE_SHELF, fixture), 0);
    free(fixture);

    json = shelf_state_load_cache(ctx);
    HOST_TEST_ASSERT(json != NULL);
    books = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(json, "data"), "books");
    HOST_TEST_ASSERT(cJSON_IsArray(books));
    HOST_TEST_ASSERT_INT_EQ(cJSON_GetArraySize(books), 2);
    first_book = cJSON_GetArrayItem(books, 0);
    HOST_TEST_ASSERT(strcmp(json_get_string(first_book, "title"), "Fixture Book") == 0);
    cJSON_Delete(json);
}

static void assert_shelf_round_trip(ApiContext *ctx) {
    char persisted_path[PATH_MAX];
    char *persisted_text;
    cJSON *json;
    char *fixture = host_test_load_fixture("state/shelf.json");

    HOST_TEST_ASSERT(fixture != NULL);
    json = cJSON_Parse(fixture);
    HOST_TEST_ASSERT(json != NULL);
    free(fixture);

    HOST_TEST_ASSERT_INT_EQ(shelf_state_save_cache(ctx, json), 0);
    cJSON_Delete(json);

    HOST_TEST_ASSERT(
        snprintf(persisted_path, sizeof(persisted_path), "%s/%s", ctx->state_dir, STATE_FILE_SHELF) <
        (int)sizeof(persisted_path)
    );
    persisted_text = host_test_read_file(persisted_path);
    HOST_TEST_ASSERT(persisted_text != NULL);
    HOST_TEST_ASSERT(strstr(persisted_text, "\"bookId\":\"book-1\"") != NULL);
    HOST_TEST_ASSERT(strstr(persisted_text, "\"source\":\"host-fixture\"") != NULL);
    free(persisted_text);
}

int main(void) {
    ApiContext ctx;
    char temp_dir[PATH_MAX];

    HOST_TEST_ASSERT_INT_EQ(host_test_init_api_context(&ctx, temp_dir, sizeof(temp_dir)), 0);

    assert_shelf_fixture_load(&ctx);
    assert_shelf_round_trip(&ctx);

    host_test_cleanup_api_context(&ctx, temp_dir);
    return 0;
}
