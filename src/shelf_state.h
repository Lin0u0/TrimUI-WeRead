#ifndef SHELF_STATE_H
#define SHELF_STATE_H

#include "api.h"

/*
 * Shelf cached-state ownership lives here. Callers should prefer this boundary
 * instead of addressing shelf.json through the generic state layer directly.
 */
cJSON *shelf_state_load_cache(ApiContext *ctx);
int shelf_state_save_cache(ApiContext *ctx, cJSON *shelf_nuxt);

#endif
