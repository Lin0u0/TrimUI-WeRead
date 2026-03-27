#ifndef JSON_H
#define JSON_H

#include "cJSON.h"

cJSON *json_get_path(cJSON *root, const char *path);
const char *json_get_string(cJSON *root, const char *path);
int json_get_int(cJSON *root, const char *path, int fallback);
int json_is_truthy(cJSON *item);

#endif
