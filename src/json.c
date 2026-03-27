#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"

cJSON *json_get_path(cJSON *root, const char *path) {
    char *copy;
    char *cursor;
    char *token;
    cJSON *node;

    if (!root || !path || !*path) {
        return root;
    }

    copy = strdup(path);
    if (!copy) {
        return NULL;
    }

    node = root;
    cursor = copy;
    while ((token = strsep(&cursor, ".")) != NULL) {
        char *bracket = strchr(token, '[');
        if (bracket) {
            int index;
            *bracket = '\0';
            if (*token) {
                node = cJSON_GetObjectItemCaseSensitive(node, token);
            }
            if (!node || !cJSON_IsArray(node)) {
                free(copy);
                return NULL;
            }
            index = atoi(bracket + 1);
            node = cJSON_GetArrayItem(node, index);
        } else {
            node = cJSON_GetObjectItemCaseSensitive(node, token);
        }

        if (!node) {
            free(copy);
            return NULL;
        }
    }

    free(copy);
    return node;
}

const char *json_get_string(cJSON *root, const char *path) {
    cJSON *node = json_get_path(root, path);
    return (node && cJSON_IsString(node) && node->valuestring) ? node->valuestring : NULL;
}

int json_get_int(cJSON *root, const char *path, int fallback) {
    cJSON *node = json_get_path(root, path);
    if (!node || !cJSON_IsNumber(node)) {
        return fallback;
    }
    return node->valueint;
}

int json_is_truthy(cJSON *item) {
    if (!item) {
        return 0;
    }
    if (cJSON_IsTrue(item)) {
        return 1;
    }
    if (cJSON_IsNumber(item)) {
        return item->valueint != 0;
    }
    if (cJSON_IsString(item) && item->valuestring) {
        return item->valuestring[0] != '\0';
    }
    return !cJSON_IsFalse(item) && !cJSON_IsNull(item);
}
