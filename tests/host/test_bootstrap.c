#include "test_support.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "state.h"

static void assert_dir_exists(const char *path) {
    struct stat st;

    HOST_TEST_ASSERT(path != NULL);
    HOST_TEST_ASSERT(stat(path, &st) == 0);
    HOST_TEST_ASSERT(S_ISDIR(st.st_mode));
}

int main(void) {
    ApiContext ctx;
    char temp_dir[PATH_MAX];
    char *fixture_text;
    char *seeded_json;
    char seeded_path[PATH_MAX];

    HOST_TEST_ASSERT_INT_EQ(host_test_init_api_context(&ctx, temp_dir, sizeof(temp_dir)), 0);

    assert_dir_exists(temp_dir);
    assert_dir_exists(ctx.state_dir);
    HOST_TEST_ASSERT(strstr(ctx.cookie_file, "cookies.txt") != NULL);

    fixture_text = host_test_load_fixture("README.md");
    HOST_TEST_ASSERT(fixture_text != NULL);
    HOST_TEST_ASSERT(strstr(fixture_text, "tests/fixtures/state/") != NULL);
    free(fixture_text);

    HOST_TEST_ASSERT_INT_EQ(
        host_test_seed_state_file(
            &ctx,
            STATE_FILE_PREFERENCES,
            "{\n  \"darkMode\": 1,\n  \"readerFontSize\": 40,\n  \"rotation\": 2\n}\n"
        ),
        0
    );
    HOST_TEST_ASSERT(
        snprintf(seeded_path, sizeof(seeded_path), "%s/%s", ctx.state_dir, STATE_FILE_PREFERENCES) <
        (int)sizeof(seeded_path)
    );
    seeded_json = host_test_read_file(seeded_path);
    HOST_TEST_ASSERT(seeded_json != NULL);
    HOST_TEST_ASSERT(strstr(seeded_json, "\"darkMode\": 1") != NULL);
    HOST_TEST_ASSERT(strstr(seeded_json, "\"readerFontSize\": 40") != NULL);
    HOST_TEST_ASSERT(strstr(seeded_json, "\"rotation\": 2") != NULL);
    free(seeded_json);

    host_test_cleanup_api_context(&ctx, temp_dir);
    return 0;
}
