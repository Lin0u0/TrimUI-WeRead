/*
 * reader_parser.c - JS/HTML parsing utilities for WeRead reader
 *
 * Handles: NUXT alias resolution, JS literal parsing, HTML attribute extraction
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reader_internal.h"
#include "json.h"

/* ====================== Basic String Utilities ====================== */

const char *reader_skip_ws(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
        p++;
    }
    return p;
}

const char *reader_find_matching_pair(const char *start, const char *end, char open_ch, char close_ch) {
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

char *reader_dup_range(const char *start, const char *end) {
    size_t len = (size_t)(end - start);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

char *reader_dup_or_null(const char *s) {
    return s ? strdup(s) : NULL;
}

int reader_join_path_checked(char *dst, size_t dst_size, const char *dir, const char *name) {
    size_t dir_len;
    size_t name_len;

    if (!dst || dst_size == 0 || !dir || !name) {
        return -1;
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

/* ====================== JS Identifier Check ====================== */

static int is_js_ident_char(char ch) {
    return isalnum((unsigned char)ch) || ch == '_' || ch == '$';
}

/* ====================== Find Top-Level Field ====================== */

const char *reader_find_top_level_field(const char *slice_start, const char *slice_end,
                                        const char *field_marker, const char **value_start) {
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
            if (value_start) {
                *value_start = p + marker_len;
            }
            return p;
        }
    }

    return NULL;
}

/* ====================== JS Literal Parsing ====================== */

static int parse_js_literal_for_alias(const char **cursor, const char *end, char **out_literal) {
    const char *p = reader_skip_ws(*cursor, end);
    const char *start;

    if (p >= end) {
        return -1;
    }

    /* String literal */
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
                *out_literal = reader_dup_range(start, p);
                *cursor = p;
                return *out_literal ? 0 : -1;
            }
            p++;
        }
        return -1;
    }

    /* Special JS values */
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
        *out_literal = reader_dup_range(p, p + 4);
        *cursor = p + 4;
        return *out_literal ? 0 : -1;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out_literal = reader_dup_range(p, p + 5);
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

    /* Object literal - skip */
    if (*p == '{') {
        const char *close = reader_find_matching_pair(p, end, '{', '}');
        if (!close) {
            return -1;
        }
        *out_literal = strdup("{}");
        *cursor = close + 1;
        return *out_literal ? 0 : -1;
    }

    /* Array literal - skip */
    if (*p == '[') {
        const char *close = reader_find_matching_pair(p, end, '[', ']');
        if (!close) {
            return -1;
        }
        *out_literal = strdup("[]");
        *cursor = close + 1;
        return *out_literal ? 0 : -1;
    }

    /* Number */
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
        *out_literal = reader_dup_range(start, p);
        *cursor = p;
        return *out_literal ? 0 : -1;
    }

    /* Identifier (alias) */
    if (is_js_ident_char(*p)) {
        start = p;
        p++;
        while (p < end && is_js_ident_char(*p)) {
            p++;
        }
        *out_literal = reader_dup_range(start, p);
        *cursor = p;
        return *out_literal ? 0 : -1;
    }

    return -1;
}

/* ====================== NUXT Alias Resolution ====================== */

char *reader_resolve_nuxt_alias_string(const char *html, const char *alias) {
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
    body_end = reader_find_matching_pair(body_start, end, '{', '}');
    if (!body_end) {
        return NULL;
    }

    params_copy = reader_dup_range(params_start, params_end);
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
    call_end = reader_find_matching_pair(call_start, end, '(', ')');
    if (!call_end) {
        goto cleanup;
    }

    {
        const char *cursor = call_start + 1;
        for (int i = 0; i <= target_index; i++) {
            char *literal = NULL;
            cJSON *json;

            cursor = reader_skip_ws(cursor, call_end);
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
            cursor = reader_skip_ws(cursor, call_end);
            if (cursor < call_end && *cursor == ',') {
                cursor++;
            }
        }
    }

cleanup:
    free(params_copy);
    return resolved;
}

char *reader_resolve_nuxt_alias_literal(const char *html, const char *alias) {
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
    body_end = reader_find_matching_pair(body_start, end, '{', '}');
    if (!body_end) {
        return NULL;
    }

    params_copy = reader_dup_range(params_start, params_end);
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
    call_end = reader_find_matching_pair(call_start, end, '(', ')');
    if (!call_end) {
        goto cleanup;
    }

    {
        const char *cursor = call_start + 1;
        for (int i = 0; i <= target_index; i++) {
            char *literal = NULL;

            cursor = reader_skip_ws(cursor, call_end);
            if (cursor >= call_end || parse_js_literal_for_alias(&cursor, call_end, &literal) != 0) {
                free(literal);
                goto cleanup;
            }
            if (i == target_index) {
                resolved = literal;
                break;
            }
            free(literal);
            cursor = reader_skip_ws(cursor, call_end);
            if (cursor < call_end && *cursor == ',') {
                cursor++;
            }
        }
    }

cleanup:
    free(params_copy);
    return resolved;
}

/* ====================== Container Extraction ====================== */

char *reader_extract_container_from_slice(const char *block_start, const char *block_end,
                                          const char *marker, char open_ch, char close_ch) {
    const char *value_start;
    const char *field = reader_find_top_level_field(block_start, block_end, marker, &value_start);
    const char *cursor;
    const char *close;
    char *alias = NULL;
    char *resolved = NULL;

    if (!field || field >= block_end) {
        return NULL;
    }
    cursor = reader_skip_ws(value_start, block_end);
    if (cursor >= block_end) {
        return NULL;
    }
    if (*cursor == open_ch) {
        close = reader_find_matching_pair(cursor, block_end, open_ch, close_ch);
        if (!close) {
            return NULL;
        }
        return reader_dup_range(cursor, close + 1);
    }
    if (parse_js_literal_for_alias(&cursor, block_end, &alias) != 0 || !alias) {
        free(alias);
        return NULL;
    }
    if (alias[0] == open_ch) {
        return alias;
    }
    resolved = reader_resolve_nuxt_alias_literal(block_start, alias);
    free(alias);
    return resolved;
}

char *reader_extract_resolved_value_after_marker(const char *html, const char *block_start,
                                                 const char *block_end, const char *marker) {
    const char *value_start;
    const char *cursor;
    char *literal = NULL;
    char *resolved = NULL;
    cJSON *json;

    const char *field = reader_find_top_level_field(block_start, block_end, marker, &value_start);
    if (!field) {
        return NULL;
    }
    cursor = value_start;
    if (parse_js_literal_for_alias(&cursor, block_end, &literal) != 0 || !literal) {
        free(literal);
        return NULL;
    }

    json = cJSON_Parse(literal);
    if (json) {
        if (cJSON_IsString(json) && json->valuestring) {
            resolved = strdup(json->valuestring);
        } else if (cJSON_IsNumber(json)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.0f", json->valuedouble);
            resolved = strdup(buf);
        }
        cJSON_Delete(json);
        free(literal);
        return resolved;
    }

    /* Try alias resolution */
    if (is_js_ident_char(literal[0])) {
        resolved = reader_resolve_nuxt_alias_string(html, literal);
        free(literal);
        return resolved;
    }

    return literal;
}

void reader_add_chapter_uid_field(cJSON *payload, const char *chapter_uid) {
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

const char *reader_choose_summary(const ReaderDocument *doc, const char *page_summary) {
    if (page_summary && page_summary[0]) {
        return page_summary;
    }
    if (doc && doc->progress_summary && doc->progress_summary[0]) {
        return doc->progress_summary;
    }
    return "";
}
