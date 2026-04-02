#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "state.h"
#include "json.h"

static char *state_path(ApiContext *ctx, const char *name) {
    char *path;
    size_t len;

    if (!ctx || !name) {
        return NULL;
    }
    len = strlen(ctx->state_dir) + strlen(name) + 2;
    path = malloc(len);
    if (!path) {
        return NULL;
    }
    snprintf(path, len, "%s/%s", ctx->state_dir, name);
    return path;
}

int state_write_json(ApiContext *ctx, const char *name, cJSON *json) {
    char *path = state_path(ctx, name);
    char *text;
    FILE *fp;
    int rc = -1;

    if (!path || !json) {
        free(path);
        return -1;
    }

    text = cJSON_PrintUnformatted(json);
    if (!text) {
        free(path);
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp) {
        size_t len = strlen(text);
        rc = fwrite(text, 1, len, fp) == len ? 0 : -1;
        fclose(fp);
    }

    free(text);
    free(path);
    return rc;
}

cJSON *state_read_json(ApiContext *ctx, const char *name) {
    char *path = state_path(ctx, name);
    FILE *fp;
    long size;
    char *buf;
    cJSON *json = NULL;

    if (!path) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        free(path);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        free(path);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        free(path);
        return NULL;
    }

    buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        free(path);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, fp) == (size_t)size) {
        buf[size] = '\0';
        json = cJSON_Parse(buf);
    }

    free(buf);
    fclose(fp);
    free(path);
    return json;
}

int state_save_last_reader(ApiContext *ctx, const char *target, int font_size, int content_font_size) {
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
    rc = state_write_json(ctx, "last-reader.json", json);
    cJSON_Delete(json);
    return rc;
}

int state_load_last_reader(ApiContext *ctx, char *target, size_t target_size, int *font_size,
                           int *content_font_size) {
    cJSON *json = state_read_json(ctx, "last-reader.json");
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

int state_save_reader_position(ApiContext *ctx, const char *book_id, const char *source_target,
                               const char *target, int font_size, int content_font_size,
                               int current_page, int current_offset) {
    cJSON *positions;
    cJSON *entry;
    int rc;

    if (!ctx || !book_id || !*book_id || !source_target || !*source_target || !target || !*target) {
        return -1;
    }

    positions = state_read_json(ctx, "reader-positions.json");
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
    rc = state_write_json(ctx, "reader-positions.json", positions);
    cJSON_Delete(positions);
    return rc;
}

int state_load_reader_position(ApiContext *ctx, const char *book_id, const char *source_target,
                               char *target, size_t target_size, int *font_size,
                               int *content_font_size, int *current_page, int *current_offset) {
    cJSON *positions;
    cJSON *entry;
    const char *saved_source_target;
    const char *saved_target;

    if (!ctx || !book_id || !*book_id || !source_target || !*source_target || !target || target_size == 0) {
        return -1;
    }

    positions = state_read_json(ctx, "reader-positions.json");
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

int state_load_reader_position_by_book_id(ApiContext *ctx, const char *book_id,
                                          char *source_target, size_t source_target_size,
                                          char *target, size_t target_size,
                                          int *font_size, int *content_font_size,
                                          int *current_page, int *current_offset) {
    cJSON *positions;
    cJSON *entry;
    const char *saved_source_target;
    const char *saved_target;

    if (!ctx || !book_id || !*book_id || !target || target_size == 0) {
        return -1;
    }

    positions = state_read_json(ctx, "reader-positions.json");
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

static int state_save_int_preference(ApiContext *ctx, const char *key, int value) {
    cJSON *json = state_read_json(ctx, "preferences.json");
    int rc;

    if (!json || !cJSON_IsObject(json)) {
        cJSON_Delete(json);
        json = cJSON_CreateObject();
        if (!json) {
            return -1;
        }
    }
    if (cJSON_GetObjectItemCaseSensitive(json, key)) {
        cJSON_ReplaceItemInObjectCaseSensitive(json, key, cJSON_CreateNumber(value));
    } else {
        cJSON_AddNumberToObject(json, key, value);
    }
    rc = state_write_json(ctx, "preferences.json", json);
    cJSON_Delete(json);
    return rc;
}

int state_save_dark_mode(ApiContext *ctx, int dark_mode) {
    return state_save_int_preference(ctx, "darkMode", dark_mode ? 1 : 0);
}

int state_load_dark_mode(ApiContext *ctx) {
    cJSON *json = state_read_json(ctx, "preferences.json");
    int value;

    if (!json) {
        return 0;
    }
    value = json_get_int(json, "darkMode", 0);
    cJSON_Delete(json);
    return value;
}

int state_save_brightness_level(ApiContext *ctx, int brightness_level) {
    return state_save_int_preference(ctx, "brightnessLevel", brightness_level);
}

int state_load_brightness_level(ApiContext *ctx, int *brightness_level) {
    cJSON *json = state_read_json(ctx, "preferences.json");

    if (!brightness_level) {
        return -1;
    }
    if (!json) {
        return -1;
    }

    if (!cJSON_HasObjectItem(json, "brightnessLevel")) {
        cJSON_Delete(json);
        return -1;
    }

    *brightness_level = json_get_int(json, "brightnessLevel", 7);
    cJSON_Delete(json);
    return 0;
}
