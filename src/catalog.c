/*
 * catalog.c - Catalog management for WeRead reader
 *
 * Handles: catalog items, param maps, catalog parsing, chapter navigation
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reader_internal.h"
#include "api.h"
#include "json.h"

/* ====================== Param Map Item Type ====================== */

typedef struct {
    char *chapter_uid;
    char *target;
} ReaderParamMapItem;

/* ====================== Memory Management ====================== */

void reader_catalog_items_free(ReaderCatalogItem *items, int count) {
    if (!items) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(items[i].chapter_uid);
        free(items[i].target);
        free(items[i].title);
    }
    free(items);
}

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

/* ====================== Param Map Operations ====================== */

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

        cursor = reader_skip_ws(cursor, end);
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

        obj_end = reader_find_matching_pair(cursor, end, '{', '}');
        if (!obj_end) {
            break;
        }

        item.chapter_uid = reader_extract_resolved_value_after_marker(html, cursor, obj_end + 1, "cUid:");
        item.target = reader_extract_resolved_value_after_marker(html, cursor, obj_end + 1, "param:");
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

static int json_array_to_param_map(cJSON *array, ReaderParamMapItem **items_out, int *count_out) {
    ReaderParamMapItem *items = NULL;
    int count = 0;
    int cap = 0;
    ReaderParamMapItem *tmp;
    cJSON *entry;

    if (!items_out || !count_out || !cJSON_IsArray(array)) {
        return -1;
    }

    cJSON_ArrayForEach(entry, array) {
        cJSON *uid_item;
        cJSON *param_item;
        ReaderParamMapItem item = {0};

        if (!cJSON_IsObject(entry)) {
            continue;
        }
        uid_item = cJSON_GetObjectItemCaseSensitive(entry, "cUid");
        param_item = cJSON_GetObjectItemCaseSensitive(entry, "param");
        if (!uid_item || !param_item || !cJSON_IsString(param_item) || !param_item->valuestring) {
            continue;
        }

        if (cJSON_IsString(uid_item) && uid_item->valuestring) {
            item.chapter_uid = strdup(uid_item->valuestring);
        } else if (cJSON_IsNumber(uid_item)) {
            char uid_buf[64];
            snprintf(uid_buf, sizeof(uid_buf), "%.0f", uid_item->valuedouble);
            item.chapter_uid = strdup(uid_buf);
        }
        item.target = strdup(param_item->valuestring);

        if (!item.chapter_uid || !item.target) {
            free(item.chapter_uid);
            free(item.target);
            continue;
        }

        if (count >= cap) {
            int new_cap = cap > 0 ? cap * 2 : 64;
            tmp = realloc(items, sizeof(*items) * (size_t)new_cap);
            if (!tmp) {
                reader_param_map_free(items, count);
                free(item.chapter_uid);
                free(item.target);
                return -1;
            }
            items = tmp;
            cap = new_cap;
        }
        items[count++] = item;
    }

    *items_out = items;
    *count_out = count;
    return 0;
}

/* ====================== Catalog Item Operations ====================== */

int catalog_item_cmp_chapter_idx(const void *a, const void *b) {
    return ((const ReaderCatalogItem *)a)->chapter_idx -
           ((const ReaderCatalogItem *)b)->chapter_idx;
}

int reader_catalog_find_index(ReaderCatalogItem *items, int count, const char *chapter_uid) {
    if (!items || count <= 0 || !chapter_uid || !*chapter_uid) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (items[i].chapter_uid && strcmp(items[i].chapter_uid, chapter_uid) == 0) {
            return i;
        }
    }
    return -1;
}

static char *reader_format_catalog_title(int chapter_idx, const char *title) {
    char prefix[64];
    size_t prefix_len;
    size_t title_len;
    char *formatted;

    if (!title || !*title) {
        return NULL;
    }
    if (chapter_idx > 0) {
        snprintf(prefix, sizeof(prefix), "第%d章 ", chapter_idx);
        if (strncmp(title, prefix, strlen(prefix)) == 0) {
            return strdup(title);
        }
    }
    if (chapter_idx <= 0) {
        return strdup(title);
    }

    snprintf(prefix, sizeof(prefix), "第%d章 ", chapter_idx);
    prefix_len = strlen(prefix);
    title_len = strlen(title);
    formatted = malloc(prefix_len + title_len + 1);
    if (!formatted) {
        return NULL;
    }
    memcpy(formatted, prefix, prefix_len);
    memcpy(formatted + prefix_len, title, title_len + 1);
    return formatted;
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

/* ====================== Extract from Slice Helpers ====================== */

static char *extract_array_from_slice(const char *html, const char *slice_start,
                                      const char *slice_end, const char *field_marker) {
    return reader_extract_container_from_slice(slice_start, slice_end, field_marker, '[', ']');
}

static int extract_int_from_slice_resolved(const char *html, const char *slice_start,
                                           const char *slice_end, const char *marker, int def) {
    char *value = reader_extract_resolved_value_after_marker(html, slice_start, slice_end, marker);
    int result = def;
    if (value) {
        result = atoi(value);
        free(value);
    }
    return result;
}

static int parse_bool_from_slice_resolved(const char *html, const char *slice_start,
                                          const char *slice_end, const char *marker, int def) {
    char *value = reader_extract_resolved_value_after_marker(html, slice_start, slice_end, marker);
    int result = def;
    if (value) {
        if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
            result = 1;
        } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
            result = 0;
        }
        free(value);
    }
    return result;
}

/* ====================== Catalog Block Parsing ====================== */

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

        cursor = reader_skip_ws(cursor, end);
        if (cursor >= end) break;
        if (*cursor == ',') { cursor++; continue; }
        if (*cursor != '{') break;

        obj_end = reader_find_matching_pair(cursor, end, '{', '}');
        if (!obj_end) break;

        item.chapter_uid = reader_extract_resolved_value_after_marker(html, cursor, obj_end + 1, "chapterUid:");
        item.title = reader_extract_resolved_value_after_marker(html, cursor, obj_end + 1, "displayTitle:");
        if (!item.title) {
            item.title = reader_extract_resolved_value_after_marker(html, cursor, obj_end + 1, "title:");
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

            field_marker = reader_dup_range(name_start, name_end + 1);
            if (!field_marker) {
                p = name_end;
                continue;
            }
            pc_block = reader_extract_container_from_slice(reader_block_start, reader_block_end,
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

/* ====================== Public Catalog Parsing ====================== */

int reader_parse_catalog(const char *html, const char *reader_block_start,
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
        free(chapter_indexes); chapter_indexes = NULL;
        free(reader_params); reader_params = NULL;
        parse_catalog_blocks_in_reader_state(html, reader_block_start, reader_block_end,
                                             &param_map, &param_count, &items, &count, &cap);

        if (count > 0) {
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

        cursor = reader_skip_ws(cursor, end);
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

        obj_end = reader_find_matching_pair(cursor, end, '{', '}');
        if (!obj_end) {
            goto cleanup;
        }

        item.chapter_uid = reader_extract_resolved_value_after_marker(html, cursor, obj_end + 1, "chapterUid:");
        item.title = reader_extract_resolved_value_after_marker(html, cursor, obj_end + 1, "displayTitle:");
        if (!item.title) {
            item.title = reader_extract_resolved_value_after_marker(html, cursor, obj_end + 1, "title:");
        }
        item.chapter_idx = extract_int_from_slice_resolved(html, cursor, obj_end + 1, "chapterIdx:", 0);
        item.word_count = extract_int_from_slice_resolved(html, cursor, obj_end + 1, "wordCount:", 0);
        item.level = extract_int_from_slice_resolved(html, cursor, obj_end + 1, "level:", 1);
        item.is_current = parse_bool_from_slice_resolved(html, cursor, obj_end + 1, "isCurrent:", 0);
        item.is_lock = parse_bool_from_slice_resolved(html, cursor, obj_end + 1, "isLock:", 0);
        target = reader_param_map_find(param_map, param_count, item.chapter_uid);
        item.target = target ? strdup(target) : NULL;

        if (!item.chapter_uid || !item.target || !item.title) {
            free(item.chapter_uid);
            free(item.target);
            free(item.title);
            cursor = obj_end + 1;
            continue;
        }

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
        cursor = obj_end + 1;
    }

    if (count > 0) {
        qsort(items, (size_t)count, sizeof(*items), catalog_item_cmp_chapter_idx);
        *items_out = items;
        *count_out = count;
        items = NULL;
        rc = 0;
    }

cleanup:
    reader_catalog_items_free(items, count);
    reader_param_map_free(param_map, param_count);
    free(chapter_indexes);
    free(reader_params);
    return rc;
}

/* ====================== Catalog JSON Parsing ====================== */

int reader_parse_catalogloadmore_json(cJSON *json, const char *current_chapter_uid,
                                             ReaderCatalogItem **items_out, int *count_out,
                                             int *first_idx_out, int *last_idx_out) {
    cJSON *chapter_infos;
    cJSON *reader_url_params;
    ReaderParamMapItem *param_map = NULL;
    int param_count = 0;
    ReaderCatalogItem *items = NULL;
    int count = 0;
    int cap = 0;
    int first_idx = 0;
    int last_idx = 0;
    cJSON *info;

    if (!json || !items_out || !count_out) {
        return -1;
    }

    chapter_infos = cJSON_GetObjectItemCaseSensitive(json, "chapterInfos");
    reader_url_params = cJSON_GetObjectItemCaseSensitive(json, "readerUrlParmas");
    if (!reader_url_params) {
        reader_url_params = cJSON_GetObjectItemCaseSensitive(json, "readerUrlParams");
    }
    if (!cJSON_IsArray(chapter_infos) || json_array_to_param_map(reader_url_params, &param_map, &param_count) != 0) {
        reader_param_map_free(param_map, param_count);
        return -1;
    }

    cJSON_ArrayForEach(info, chapter_infos) {
        cJSON *uid_item;
        cJSON *idx_item;
        cJSON *title_item;
        cJSON *paid_item;
        ReaderCatalogItem item = {0};
        const char *target;
        char uid_buf[64];

        if (!cJSON_IsObject(info)) {
            continue;
        }

        uid_item = cJSON_GetObjectItemCaseSensitive(info, "chapterUid");
        idx_item = cJSON_GetObjectItemCaseSensitive(info, "chapterIdx");
        title_item = cJSON_GetObjectItemCaseSensitive(info, "title");
        paid_item = cJSON_GetObjectItemCaseSensitive(info, "paid");
        if (!uid_item || !idx_item || !title_item || !cJSON_IsString(title_item) || !title_item->valuestring) {
            continue;
        }

        if (cJSON_IsString(uid_item) && uid_item->valuestring) {
            snprintf(uid_buf, sizeof(uid_buf), "%s", uid_item->valuestring);
        } else if (cJSON_IsNumber(uid_item)) {
            snprintf(uid_buf, sizeof(uid_buf), "%.0f", uid_item->valuedouble);
        } else {
            continue;
        }

        item.chapter_uid = strdup(uid_buf);
        item.chapter_idx = cJSON_IsNumber(idx_item) ? (int)idx_item->valuedouble : 0;
        {
            cJSON *wc_item = cJSON_GetObjectItemCaseSensitive(info, "wordCount");
            item.word_count = (wc_item && cJSON_IsNumber(wc_item)) ? (int)wc_item->valuedouble : 0;
        }
        item.title = reader_format_catalog_title(item.chapter_idx, title_item->valuestring);
        item.level = 1;
        item.is_current = current_chapter_uid && strcmp(current_chapter_uid, uid_buf) == 0;
        item.is_lock = paid_item && cJSON_IsNumber(paid_item) ? ((int)paid_item->valuedouble != 0) : 0;
        target = reader_param_map_find(param_map, param_count, uid_buf);
        item.target = target ? strdup(target) : NULL;

        if (!item.chapter_uid || !item.title || !item.target) {
            free(item.chapter_uid);
            free(item.title);
            free(item.target);
            continue;
        }

        if (count == 0) {
            first_idx = item.chapter_idx;
        }
        last_idx = item.chapter_idx;

        if (reader_catalog_merge_item(&items, &count, &cap, &item) != 0) {
            free(item.chapter_uid);
            free(item.title);
            free(item.target);
            reader_catalog_items_free(items, count);
            reader_param_map_free(param_map, param_count);
            return -1;
        }
    }

    reader_param_map_free(param_map, param_count);
    *items_out = items;
    *count_out = count;
    if (first_idx_out) {
        *first_idx_out = first_idx;
    }
    if (last_idx_out) {
        *last_idx_out = last_idx;
    }
    return count > 0 ? 0 : -1;
}

/* ====================== Catalog Fetching ====================== */

int reader_fetch_catalog_chunk(ApiContext *ctx, const char *book_id, int type,
                                      int range_start, int range_end, const char *current_chapter_uid,
                                      ReaderCatalogItem **items_out, int *count_out,
                                      int *first_idx_out, int *last_idx_out) {
    char *url;
    cJSON *json;
    int rc;

    if (!ctx || !book_id || !*book_id || !items_out || !count_out) {
        return -1;
    }

    url = malloc(strlen(WEREAD_API_BASE_URL) + 128 + strlen(book_id));
    if (!url) {
        return -1;
    }
    snprintf(url, strlen(WEREAD_API_BASE_URL) + 128 + strlen(book_id),
             "%s/catalogloadmore?type=%d&bookId=%s&rangeStart=%d&rangeEnd=%d",
             WEREAD_API_BASE_URL, type, book_id, range_start, range_end);

    json = api_get_json(ctx, url);
    free(url);
    if (!json) {
        return -1;
    }

    rc = reader_parse_catalogloadmore_json(json, current_chapter_uid, items_out, count_out,
                                           first_idx_out, last_idx_out);
    cJSON_Delete(json);
    return rc;
}

int reader_hydrate_full_catalog(ApiContext *ctx, ReaderDocument *doc) {
    int max_passes = 64;
    int pass = 0;

    if (!ctx || !doc || !doc->book_id || !doc->catalog_total_count ||
        !doc->catalog_items || doc->catalog_count <= 0) {
        return -1;
    }

    qsort(doc->catalog_items, (size_t)doc->catalog_count, sizeof(*doc->catalog_items),
          catalog_item_cmp_chapter_idx);
    fprintf(stderr,
            "reader-catalog-hydrate: begin bookId=%s count=%d total=%d first=%d last=%d\n",
            doc->book_id,
            doc->catalog_count,
            doc->catalog_total_count,
            doc->catalog_items[0].chapter_idx,
            doc->catalog_items[doc->catalog_count - 1].chapter_idx);

    while (pass++ < max_passes) {
        int added_total = 0;
        int first_idx;
        int last_idx;

        if (!doc->catalog_items || doc->catalog_count <= 0) {
            return -1;
        }
        first_idx = doc->catalog_items[0].chapter_idx;
        last_idx = doc->catalog_items[doc->catalog_count - 1].chapter_idx;
        if (first_idx <= 1 && last_idx >= doc->catalog_total_count) {
            break;
        }
        if (first_idx > 1) {
            int added = 0;

            if (reader_expand_catalog(ctx, doc, -1, &added) != 0) {
                return -1;
            }
            added_total += added;
        }
        if (doc->catalog_items && doc->catalog_count > 0 &&
            doc->catalog_items[doc->catalog_count - 1].chapter_idx < doc->catalog_total_count) {
            int added = 0;

            if (reader_expand_catalog(ctx, doc, 1, &added) != 0) {
                return -1;
            }
            added_total += added;
        }
        if (!doc->catalog_items || doc->catalog_count <= 0) {
            return -1;
        }
        fprintf(stderr,
                "reader-catalog-hydrate: pass=%d count=%d total=%d first=%d last=%d added=%d\n",
                pass,
                doc->catalog_count,
                doc->catalog_total_count,
                doc->catalog_items[0].chapter_idx,
                doc->catalog_items[doc->catalog_count - 1].chapter_idx,
                added_total);
        if (added_total <= 0) {
            break;
        }
    }
    return 0;
}

/* ====================== Catalog Focus ====================== */

static int reader_catalog_needs_focus_window(const ReaderDocument *doc) {
    if (!doc || !doc->catalog_items || doc->catalog_count <= 0 || doc->chapter_idx <= 0) {
        return 0;
    }
    if (doc->chapter_idx < doc->catalog_items[0].chapter_idx ||
        doc->chapter_idx > doc->catalog_items[doc->catalog_count - 1].chapter_idx) {
        return 1;
    }
    for (int i = 0; i + 1 < doc->catalog_count; i++) {
        if (doc->catalog_items[i + 1].chapter_idx - doc->catalog_items[i].chapter_idx > 3) {
            return 1;
        }
    }
    return 0;
}

static int reader_focus_catalog_window(ApiContext *ctx, ReaderDocument *doc) {
    ReaderCatalogItem *chunk = NULL;
    int chunk_count = 0;
    int first_idx = 0;
    int last_idx = 0;
    int range_start;

    if (!ctx || !doc || !doc->book_id || !doc->book_id[0] || doc->chapter_idx <= 0) {
        return -1;
    }

    range_start = doc->chapter_idx > 20 ? doc->chapter_idx - 20 : 0;
    if (reader_fetch_catalog_chunk(ctx, doc->book_id, 2, range_start, range_start,
                                   doc->chapter_uid, &chunk, &chunk_count,
                                   &first_idx, &last_idx) != 0 || chunk_count <= 0) {
        return -1;
    }

    reader_catalog_items_free(doc->catalog_items, doc->catalog_count);
    doc->catalog_items = chunk;
    doc->catalog_count = chunk_count;
    qsort(doc->catalog_items, (size_t)doc->catalog_count, sizeof(*doc->catalog_items),
          catalog_item_cmp_chapter_idx);
    return 0;
}

int reader_focus_catalog(ApiContext *ctx, ReaderDocument *doc) {
    if (!doc) {
        return -1;
    }
    if (!reader_catalog_needs_focus_window(doc)) {
        return 0;
    }
    return reader_focus_catalog_window(ctx, doc);
}
