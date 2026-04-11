#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "reader.h"
#include "reader_internal.h"
#include "html_strip.h"
#include "json.h"
#include "reader_state.h"

#define WEREAD_RANDOM_FONT_URL "https://cdn.weread.qq.com/app/assets/test-font/fzys_reversed.ttf"

static ReaderFetchPageFn reader_fetch_override = NULL;

static char *resolve_nuxt_alias_string(const char *html, const char *alias);
static char *resolve_nuxt_alias_literal(const char *html, const char *alias);
static int join_path_checked(char *dst, size_t dst_size, const char *dir, const char *name);
static char *reader_extract_query_value(const char *target, const char *key);
static int reader_parse_article_catalog(ApiContext *ctx, const char *html,
                                        const char *article_block_start,
                                        const char *article_block_end,
                                        int font_size,
                                        const char *current_review_id,
                                        ReaderCatalogItem **items_out,
                                        int *count_out,
                                        int *current_index_out);
static int reader_parse_article_document(ApiContext *ctx, const char *target, int font_size,
                                         const char *html, ReaderDocument *doc);

static void reader_add_chapter_uid_field(cJSON *payload, const char *chapter_uid) {
    const unsigned char *p = (const unsigned char *)chapter_uid;

    if (!payload || !chapter_uid || !chapter_uid[0]) {
        return;
    }

    while (*p) {
        if (!isdigit(*p)) {
            cJSON_AddStringToObject(payload, "c", chapter_uid);
            return;
        }
        p++;
    }

    cJSON_AddNumberToObject(payload, "c", atoi(chapter_uid));
}

static const char *skip_ws(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
        p++;
    }
    return p;
}

static const char *find_matching_pair(const char *start, const char *end, char open_ch, char close_ch) {
    int depth = 0;
    int in_string = 0;
    char quote = 0;

    for (const char *p = start; p < end; p++) {
        if (in_string) {
            if (*p == '\\' && p + 1 < end) {
                p++;
                continue;
            }
            if (*p == quote) {
                in_string = 0;
            }
            continue;
        }

        if (*p == '"' || *p == '\'') {
            in_string = 1;
            quote = *p;
            continue;
        }

        if (*p == open_ch) {
            depth++;
        } else if (*p == close_ch) {
            depth--;
            if (depth == 0) {
                return p;
            }
        }
    }

    return NULL;
}

static char *dup_range(const char *start, const char *end) {
    size_t len = (size_t)(end - start);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

static char *dup_or_null_reader(const char *s) {
    return s ? strdup(s) : NULL;
}

static const char *reader_choose_summary(const ReaderDocument *doc, const char *page_summary) {
    if (page_summary && page_summary[0]) {
        return page_summary;
    }
    if (doc && doc->progress_summary && doc->progress_summary[0]) {
        return doc->progress_summary;
    }
    return "";
}

static int join_path_checked(char *dst, size_t dst_size, const char *dir, const char *name) {
    size_t dir_len;
    size_t name_len;

    if (!dst || dst_size == 0 || !dir || !name) {
        return READER_REPORT_ERROR;
    }

    dir_len = strlen(dir);
    name_len = strlen(name);
    if (dir_len + 1 + name_len + 1 > dst_size) {
        dst[0] = '\0';
        return -1;
    }

    memcpy(dst, dir, dir_len);
    dst[dir_len] = '/';
    memcpy(dst + dir_len + 1, name, name_len + 1);
    return 0;
}

static const char *find_top_level_field(const char *slice_start, const char *slice_end,
                                        const char *field_marker) {
    const char *limit;
    int brace_depth = 0;
    int bracket_depth = 0;
    int in_string = 0;
    char quote = 0;
    size_t marker_len;

    if (!slice_start || !slice_end || !field_marker) {
        return NULL;
    }
    marker_len = strlen(field_marker);
    if (marker_len == 0 || slice_end <= slice_start) {
        return NULL;
    }

    if (*slice_start == '{') {
        brace_depth = 1;
        slice_start++;
    }
    limit = slice_end - marker_len;
    for (const char *p = slice_start; p <= limit; p++) {
        if (in_string) {
            if (*p == '\\' && p + 1 < slice_end) {
                p++;
                continue;
            }
            if (*p == quote) {
                in_string = 0;
            }
            continue;
        }

        if (*p == '"' || *p == '\'') {
            in_string = 1;
            quote = *p;
            continue;
        }
        if (*p == '{') {
            brace_depth++;
            continue;
        }
        if (*p == '}') {
            brace_depth--;
            continue;
        }
        if (*p == '[') {
            bracket_depth++;
            continue;
        }
        if (*p == ']') {
            bracket_depth--;
            continue;
        }

        if (brace_depth == 1 && bracket_depth == 0 &&
            strncmp(p, field_marker, marker_len) == 0) {
            return p;
        }
    }

    return NULL;
}

static char *extract_attr_text(const char *html, const char *marker) {
    const char *start = strstr(html, marker);
    const char *end;

    if (!start) {
        return NULL;
    }
    start += strlen(marker);
    end = strchr(start, '<');
    if (!end || end <= start) {
        return NULL;
    }
    return dup_range(start, end);
}

static int tag_has_id(const char *tag_start, const char *tag_end, const char *id_value) {
    size_t id_len;
    const char *p;

    if (!tag_start || !tag_end || !id_value || tag_end <= tag_start) {
        return 0;
    }

    id_len = strlen(id_value);
    for (p = tag_start; p + 4 + id_len < tag_end; p++) {
        if (strncmp(p, "id=\"", 4) == 0 &&
            strncmp(p + 4, id_value, id_len) == 0 &&
            p[4 + id_len] == '"') {
            return 1;
        }
        if (strncmp(p, "id='", 4) == 0 &&
            strncmp(p + 4, id_value, id_len) == 0 &&
            p[4 + id_len] == '\'') {
            return 1;
        }
    }

    return 0;
}

static const char *find_div_with_id(const char *html, const char *id_value) {
    const char *p = html;

    if (!html || !id_value || !id_value[0]) {
        return NULL;
    }

    while ((p = strstr(p, "<div")) != NULL) {
        const char *tag_end = strchr(p, '>');

        if (!tag_end) {
            return NULL;
        }
        if (tag_has_id(p, tag_end, id_value)) {
            return p;
        }
        p = tag_end + 1;
    }

    return NULL;
}

static const char *find_matching_div_close(const char *start) {
    const char *p = start;
    int depth = 1;

    while (*p) {
        const char *open_div = strstr(p, "<div");
        const char *close_div = strstr(p, "</div>");

        if (!close_div) {
            return NULL;
        }
        if (open_div && open_div < close_div) {
            depth++;
            p = open_div + 4;
            continue;
        }

        depth--;
        if (depth == 0) {
            return close_div;
        }
        p = close_div + 6;
    }

    return NULL;
}

static char *extract_div_inner_html(const char *div_open) {
    const char *start;
    const char *end;

    if (!div_open) {
        return NULL;
    }

    start = strchr(div_open, '>');
    if (!start) {
        return NULL;
    }
    start++;
    end = find_matching_div_close(start);
    if (!end || end < start) {
        return NULL;
    }

    return dup_range(start, end);
}

static char *extract_obfuscated_content_html(const char *html) {
    const char *container_marker = "<div id=\"readerContentRenderContainer\"";
    const char *content_marker = "<div class=\"chapterContent_txt\">";
    const char *container = strstr(html, container_marker);
    const char *content = strstr(html, content_marker);
    const char *start;
    const char *end;

    if (container) {
        char *container_html = extract_div_inner_html(container);
        if (container_html) {
            return container_html;
        }
    }

    if (!content) {
        return NULL;
    }
    start = content + strlen(content_marker);
    end = strstr(start, "</div>");
    if (!end || end <= start) {
        return NULL;
    }
    return dup_range(start, end);
}

static char *extract_article_content_html(const char *html) {
    const char *article_content = find_div_with_id(html, "js_content");

    if (!article_content) {
        article_content = find_div_with_id(html, "mpDetailContent");
    }

    return extract_div_inner_html(article_content);
}

static int is_js_ident_char(char ch) {
    return isalnum((unsigned char)ch) || ch == '_' || ch == '$';
}

static int parse_js_literal_for_alias(const char **cursor, const char *end, char **out_literal) {
    const char *p = skip_ws(*cursor, end);
    const char *start;

    if (p >= end) {
        return -1;
    }

    if (*p == '"' || *p == '\'') {
        char quote = *p++;
        start = p - 1;
        while (p < end) {
            if (*p == '\\' && p + 1 < end) {
                p += 2;
                continue;
            }
            if (*p == quote) {
                p++;
                *out_literal = dup_range(start, p);
                *cursor = p;
                return *out_literal ? 0 : -1;
            }
            p++;
        }
        return -1;
    }

    if (strncmp(p, "void 0", 6) == 0) {
        *out_literal = strdup("null");
        *cursor = p + 6;
        return *out_literal ? 0 : -1;
    }
    if (strncmp(p, "!0", 2) == 0) {
        *out_literal = strdup("true");
        *cursor = p + 2;
        return *out_literal ? 0 : -1;
    }
    if (strncmp(p, "!1", 2) == 0) {
        *out_literal = strdup("false");
        *cursor = p + 2;
        return *out_literal ? 0 : -1;
    }
    if (strncmp(p, "true", 4) == 0 || strncmp(p, "null", 4) == 0) {
        *out_literal = dup_range(p, p + 4);
        *cursor = p + 4;
        return *out_literal ? 0 : -1;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out_literal = dup_range(p, p + 5);
        *cursor = p + 5;
        return *out_literal ? 0 : -1;
    }
    if (strncmp(p, "Array(", 6) == 0) {
        const char *close = p + 6;
        while (close < end && *close != ')') {
            close++;
        }
        if (close < end) {
            *out_literal = strdup("[]");
            *cursor = close + 1;
            return *out_literal ? 0 : -1;
        }
        return -1;
    }
    if (*p == '{') {
        const char *close = find_matching_pair(p, end, '{', '}');
        if (!close) {
            return -1;
        }
        *out_literal = strdup("{}");
        *cursor = close + 1;
        return *out_literal ? 0 : -1;
    }
    if (*p == '[') {
        const char *close = find_matching_pair(p, end, '[', ']');
        if (!close) {
            return -1;
        }
        *out_literal = strdup("[]");
        *cursor = close + 1;
        return *out_literal ? 0 : -1;
    }
    if (*p == '-' || *p == '.' || isdigit((unsigned char)*p)) {
        start = p;
        if (*p == '-') {
            p++;
        }
        if (p < end && *p == '.') {
            p++;
        }
        while (p < end && isdigit((unsigned char)*p)) {
            p++;
        }
        if (p < end && *p == '.') {
            p++;
            while (p < end && isdigit((unsigned char)*p)) {
                p++;
            }
        }
        *out_literal = dup_range(start, p);
        *cursor = p;
        return *out_literal ? 0 : -1;
    }
    if (is_js_ident_char(*p)) {
        start = p;
        p++;
        while (p < end && is_js_ident_char(*p)) {
            p++;
        }
        *out_literal = dup_range(start, p);
        *cursor = p;
        return *out_literal ? 0 : -1;
    }
    return -1;
}

static int find_named_object_block(const char *html, const char *marker,
                                   const char **block_start, const char **block_end) {
    const char *start = strstr(html, marker);
    const char *end = html + strlen(html);

    if (!start) {
        return -1;
    }
    if (marker[strlen(marker) - 1] == '{') {
        start = start + strlen(marker) - 1;
    } else {
        start = strchr(start, '{');
        if (!start) {
            return -1;
        }
    }
    *block_start = start;
    *block_end = find_matching_pair(start, end, '{', '}');
    return *block_end ? 0 : -1;
}

static char *extract_literal_from_slice(const char *slice_start, const char *slice_end,
                                        const char *field_marker) {
    const char *field = find_top_level_field(slice_start, slice_end, field_marker);
    const char *cursor;
    char *literal = NULL;

    if (!field || field >= slice_end) {
        return NULL;
    }
    cursor = field + strlen(field_marker);
    if (parse_js_literal_for_alias(&cursor, slice_end, &literal) != 0) {
        free(literal);
        return NULL;
    }
    return literal;
}

static char *extract_container_from_slice(const char *slice_start, const char *slice_end,
                                          const char *field_marker, char open_ch, char close_ch) {
    const char *field = find_top_level_field(slice_start, slice_end, field_marker);
    const char *cursor;
    const char *close;
    char *alias = NULL;
    char *resolved = NULL;

    if (!field || field >= slice_end) {
        return NULL;
    }
    cursor = skip_ws(field + strlen(field_marker), slice_end);
    if (cursor >= slice_end) {
        return NULL;
    }
    if (*cursor == open_ch) {
        close = find_matching_pair(cursor, slice_end, open_ch, close_ch);
        if (!close) {
            return NULL;
        }
        return dup_range(cursor, close + 1);
    }
    if (parse_js_literal_for_alias(&cursor, slice_end, &alias) != 0 || !alias) {
        free(alias);
        return NULL;
    }
    if (alias[0] == open_ch) {
        return alias;
    }
    resolved = resolve_nuxt_alias_literal(slice_start, alias);
    free(alias);
    if (!resolved || resolved[0] != open_ch) {
        free(resolved);
        return NULL;
    }
    return resolved;
}

static char *resolve_literal_or_alias(const char *html, const char *literal) {
    char *resolved = NULL;

    if (!literal || !*literal) {
        return NULL;
    }
    if (literal[0] == '"' || literal[0] == '\'' ||
        literal[0] == '{' || literal[0] == '[' ||
        literal[0] == '-' || literal[0] == '.' || isdigit((unsigned char)literal[0]) ||
        strcmp(literal, "true") == 0 || strcmp(literal, "false") == 0 ||
        strcmp(literal, "null") == 0) {
        cJSON *json = cJSON_Parse(literal);
        if (json) {
            if (cJSON_IsString(json) && json->valuestring) {
                resolved = strdup(json->valuestring);
            } else if (cJSON_IsNumber(json)) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.0f", json->valuedouble);
                resolved = strdup(buf);
            } else if (cJSON_IsTrue(json)) {
                resolved = strdup("true");
            } else if (cJSON_IsFalse(json)) {
                resolved = strdup("false");
            }
            cJSON_Delete(json);
        }
    } else {
        resolved = resolve_nuxt_alias_string(html, literal);
    }
    return resolved;
}

static char *extract_resolved_from_slice(const char *html, const char *slice_start,
                                         const char *slice_end, const char *field_marker) {
    char *literal = extract_literal_from_slice(slice_start, slice_end, field_marker);
    char *resolved = resolve_literal_or_alias(html, literal);
    free(literal);
    return resolved;
}

static char *extract_reader_param_alias_value(const char *html, const char *field_name) {
    const char *marker = "window.__NUXT__=";
    const char *start = strstr(html, marker);
    const char *end = html + strlen(html);
    const char *fn_start;
    const char *params_start;
    const char *params_end;
    const char *body_start;
    const char *body_end;
    const char *call_start;
    const char *call_end;
    const char *field;
    const char *block_end;
    const char *param_key;
    const char *alias_start;
    size_t alias_len;
    char *alias = NULL;
    char *params_copy = NULL;
    char *params_cursor = NULL;
    char *token;
    int target_index = -1;
    int index = 0;
    char *resolved = NULL;

    if (!start) {
        return NULL;
    }

    field = strstr(start, field_name);
    if (!field) {
        return NULL;
    }
    block_end = strchr(field, '}');
    if (!block_end) {
        return NULL;
    }
    param_key = strstr(field, "param:");
    if (!param_key || param_key > block_end) {
        return NULL;
    }

    alias_start = skip_ws(param_key + 6, block_end);
    if (alias_start >= block_end) {
        return NULL;
    }

    if (*alias_start == '"' || *alias_start == '\'') {
        char *literal = NULL;
        cJSON *json;
        const char *cursor = alias_start;
        if (parse_js_literal_for_alias(&cursor, block_end, &literal) != 0 || !literal) {
            return NULL;
        }
        json = cJSON_Parse(literal);
        free(literal);
        if (!json) {
            return NULL;
        }
        if (cJSON_IsString(json) && json->valuestring) {
            resolved = strdup(json->valuestring);
        }
        cJSON_Delete(json);
        return resolved;
    }

    alias_len = 0;
    while (alias_start + alias_len < block_end && is_js_ident_char(alias_start[alias_len])) {
        alias_len++;
    }
    if (alias_len == 0) {
        return NULL;
    }
    alias = dup_range(alias_start, alias_start + alias_len);
    if (!alias) {
        return NULL;
    }

    fn_start = strstr(start, "(function(");
    if (!fn_start) {
        goto cleanup;
    }
    params_start = fn_start + strlen("(function(");
    params_end = strchr(params_start, ')');
    if (!params_end) {
        goto cleanup;
    }
    body_start = strchr(params_end, '{');
    if (!body_start) {
        goto cleanup;
    }
    body_end = find_matching_pair(body_start, end, '{', '}');
    if (!body_end) {
        goto cleanup;
    }

    params_copy = dup_range(params_start, params_end);
    if (!params_copy) {
        goto cleanup;
    }

    params_cursor = params_copy;
    while ((token = strsep(&params_cursor, ",")) != NULL) {
        while (*token && isspace((unsigned char)*token)) {
            token++;
        }
        if (!*token) {
            continue;
        }
        if (strcmp(token, alias) == 0) {
            target_index = index;
            break;
        }
        index++;
    }
    if (target_index < 0) {
        goto cleanup;
    }

    call_start = body_end;
    while (call_start < end && *call_start != '(') {
        call_start++;
    }
    if (call_start >= end) {
        goto cleanup;
    }
    call_end = find_matching_pair(call_start, end, '(', ')');
    if (!call_end) {
        goto cleanup;
    }

    {
        const char *cursor = call_start + 1;
        for (int i = 0; i <= target_index; i++) {
            char *literal = NULL;
            cJSON *json;

            cursor = skip_ws(cursor, call_end);
            if (cursor >= call_end || parse_js_literal_for_alias(&cursor, call_end, &literal) != 0) {
                free(literal);
                goto cleanup;
            }
            if (i == target_index) {
                json = cJSON_Parse(literal);
                if (json && cJSON_IsString(json) && json->valuestring) {
                    resolved = strdup(json->valuestring);
                }
                cJSON_Delete(json);
                free(literal);
                break;
            }
            free(literal);
            cursor = skip_ws(cursor, call_end);
            if (cursor < call_end && *cursor == ',') {
                cursor++;
            }
        }
    }

cleanup:
    free(alias);
    free(params_copy);
    return resolved;
}

static char *resolve_nuxt_alias_string(const char *html, const char *alias) {
    const char *marker = "window.__NUXT__=";
    const char *start = strstr(html, marker);
    const char *end = html + strlen(html);
    const char *fn_start;
    const char *params_start;
    const char *params_end;
    const char *body_start;
    const char *body_end;
    const char *call_start;
    const char *call_end;
    char *params_copy = NULL;
    char *params_cursor = NULL;
    char *token;
    int target_index = -1;
    int index = 0;
    char *resolved = NULL;

    if (!start || !alias || !*alias) {
        return NULL;
    }

    fn_start = strstr(start, "(function(");
    if (!fn_start) {
        return NULL;
    }
    params_start = fn_start + strlen("(function(");
    params_end = strchr(params_start, ')');
    if (!params_end) {
        return NULL;
    }
    body_start = strchr(params_end, '{');
    if (!body_start) {
        return NULL;
    }
    body_end = find_matching_pair(body_start, end, '{', '}');
    if (!body_end) {
        return NULL;
    }

    params_copy = dup_range(params_start, params_end);
    if (!params_copy) {
        return NULL;
    }

    params_cursor = params_copy;
    while ((token = strsep(&params_cursor, ",")) != NULL) {
        while (*token && isspace((unsigned char)*token)) {
            token++;
        }
        if (!*token) {
            continue;
        }
        if (strcmp(token, alias) == 0) {
            target_index = index;
            break;
        }
        index++;
    }
    if (target_index < 0) {
        goto cleanup;
    }

    call_start = body_end;
    while (call_start < end && *call_start != '(') {
        call_start++;
    }
    if (call_start >= end) {
        goto cleanup;
    }
    call_end = find_matching_pair(call_start, end, '(', ')');
    if (!call_end) {
        goto cleanup;
    }

    {
        const char *cursor = call_start + 1;
        for (int i = 0; i <= target_index; i++) {
            char *literal = NULL;
            cJSON *json;

            cursor = skip_ws(cursor, call_end);
            if (cursor >= call_end || parse_js_literal_for_alias(&cursor, call_end, &literal) != 0) {
                free(literal);
                goto cleanup;
            }
            if (i == target_index) {
                json = cJSON_Parse(literal);
                if (json) {
                    if (cJSON_IsString(json) && json->valuestring) {
                        resolved = strdup(json->valuestring);
                    } else if (cJSON_IsNumber(json)) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%.0f", json->valuedouble);
                        resolved = strdup(buf);
                    } else if (cJSON_IsTrue(json)) {
                        resolved = strdup("true");
                    } else if (cJSON_IsFalse(json)) {
                        resolved = strdup("false");
                    }
                } else if (literal && *literal) {
                    resolved = strdup(literal);
                }
                cJSON_Delete(json);
                free(literal);
                break;
            }
            free(literal);
            cursor = skip_ws(cursor, call_end);
            if (cursor < call_end && *cursor == ',') {
                cursor++;
            }
        }
    }

cleanup:
    free(params_copy);
    return resolved;
}

static char *resolve_nuxt_alias_literal(const char *html, const char *alias) {
    const char *marker = "window.__NUXT__=";
    const char *start = strstr(html, marker);
    const char *end = html + strlen(html);
    const char *fn_start;
    const char *params_start;
    const char *params_end;
    const char *body_start;
    const char *body_end;
    const char *call_start;
    const char *call_end;
    char *params_copy = NULL;
    char *params_cursor = NULL;
    char *token;
    int target_index = -1;
    int index = 0;
    char *resolved = NULL;

    if (!start || !alias || !*alias) {
        return NULL;
    }

    fn_start = strstr(start, "(function(");
    if (!fn_start) {
        return NULL;
    }
    params_start = fn_start + strlen("(function(");
    params_end = strchr(params_start, ')');
    if (!params_end) {
        return NULL;
    }
    body_start = strchr(params_end, '{');
    if (!body_start) {
        return NULL;
    }
    body_end = find_matching_pair(body_start, end, '{', '}');
    if (!body_end) {
        return NULL;
    }

    params_copy = dup_range(params_start, params_end);
    if (!params_copy) {
        return NULL;
    }

    params_cursor = params_copy;
    while ((token = strsep(&params_cursor, ",")) != NULL) {
        while (*token && isspace((unsigned char)*token)) {
            token++;
        }
        if (!*token) {
            continue;
        }
        if (strcmp(token, alias) == 0) {
            target_index = index;
            break;
        }
        index++;
    }
    if (target_index < 0) {
        goto cleanup;
    }

    call_start = body_end;
    while (call_start < end && *call_start != '(') {
        call_start++;
    }
    if (call_start >= end) {
        goto cleanup;
    }
    call_end = find_matching_pair(call_start, end, '(', ')');
    if (!call_end) {
        goto cleanup;
    }

    {
        const char *cursor = call_start + 1;
        for (int i = 0; i <= target_index; i++) {
            char *literal = NULL;

            cursor = skip_ws(cursor, call_end);
            if (cursor >= call_end || parse_js_literal_for_alias(&cursor, call_end, &literal) != 0) {
                free(literal);
                goto cleanup;
            }
            if (i == target_index) {
                resolved = literal;
                break;
            }
            free(literal);
            cursor = skip_ws(cursor, call_end);
            if (cursor < call_end && *cursor == ',') {
                cursor++;
            }
        }
    }

cleanup:
    free(params_copy);
    return resolved;
}

static char *extract_resolved_value_after_marker(const char *html, const char *marker) {
    const char *end = html + strlen(html);
    const char *field = strstr(html, marker);
    const char *cursor;
    char *literal = NULL;
    char *resolved = NULL;
    cJSON *json;

    if (!field) {
        return NULL;
    }
    cursor = field + strlen(marker);
    if (parse_js_literal_for_alias(&cursor, end, &literal) != 0 || !literal) {
        free(literal);
        return NULL;
    }

    if (literal[0] == '"' || literal[0] == '\'' ||
        literal[0] == '{' || literal[0] == '[' ||
        literal[0] == '-' || literal[0] == '.' || isdigit((unsigned char)literal[0]) ||
        strcmp(literal, "true") == 0 || strcmp(literal, "false") == 0 ||
        strcmp(literal, "null") == 0) {
        json = cJSON_Parse(literal);
        if (json) {
            if (cJSON_IsString(json) && json->valuestring) {
                resolved = strdup(json->valuestring);
            } else if (cJSON_IsNumber(json)) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.0f", json->valuedouble);
                resolved = strdup(buf);
            } else if (cJSON_IsTrue(json)) {
                resolved = strdup("true");
            } else if (cJSON_IsFalse(json)) {
                resolved = strdup("false");
            }
            cJSON_Delete(json);
        }
    } else {
        resolved = resolve_nuxt_alias_string(html, literal);
    }

    free(literal);
    return resolved;
}

static int extract_int_after_marker(const char *html, const char *marker, int fallback) {
    char *value = extract_resolved_value_after_marker(html, marker);
    int result = fallback;

    if (value) {
        result = atoi(value);
    }
    free(value);
    return result;
}

static int extract_max_wco(const char *html) {
    const char *p = html;
    int max_wco = 0;

    while ((p = strstr(p, "wco=\"")) != NULL) {
        int value = atoi(p + 5);
        if (value > max_wco) {
            max_wco = value;
        }
        p += 5;
    }

    return max_wco;
}

static int extract_wco_values(const char *html, int **values_out, int *count_out) {
    const char *p = html;
    int *values = NULL;
    int count = 0;
    int cap = 0;

    if (!html || !values_out || !count_out) {
        return -1;
    }

    while ((p = strstr(p, "wco=\"")) != NULL) {
        int value = atoi(p + 5);
        if (value > 0) {
            if (count == 0 || values[count - 1] != value) {
                if (count >= cap) {
                    int new_cap = cap > 0 ? cap * 2 : 64;
                    int *tmp = realloc(values, sizeof(int) * new_cap);
                    if (!tmp) {
                        free(values);
                        return -1;
                    }
                    values = tmp;
                    cap = new_cap;
                }
                values[count++] = value;
            }
        }
        p += 5;
    }

    *values_out = values;
    *count_out = count;
    return 0;
}

static char *extract_resolved_value_in_object_block(const char *html, const char *block_marker,
                                                    const char *field_marker) {
    const char *block_start;
    const char *block_end;

    if (find_named_object_block(html, block_marker, &block_start, &block_end) != 0) {
        return NULL;
    }
    return extract_resolved_from_slice(html, block_start, block_end, field_marker);
}

static int extract_int_in_object_block(const char *html, const char *block_marker,
                                       const char *field_marker, int fallback) {
    char *value = extract_resolved_value_in_object_block(html, block_marker, field_marker);
    int result = fallback;

    if (value) {
        result = atoi(value);
    }
    free(value);
    return result;
}

static int extract_int_from_slice_resolved(const char *html, const char *slice_start,
                                           const char *slice_end, const char *field_marker,
                                           int fallback) {
    char *value = extract_resolved_from_slice(html, slice_start, slice_end, field_marker);
    int result = fallback;

    if (value) {
        result = atoi(value);
    }
    free(value);
    return result;
}

static int parse_bool_from_slice_resolved(const char *html, const char *slice_start,
                                          const char *slice_end, const char *field_marker,
                                          int fallback) {
    char *value = extract_resolved_from_slice(html, slice_start, slice_end, field_marker);
    int result = fallback;

    if (value) {
        if (strcmp(value, "true") == 0) {
            result = 1;
        } else if (strcmp(value, "false") == 0) {
            result = 0;
        } else {
            result = atoi(value) != 0;
        }
    }
    free(value);
    return result;
}

typedef struct {
    char *chapter_uid;
    char *target;
} ReaderParamMapItem;

static void reader_param_map_free(ReaderParamMapItem *items, int count);


static void reader_param_map_free(ReaderParamMapItem *items, int count) {
    if (!items) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(items[i].chapter_uid);
        free(items[i].target);
    }
    free(items);
}

static const char *reader_param_map_find(ReaderParamMapItem *items, int count, const char *chapter_uid) {
    if (!items || !chapter_uid) {
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        if (items[i].chapter_uid && strcmp(items[i].chapter_uid, chapter_uid) == 0) {
            return items[i].target;
        }
    }
    return NULL;
}

static int parse_reader_param_map(const char *html, const char *array_literal,
                                  ReaderParamMapItem **items_out, int *count_out) {
    const char *cursor;
    const char *end;
    ReaderParamMapItem *items = NULL;
    int count = 0;
    int cap = 0;

    if (!array_literal || !items_out || !count_out) {
        return -1;
    }

    cursor = array_literal + 1;
    end = array_literal + strlen(array_literal) - 1;
    while (cursor < end) {
        const char *obj_end;
        ReaderParamMapItem item = {0};
        ReaderParamMapItem *tmp;

        cursor = skip_ws(cursor, end);
        if (cursor >= end) {
            break;
        }
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor != '{') {
            break;
        }

        obj_end = find_matching_pair(cursor, end, '{', '}');
        if (!obj_end) {
            break;
        }

        item.chapter_uid = extract_resolved_from_slice(html, cursor, obj_end + 1, "cUid:");
        item.target = extract_resolved_from_slice(html, cursor, obj_end + 1, "param:");
        if (item.chapter_uid && item.target) {
            if (count >= cap) {
                int new_cap = cap > 0 ? cap * 2 : 32;
                tmp = realloc(items, sizeof(*items) * (size_t)new_cap);
                if (!tmp) {
                    free(item.chapter_uid);
                    free(item.target);
                    reader_param_map_free(items, count);
                    return -1;
                }
                items = tmp;
                cap = new_cap;
            }
            items[count++] = item;
        } else {
            free(item.chapter_uid);
            free(item.target);
        }

        cursor = obj_end + 1;
    }

    *items_out = items;
    *count_out = count;
    return 0;
}

/*
 * Extract a JSON array value from a field in a JS object slice.
 * Handles both inline arrays ([...]) and alias identifiers that resolve to arrays.
 * Returns a malloc'd string of the full [...] content, or NULL on failure.
 */
static char *extract_array_from_slice(const char *html, const char *slice_start,
                                      const char *slice_end, const char *field_marker) {
    const char *field = find_top_level_field(slice_start, slice_end, field_marker);
    const char *cursor;
    char *alias = NULL;
    char *resolved;

    if (!field || field >= slice_end) {
        return NULL;
    }
    cursor = skip_ws(field + strlen(field_marker), slice_end);
    if (cursor >= slice_end) {
        return NULL;
    }
    if (*cursor == '[') {
        const char *close = find_matching_pair(cursor, slice_end, '[', ']');
        if (!close) {
            return NULL;
        }
        return dup_range(cursor, close + 1);
    }
    if (parse_js_literal_for_alias(&cursor, slice_end, &alias) != 0 || !alias) {
        free(alias);
        return NULL;
    }
    if (alias[0] == '[') {
        return alias;
    }
    resolved = resolve_nuxt_alias_literal(html, alias);
    free(alias);
    return resolved;
}

/*
 * Parse chapterIndexes + readerUrlParams from a page-catalog block ({indexes:[...],readerUrlParams:[...]}).
 * Appends newly found items to *items_inout / *count_inout / *cap_inout, skipping duplicate chapter_uid.
 * param_map is used to resolve chapter UID -> URL target.
 */
static void parse_catalog_from_page_block(const char *html, const char *block,
                                          ReaderParamMapItem *param_map, int param_count,
                                          ReaderCatalogItem **items_inout, int *count_inout,
                                          int *cap_inout) {
    char *indexes_arr;
    const char *cursor, *end;
    const char *block_end;

    if (!html || !block || !items_inout || !count_inout || !cap_inout) {
        return;
    }
    block_end = block + strlen(block);
    indexes_arr = extract_array_from_slice(html, block, block_end, "indexes:");
    if (!indexes_arr || strcmp(indexes_arr, "[]") == 0) {
        free(indexes_arr);
        return;
    }

    cursor = indexes_arr + 1;
    end = indexes_arr + strlen(indexes_arr) - 1;
    while (cursor < end) {
        const char *obj_end;
        ReaderCatalogItem item = {0};
        ReaderCatalogItem *tmp;
        const char *target;
        int i;

        cursor = skip_ws(cursor, end);
        if (cursor >= end) break;
        if (*cursor == ',') { cursor++; continue; }
        if (*cursor != '{') break;

        obj_end = find_matching_pair(cursor, end, '{', '}');
        if (!obj_end) break;

        item.chapter_uid = extract_resolved_from_slice(html, cursor, obj_end + 1, "chapterUid:");
        item.title = extract_resolved_from_slice(html, cursor, obj_end + 1, "displayTitle:");
        if (!item.title) {
            item.title = extract_resolved_from_slice(html, cursor, obj_end + 1, "title:");
        }
        item.chapter_idx = extract_int_from_slice_resolved(html, cursor, obj_end + 1, "chapterIdx:", 0);
        item.word_count = extract_int_from_slice_resolved(html, cursor, obj_end + 1, "wordCount:", 0);
        item.level = extract_int_from_slice_resolved(html, cursor, obj_end + 1, "level:", 1);
        item.is_current = parse_bool_from_slice_resolved(html, cursor, obj_end + 1, "isCurrent:", 0);
        item.is_lock = parse_bool_from_slice_resolved(html, cursor, obj_end + 1, "isLock:", 0);
        target = reader_param_map_find(param_map, param_count, item.chapter_uid);
        item.target = target ? strdup(target) : NULL;

        /* Skip if missing required fields or duplicate uid */
        if (!item.chapter_uid || !item.target || !item.title) {
            free(item.chapter_uid);
            free(item.target);
            free(item.title);
            cursor = obj_end + 1;
            continue;
        }
        for (i = 0; i < *count_inout; i++) {
            if ((*items_inout)[i].chapter_uid &&
                strcmp((*items_inout)[i].chapter_uid, item.chapter_uid) == 0) {
                break;
            }
        }
        if (i < *count_inout) {
            free(item.chapter_uid);
            free(item.target);
            free(item.title);
            cursor = obj_end + 1;
            continue;
        }

        if (*count_inout >= *cap_inout) {
            int new_cap = *cap_inout > 0 ? *cap_inout * 2 : 32;
            tmp = realloc(*items_inout, sizeof(**items_inout) * (size_t)new_cap);
            if (!tmp) {
                free(item.chapter_uid);
                free(item.target);
                free(item.title);
                break;
            }
            *items_inout = tmp;
            *cap_inout = new_cap;
        }
        (*items_inout)[(*count_inout)++] = item;
        cursor = obj_end + 1;
    }
    free(indexes_arr);
}

static int reader_catalog_merge_item(ReaderCatalogItem **items_inout, int *count_inout, int *cap_inout,
                                     ReaderCatalogItem *item) {
    ReaderCatalogItem *tmp;
    int existing_index;

    if (!items_inout || !count_inout || !cap_inout || !item || !item->chapter_uid) {
        return -1;
    }

    existing_index = reader_catalog_find_index(*items_inout, *count_inout, item->chapter_uid);
    if (existing_index >= 0) {
        ReaderCatalogItem *dst = &(*items_inout)[existing_index];

        if ((!dst->title || !dst->title[0]) && item->title) {
            dst->title = item->title;
            item->title = NULL;
        }
        if ((!dst->target || !dst->target[0]) && item->target) {
            dst->target = item->target;
            item->target = NULL;
        }
        if (dst->chapter_idx <= 0 && item->chapter_idx > 0) {
            dst->chapter_idx = item->chapter_idx;
        }
        if (dst->word_count <= 0 && item->word_count > 0) {
            dst->word_count = item->word_count;
        }
        if (dst->level <= 0 && item->level > 0) {
            dst->level = item->level;
        }
        if (!dst->is_current && item->is_current) {
            dst->is_current = 1;
        }
        if (!dst->is_lock && item->is_lock) {
            dst->is_lock = 1;
        }
        return 0;
    }

    if (*count_inout >= *cap_inout) {
        int new_cap = *cap_inout > 0 ? *cap_inout * 2 : 64;
        tmp = realloc(*items_inout, sizeof(**items_inout) * (size_t)new_cap);
        if (!tmp) {
            return -1;
        }
        *items_inout = tmp;
        *cap_inout = new_cap;
    }

    (*items_inout)[(*count_inout)++] = *item;
    memset(item, 0, sizeof(*item));
    return 0;
}


static int is_catalog_block_name(const char *name, size_t len) {
    static const char suffix[] = "Catalogs";
    size_t suffix_len = sizeof(suffix) - 1;

    if (!name || len < suffix_len) {
        return 0;
    }
    return strncmp(name + len - suffix_len, suffix, suffix_len) == 0;
}

static void parse_catalog_blocks_in_reader_state(const char *html, const char *reader_block_start,
                                                 const char *reader_block_end,
                                                 ReaderParamMapItem **param_map_inout,
                                                 int *param_count_inout,
                                                 ReaderCatalogItem **items_inout,
                                                 int *count_inout, int *cap_inout) {
    const char *p;
    int brace_depth = 0;
    int bracket_depth = 0;
    int in_string = 0;
    char quote = 0;

    if (!html || !reader_block_start || !reader_block_end || !param_map_inout ||
        !param_count_inout || !items_inout || !count_inout || !cap_inout) {
        return;
    }

    p = reader_block_start;
    if (p < reader_block_end && *p == '{') {
        brace_depth = 1;
        p++;
    }

    for (; p < reader_block_end; p++) {
        if (in_string) {
            if (*p == '\\' && p + 1 < reader_block_end) {
                p++;
                continue;
            }
            if (*p == quote) {
                in_string = 0;
            }
            continue;
        }

        if (*p == '"' || *p == '\'') {
            in_string = 1;
            quote = *p;
            continue;
        }
        if (*p == '{') {
            brace_depth++;
            continue;
        }
        if (*p == '}') {
            brace_depth--;
            continue;
        }
        if (*p == '[') {
            bracket_depth++;
            continue;
        }
        if (*p == ']') {
            bracket_depth--;
            continue;
        }

        if (brace_depth != 1 || bracket_depth != 0 || !isalpha((unsigned char)*p)) {
            continue;
        }

        {
            const char *name_start = p;
            const char *name_end = p + 1;
            char *field_marker = NULL;
            char *pc_block = NULL;

            while (name_end < reader_block_end &&
                   (isalnum((unsigned char)*name_end) || *name_end == '_' || *name_end == '$')) {
                name_end++;
            }
            if (name_end >= reader_block_end || *name_end != ':') {
                p = name_end;
                continue;
            }
            if (!is_catalog_block_name(name_start, (size_t)(name_end - name_start))) {
                p = name_end;
                continue;
            }

            field_marker = dup_range(name_start, name_end + 1);
            if (!field_marker) {
                p = name_end;
                continue;
            }
            pc_block = extract_container_from_slice(reader_block_start, reader_block_end,
                                                    field_marker, '{', '}');
            free(field_marker);
            if (!pc_block) {
                p = name_end;
                continue;
            }

            {
                const char *pc_end = pc_block + strlen(pc_block);
                char *rp = extract_array_from_slice(html, pc_block, pc_end, "readerUrlParams:");
                if (rp && strcmp(rp, "[]") != 0) {
                    ReaderParamMapItem *new_map = NULL;
                    int new_count = 0;
                    if (parse_reader_param_map(html, rp, &new_map, &new_count) == 0 && new_count > 0) {
                        ReaderParamMapItem *merged = realloc(
                            *param_map_inout,
                            sizeof(**param_map_inout) * (size_t)(*param_count_inout + new_count));
                        if (merged) {
                            *param_map_inout = merged;
                            memcpy(*param_map_inout + *param_count_inout, new_map,
                                   sizeof(**param_map_inout) * (size_t)new_count);
                            *param_count_inout += new_count;
                            free(new_map);
                        } else {
                            reader_param_map_free(new_map, new_count);
                        }
                    } else {
                        reader_param_map_free(new_map, new_count);
                    }
                }
                free(rp);
            }

            parse_catalog_from_page_block(html, pc_block, *param_map_inout, *param_count_inout,
                                          items_inout, count_inout, cap_inout);
            free(pc_block);
            p = name_end;
        }
    }
}

static int parse_reader_catalog(const char *html, const char *reader_block_start,
                                const char *reader_block_end, ReaderCatalogItem **items_out,
                                int *count_out) {
    char *chapter_indexes = NULL;
    char *reader_params = NULL;
    ReaderParamMapItem *param_map = NULL;
    int param_count = 0;
    ReaderCatalogItem *items = NULL;
    int count = 0;
    int cap = 0;
    int rc = -1;
    const char *cursor;
    const char *end;

    if (!html || !reader_block_start || !reader_block_end || !items_out || !count_out) {
        return -1;
    }

    chapter_indexes = extract_array_from_slice(html, reader_block_start, reader_block_end,
                                               "chapterIndexes:");
    reader_params = extract_array_from_slice(html, reader_block_start, reader_block_end,
                                             "readerUrlParams:");

    if ((!chapter_indexes || strcmp(chapter_indexes, "[]") == 0) ||
        (!reader_params || strcmp(reader_params, "[]") == 0)) {
        /* Top-level arrays are empty (large book with Array(N) aliases).
         * Scan every top-level *Catalogs block instead of hard-coding a small
         * subset so we can pick up middle catalog pages when the reader state
         * exposes them through additional buckets or alias-resolved objects. */
        free(chapter_indexes); chapter_indexes = NULL;
        free(reader_params); reader_params = NULL;
        parse_catalog_blocks_in_reader_state(html, reader_block_start, reader_block_end,
                                             &param_map, &param_count, &items, &count, &cap);

        if (count > 0) {
            /* Sort by chapter_idx so items are in book order regardless of
             * which page catalog block each came from. */
            qsort(items, (size_t)count, sizeof(*items), catalog_item_cmp_chapter_idx);
            *items_out = items;
            *count_out = count;
            items = NULL;
            rc = 0;
        }
        goto cleanup;
    }

    if (parse_reader_param_map(html, reader_params, &param_map, &param_count) != 0) {
        goto cleanup;
    }

    cursor = chapter_indexes + 1;
    end = chapter_indexes + strlen(chapter_indexes) - 1;
    while (cursor < end) {
        const char *obj_end;
        ReaderCatalogItem item = {0};
        ReaderCatalogItem *tmp;
        const char *target;

        cursor = skip_ws(cursor, end);
        if (cursor >= end) {
            break;
        }
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor != '{') {
            break;
        }

        obj_end = find_matching_pair(cursor, end, '{', '}');
        if (!obj_end) {
            goto cleanup;
        }

        item.chapter_uid = extract_resolved_from_slice(html, cursor, obj_end + 1, "chapterUid:");
        item.title = extract_resolved_from_slice(html, cursor, obj_end + 1, "displayTitle:");
        if (!item.title) {
            item.title = extract_resolved_from_slice(html, cursor, obj_end + 1, "title:");
        }
        item.chapter_idx = extract_int_from_slice_resolved(html, cursor, obj_end + 1,
                                                           "chapterIdx:", 0);
        item.word_count = extract_int_from_slice_resolved(html, cursor, obj_end + 1,
                                                          "wordCount:", 0);
        item.level = extract_int_from_slice_resolved(html, cursor, obj_end + 1, "level:", 1);
        item.is_current = parse_bool_from_slice_resolved(html, cursor, obj_end + 1,
                                                         "isCurrent:", 0);
        item.is_lock = parse_bool_from_slice_resolved(html, cursor, obj_end + 1,
                                                      "isLock:", 0);
        target = reader_param_map_find(param_map, param_count, item.chapter_uid);
        item.target = target ? strdup(target) : NULL;

        if (item.chapter_uid && item.target && item.title) {
            if (count >= cap) {
                int new_cap = cap > 0 ? cap * 2 : 32;
                tmp = realloc(items, sizeof(*items) * (size_t)new_cap);
                if (!tmp) {
                    free(item.chapter_uid);
                    free(item.target);
                    free(item.title);
                    goto cleanup;
                }
                items = tmp;
                cap = new_cap;
            }
            items[count++] = item;
        } else {
            free(item.chapter_uid);
            free(item.target);
            free(item.title);
        }

        cursor = obj_end + 1;
    }
    *items_out = items;
    *count_out = count;
    items = NULL;
    rc = 0;

cleanup:
    reader_catalog_items_free(items, count);
    reader_param_map_free(param_map, param_count);
    free(chapter_indexes);
    free(reader_params);
    return rc;
}

static int ensure_random_font(ApiContext *ctx, char *path, size_t path_size) {
    struct stat st;
    Buffer buf = {0};
    FILE *fp;

    if (join_path_checked(path, path_size, ctx->data_dir, "fzys_reversed.ttf") != 0) {
        return -1;
    }
    if (stat(path, &st) == 0 && st.st_size > 0) {
        return 0;
    }

    if (api_download(ctx, WEREAD_RANDOM_FONT_URL, &buf) != 0) {
        return -1;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        api_buffer_free(&buf);
        return -1;
    }
    fwrite(buf.data, 1, buf.size, fp);
    fclose(fp);
    api_buffer_free(&buf);
    return 0;
}

int reader_warmup_font(ApiContext *ctx) {
    char path[512];

    if (!ctx) {
        return -1;
    }
    return ensure_random_font(ctx, path, sizeof(path));
}

static int cached_random_font_path(ApiContext *ctx, char *path, size_t path_size) {
    struct stat st;

    if (!ctx || !path || path_size == 0) {
        return -1;
    }
    if (join_path_checked(path, path_size, ctx->data_dir, "fzys_reversed.ttf") != 0) {
        return -1;
    }
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        path[0] = '\0';
        return -1;
    }
    return 0;
}

void reader_set_fetch_override(ReaderFetchPageFn fn) {
    reader_fetch_override = fn;
}

static int reader_target_is_article(const char *target) {
    if (!target || !*target) {
        return 0;
    }
    return strstr(target, "mpdetail?") != NULL || strstr(target, "/mpdetail") != NULL;
}

static char *reader_extract_query_value(const char *target, const char *key) {
    const char *query;
    const char *p;
    size_t key_len;

    if (!target || !key || !key[0]) {
        return NULL;
    }

    query = strchr(target, '?');
    p = query ? query + 1 : target;
    key_len = strlen(key);
    while (p && *p) {
        const char *value_start;
        const char *value_end;

        if ((p == target || p[-1] == '?' || p[-1] == '&') &&
            strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            value_start = p + key_len + 1;
            value_end = value_start;
            while (*value_end && *value_end != '&' && *value_end != '#') {
                value_end++;
            }
            if (value_end > value_start) {
                return dup_range(value_start, value_end);
            }
            return NULL;
        }
        p = strchr(p, '&');
        if (p) {
            p++;
        }
    }

    return NULL;
}

static int reader_build_mpdetail_target(ApiContext *ctx, const char *review_id, int font_size,
                                        char **target_out) {
    char *escaped = NULL;
    char *target = NULL;

    if (!ctx || !review_id || !*review_id || !target_out) {
        return -1;
    }

    escaped = api_escape(ctx, review_id);
    if (!escaped) {
        return -1;
    }

    target = malloc(strlen(WEREAD_BASE_URL) + strlen("/mpdetail?reviewId=&fs=") +
                    strlen(escaped) + 12);
    if (!target) {
        free(escaped);
        return -1;
    }

    sprintf(target, "%s/mpdetail?reviewId=%s&fs=%d", WEREAD_BASE_URL, escaped, font_size);
    free(escaped);
    *target_out = target;
    return 0;
}

static int reader_extract_article_review_id_from_reader_shell(const char *html,
                                                              char **review_id_out) {
    const char *prog_start = NULL;
    const char *prog_end = NULL;
    char *book_id = NULL;
    char *author = NULL;
    char *review_id = NULL;
    char *book_block = NULL;
    int book_type;
    int rc = 0;

    if (!html || !review_id_out) {
        return 0;
    }

    book_id = extract_resolved_value_in_object_block(html, "reader:{bookId:", "bookId:");
    author = extract_resolved_value_in_object_block(html, "bookInfo:{", "author:");
    book_type = extract_int_in_object_block(html, "bookInfo:{", "type:", 0);

    if (!(book_type == 3 ||
          (book_id && strncmp(book_id, "MP", 2) == 0) ||
          (author && strcmp(author, "公众号") == 0))) {
        goto cleanup;
    }

    if (find_named_object_block(html, "progress:{bookId:", &prog_start, &prog_end) != 0) {
        goto cleanup;
    }

    book_block = extract_container_from_slice(prog_start, prog_end, "book:", '{', '}');
    if (!book_block) {
        goto cleanup;
    }
    review_id = extract_resolved_from_slice(html, book_block, book_block + strlen(book_block),
                                            "reviewId:");
    if (!review_id || !review_id[0]) {
        goto cleanup;
    }

    *review_id_out = review_id;
    review_id = NULL;
    rc = 1;

cleanup:
    free(book_id);
    free(author);
    free(review_id);
    free(book_block);
    return rc;
}

static int reader_load_article_from_review_id(ApiContext *ctx, const char *review_id,
                                              int font_size, ReaderDocument *doc) {
    Buffer article_buf = {0};
    char *article_target = NULL;
    int rc = -1;

    if (!ctx || !review_id || !*review_id || !doc) {
        return -1;
    }

    if (reader_build_mpdetail_target(ctx, review_id, font_size, &article_target) != 0) {
        return -1;
    }

    if ((reader_fetch_override ? reader_fetch_override(ctx, article_target, &article_buf) :
                                 api_get(ctx, article_target, &article_buf)) != 0) {
        fprintf(stderr,
                "reader-load: mpdetail fetch failed reviewId=%s target=%s\n",
                review_id, article_target);
        goto cleanup;
    }

    rc = reader_parse_article_document(ctx, article_target, font_size, article_buf.data, doc);
    if (rc == 0) {
        fprintf(stderr,
                "reader-load: article shell resolved reviewId=%s target=%s title=%s\n",
                review_id, article_target,
                doc->book_title ? doc->book_title : "(null)");
    } else {
        fprintf(stderr,
                "reader-load: article shell parse failed reviewId=%s target=%s\n",
                review_id, article_target);
    }

cleanup:
    api_buffer_free(&article_buf);
    free(article_target);
    return rc;
}

static void reader_log_article_page_markers(const char *target, const char *html) {
    const char *marker;
    const char *value_start;
    const char *value_end;
    char page_mid[128];
    int has_mpdetail = 0;

    if (!target || !html) {
        return;
    }

    marker = strstr(html, "window.PAGE_MID=");
    page_mid[0] = '\0';
    if (marker) {
        marker += strlen("window.PAGE_MID=");
        if (*marker == '"' || *marker == '\'') {
            char quote = *marker++;
            value_start = marker;
            value_end = strchr(value_start, quote);
            if (value_end && value_end > value_start) {
                size_t len = (size_t)(value_end - value_start);
                if (len >= sizeof(page_mid)) {
                    len = sizeof(page_mid) - 1;
                }
                memcpy(page_mid, value_start, len);
                page_mid[len] = '\0';
            }
        }
    }

    has_mpdetail = strstr(html, "mpDetailContent") != NULL;
    fprintf(stderr,
            "reader-article-page: target=%s pageMid=%s hasMpDetail=%d\n",
            target,
            page_mid[0] ? page_mid : "(missing)",
            has_mpdetail);
}

static int reader_parse_article_catalog(ApiContext *ctx, const char *html,
                                        const char *article_block_start,
                                        const char *article_block_end,
                                        int font_size,
                                        const char *current_review_id,
                                        ReaderCatalogItem **items_out,
                                        int *count_out,
                                        int *current_index_out) {
    char *catalog_array = NULL;
    ReaderCatalogItem *items = NULL;
    int count = 0;
    int cap = 0;
    int current_index = -1;
    int rc = 0;
    const char *cursor;
    const char *end;

    if (items_out) {
        *items_out = NULL;
    }
    if (count_out) {
        *count_out = 0;
    }
    if (current_index_out) {
        *current_index_out = -1;
    }
    if (!ctx || !html || !article_block_start || !article_block_end ||
        !items_out || !count_out) {
        return -1;
    }

    catalog_array = extract_container_from_slice(article_block_start, article_block_end,
                                                 "mpChaptersInfo:", '[', ']');
    if (!catalog_array || strcmp(catalog_array, "[]") == 0) {
        rc = 0;
        goto cleanup;
    }

    cursor = catalog_array + 1;
    end = catalog_array + strlen(catalog_array) - 1;
    while (cursor < end) {
        const char *obj_end;
        ReaderCatalogItem item = {0};
        ReaderCatalogItem *tmp;
        char *review_id = NULL;
        char *mp_info = NULL;
        int raw_idx;

        cursor = skip_ws(cursor, end);
        if (cursor >= end) {
            break;
        }
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor != '{') {
            break;
        }

        obj_end = find_matching_pair(cursor, end, '{', '}');
        if (!obj_end) {
            rc = -1;
            goto cleanup;
        }

        review_id = extract_resolved_from_slice(html, cursor, obj_end + 1, "reviewId:");
        mp_info = extract_container_from_slice(cursor, obj_end + 1, "mpInfo:", '{', '}');
        if (mp_info) {
            item.title = extract_resolved_from_slice(html, mp_info,
                                                     mp_info + strlen(mp_info), "title:");
        }
        if (!item.title) {
            item.title = extract_resolved_from_slice(html, cursor, obj_end + 1, "title:");
        }

        raw_idx = extract_int_from_slice_resolved(html, cursor, obj_end + 1, "idx:", -1);
        item.chapter_idx = raw_idx >= 0 ? raw_idx + 1 : count + 1;
        item.is_current = parse_bool_from_slice_resolved(html, cursor, obj_end + 1,
                                                         "isCurrent:", 0);
        if (!item.is_current && current_review_id && review_id &&
            strcmp(current_review_id, review_id) == 0) {
            item.is_current = 1;
        }
        item.chapter_uid = review_id;
        review_id = NULL;
        if (item.chapter_uid) {
            if (reader_build_mpdetail_target(ctx, item.chapter_uid, font_size,
                                             &item.target) != 0) {
                free(item.chapter_uid);
                free(item.title);
                item.chapter_uid = NULL;
                item.title = NULL;
            }
        }

        if (item.chapter_uid && item.target && item.title) {
            if (count >= cap) {
                int new_cap = cap > 0 ? cap * 2 : 16;

                tmp = realloc(items, sizeof(*items) * (size_t)new_cap);
                if (!tmp) {
                    free(item.chapter_uid);
                    free(item.target);
                    free(item.title);
                    rc = -1;
                    free(mp_info);
                    goto cleanup;
                }
                items = tmp;
                cap = new_cap;
            }
            if (item.is_current) {
                current_index = count;
            }
            items[count++] = item;
        } else {
            free(item.chapter_uid);
            free(item.target);
            free(item.title);
        }

        free(mp_info);
        cursor = obj_end + 1;
    }

cleanup:
    if (rc == 0) {
        *items_out = items;
        *count_out = count;
        if (current_index_out) {
            *current_index_out = current_index;
        }
        items = NULL;
        count = 0;
    }
    reader_catalog_items_free(items, count);
    free(catalog_array);
    return rc;
}

static int reader_parse_article_document(ApiContext *ctx, const char *target, int font_size,
                                         const char *html, ReaderDocument *doc) {
    const char *article_block_start = NULL;
    const char *article_block_end = NULL;
    char *content_html = NULL;
    char *current_review_id = NULL;
    char *status_value = NULL;
    int current_catalog_index = -1;
    int has_mpdetail_content = 0;
    int rc = -1;

    if (!ctx || !target || !html || !doc) {
        return -1;
    }

    doc->kind = READER_DOCUMENT_KIND_ARTICLE;
    doc->catalog_total_count = 0;
    doc->catalog_range_start = 0;
    doc->catalog_range_end = 0;
    doc->catalog_count = 0;
    doc->catalog_items = NULL;
    reader_log_article_page_markers(target, html);
    has_mpdetail_content = strstr(html, "mpDetailContent") != NULL;
    if (!has_mpdetail_content &&
        (strstr(html, "PAGE_MID='mmbizwap:secitptpage/verify.html'") != NULL ||
         strstr(html, "PAGE_MID=\"mmbizwap:secitptpage/verify.html\"") != NULL)) {
        fprintf(stderr,
                "reader-article-verify-page: target=%s\n",
                target);
        goto cleanup;
    }
    if (find_named_object_block(html, "mpdetail:{status:", &article_block_start,
                                &article_block_end) != 0) {
        fprintf(stderr,
                "reader-article-parse: mpdetail block missing target=%s\n",
                target);
        goto cleanup;
    }

    status_value = extract_resolved_from_slice(html, article_block_start, article_block_end,
                                               "status:");
    if (!status_value || !status_value[0] ||
        strcmp(status_value, "-1") == 0 ||
        strcmp(status_value, "null") == 0) {
        fprintf(stderr,
                "reader-article-parse: invalid status target=%s status=%s\n",
                target, status_value ? status_value : "(null)");
        goto cleanup;
    }

    doc->book_title = extract_resolved_from_slice(html, article_block_start, article_block_end,
                                                  "bookTitle:");
    if (!doc->book_title || !doc->book_title[0]) {
        free(doc->book_title);
        doc->book_title = extract_attr_text(html, "<p class=\"mpCatalog_bookTitle\">");
    }
    if (doc->book_title && doc->book_title[0]) {
        doc->chapter_title = dup_or_null_reader(doc->book_title);
    }

    content_html = extract_article_content_html(html);
    if (!content_html || !content_html[0]) {
        fprintf(stderr,
                "reader-article-parse: content html missing target=%s\n",
                target);
        goto cleanup;
    }

    doc->content_text = html_strip_to_text(content_html);
    doc->target = strdup(target);
    doc->chapter_max_offset = extract_max_wco(content_html);
    if (extract_wco_values(content_html, &doc->chapter_offsets,
                           &doc->chapter_offset_count) != 0) {
        goto cleanup;
    }
    doc->font_size = font_size;
    if (!doc->content_text || !doc->content_text[0] || !doc->target) {
        fprintf(stderr,
                "reader-article-parse: content text missing target=%s\n",
                target);
        goto cleanup;
    }
    doc->use_content_font = 0;
    doc->content_font_path[0] = '\0';
    current_review_id = extract_resolved_from_slice(html, article_block_start, article_block_end,
                                                    "reviewId:");
    if ((!current_review_id || !current_review_id[0]) && target) {
        free(current_review_id);
        current_review_id = reader_extract_query_value(target, "reviewId");
    }
    if (reader_parse_article_catalog(ctx, html, article_block_start, article_block_end,
                                     font_size, current_review_id,
                                     &doc->catalog_items, &doc->catalog_count,
                                     &current_catalog_index) != 0) {
        fprintf(stderr,
                "reader-article-parse: catalog parse failed target=%s\n",
                target);
        goto cleanup;
    }
    if (doc->catalog_count > 0) {
        ReaderCatalogItem *current_item = NULL;

        doc->catalog_total_count = doc->catalog_count;
        doc->catalog_range_start = 1;
        doc->catalog_range_end = doc->catalog_count;
        if (current_catalog_index < 0 && current_review_id) {
            for (int i = 0; i < doc->catalog_count; i++) {
                if (doc->catalog_items[i].chapter_uid &&
                    strcmp(doc->catalog_items[i].chapter_uid, current_review_id) == 0) {
                    current_catalog_index = i;
                    doc->catalog_items[i].is_current = 1;
                    break;
                }
            }
        }
        if (current_catalog_index >= 0 && current_catalog_index < doc->catalog_count) {
            current_item = &doc->catalog_items[current_catalog_index];
        }
        if (current_item) {
            free(doc->chapter_uid);
            doc->chapter_uid = dup_or_null_reader(current_item->chapter_uid);
            doc->chapter_idx = current_item->chapter_idx;
            if (current_item->title && current_item->title[0]) {
                free(doc->chapter_title);
                doc->chapter_title = dup_or_null_reader(current_item->title);
            }
            if (current_catalog_index > 0) {
                doc->prev_target =
                    dup_or_null_reader(doc->catalog_items[current_catalog_index - 1].target);
            }
            if (current_catalog_index + 1 < doc->catalog_count) {
                doc->next_target =
                    dup_or_null_reader(doc->catalog_items[current_catalog_index + 1].target);
            }
        }
    } else if (current_review_id && current_review_id[0]) {
        doc->chapter_uid = dup_or_null_reader(current_review_id);
    }

    rc = 0;

cleanup:
    free(content_html);
    free(current_review_id);
    free(status_value);
    return rc;
}

static char *build_reader_url(ApiContext *ctx, const char *target, int font_size) {
    char *escaped;
    char *url;

    if (!target) {
        return NULL;
    }
    if (strncmp(target, "http://", 7) == 0 || strncmp(target, "https://", 8) == 0) {
        return strdup(target);
    }
    if (strncmp(target, "/web/reader/", 12) == 0) {
        escaped = api_escape(ctx, target + 12);
        if (!escaped) {
            return NULL;
        }
        url = malloc(strlen(WEREAD_BASE_URL) + strlen("/reader?bc=&fs=") + strlen(escaped) + 8);
        if (!url) {
            free(escaped);
            return NULL;
        }
        sprintf(url, "%s/reader?bc=%s&fs=%d", WEREAD_BASE_URL, escaped, font_size);
        free(escaped);
        return url;
    }
    if (strncmp(target, "/wrwebsimplenjlogic/mpdetail?", 28) == 0) {
        return api_build_url("https://weread.qq.com", target + 1);
    }
    if (strncmp(target, "/mpdetail?", 10) == 0) {
        return api_build_url(WEREAD_BASE_URL, target + 1);
    }
    if (strncmp(target, "/wrwebsimplenjlogic/reader?", 26) == 0) {
        return api_build_url("https://weread.qq.com", target + 1);
    }
    if (strncmp(target, "/reader?", 8) == 0) {
        return api_build_url(WEREAD_BASE_URL, target + 1);
    }

    escaped = api_escape(ctx, target);
    if (!escaped) {
        return NULL;
    }
    url = malloc(strlen(WEREAD_BASE_URL) + strlen("/reader?bc=&fs=") + strlen(escaped) + 8);
    if (!url) {
        free(escaped);
        return NULL;
    }
    sprintf(url, "%s/reader?bc=%s&fs=%d", WEREAD_BASE_URL, escaped, font_size);
    free(escaped);
    return url;
}

void reader_document_free(ReaderDocument *doc) {
    if (!doc) {
        return;
    }
    free(doc->target);
    free(doc->prev_target);
    free(doc->next_target);
    free(doc->book_id);
    free(doc->token);
    free(doc->chapter_uid);
    free(doc->progress_chapter_uid);
    free(doc->progress_summary);
    free(doc->book_title);
    free(doc->chapter_title);
    free(doc->content_text);
    free(doc->chapter_offsets);
    reader_catalog_items_free(doc->catalog_items, doc->catalog_count);
    memset(doc, 0, sizeof(*doc));
}

static int reader_load_internal(ApiContext *ctx, const char *target, int font_size,
                                ReaderDocument *doc, int save_last_reader) {
    Buffer buf = {0};
    char *url = build_reader_url(ctx, target, font_size);
    char *article_review_id = NULL;
    char *content_html_fallback = NULL;
    char *cur_chapter_alias = NULL;
    char *chapter_idx_value = NULL;
    char *chapter_word_count_value = NULL;
    const char *reader_block_start = NULL;
    const char *reader_block_end = NULL;
    int rc = -1;
    int is_article_target = reader_target_is_article(target);

    if (!url || !doc) {
        free(url);
        return -1;
    }

    fprintf(stderr,
            "reader-load: target=%s url=%s font=%d saveLast=%d article=%d\n",
            target, url, font_size, save_last_reader, is_article_target);
    memset(doc, 0, sizeof(*doc));
    if ((reader_fetch_override ? reader_fetch_override(ctx, url, &buf) :
                                 api_get(ctx, url, &buf)) != 0) {
        fprintf(stderr, "reader-load: fetch failed url=%s\n", url);
        goto cleanup;
    }

    doc->kind = is_article_target ? READER_DOCUMENT_KIND_ARTICLE
                                  : READER_DOCUMENT_KIND_BOOK;
    if (is_article_target) {
        rc = reader_parse_article_document(ctx, target, font_size, buf.data, doc);
        if (rc == 0) {
            fprintf(stderr,
                    "reader-load: article parsed target=%s docTarget=%s title=%s\n",
                    target,
                    doc->target ? doc->target : "(null)",
                    doc->book_title ? doc->book_title : "(null)");
        } else {
            fprintf(stderr, "reader-load: article parse failed target=%s\n", target);
        }
        goto cleanup;
    }

    if (reader_extract_article_review_id_from_reader_shell(buf.data, &article_review_id) == 1) {
        fprintf(stderr,
                "reader-load: article reader shell detected target=%s reviewId=%s\n",
                target, article_review_id);
        rc = reader_load_article_from_review_id(ctx, article_review_id, font_size, doc);
        free(article_review_id);
        article_review_id = NULL;
        goto cleanup;
    }

    doc->target = extract_reader_param_alias_value(buf.data, "curChapterUrlParam:{");
    doc->prev_target = extract_reader_param_alias_value(buf.data, "prevChapterUrlParam:{");
    doc->next_target = extract_reader_param_alias_value(buf.data, "nextChapterUrlParam:{");
    doc->book_id = extract_resolved_value_in_object_block(buf.data, "reader:{bookId:", "bookId:");
    doc->token = extract_resolved_value_in_object_block(buf.data, "reader:{bookId:", "token:");
    /* progress fields are nested: progress:{bookId:...,book:{chapterUid:...,chapterIdx:...,...}} */
    {
        const char *prog_start = NULL, *prog_end = NULL;
        char *book_block = NULL;
        if (find_named_object_block(buf.data, "progress:{bookId:", &prog_start, &prog_end) == 0) {
            book_block = extract_container_from_slice(prog_start, prog_end, "book:", '{', '}');
        }
        if (book_block) {
            const char *bb_end = book_block + strlen(book_block);
            doc->progress_summary = extract_resolved_from_slice(buf.data, book_block, bb_end, "summary:");
            doc->progress_chapter_uid = extract_resolved_from_slice(buf.data, book_block, bb_end, "chapterUid:");
            doc->progress_chapter_idx = extract_int_from_slice_resolved(buf.data, book_block, bb_end, "chapterIdx:", 0);
            doc->saved_chapter_offset = extract_int_from_slice_resolved(buf.data, book_block, bb_end, "chapterOffset:", 0);
            doc->last_reported_progress = extract_int_from_slice_resolved(buf.data, book_block, bb_end, "progress:", 0);
            free(book_block);
        }
    }
    doc->total_words = extract_int_in_object_block(buf.data, "bookInfo:{", "totalWords:", 0);
    doc->prev_chapters_word_count = extract_int_after_marker(buf.data, "prevChaptersWordCount:", 0);
    if (find_named_object_block(buf.data, "reader:{bookId:", &reader_block_start, &reader_block_end) == 0) {
        doc->catalog_total_count = extract_int_from_slice_resolved(
            buf.data, reader_block_start, reader_block_end, "chapterInfoCount:", 0);
        {
            char *range_block = extract_container_from_slice(reader_block_start, reader_block_end,
                                                             "curCatalogRange:", '{', '}');
            if (range_block) {
                const char *range_end = range_block + strlen(range_block);
                doc->catalog_range_start = extract_int_from_slice_resolved(
                    buf.data, range_block, range_end, "start:", 0);
                doc->catalog_range_end = extract_int_from_slice_resolved(
                    buf.data, range_block, range_end, "end:", 0);
                free(range_block);
            }
        }
        cur_chapter_alias = extract_literal_from_slice(reader_block_start, reader_block_end, "curChapter:");
    }
    if (cur_chapter_alias) {
        char *cur_chapter_literal = resolve_nuxt_alias_literal(buf.data, cur_chapter_alias);
        if (cur_chapter_literal) {
            const char *cur_start = cur_chapter_literal;
            const char *cur_end = cur_chapter_literal + strlen(cur_chapter_literal);
            free(doc->chapter_uid);
            doc->chapter_uid = extract_resolved_from_slice(buf.data, cur_start, cur_end, "chapterUid:");
            chapter_idx_value = extract_resolved_from_slice(buf.data, cur_start, cur_end, "chapterIdx:");
            chapter_word_count_value =
                extract_resolved_from_slice(buf.data, cur_start, cur_end, "wordCount:");
            free(cur_chapter_literal);
        }
        if (chapter_idx_value) {
            doc->chapter_idx = atoi(chapter_idx_value);
        }
        if (chapter_word_count_value) {
            doc->chapter_word_count = atoi(chapter_word_count_value);
        }
    }
    if ((!doc->chapter_uid || strcmp(doc->chapter_uid, "0") == 0) && doc->progress_chapter_uid) {
        free(doc->chapter_uid);
        doc->chapter_uid = dup_or_null_reader(doc->progress_chapter_uid);
    }
    if (doc->chapter_idx <= 0 && doc->progress_chapter_idx > 0) {
        doc->chapter_idx = doc->progress_chapter_idx;
    }

    if (reader_block_start && reader_block_end) {
        parse_reader_catalog(buf.data, reader_block_start, reader_block_end,
                             &doc->catalog_items, &doc->catalog_count);
    }
    /* When curChapter is empty (Kindle-simplified SSR), chapter_uid/chapter_idx
     * came from the progress fallback which reflects the SERVER's last known
     * progress, not the chapter being viewed right now.  Fix this by looking up
     * the actual chapter in the catalog using the target URL. */
    {
        const char *lookup_target = doc->target ? doc->target : target;
        int found_in_catalog = 0;
        if (lookup_target && doc->catalog_items && doc->catalog_count > 0) {
            for (int i = 0; i < doc->catalog_count; i++) {
                if (doc->catalog_items[i].target &&
                    strcmp(doc->catalog_items[i].target, lookup_target) == 0) {
                    if (doc->catalog_items[i].chapter_uid) {
                        free(doc->chapter_uid);
                        doc->chapter_uid = strdup(doc->catalog_items[i].chapter_uid);
                    }
                    if (doc->catalog_items[i].chapter_idx > 0) {
                        doc->chapter_idx = doc->catalog_items[i].chapter_idx;
                    }
                    if (doc->chapter_word_count <= 0 && doc->catalog_items[i].word_count > 0) {
                        doc->chapter_word_count = doc->catalog_items[i].word_count;
                    }
                    found_in_catalog = 1;
                    break;
                }
            }
        }
        /* Fallback: chapter not in local catalog (non-contiguous range gap).
         * Fetch a catalog chunk around the progress chapter index and search. */
        if (!found_in_catalog && lookup_target && doc->book_id && doc->book_id[0] &&
            doc->progress_chapter_idx > 0) {
            ReaderCatalogItem *chunk = NULL;
            int chunk_count = 0;
            int first_idx = 0, last_idx = 0;
            int range = doc->progress_chapter_idx > 40 ? doc->progress_chapter_idx - 40 : 0;
            if (reader_fetch_catalog_chunk(ctx, doc->book_id, 2, range, range,
                                           NULL, &chunk, &chunk_count,
                                           &first_idx, &last_idx) == 0 && chunk_count > 0) {
                for (int i = 0; i < chunk_count; i++) {
                    if (!found_in_catalog && chunk[i].target &&
                        strcmp(chunk[i].target, lookup_target) == 0) {
                        if (chunk[i].chapter_uid) {
                            free(doc->chapter_uid);
                            doc->chapter_uid = strdup(chunk[i].chapter_uid);
                        }
                        if (chunk[i].chapter_idx > 0) {
                            doc->chapter_idx = chunk[i].chapter_idx;
                        }
                        if (doc->chapter_word_count <= 0 && chunk[i].word_count > 0) {
                            doc->chapter_word_count = chunk[i].word_count;
                        }
                        found_in_catalog = 1;
                    }
                    free(chunk[i].chapter_uid);
                    free(chunk[i].target);
                    free(chunk[i].title);
                }
                free(chunk);
            }
        }
    }
    content_html_fallback = extract_obfuscated_content_html(buf.data);
    if (!content_html_fallback || !*content_html_fallback) {
        fprintf(stderr, "Kindle simplified reader content not found in page DOM.\n");
        goto cleanup;
    }

    doc->content_text = html_strip_to_text(content_html_fallback);
    doc->book_title = extract_attr_text(buf.data, "<p class=\"readerCatalog_bookTitle\">");
    doc->chapter_title = extract_attr_text(buf.data, "<p class=\"name\">");
    if (!doc->target) {
        doc->target = strdup(target);
    }
    doc->chapter_max_offset = extract_max_wco(content_html_fallback);
    if (extract_wco_values(content_html_fallback, &doc->chapter_offsets, &doc->chapter_offset_count) != 0) {
        goto cleanup;
    }
    doc->font_size = font_size;
    if (!doc->content_text || !doc->target) {
        goto cleanup;
    }
    reader_focus_catalog(ctx, doc);
    if (cached_random_font_path(ctx, doc->content_font_path, sizeof(doc->content_font_path)) == 0) {
        doc->use_content_font = 1;
    }

    rc = 0;
    if (save_last_reader && doc->kind == READER_DOCUMENT_KIND_BOOK) {
        reader_state_save_last_reader(ctx, doc->target, font_size, 36);
    }

cleanup:
    if (rc != 0) {
        reader_document_free(doc);
    }
    api_buffer_free(&buf);
    free(article_review_id);
    free(url);
    free(content_html_fallback);
    free(cur_chapter_alias);
    free(chapter_idx_value);
    free(chapter_word_count_value);
    return rc;
}

int reader_load(ApiContext *ctx, const char *target, int font_size, ReaderDocument *doc) {
    return reader_load_internal(ctx, target, font_size, doc, 1);
}

int reader_prefetch(ApiContext *ctx, const char *target, int font_size, ReaderDocument *doc) {
    return reader_load_internal(ctx, target, font_size, doc, 0);
}

int reader_ensure_full_catalog(ApiContext *ctx, ReaderDocument *doc) {
    if (!doc) {
        return -1;
    }
    if (doc->catalog_total_count <= 0 || doc->catalog_count <= 0 ||
        doc->catalog_count >= doc->catalog_total_count) {
        return 0;
    }
    return reader_hydrate_full_catalog(ctx, doc);
}

int reader_merge_catalog_chunk(ReaderDocument *doc, ReaderCatalogItem *chunk, int chunk_count,
                               int *added_count) {
    int cap = 0;
    int before_count = 0;

    if (added_count) {
        *added_count = 0;
    }
    if (!doc || !doc->catalog_items || doc->catalog_count < 0 || !chunk || chunk_count < 0) {
        return -1;
    }

    before_count = doc->catalog_count;
    cap = doc->catalog_count;
    for (int i = 0; i < chunk_count; i++) {
        if (reader_catalog_merge_item(&doc->catalog_items, &doc->catalog_count, &cap,
                                      &chunk[i]) != 0) {
            return -1;
        }
    }
    if (doc->catalog_count > 1) {
        qsort(doc->catalog_items, (size_t)doc->catalog_count, sizeof(*doc->catalog_items),
              catalog_item_cmp_chapter_idx);
    }
    if (added_count) {
        *added_count = doc->catalog_count - before_count;
    }
    return 0;
}

int reader_expand_catalog(ApiContext *ctx, ReaderDocument *doc, int direction, int *added_count) {
    ReaderCatalogItem *chunk = NULL;
    int chunk_count = 0;
    int first_idx = 0;
    int last_idx = 0;

    if (added_count) {
        *added_count = 0;
    }
    if (!ctx || !doc || !doc->book_id || !doc->catalog_items || doc->catalog_count <= 0 ||
        doc->catalog_total_count <= 0) {
        return -1;
    }

    first_idx = doc->catalog_items[0].chapter_idx;
    last_idx = doc->catalog_items[doc->catalog_count - 1].chapter_idx;

    if (direction < 0) {
        if (first_idx <= 1) {
            return 0;
        }
        if (reader_fetch_catalog_chunk(ctx, doc->book_id, 1, first_idx, last_idx,
                                       doc->chapter_uid, &chunk, &chunk_count,
                                       &first_idx, &last_idx) != 0 || chunk_count <= 0) {
            return -1;
        }
    } else if (direction > 0) {
        if (last_idx >= doc->catalog_total_count) {
            return 0;
        }
        if (reader_fetch_catalog_chunk(ctx, doc->book_id, 2, first_idx, last_idx,
                                       doc->chapter_uid, &chunk, &chunk_count,
                                       &first_idx, &last_idx) != 0 || chunk_count <= 0) {
            return -1;
        }
    } else {
        return -1;
    }

    if (reader_merge_catalog_chunk(doc, chunk, chunk_count, added_count) != 0) {
        reader_catalog_items_free(chunk, chunk_count);
        return -1;
    }
    reader_catalog_items_free(chunk, chunk_count);
    return 0;
}

char *reader_find_chapter_target(ApiContext *ctx, const char *book_id,
                                const char *chapter_uid, int chapter_idx) {
    ReaderCatalogItem *chunk = NULL;
    int chunk_count = 0;
    int first_idx = 0;
    int last_idx = 0;
    char *result = NULL;
    int range;

    if (!ctx || !book_id || !*book_id || (chapter_idx <= 0 && (!chapter_uid || !*chapter_uid))) {
        return NULL;
    }
    /* Use chapter_idx as the center of the range to fetch. Ask for a small
       window around it so the API returns the chunk that contains it. */
    range = chapter_idx > 20 ? chapter_idx - 20 : 0;
    if (reader_fetch_catalog_chunk(ctx, book_id, 2, range, range,
                                   NULL, &chunk, &chunk_count,
                                   &first_idx, &last_idx) != 0 || chunk_count <= 0) {
        return NULL;
    }
    for (int i = 0; i < chunk_count; i++) {
        if (!result) {
            if (chapter_uid && chapter_uid[0] && strcmp(chapter_uid, "0") != 0 &&
                chunk[i].chapter_uid && strcmp(chunk[i].chapter_uid, chapter_uid) == 0 &&
                chunk[i].target) {
                result = strdup(chunk[i].target);
            } else if (chapter_idx > 0 && chunk[i].chapter_idx == chapter_idx &&
                       chunk[i].target) {
                result = strdup(chunk[i].target);
            }
        }
        free(chunk[i].chapter_uid);
        free(chunk[i].target);
        free(chunk[i].title);
    }
    free(chunk);
    return result;
}

int reader_estimate_chapter_offset(const ReaderDocument *doc, int current_page, int total_pages) {
    int total_pages_safe = total_pages > 0 ? total_pages : 1;

    if (!doc) {
        return 0;
    }

    if (current_page < 0) {
        current_page = 0;
    }
    if (current_page >= total_pages_safe) {
        current_page = total_pages_safe - 1;
    }

    if (doc->chapter_offset_count > 0 && doc->chapter_offsets) {
        int sample_index =
            (int)(((long long)doc->chapter_offset_count * current_page) / total_pages_safe);
        if (sample_index < 0) {
            sample_index = 0;
        }
        if (sample_index >= doc->chapter_offset_count) {
            sample_index = doc->chapter_offset_count - 1;
        }
        return doc->chapter_offsets[sample_index];
    }

    if (doc->chapter_max_offset > 0) {
        return (int)(((long long)doc->chapter_max_offset * current_page) / total_pages_safe);
    }

    return 0;
}

int reader_report_progress_at_offset(ApiContext *ctx, const ReaderDocument *doc, int current_page,
                                     int total_pages, int reading_seconds,
                                     const char *page_summary, int compute_progress,
                                     int chapter_offset_override) {
    char *url;
    Buffer buf = {0};
    char *body = NULL;
    cJSON *payload = NULL;
    cJSON *resp_json = NULL;
    cJSON *succ = NULL;
    cJSON *timeout = NULL;
    const char *summary = NULL;
    int offset = 0;
    int progress = 0;
    int total_pages_safe = total_pages > 0 ? total_pages : 1;
    long long now_ms = (long long)time(NULL) * 1000LL;
    long long now_seconds = (long long)time(NULL);
    int random_value = rand() % 1000;

    if (!ctx || !doc || !doc->book_id || !doc->token || !doc->chapter_uid || doc->chapter_idx <= 0) {
        return READER_REPORT_ERROR;
    }

    if (reading_seconds < 0) {
        reading_seconds = 0;
    }
    if (reading_seconds > 60) {
        reading_seconds = 60;
    }

    offset = chapter_offset_override >= 0 ?
        chapter_offset_override :
        reader_estimate_chapter_offset(doc, current_page, total_pages_safe);

    if (!compute_progress) {
        progress = doc->last_reported_progress;
    } else if (doc->total_words > 0) {
        int chapter_progress_words = 0;
        if (doc->chapter_word_count > 0) {
            chapter_progress_words =
                (int)(((long long)doc->chapter_word_count * current_page) / total_pages_safe);
        }
        progress = (int)(((long long)(doc->prev_chapters_word_count + chapter_progress_words) * 100) /
                         doc->total_words);
        if (progress < 0) {
            progress = 0;
        }
        if (progress > 100) {
            progress = 100;
        }
    } else {
        progress = doc->last_reported_progress;
    }

    summary = reader_choose_summary(doc, page_summary);

    payload = cJSON_CreateObject();
    if (!payload) {
        return READER_REPORT_ERROR;
    }
    cJSON_AddStringToObject(payload, "b", doc->book_id);
    reader_add_chapter_uid_field(payload, doc->chapter_uid);
    cJSON_AddNumberToObject(payload, "ci", doc->chapter_idx);
    cJSON_AddNumberToObject(payload, "co", offset);
    cJSON_AddStringToObject(payload, "sm", summary);
    cJSON_AddNumberToObject(payload, "pr", progress);
    cJSON_AddNumberToObject(payload, "rt", reading_seconds);
    cJSON_AddNumberToObject(payload, "ts", (double)now_ms);
    cJSON_AddNumberToObject(payload, "rn", random_value);
    cJSON_AddStringToObject(payload, "tk", doc->token);
    cJSON_AddNumberToObject(payload, "ct", (double)now_seconds);
    body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    payload = NULL;
    if (!body) {
        return READER_REPORT_ERROR;
    }

    /* The Kindle simplified endpoint is the one that actually persists
     * cloud reading progress for this client flow. */
    url = strdup(WEREAD_API_BASE_URL "/bookread");
    if (!url) {
        free(body);
        return READER_REPORT_ERROR;
    }

    if (api_post(ctx, url, body, &buf) != 0) {
        fprintf(stderr, "Failed to report reading progress.\n");
        free(url);
        free(body);
        return READER_REPORT_ERROR;
    }

    resp_json = buf.data ? cJSON_Parse(buf.data) : NULL;
    if (!resp_json) {
        fprintf(stderr, "Reading progress response was invalid.\n");
        api_buffer_free(&buf);
        free(url);
        free(body);
        return READER_REPORT_ERROR;
    }

    timeout = cJSON_GetObjectItem(resp_json, "sessionTimeout");
    if (timeout && cJSON_IsNumber(timeout) && timeout->valueint != 0) {
        cJSON_Delete(resp_json);
        api_buffer_free(&buf);
        free(url);
        free(body);
        return READER_REPORT_SESSION_EXPIRED;
    }

    succ = json_get_path(resp_json, "data.succ");
    if (!json_is_truthy(succ)) {
        cJSON *err = cJSON_GetObjectItem(resp_json, "errMsg");
        fprintf(stderr, "bookread: server rejected: %s\n",
                (err && cJSON_IsString(err)) ? err->valuestring : "(unknown)");
        cJSON_Delete(resp_json);
        api_buffer_free(&buf);
        free(url);
        free(body);
        return READER_REPORT_ERROR;
    }

    cJSON_Delete(resp_json);
    api_buffer_free(&buf);
    free(url);
    free(body);
    return READER_REPORT_OK;
}

int reader_report_progress(ApiContext *ctx, const ReaderDocument *doc, int current_page,
                           int total_pages, int reading_seconds, const char *page_summary,
                           int compute_progress) {
    return reader_report_progress_at_offset(ctx, doc, current_page, total_pages,
                                            reading_seconds, page_summary,
                                            compute_progress, -1);
}
