#include "test_support.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "json.h"
#include "preferences_state.h"
#include "reader_service.h"
#include "reader_state.h"
#include "session_service.h"
#include "shelf_service.h"
#include "state.h"

typedef struct {
    cJSON *case_json;
    cJSON *documents;
    cJSON *find_targets;
} ReaderServiceFixtureContext;

static ReaderServiceFixtureContext g_reader_fixture = {0};
static int g_session_validate_result = 1;
static int g_session_remote_logout_result = 0;
static int g_report_progress_results[4] = {0};
static int g_report_progress_result_count = 0;
static int g_report_progress_call_count = 0;
static cJSON *g_article_open_case = NULL;
static cJSON *g_reader_page_case = NULL;

static ReaderDocumentKind reader_kind_from_json(cJSON *json, const char *field_name) {
    const char *kind = json_get_string(json, field_name);

    if (kind && strcmp(kind, "article") == 0) {
        return READER_DOCUMENT_KIND_ARTICLE;
    }
    return READER_DOCUMENT_KIND_BOOK;
}

static char *dup_or_null(const char *value) {
    if (!value) {
        return NULL;
    }
    return strdup(value);
}

static cJSON *load_fixture_json(const char *relative_path) {
    char *text = host_test_load_fixture(relative_path);
    cJSON *json;

    HOST_TEST_ASSERT(text != NULL);
    json = cJSON_Parse(text);
    free(text);
    HOST_TEST_ASSERT(json != NULL);
    return json;
}

static void fill_reader_document_from_json(ReaderDocument *doc, cJSON *doc_json) {
    cJSON *catalog;
    int catalog_count;
    int i;

    memset(doc, 0, sizeof(*doc));
    HOST_TEST_ASSERT(doc_json != NULL);

    doc->kind = reader_kind_from_json(doc_json, "kind");
    doc->target = dup_or_null(json_get_string(doc_json, "target"));
    doc->book_id = dup_or_null(json_get_string(doc_json, "bookId"));
    doc->chapter_uid = dup_or_null(json_get_string(doc_json, "chapterUid"));
    doc->progress_chapter_uid = dup_or_null(json_get_string(doc_json, "progressChapterUid"));
    doc->book_title = dup_or_null(json_get_string(doc_json, "bookTitle"));
    doc->chapter_title = dup_or_null(json_get_string(doc_json, "chapterTitle"));
    doc->content_text = dup_or_null(json_get_string(doc_json, "contentText"));
    doc->chapter_idx = json_get_int(doc_json, "chapterIdx", 0);
    doc->progress_chapter_idx = json_get_int(doc_json, "progressChapterIdx", 0);
    doc->saved_chapter_offset = json_get_int(doc_json, "savedChapterOffset", 0);
    doc->font_size = json_get_int(doc_json, "fontSize", 0);

    catalog = cJSON_GetObjectItemCaseSensitive(doc_json, "catalog");
    if (!catalog || !cJSON_IsArray(catalog)) {
        return;
    }

    catalog_count = cJSON_GetArraySize(catalog);
    if (catalog_count <= 0) {
        return;
    }

    doc->catalog_items = calloc((size_t)catalog_count, sizeof(*doc->catalog_items));
    HOST_TEST_ASSERT(doc->catalog_items != NULL);
    doc->catalog_count = catalog_count;
    for (i = 0; i < catalog_count; i++) {
        cJSON *item_json = cJSON_GetArrayItem(catalog, i);
        ReaderCatalogItem *item = &doc->catalog_items[i];

        item->target = dup_or_null(json_get_string(item_json, "target"));
        item->chapter_uid = dup_or_null(json_get_string(item_json, "chapterUid"));
        item->title = dup_or_null(json_get_string(item_json, "title"));
        item->chapter_idx = json_get_int(item_json, "chapterIdx", 0);
    }
}

static int reader_service_test_load(ApiContext *ctx, const char *target, int font_size,
                                    ReaderDocument *doc) {
    int i;

    (void)ctx;
    (void)font_size;

    HOST_TEST_ASSERT(target != NULL);
    HOST_TEST_ASSERT(g_reader_fixture.documents != NULL);

    for (i = 0; i < cJSON_GetArraySize(g_reader_fixture.documents); i++) {
        cJSON *doc_json = cJSON_GetArrayItem(g_reader_fixture.documents, i);
        const char *expected_target = json_get_string(doc_json, "loadTarget");

        if (!expected_target) {
            expected_target = json_get_string(doc_json, "target");
        }

        if (expected_target && strcmp(expected_target, target) == 0) {
            fill_reader_document_from_json(doc, doc_json);
            return 0;
        }
    }
    return -1;
}

static char *reader_service_test_find_target(ApiContext *ctx, const char *book_id,
                                             const char *chapter_uid, int chapter_idx) {
    int i;

    (void)ctx;

    for (i = 0; g_reader_fixture.find_targets &&
                i < cJSON_GetArraySize(g_reader_fixture.find_targets); i++) {
        cJSON *target_json = cJSON_GetArrayItem(g_reader_fixture.find_targets, i);
        const char *expected_book_id = json_get_string(target_json, "bookId");
        const char *expected_chapter_uid = json_get_string(target_json, "chapterUid");
        int expected_chapter_idx = json_get_int(target_json, "chapterIdx", 0);
        const char *resolved_target = json_get_string(target_json, "target");

        if (expected_book_id && book_id && strcmp(expected_book_id, book_id) != 0) {
            continue;
        }
        if (expected_chapter_uid || chapter_uid) {
            if (!expected_chapter_uid || !chapter_uid ||
                strcmp(expected_chapter_uid, chapter_uid) != 0) {
                continue;
            }
        }
        if (expected_chapter_idx != chapter_idx) {
            continue;
        }
        return dup_or_null(resolved_target);
    }
    return NULL;
}

static int session_service_test_validate(ApiContext *ctx, cJSON **shelf_nuxt_out) {
    (void)ctx;
    (void)shelf_nuxt_out;
    return g_session_validate_result;
}

static int session_service_test_remote_logout(ApiContext *ctx) {
    (void)ctx;
    return g_session_remote_logout_result;
}

static int reader_service_test_report_progress(ApiContext *ctx, const ReaderDocument *doc,
                                               int current_page, int total_pages,
                                               int chapter_offset, int reading_seconds,
                                               const char *page_summary,
                                               int compute_progress) {
    int index = g_report_progress_call_count++;

    (void)ctx;
    (void)doc;
    (void)current_page;
    (void)total_pages;
    (void)chapter_offset;
    (void)reading_seconds;
    (void)page_summary;
    (void)compute_progress;

    if (index >= 0 && index < g_report_progress_result_count) {
        return g_report_progress_results[index];
    }
    return READER_REPORT_OK;
}

static int shelf_service_test_resolve_review_id(ApiContext *ctx, const char *entry_id,
                                                char *review_id, size_t review_id_size) {
    const char *expected_entry_id;
    const char *resolved_review_id;

    (void)ctx;

    if (!g_article_open_case || !entry_id || !review_id || review_id_size == 0) {
        return -1;
    }

    expected_entry_id = json_get_string(g_article_open_case, "entryId");
    if (!expected_entry_id || strcmp(expected_entry_id, entry_id) != 0) {
        return -1;
    }

    resolved_review_id = json_get_string(g_article_open_case, "resolvedReviewId");
    if (!resolved_review_id || !*resolved_review_id) {
        return -1;
    }

    snprintf(review_id, review_id_size, "%s", resolved_review_id);
    return 0;
}

static int reader_test_fetch_page(ApiContext *ctx, const char *url, Buffer *buf) {
    cJSON *fetches;
    const char *fixture_path;
    const char *expected_url;
    char *html;

    (void)ctx;

    if (!g_reader_page_case || !url || !buf) {
        return -1;
    }

    fetches = cJSON_GetObjectItemCaseSensitive(g_reader_page_case, "fetches");
    if (cJSON_IsArray(fetches)) {
        int matched = 0;

        for (int i = 0; i < cJSON_GetArraySize(fetches); i++) {
            cJSON *fetch_json = cJSON_GetArrayItem(fetches, i);
            expected_url = json_get_string(fetch_json, "expectedUrl");
            if (expected_url && strcmp(expected_url, url) == 0) {
                fixture_path = json_get_string(fetch_json, "fixturePath");
                HOST_TEST_ASSERT(fixture_path != NULL);
                html = host_test_load_fixture(fixture_path);
                HOST_TEST_ASSERT(html != NULL);
                buf->data = html;
                buf->size = strlen(html);
                matched = 1;
                break;
            }
        }
        HOST_TEST_ASSERT(matched);
        return 0;
    }

    expected_url = json_get_string(g_reader_page_case, "expectedUrl");
    if (expected_url) {
        HOST_TEST_ASSERT(strcmp(expected_url, url) == 0);
    }

    fixture_path = json_get_string(g_reader_page_case, "fixturePath");
    HOST_TEST_ASSERT(fixture_path != NULL);
    html = host_test_load_fixture(fixture_path);
    HOST_TEST_ASSERT(html != NULL);

    buf->data = html;
    buf->size = strlen(html);
    return 0;
}

static void assert_review_id_response_cases(void) {
    char review_id[128];

    memset(review_id, 0, sizeof(review_id));
    HOST_TEST_ASSERT_INT_EQ(
        shelf_service_extract_review_id_response(
            "MP_WXS_3931271523_sGaYRHC2Jr3EcV4GqbKMGA",
            review_id,
            sizeof(review_id)
        ),
        0
    );
    HOST_TEST_ASSERT(strcmp(review_id, "MP_WXS_3931271523_sGaYRHC2Jr3EcV4GqbKMGA") == 0);

    memset(review_id, 0, sizeof(review_id));
    HOST_TEST_ASSERT_INT_EQ(
        shelf_service_extract_review_id_response(
            "{\"reviewId\":\"review-123\"}",
            review_id,
            sizeof(review_id)
        ),
        0
    );
    HOST_TEST_ASSERT(strcmp(review_id, "review-123") == 0);

    memset(review_id, 0, sizeof(review_id));
    HOST_TEST_ASSERT_INT_EQ(
        shelf_service_extract_review_id_response(
            "{ errCode: int, errMsg: string[1179], succ: int }",
            review_id,
            sizeof(review_id)
        ),
        -1
    );
}

static void seed_local_position(ApiContext *ctx, cJSON *position_json) {
    if (!position_json) {
        return;
    }

    HOST_TEST_ASSERT_INT_EQ(
        reader_state_save_position(
            ctx,
            json_get_string(position_json, "bookId"),
            json_get_string(position_json, "sourceTarget"),
            json_get_string(position_json, "target"),
            json_get_int(position_json, "fontSize", 3),
            json_get_int(position_json, "contentFontSize", 36),
            json_get_int(position_json, "currentPage", 0),
            json_get_int(position_json, "currentOffset", 0)
        ),
        0
    );
}

static void assert_resume_cases(ApiContext *ctx, cJSON *fixture) {
    cJSON *cases = cJSON_GetObjectItemCaseSensitive(fixture, "resumeCases");
    int i;

    HOST_TEST_ASSERT(cJSON_IsArray(cases));
    for (i = 0; i < cJSON_GetArraySize(cases); i++) {
        cJSON *case_json = cJSON_GetArrayItem(cases, i);
        char target[2048] = {0};
        char loading_title[64] = {0};
        char status[128] = {0};
        int font_size = 0;
        int ok;

        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(case_json, "seedLastReader"))) {
            cJSON *last_reader = cJSON_GetObjectItemCaseSensitive(case_json, "lastReader");

            HOST_TEST_ASSERT(last_reader != NULL);
            HOST_TEST_ASSERT_INT_EQ(
                reader_state_save_last_reader(
                    ctx,
                    json_get_string(last_reader, "target"),
                    json_get_int(last_reader, "fontSize", 3),
                    json_get_int(last_reader, "contentFontSize", 36)
                ),
                0
            );
        }

        ok = shelf_service_prepare_resume(ctx, target, sizeof(target), &font_size,
                                          loading_title, sizeof(loading_title),
                                          status, sizeof(status));
        HOST_TEST_ASSERT_INT_EQ(ok, json_get_int(case_json, "expectedOk", 0));
        if (ok == 1) {
            HOST_TEST_ASSERT(strcmp(target, json_get_string(case_json, "expectedTarget")) == 0);
            HOST_TEST_ASSERT_INT_EQ(font_size, json_get_int(case_json, "expectedFontSize", 0));
            HOST_TEST_ASSERT(strcmp(loading_title, json_get_string(case_json, "expectedLoadingTitle")) == 0);
            HOST_TEST_ASSERT(strcmp(status, json_get_string(case_json, "expectedStatus")) == 0);
        }
    }
}

static void assert_selected_open_cases(ApiContext *ctx, cJSON *fixture) {
    cJSON *cases = cJSON_GetObjectItemCaseSensitive(fixture, "selectedOpenCases");
    int i;

    HOST_TEST_ASSERT(cJSON_IsArray(cases));
    for (i = 0; i < cJSON_GetArraySize(cases); i++) {
        cJSON *case_json = cJSON_GetArrayItem(cases, i);
        cJSON *nuxt = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(case_json, "shelfNuxt"), 1);
        char target[2048] = {0};
        char book_id[128] = {0};
        char loading_title[64] = {0};
        char status[128] = {0};
        char shelf_status[128] = {0};
        int ok;

        HOST_TEST_ASSERT(nuxt != NULL);
        ok = shelf_service_prepare_selected_open(
            nuxt,
            json_get_int(case_json, "selected", 0),
            target,
            sizeof(target),
            book_id,
            sizeof(book_id),
            loading_title,
            sizeof(loading_title),
            status,
            sizeof(status),
            shelf_status,
            sizeof(shelf_status)
        );
        HOST_TEST_ASSERT_INT_EQ(ok, json_get_int(case_json, "expectedOk", 0));
        if (ok == 1) {
            HOST_TEST_ASSERT(strcmp(target, json_get_string(case_json, "expectedTarget")) == 0);
            HOST_TEST_ASSERT(strcmp(book_id, json_get_string(case_json, "expectedBookId")) == 0);
            HOST_TEST_ASSERT(strcmp(loading_title, json_get_string(case_json, "expectedLoadingTitle")) == 0);
            HOST_TEST_ASSERT(strcmp(status, json_get_string(case_json, "expectedStatus")) == 0);
        } else {
            HOST_TEST_ASSERT(strcmp(shelf_status, json_get_string(case_json, "expectedShelfStatus")) == 0);
        }
        cJSON_Delete(nuxt);
    }
}

static void assert_article_open_cases(ApiContext *ctx, cJSON *fixture) {
    cJSON *cases = cJSON_GetObjectItemCaseSensitive(fixture, "articleOpenCases");
    int i;

    HOST_TEST_ASSERT(cJSON_IsArray(cases));
    shelf_service_set_review_id_override(shelf_service_test_resolve_review_id);
    for (i = 0; i < cJSON_GetArraySize(cases); i++) {
        cJSON *case_json = cJSON_GetArrayItem(cases, i);
        cJSON *nuxt = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(case_json, "shelfNuxt"), 1);
        char target[2048] = {0};
        char loading_title[64] = {0};
        char status[128] = {0};
        char shelf_status[128] = {0};
        int ok;

        HOST_TEST_ASSERT(nuxt != NULL);
        g_article_open_case = case_json;
        ok = shelf_service_prepare_article_open(
            ctx,
            nuxt,
            json_get_int(case_json, "fontSize", 3),
            target,
            sizeof(target),
            loading_title,
            sizeof(loading_title),
            status,
            sizeof(status),
            shelf_status,
            sizeof(shelf_status)
        );
        HOST_TEST_ASSERT_INT_EQ(ok, json_get_int(case_json, "expectedOk", 0));
        if (ok == 1) {
            HOST_TEST_ASSERT(strcmp(target, json_get_string(case_json, "expectedTarget")) == 0);
            HOST_TEST_ASSERT(strcmp(loading_title, json_get_string(case_json, "expectedLoadingTitle")) == 0);
            HOST_TEST_ASSERT(strcmp(status, json_get_string(case_json, "expectedStatus")) == 0);
        } else {
            HOST_TEST_ASSERT(strcmp(shelf_status, json_get_string(case_json, "expectedShelfStatus")) == 0);
        }
        cJSON_Delete(nuxt);
    }
    g_article_open_case = NULL;
    shelf_service_set_review_id_override(NULL);
}

static void assert_session_cases(cJSON *fixture) {
    cJSON *cases = cJSON_GetObjectItemCaseSensitive(fixture, "sessionCases");
    int i;

    HOST_TEST_ASSERT(cJSON_IsArray(cases));
    session_service_set_validate_session_override(session_service_test_validate);
    for (i = 0; i < cJSON_GetArraySize(cases); i++) {
        cJSON *case_json = cJSON_GetArrayItem(cases, i);
        const char *error_message = "sentinel";
        int result;

        g_session_validate_result = json_get_int(case_json, "validateResult", -1);
        result = session_service_require_valid_session(NULL, &error_message);
        HOST_TEST_ASSERT_INT_EQ(result, json_get_int(case_json, "expectedResult", -1));
        if (result == 1) {
            HOST_TEST_ASSERT(error_message == NULL);
        } else {
            HOST_TEST_ASSERT(strcmp(error_message, json_get_string(case_json, "expectedError")) == 0);
        }
    }
    session_service_set_validate_session_override(NULL);
}

static void seed_session_artifacts(ApiContext *ctx) {
    FILE *cookie_fp;
    char persisted_path[PATH_MAX];

    HOST_TEST_ASSERT_INT_EQ(host_test_seed_state_file(ctx, STATE_FILE_SHELF, "{\"books\":[]}"), 0);
    HOST_TEST_ASSERT_INT_EQ(
        host_test_seed_state_file(ctx, STATE_FILE_LAST_READER, "{\"target\":\"reader-token\"}"),
        0
    );
    HOST_TEST_ASSERT_INT_EQ(
        host_test_seed_state_file(ctx, STATE_FILE_READER_POSITIONS, "{\"book\":[1]}"),
        0
    );
    HOST_TEST_ASSERT_INT_EQ(
        host_test_seed_state_file(
            ctx,
            STATE_FILE_PREFERENCES,
            "{\"darkMode\":1,\"brightnessLevel\":8,\"readerFontSize\":40,\"rotation\":2}"
        ),
        0
    );

    cookie_fp = fopen(ctx->cookie_file, "wb");
    HOST_TEST_ASSERT(cookie_fp != NULL);
    HOST_TEST_ASSERT(fputs("# Netscape HTTP Cookie File\n", cookie_fp) >= 0);
    HOST_TEST_ASSERT_INT_EQ(fclose(cookie_fp), 0);

    HOST_TEST_ASSERT(
        snprintf(persisted_path, sizeof(persisted_path), "%s/%s", ctx->state_dir, STATE_FILE_SHELF) <
        (int)sizeof(persisted_path)
    );
    HOST_TEST_ASSERT(access(persisted_path, F_OK) == 0);
}

static void assert_logout_result_cases(ApiContext *ctx) {
    SessionLogoutResult result = {0};
    char path[PATH_MAX];
    char *preferences_text;

    seed_session_artifacts(ctx);
    g_session_remote_logout_result = -1;
    session_service_set_remote_logout_override(session_service_test_remote_logout);
    HOST_TEST_ASSERT_INT_EQ(session_service_logout(ctx, &result), 0);
    HOST_TEST_ASSERT_INT_EQ(result.outcome, SESSION_LOGOUT_REMOTE_FAILED);
    HOST_TEST_ASSERT_INT_EQ(result.local_cleanup_ok, 1);
    HOST_TEST_ASSERT_INT_EQ(result.remote_attempted, 1);
    HOST_TEST_ASSERT_INT_EQ(result.remote_logout_ok, 0);

    HOST_TEST_ASSERT(access(ctx->cookie_file, F_OK) != 0);
    HOST_TEST_ASSERT(
        snprintf(path, sizeof(path), "%s/%s", ctx->state_dir, STATE_FILE_SHELF) < (int)sizeof(path)
    );
    HOST_TEST_ASSERT(access(path, F_OK) != 0);
    HOST_TEST_ASSERT(
        snprintf(path, sizeof(path), "%s/%s", ctx->state_dir, STATE_FILE_LAST_READER) <
        (int)sizeof(path)
    );
    HOST_TEST_ASSERT(access(path, F_OK) != 0);
    HOST_TEST_ASSERT(
        snprintf(path, sizeof(path), "%s/%s", ctx->state_dir, STATE_FILE_READER_POSITIONS) <
        (int)sizeof(path)
    );
    HOST_TEST_ASSERT(access(path, F_OK) != 0);
    HOST_TEST_ASSERT(
        snprintf(path, sizeof(path), "%s/%s", ctx->state_dir, STATE_FILE_PREFERENCES) <
        (int)sizeof(path)
    );
    preferences_text = host_test_read_file(path);
    HOST_TEST_ASSERT(preferences_text != NULL);
    HOST_TEST_ASSERT(strstr(preferences_text, "\"darkMode\":1") != NULL);
    HOST_TEST_ASSERT(strstr(preferences_text, "\"readerFontSize\":40") != NULL);
    free(preferences_text);
    session_service_set_remote_logout_override(NULL);
}

static void assert_logout_success_case(ApiContext *ctx) {
    SessionLogoutResult result = {0};
    char path[PATH_MAX];

    seed_session_artifacts(ctx);
    g_session_remote_logout_result = 0;
    session_service_set_remote_logout_override(session_service_test_remote_logout);
    HOST_TEST_ASSERT_INT_EQ(session_service_logout(ctx, &result), 0);
    HOST_TEST_ASSERT_INT_EQ(result.outcome, SESSION_LOGOUT_SUCCESS);
    HOST_TEST_ASSERT_INT_EQ(result.local_cleanup_ok, 1);
    HOST_TEST_ASSERT_INT_EQ(result.remote_attempted, 1);
    HOST_TEST_ASSERT_INT_EQ(result.remote_logout_ok, 1);

    HOST_TEST_ASSERT(access(ctx->cookie_file, F_OK) != 0);
    HOST_TEST_ASSERT(
        snprintf(path, sizeof(path), "%s/%s", ctx->state_dir, STATE_FILE_SHELF) < (int)sizeof(path)
    );
    HOST_TEST_ASSERT(access(path, F_OK) != 0);
    HOST_TEST_ASSERT(
        snprintf(path, sizeof(path), "%s/%s", ctx->state_dir, STATE_FILE_LAST_READER) <
        (int)sizeof(path)
    );
    HOST_TEST_ASSERT(access(path, F_OK) != 0);
    HOST_TEST_ASSERT(
        snprintf(path, sizeof(path), "%s/%s", ctx->state_dir, STATE_FILE_READER_POSITIONS) <
        (int)sizeof(path)
    );
    HOST_TEST_ASSERT(access(path, F_OK) != 0);
    session_service_set_remote_logout_override(NULL);
}

static void assert_logout_local_failure_case(ApiContext *ctx) {
    SessionLogoutResult result = {0};

    session_service_set_remote_logout_override(session_service_test_remote_logout);
    HOST_TEST_ASSERT_INT_EQ(session_service_logout(NULL, &result), -1);
    HOST_TEST_ASSERT_INT_EQ(result.outcome, SESSION_LOGOUT_LOCAL_FAILED);
    HOST_TEST_ASSERT_INT_EQ(result.local_cleanup_ok, 0);
    HOST_TEST_ASSERT_INT_EQ(result.remote_attempted, 0);
    HOST_TEST_ASSERT_INT_EQ(result.remote_logout_ok, 0);
    session_service_set_remote_logout_override(NULL);

    HOST_TEST_ASSERT(ctx != NULL);
}

static void assert_logout_local_failure_preserves_artifacts(ApiContext *ctx) {
    SessionLogoutResult result = {0};
    char shelf_path[PATH_MAX];
    char last_reader_path[PATH_MAX];
    char positions_path[PATH_MAX];

    seed_session_artifacts(ctx);
    HOST_TEST_ASSERT(
        snprintf(shelf_path, sizeof(shelf_path), "%s/%s", ctx->state_dir, STATE_FILE_SHELF) <
        (int)sizeof(shelf_path)
    );
    HOST_TEST_ASSERT_INT_EQ(unlink(shelf_path), 0);
    HOST_TEST_ASSERT_INT_EQ(mkdir(shelf_path, 0700), 0);
    HOST_TEST_ASSERT(
        snprintf(last_reader_path, sizeof(last_reader_path), "%s/%s",
                 ctx->state_dir, STATE_FILE_LAST_READER) < (int)sizeof(last_reader_path)
    );
    HOST_TEST_ASSERT(
        snprintf(positions_path, sizeof(positions_path), "%s/%s",
                 ctx->state_dir, STATE_FILE_READER_POSITIONS) < (int)sizeof(positions_path)
    );

    session_service_set_remote_logout_override(session_service_test_remote_logout);
    HOST_TEST_ASSERT_INT_EQ(session_service_logout(ctx, &result), -1);
    HOST_TEST_ASSERT_INT_EQ(result.outcome, SESSION_LOGOUT_LOCAL_FAILED);
    HOST_TEST_ASSERT_INT_EQ(result.local_cleanup_ok, 0);
    HOST_TEST_ASSERT_INT_EQ(result.remote_attempted, 0);
    HOST_TEST_ASSERT_INT_EQ(result.remote_logout_ok, 0);
    HOST_TEST_ASSERT(access(ctx->cookie_file, F_OK) == 0);
    HOST_TEST_ASSERT(access(shelf_path, F_OK) == 0);
    HOST_TEST_ASSERT(access(last_reader_path, F_OK) == 0);
    HOST_TEST_ASSERT(access(positions_path, F_OK) == 0);
    session_service_set_remote_logout_override(NULL);

    HOST_TEST_ASSERT_INT_EQ(rmdir(shelf_path), 0);
    HOST_TEST_ASSERT_INT_EQ(host_test_seed_state_file(ctx, STATE_FILE_SHELF, "{\"books\":[]}"), 0);
}

static void assert_logout_requires_login_on_startup(ApiContext *ctx) {
    SessionLogoutResult result = {0};
    int poor_network = 1;
    int startup_result;

    seed_session_artifacts(ctx);
    g_session_remote_logout_result = 0;
    session_service_set_remote_logout_override(session_service_test_remote_logout);
    HOST_TEST_ASSERT_INT_EQ(session_service_logout(ctx, &result), 0);
    session_service_set_remote_logout_override(NULL);

    HOST_TEST_ASSERT_INT_EQ(setenv("WEREAD_TEST_SESSION_STATUS", "0", 1), 0);
    startup_result = session_service_startup_refresh(ctx, NULL, &poor_network);
    HOST_TEST_ASSERT_INT_EQ(startup_result, 0);
    HOST_TEST_ASSERT_INT_EQ(poor_network, 0);
    HOST_TEST_ASSERT_INT_EQ(unsetenv("WEREAD_TEST_SESSION_STATUS"), 0);
}

static void assert_reader_open_cases(ApiContext *ctx, cJSON *fixture) {
    cJSON *cases = cJSON_GetObjectItemCaseSensitive(fixture, "openCases");
    ReaderServiceOps ops = {
        .load = reader_service_test_load,
        .find_chapter_target = reader_service_test_find_target,
    };
    int i;

    HOST_TEST_ASSERT(cJSON_IsArray(cases));
    HOST_TEST_ASSERT_INT_EQ(preferences_state_save_reader_font_size(ctx, 36), 0);
    reader_service_set_test_ops(&ops);
    for (i = 0; i < cJSON_GetArraySize(cases); i++) {
        cJSON *case_json = cJSON_GetArrayItem(cases, i);
        cJSON *position_json = cJSON_GetObjectItemCaseSensitive(case_json, "localPosition");
        ReaderOpenResult result;
        int expected_result;

        g_reader_fixture.case_json = case_json;
        g_reader_fixture.documents = cJSON_GetObjectItemCaseSensitive(case_json, "documents");
        g_reader_fixture.find_targets = cJSON_GetObjectItemCaseSensitive(case_json, "findChapterTargets");

        HOST_TEST_ASSERT(cJSON_IsArray(g_reader_fixture.documents));
        seed_local_position(ctx, position_json);
        expected_result = json_get_int(case_json, "expectedResult", 0);
        HOST_TEST_ASSERT_INT_EQ(
            reader_service_prepare_open_document(
                ctx,
                json_get_string(case_json, "sourceTarget"),
                json_get_string(case_json, "bookIdHint"),
                json_get_int(case_json, "fontSize", 3),
                &result
            ),
            expected_result
        );
        if (expected_result != 0) {
            continue;
        }
        HOST_TEST_ASSERT(strcmp(result.doc.target, json_get_string(case_json, "expectedDocTarget")) == 0);
        HOST_TEST_ASSERT(strcmp(result.source_target, json_get_string(case_json, "expectedSourceTarget")) == 0);
        HOST_TEST_ASSERT_INT_EQ(result.content_font_size, json_get_int(case_json, "expectedContentFontSize", 36));
        HOST_TEST_ASSERT_INT_EQ(result.initial_page, json_get_int(case_json, "expectedInitialPage", 0));
        HOST_TEST_ASSERT_INT_EQ(result.initial_offset, json_get_int(case_json, "expectedInitialOffset", 0));
        HOST_TEST_ASSERT_INT_EQ(result.honor_saved_position, json_get_int(case_json, "expectedHonorSavedPosition", 1));
        HOST_TEST_ASSERT_INT_EQ(result.doc.kind, reader_kind_from_json(case_json, "expectedDocKind"));
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(case_json, "expectNoDocBookId"))) {
            HOST_TEST_ASSERT(result.doc.book_id == NULL || result.doc.book_id[0] == '\0');
        }
        reader_document_free(&result.doc);
    }
    reader_service_set_test_ops(NULL);
}

static void assert_reader_page_cases(ApiContext *ctx, cJSON *fixture) {
    cJSON *cases = cJSON_GetObjectItemCaseSensitive(fixture, "pageCases");
    int i;

    HOST_TEST_ASSERT(cJSON_IsArray(cases));
    reader_set_fetch_override(reader_test_fetch_page);
    for (i = 0; i < cJSON_GetArraySize(cases); i++) {
        cJSON *case_json = cJSON_GetArrayItem(cases, i);
        ReaderDocument doc;
        int result;

        g_reader_page_case = case_json;
        result = reader_load(ctx,
                             json_get_string(case_json, "target"),
                             json_get_int(case_json, "fontSize", 3),
                             &doc);
        HOST_TEST_ASSERT_INT_EQ(result, json_get_int(case_json, "expectedResult", 0));
        if (result == 0) {
            cJSON *expected_catalog_count;
            cJSON *expected_current_chapter_idx;
            cJSON *expected_catalog_titles;
            cJSON *expected_use_content_font;
            const char *expected_chapter_title;
            const char *expected_current_chapter_uid;
            const char *expected_prev_target;
            const char *expected_next_target;
            const char *unexpected_content;

            HOST_TEST_ASSERT_INT_EQ(doc.kind, reader_kind_from_json(case_json, "expectedDocKind"));
            HOST_TEST_ASSERT(strcmp(doc.target, json_get_string(case_json, "expectedDocTarget")) == 0);
            HOST_TEST_ASSERT(strcmp(doc.book_title, json_get_string(case_json, "expectedBookTitle")) == 0);
            expected_chapter_title = json_get_string(case_json, "expectedChapterTitle");
            if (expected_chapter_title) {
                HOST_TEST_ASSERT(doc.chapter_title != NULL);
                HOST_TEST_ASSERT(strcmp(doc.chapter_title, expected_chapter_title) == 0);
            }
            HOST_TEST_ASSERT(doc.content_text != NULL);
            HOST_TEST_ASSERT(strstr(doc.content_text,
                                    json_get_string(case_json, "expectedContentContains")) != NULL);
            unexpected_content = json_get_string(case_json, "unexpectedContentContains");
            if (unexpected_content && unexpected_content[0]) {
                HOST_TEST_ASSERT(strstr(doc.content_text, unexpected_content) == NULL);
            }
            expected_catalog_count =
                cJSON_GetObjectItemCaseSensitive(case_json, "expectedCatalogCount");
            if (cJSON_IsNumber(expected_catalog_count)) {
                HOST_TEST_ASSERT_INT_EQ(doc.catalog_count, expected_catalog_count->valueint);
                HOST_TEST_ASSERT_INT_EQ(doc.catalog_total_count, expected_catalog_count->valueint);
            }
            expected_current_chapter_uid =
                json_get_string(case_json, "expectedCurrentChapterUid");
            if (expected_current_chapter_uid) {
                HOST_TEST_ASSERT(doc.chapter_uid != NULL);
                HOST_TEST_ASSERT(strcmp(doc.chapter_uid, expected_current_chapter_uid) == 0);
            }
            expected_current_chapter_idx =
                cJSON_GetObjectItemCaseSensitive(case_json, "expectedCurrentChapterIdx");
            if (cJSON_IsNumber(expected_current_chapter_idx)) {
                HOST_TEST_ASSERT_INT_EQ(doc.chapter_idx, expected_current_chapter_idx->valueint);
            }
            expected_prev_target = json_get_string(case_json, "expectedPrevTarget");
            if (expected_prev_target) {
                HOST_TEST_ASSERT(doc.prev_target != NULL);
                HOST_TEST_ASSERT(strcmp(doc.prev_target, expected_prev_target) == 0);
            }
            expected_next_target = json_get_string(case_json, "expectedNextTarget");
            if (expected_next_target) {
                HOST_TEST_ASSERT(doc.next_target != NULL);
                HOST_TEST_ASSERT(strcmp(doc.next_target, expected_next_target) == 0);
            }
            expected_catalog_titles =
                cJSON_GetObjectItemCaseSensitive(case_json, "expectedCatalogTitles");
            if (cJSON_IsArray(expected_catalog_titles)) {
                HOST_TEST_ASSERT_INT_EQ(doc.catalog_count,
                                        cJSON_GetArraySize(expected_catalog_titles));
                for (int j = 0; j < cJSON_GetArraySize(expected_catalog_titles); j++) {
                    cJSON *title_json = cJSON_GetArrayItem(expected_catalog_titles, j);

                    HOST_TEST_ASSERT(cJSON_IsString(title_json));
                    HOST_TEST_ASSERT(doc.catalog_items != NULL);
                    HOST_TEST_ASSERT(doc.catalog_items[j].title != NULL);
                    HOST_TEST_ASSERT(strcmp(doc.catalog_items[j].title,
                                            title_json->valuestring) == 0);
                }
            }
            expected_use_content_font =
                cJSON_GetObjectItemCaseSensitive(case_json, "expectedUseContentFont");
            if (cJSON_IsBool(expected_use_content_font)) {
                HOST_TEST_ASSERT_INT_EQ(doc.use_content_font, cJSON_IsTrue(expected_use_content_font) ? 1 : 0);
            }
            reader_document_free(&doc);
        }
    }
    g_reader_page_case = NULL;
    reader_set_fetch_override(NULL);
}

static void assert_reader_preference_fallback(ApiContext *ctx) {
    ReaderServiceOps ops = {
        .load = reader_service_test_load,
        .find_chapter_target = reader_service_test_find_target,
    };
    cJSON *documents = cJSON_Parse(
        "[{\"target\":\"chapter-pref\",\"bookId\":\"pref-book\",\"chapterUid\":\"c1\","
        "\"chapterIdx\":1,\"fontSize\":3,\"savedChapterOffset\":0,\"catalog\":[]}]"
    );
    ReaderOpenResult result;

    HOST_TEST_ASSERT(documents != NULL);
    HOST_TEST_ASSERT_INT_EQ(preferences_state_save_reader_font_size(ctx, 44), 0);

    g_reader_fixture.case_json = NULL;
    g_reader_fixture.documents = documents;
    g_reader_fixture.find_targets = NULL;
    reader_service_set_test_ops(&ops);
    HOST_TEST_ASSERT_INT_EQ(
        reader_service_prepare_open_document(ctx, "chapter-pref", "pref-book", 3, &result),
        0
    );
    HOST_TEST_ASSERT_INT_EQ(result.content_font_size, 44);
    reader_document_free(&result.doc);
    reader_service_set_test_ops(NULL);
    cJSON_Delete(documents);
}

static void assert_progress_retry_behavior(ApiContext *ctx) {
    ReaderDocument doc = {
        .kind = READER_DOCUMENT_KIND_BOOK,
        .book_id = (char *)"book-1",
        .token = (char *)"token-1",
        .chapter_uid = (char *)"chapter-1",
        .chapter_idx = 1,
    };

    HOST_TEST_ASSERT(ctx != NULL);

    reader_service_set_report_progress_override(reader_service_test_report_progress);

    g_report_progress_call_count = 0;
    g_report_progress_result_count = 1;
    g_report_progress_results[0] = READER_REPORT_SESSION_EXPIRED;
    HOST_TEST_ASSERT_INT_EQ(
        reader_service_report_progress_with_retry(ctx->data_dir, ctx->ca_file,
                                                  &doc, 0, 1, 0, 0, "", 0),
        READER_REPORT_SESSION_EXPIRED
    );
    HOST_TEST_ASSERT_INT_EQ(g_report_progress_call_count, 1);

    g_report_progress_call_count = 0;
    g_report_progress_result_count = 2;
    g_report_progress_results[0] = READER_REPORT_ERROR;
    g_report_progress_results[1] = READER_REPORT_OK;
    HOST_TEST_ASSERT_INT_EQ(
        reader_service_report_progress_with_retry(ctx->data_dir, ctx->ca_file,
                                                  &doc, 0, 1, 0, 0, "", 0),
        READER_REPORT_OK
    );
    HOST_TEST_ASSERT_INT_EQ(g_report_progress_call_count, 2);

    reader_service_set_report_progress_override(NULL);
    g_report_progress_result_count = 0;
    g_report_progress_call_count = 0;
}

int main(void) {
    ApiContext ctx;
    char temp_dir[PATH_MAX];
    cJSON *shelf_fixture = load_fixture_json("service/shelf_resume_cases.json");
    cJSON *reader_fixture = load_fixture_json("service/reader_open_cases.json");

    HOST_TEST_ASSERT_INT_EQ(host_test_init_api_context(&ctx, temp_dir, sizeof(temp_dir)), 0);

    assert_review_id_response_cases();
    assert_resume_cases(&ctx, shelf_fixture);
    assert_selected_open_cases(&ctx, shelf_fixture);
    assert_article_open_cases(&ctx, shelf_fixture);
    assert_session_cases(reader_fixture);
    assert_logout_success_case(&ctx);
    assert_logout_result_cases(&ctx);
    assert_logout_local_failure_case(&ctx);
    assert_logout_local_failure_preserves_artifacts(&ctx);
    assert_logout_requires_login_on_startup(&ctx);
    assert_reader_open_cases(&ctx, reader_fixture);
    assert_reader_page_cases(&ctx, reader_fixture);
    assert_reader_preference_fallback(&ctx);
    assert_progress_retry_behavior(&ctx);

    cJSON_Delete(reader_fixture);
    cJSON_Delete(shelf_fixture);
    host_test_cleanup_api_context(&ctx, temp_dir);
    return 0;
}
