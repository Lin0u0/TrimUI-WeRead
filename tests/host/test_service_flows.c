#include "test_support.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "reader_service.h"
#include "reader_state.h"
#include "session_service.h"
#include "shelf_service.h"

typedef struct {
    cJSON *case_json;
    cJSON *documents;
    cJSON *find_targets;
} ReaderServiceFixtureContext;

static ReaderServiceFixtureContext g_reader_fixture = {0};
static int g_session_validate_result = 1;

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

    doc->target = dup_or_null(json_get_string(doc_json, "target"));
    doc->book_id = dup_or_null(json_get_string(doc_json, "bookId"));
    doc->chapter_uid = dup_or_null(json_get_string(doc_json, "chapterUid"));
    doc->progress_chapter_uid = dup_or_null(json_get_string(doc_json, "progressChapterUid"));
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
        const char *expected_target = json_get_string(doc_json, "target");

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

static void assert_selected_open_cases(cJSON *fixture) {
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

static void assert_reader_open_cases(ApiContext *ctx, cJSON *fixture) {
    cJSON *cases = cJSON_GetObjectItemCaseSensitive(fixture, "openCases");
    ReaderServiceOps ops = {
        .load = reader_service_test_load,
        .find_chapter_target = reader_service_test_find_target,
    };
    int i;

    HOST_TEST_ASSERT(cJSON_IsArray(cases));
    reader_service_set_test_ops(&ops);
    for (i = 0; i < cJSON_GetArraySize(cases); i++) {
        cJSON *case_json = cJSON_GetArrayItem(cases, i);
        cJSON *position_json = cJSON_GetObjectItemCaseSensitive(case_json, "localPosition");
        ReaderOpenResult result;

        g_reader_fixture.case_json = case_json;
        g_reader_fixture.documents = cJSON_GetObjectItemCaseSensitive(case_json, "documents");
        g_reader_fixture.find_targets = cJSON_GetObjectItemCaseSensitive(case_json, "findChapterTargets");

        HOST_TEST_ASSERT(cJSON_IsArray(g_reader_fixture.documents));
        seed_local_position(ctx, position_json);
        HOST_TEST_ASSERT_INT_EQ(
            reader_service_prepare_open_document(
                ctx,
                json_get_string(case_json, "sourceTarget"),
                json_get_string(case_json, "bookIdHint"),
                json_get_int(case_json, "fontSize", 3),
                &result
            ),
            0
        );
        HOST_TEST_ASSERT(strcmp(result.doc.target, json_get_string(case_json, "expectedDocTarget")) == 0);
        HOST_TEST_ASSERT(strcmp(result.source_target, json_get_string(case_json, "expectedSourceTarget")) == 0);
        HOST_TEST_ASSERT_INT_EQ(result.content_font_size, json_get_int(case_json, "expectedContentFontSize", 36));
        HOST_TEST_ASSERT_INT_EQ(result.initial_page, json_get_int(case_json, "expectedInitialPage", 0));
        HOST_TEST_ASSERT_INT_EQ(result.initial_offset, json_get_int(case_json, "expectedInitialOffset", 0));
        HOST_TEST_ASSERT_INT_EQ(result.honor_saved_position, json_get_int(case_json, "expectedHonorSavedPosition", 1));
        reader_document_free(&result.doc);
    }
    reader_service_set_test_ops(NULL);
}

int main(void) {
    ApiContext ctx;
    char temp_dir[PATH_MAX];
    cJSON *shelf_fixture = load_fixture_json("service/shelf_resume_cases.json");
    cJSON *reader_fixture = load_fixture_json("service/reader_open_cases.json");

    HOST_TEST_ASSERT_INT_EQ(host_test_init_api_context(&ctx, temp_dir, sizeof(temp_dir)), 0);

    assert_resume_cases(&ctx, shelf_fixture);
    assert_selected_open_cases(shelf_fixture);
    assert_session_cases(reader_fixture);
    assert_reader_open_cases(&ctx, reader_fixture);

    cJSON_Delete(reader_fixture);
    cJSON_Delete(shelf_fixture);
    host_test_cleanup_api_context(&ctx, temp_dir);
    return 0;
}
