#ifndef SHELF_H
#define SHELF_H

#include "api.h"

int shelf_print(ApiContext *ctx);
int shelf_print_cached(ApiContext *ctx);
cJSON *shelf_load(ApiContext *ctx, int allow_cache, int *from_cache);
cJSON *shelf_books(cJSON *nuxt);
cJSON *shelf_reader_urls(cJSON *nuxt);
const char *shelf_reader_target(cJSON *urls, int index);

#endif
