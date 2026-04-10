#include "shelf_state.h"
#include "state.h"

cJSON *shelf_state_load_cache(ApiContext *ctx) {
    return state_read_json(ctx, STATE_FILE_SHELF);
}

int shelf_state_save_cache(ApiContext *ctx, cJSON *shelf_nuxt) {
    return state_write_json(ctx, STATE_FILE_SHELF, shelf_nuxt);
}
