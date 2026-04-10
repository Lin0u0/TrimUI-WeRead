#include <stdio.h>
#include <string.h>
#include "json.h"
#include "reader_state.h"
#include "state.h"

int reader_state_save_last_reader(ApiContext *ctx, const char *target, int font_size,
                                  int content_font_size) {
    cJSON *json;
    int rc;

    if (!target) {
        return -1;
    }

    json = cJSON_CreateObject();
    if (!json) {
        return -1;
    }
    cJSON_AddStringToObject(json, "target", target);
    cJSON_AddNumberToObject(json, "fontSize", font_size);
    cJSON_AddNumberToObject(json, "contentFontSize", content_font_size);
    rc = state_write_json(ctx, STATE_FILE_LAST_READER, json);
    cJSON_Delete(json);
    return rc;
}

int reader_state_load_last_reader(ApiContext *ctx, char *target, size_t target_size,
                                  int *font_size, int *content_font_size) {
    cJSON *json = state_read_json(ctx, STATE_FILE_LAST_READER);
    const char *saved_target;

    if (!json) {
        return -1;
    }

    saved_target = json_get_string(json, "target");
    if (!saved_target) {
        cJSON_Delete(json);
        return -1;
    }

    snprintf(target, target_size, "%s", saved_target);
    if (font_size) {
        *font_size = json_get_int(json, "fontSize", 3);
    }
    if (content_font_size) {
        *content_font_size = json_get_int(json, "contentFontSize", 36);
    }
    cJSON_Delete(json);
    return 0;
}

int reader_state_save_position(ApiContext *ctx, const char *book_id, const char *source_target,
                               const char *target, int font_size, int content_font_size,
                               int current_page, int current_offset) {
    cJSON *positions;
    cJSON *entry;
    int rc;

    if (!ctx || !book_id || !*book_id || !source_target || !*source_target || !target || !*target) {
        return -1;
    }

    positions = state_read_json(ctx, STATE_FILE_READER_POSITIONS);
    if (!positions || !cJSON_IsObject(positions)) {
        cJSON_Delete(positions);
        positions = cJSON_CreateObject();
        if (!positions) {
            return -1;
        }
    }

    entry = cJSON_CreateObject();
    if (!entry) {
        cJSON_Delete(positions);
        return -1;
    }
    cJSON_AddStringToObject(entry, "sourceTarget", source_target);
    cJSON_AddStringToObject(entry, "target", target);
    cJSON_AddNumberToObject(entry, "fontSize", font_size);
    cJSON_AddNumberToObject(entry, "contentFontSize", content_font_size);
    cJSON_AddNumberToObject(entry, "currentPage", current_page);
    cJSON_AddNumberToObject(entry, "currentOffset", current_offset);

    if (cJSON_GetObjectItemCaseSensitive(positions, book_id)) {
        cJSON_ReplaceItemInObjectCaseSensitive(positions, book_id, entry);
    } else {
        cJSON_AddItemToObject(positions, book_id, entry);
    }
    rc = state_write_json(ctx, STATE_FILE_READER_POSITIONS, positions);
    cJSON_Delete(positions);
    return rc;
}

int reader_state_load_position(ApiContext *ctx, const char *book_id, const char *source_target,
                               char *target, size_t target_size, int *font_size,
                               int *content_font_size, int *current_page, int *current_offset) {
    cJSON *positions;
    cJSON *entry;
    const char *saved_source_target;
    const char *saved_target;

    if (!ctx || !book_id || !*book_id || !source_target || !*source_target || !target || target_size == 0) {
        return -1;
    }

    positions = state_read_json(ctx, STATE_FILE_READER_POSITIONS);
    if (!positions || !cJSON_IsObject(positions)) {
        cJSON_Delete(positions);
        return -1;
    }

    entry = cJSON_GetObjectItemCaseSensitive(positions, book_id);
    if (!entry || !cJSON_IsObject(entry)) {
        cJSON_Delete(positions);
        return -1;
    }

    saved_source_target = json_get_string(entry, "sourceTarget");
    if (!saved_source_target || strcmp(saved_source_target, source_target) != 0) {
        cJSON_Delete(positions);
        return -1;
    }

    saved_target = json_get_string(entry, "target");
    if (!saved_target || !*saved_target) {
        cJSON_Delete(positions);
        return -1;
    }

    snprintf(target, target_size, "%s", saved_target);
    if (font_size) {
        *font_size = json_get_int(entry, "fontSize", 3);
    }
    if (content_font_size) {
        *content_font_size = json_get_int(entry, "contentFontSize", 36);
    }
    if (current_page) {
        *current_page = json_get_int(entry, "currentPage", 0);
    }
    if (current_offset) {
        *current_offset = json_get_int(entry, "currentOffset", 0);
    }
    cJSON_Delete(positions);
    return 0;
}

int reader_state_load_position_by_book_id(ApiContext *ctx, const char *book_id,
                                          char *source_target, size_t source_target_size,
                                          char *target, size_t target_size, int *font_size,
                                          int *content_font_size, int *current_page,
                                          int *current_offset) {
    cJSON *positions;
    cJSON *entry;
    const char *saved_source_target;
    const char *saved_target;

    if (!ctx || !book_id || !*book_id || !target || target_size == 0) {
        return -1;
    }

    positions = state_read_json(ctx, STATE_FILE_READER_POSITIONS);
    if (!positions || !cJSON_IsObject(positions)) {
        cJSON_Delete(positions);
        return -1;
    }

    entry = cJSON_GetObjectItemCaseSensitive(positions, book_id);
    if (!entry || !cJSON_IsObject(entry)) {
        cJSON_Delete(positions);
        return -1;
    }

    saved_target = json_get_string(entry, "target");
    if (!saved_target || !*saved_target) {
        cJSON_Delete(positions);
        return -1;
    }

    snprintf(target, target_size, "%s", saved_target);
    saved_source_target = json_get_string(entry, "sourceTarget");
    if (source_target && source_target_size > 0 && saved_source_target) {
        snprintf(source_target, source_target_size, "%s", saved_source_target);
    }
    if (font_size) {
        *font_size = json_get_int(entry, "fontSize", 3);
    }
    if (content_font_size) {
        *content_font_size = json_get_int(entry, "contentFontSize", 36);
    }
    if (current_page) {
        *current_page = json_get_int(entry, "currentPage", 0);
    }
    if (current_offset) {
        *current_offset = json_get_int(entry, "currentOffset", 0);
    }
    cJSON_Delete(positions);
    return 0;
}
