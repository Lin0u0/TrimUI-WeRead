#include "test_support.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "preferences_state.h"
#include "state.h"

static void assert_preferences_fixture(ApiContext *ctx) {
    int brightness_level = 0;
    int reader_font_size = 0;
    int rotation = -1;
    char *fixture = host_test_load_fixture("state/preferences.json");

    HOST_TEST_ASSERT(fixture != NULL);
    HOST_TEST_ASSERT_INT_EQ(host_test_seed_state_file(ctx, STATE_FILE_PREFERENCES, fixture), 0);
    free(fixture);

    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_dark_mode(ctx), 1);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_brightness_level(ctx, &brightness_level), 0);
    HOST_TEST_ASSERT_INT_EQ(brightness_level, 8);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_reader_font_size(ctx, &reader_font_size), 0);
    HOST_TEST_ASSERT_INT_EQ(reader_font_size, 40);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_rotation(ctx, &rotation), 0);
    HOST_TEST_ASSERT_INT_EQ(rotation, 2);
}

static void assert_preferences_round_trip(ApiContext *ctx) {
    char persisted_path[PATH_MAX];
    char *persisted_text;
    int brightness_level = 0;
    int reader_font_size = 0;
    int rotation = -1;

    HOST_TEST_ASSERT_INT_EQ(preferences_state_save_dark_mode(ctx, 0), 0);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_save_brightness_level(ctx, 6), 0);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_save_reader_font_size(ctx, 44), 0);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_save_rotation(ctx, 1), 0);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_dark_mode(ctx), 0);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_brightness_level(ctx, &brightness_level), 0);
    HOST_TEST_ASSERT_INT_EQ(brightness_level, 6);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_reader_font_size(ctx, &reader_font_size), 0);
    HOST_TEST_ASSERT_INT_EQ(reader_font_size, 44);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_rotation(ctx, &rotation), 0);
    HOST_TEST_ASSERT_INT_EQ(rotation, 1);

    HOST_TEST_ASSERT(
        snprintf(persisted_path, sizeof(persisted_path), "%s/%s", ctx->state_dir, STATE_FILE_PREFERENCES) <
        (int)sizeof(persisted_path)
    );
    persisted_text = host_test_read_file(persisted_path);
    HOST_TEST_ASSERT(persisted_text != NULL);
    HOST_TEST_ASSERT(strstr(persisted_text, "\"darkMode\":0") != NULL);
    HOST_TEST_ASSERT(strstr(persisted_text, "\"brightnessLevel\":6") != NULL);
    HOST_TEST_ASSERT(strstr(persisted_text, "\"readerFontSize\":44") != NULL);
    HOST_TEST_ASSERT(strstr(persisted_text, "\"rotation\":1") != NULL);
    free(persisted_text);
}

static void assert_preferences_missing_key_fallbacks(ApiContext *ctx) {
    int brightness_level = 123;
    int reader_font_size = 123;
    int rotation = 123;

    HOST_TEST_ASSERT_INT_EQ(host_test_seed_state_file(ctx, STATE_FILE_PREFERENCES, "{}"), 0);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_dark_mode(ctx), 0);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_brightness_level(ctx, &brightness_level), -1);
    HOST_TEST_ASSERT_INT_EQ(brightness_level, 123);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_brightness_level(ctx, NULL), -1);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_reader_font_size(ctx, &reader_font_size), -1);
    HOST_TEST_ASSERT_INT_EQ(reader_font_size, 123);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_reader_font_size(ctx, NULL), -1);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_rotation(ctx, &rotation), -1);
    HOST_TEST_ASSERT_INT_EQ(rotation, 123);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_load_rotation(ctx, NULL), -1);
}

int main(void) {
    ApiContext ctx;
    char temp_dir[PATH_MAX];

    HOST_TEST_ASSERT_INT_EQ(host_test_init_api_context(&ctx, temp_dir, sizeof(temp_dir)), 0);

    assert_preferences_fixture(&ctx);
    assert_preferences_round_trip(&ctx);
    assert_preferences_missing_key_fallbacks(&ctx);

    host_test_cleanup_api_context(&ctx, temp_dir);
    return 0;
}
