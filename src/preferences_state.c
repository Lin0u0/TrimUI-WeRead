#include "json.h"
#include "preferences_state.h"
#include "state.h"

static int preferences_state_save_int(ApiContext *ctx, const char *key, int value) {
    cJSON *json = state_read_json(ctx, STATE_FILE_PREFERENCES);
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
    rc = state_write_json(ctx, STATE_FILE_PREFERENCES, json);
    cJSON_Delete(json);
    return rc;
}

int preferences_state_save_dark_mode(ApiContext *ctx, int dark_mode) {
    return preferences_state_save_int(ctx, "darkMode", dark_mode ? 1 : 0);
}

int preferences_state_load_dark_mode(ApiContext *ctx) {
    cJSON *json = state_read_json(ctx, STATE_FILE_PREFERENCES);
    int value;

    if (!json) {
        return 0;
    }
    value = json_get_int(json, "darkMode", 0);
    cJSON_Delete(json);
    return value;
}

int preferences_state_save_brightness_level(ApiContext *ctx, int brightness_level) {
    return preferences_state_save_int(ctx, "brightnessLevel", brightness_level);
}

int preferences_state_load_brightness_level(ApiContext *ctx, int *brightness_level) {
    cJSON *json = state_read_json(ctx, STATE_FILE_PREFERENCES);

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
