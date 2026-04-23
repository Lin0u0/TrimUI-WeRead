#include "test_support.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "json.h"
#include "parser_internal.h"
#include "reader_internal.h"

static char *extract_section(const char *text, const char *name) {
    char marker[64];
    const char *start;
    const char *end;

    HOST_TEST_ASSERT(
        snprintf(marker, sizeof(marker), "[%s]", name) > 0 &&
        snprintf(marker, sizeof(marker), "[%s]", name) < (int)sizeof(marker)
    );
    start = strstr(text, marker);
    HOST_TEST_ASSERT(start != NULL);
    start += strlen(marker);
    if (*start == '\n') {
        start++;
    }
    end = strstr(start, "\n[");
    if (!end) {
        end = text + strlen(text);
    }
    return parser_dup_range(start, end);
}

static void assert_parser_common_fragments(void) {
    char *fragments = host_test_load_fixture("parser/parser_fragments.txt");
    char *skip_ws_section;
    char *matching_pair_section;
    char *dup_range_section;
    const char *trimmed;
    const char *match_end;
    char *dup;

    HOST_TEST_ASSERT(fragments != NULL);
    skip_ws_section = extract_section(fragments, "skip_ws");
    matching_pair_section = extract_section(fragments, "matching_pair");
    dup_range_section = extract_section(fragments, "dup_range");

    trimmed = parser_skip_ws(skip_ws_section, skip_ws_section + strlen(skip_ws_section));
    HOST_TEST_ASSERT(strcmp(trimmed, "reader-start") == 0);

    match_end = parser_find_matching_pair(
        matching_pair_section,
        matching_pair_section + strlen(matching_pair_section),
        '{',
        '}'
    );
    HOST_TEST_ASSERT(match_end != NULL);
    HOST_TEST_ASSERT(*(match_end + 1) == '\0');

    dup = parser_dup_range(dup_range_section, strchr(dup_range_section, ','));
    HOST_TEST_ASSERT(dup != NULL);
    HOST_TEST_ASSERT(strcmp(dup, "chapterUid:aliasUid") == 0);

    free(dup);
    free(dup_range_section);
    free(matching_pair_section);
    free(skip_ws_section);
    free(fragments);
}

static void assert_api_extract_nuxt_fixture(void) {
    cJSON *nuxt;
    cJSON *meta;
    cJSON *has_more;
    char *html = host_test_load_fixture("parser/shelf_nuxt.html");

    HOST_TEST_ASSERT(html != NULL);
    nuxt = api_extract_nuxt(html, strlen(html));
    HOST_TEST_ASSERT(nuxt != NULL);
    HOST_TEST_ASSERT_INT_EQ(json_get_int(nuxt, "shelfCount", -1), 2);
    has_more = cJSON_GetObjectItemCaseSensitive(nuxt, "hasMore");
    HOST_TEST_ASSERT(cJSON_IsTrue(has_more));
    meta = cJSON_GetObjectItemCaseSensitive(nuxt, "meta");
    HOST_TEST_ASSERT(cJSON_IsObject(meta));
    HOST_TEST_ASSERT(strcmp(json_get_string(meta, "source"), "fixture") == 0);

    cJSON_Delete(nuxt);
    free(html);
}

static void assert_reader_parser_helpers(void) {
    char *html = host_test_load_fixture("parser/reader_alias.html");
    const char *block_start;
    const char *block_end;
    const char *value_start = NULL;
    const char *field;
    char *title;
    char *chapter_uid;
    char *toc;
    char *literal;
    char joined_path[PATH_MAX];

    HOST_TEST_ASSERT(html != NULL);
    block_start = strstr(html, "{\n        title:b,");
    HOST_TEST_ASSERT(block_start != NULL);
    block_end = strstr(block_start, "\n      };");
    HOST_TEST_ASSERT(block_end != NULL);

    title = reader_resolve_nuxt_alias_string(html, "b");
    HOST_TEST_ASSERT(title != NULL);
    HOST_TEST_ASSERT(strcmp(title, "Chapter Alias 1") == 0);

    literal = reader_resolve_nuxt_alias_literal(html, "d");
    HOST_TEST_ASSERT(literal != NULL);
    HOST_TEST_ASSERT(strcmp(literal, "[]") == 0);

    field = reader_find_top_level_field(block_start, block_end, "title:", &value_start);
    HOST_TEST_ASSERT(field != NULL);
    HOST_TEST_ASSERT(value_start != NULL);
    HOST_TEST_ASSERT(*value_start == 'b');

    chapter_uid = reader_extract_resolved_value_after_marker(html, block_start, block_end, "chapterUid:");
    HOST_TEST_ASSERT(chapter_uid != NULL);
    HOST_TEST_ASSERT(strcmp(chapter_uid, "101") == 0);

    toc = reader_extract_container_from_slice(block_start, block_end, "toc:", '[', ']');
    HOST_TEST_ASSERT(toc != NULL);
    HOST_TEST_ASSERT(strstr(toc, "chapterUid:\"101\"") != NULL);
    HOST_TEST_ASSERT(strstr(toc, "title:\"Next\"") != NULL);

    HOST_TEST_ASSERT_INT_EQ(
        reader_join_path_checked(joined_path, sizeof(joined_path), "/tmp/weread", "chapter.json"),
        0
    );
    HOST_TEST_ASSERT(strcmp(joined_path, "/tmp/weread/chapter.json") == 0);
    HOST_TEST_ASSERT(reader_dup_or_null(NULL) == NULL);

    free(toc);
    free(chapter_uid);
    free(literal);
    free(title);
    free(html);
}

static void assert_reader_alias_string_decoding(void) {
    const char *html =
        "<script>window.__NUXT__=(function(a){return {title:a}}('Ch\\u0061pter\\'s'));</script>";
    char *title = reader_resolve_nuxt_alias_string(html, "a");

    HOST_TEST_ASSERT(title != NULL);
    HOST_TEST_ASSERT(strcmp(title, "Chapter's") == 0);
    free(title);
}

int main(void) {
    assert_parser_common_fragments();
    assert_api_extract_nuxt_fixture();
    assert_reader_parser_helpers();
    assert_reader_alias_string_decoding();
    return 0;
}
