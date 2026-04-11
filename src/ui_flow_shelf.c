/*
 * ui_flow_shelf.c - Shelf flow ownership for cover loading and open handoff
 */
#include "ui_internal.h"

#if HAVE_SDL

#include <string.h>
#include "shelf_service.h"

static int ui_shelf_flow_cover_download_thread(void *userdata) {
    ShelfCoverDownloadState *state = (ShelfCoverDownloadState *)userdata;

    if (!state) {
        return -1;
    }
    if (shelf_service_download_cover_to_cache(state->data_dir, state->ca_file,
                                              state->cover_url, state->cache_path) == 0) {
        state->ready = 1;
    } else {
        state->failed = 1;
    }
    state->running = 0;
    return state->ready ? 0 : -1;
}

void ui_shelf_flow_cover_download_state_reset(ShelfCoverDownloadState *state) {
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->entry_index = -1;
}

void ui_shelf_flow_cover_download_maybe_start(ApiContext *ctx, ShelfCoverCache *cache,
                                              ShelfCoverDownloadState *state,
                                              SDL_Thread **thread_handle, int selected) {
    int min_index;
    int max_index;

    if (!ctx || !cache || !cache->entries || cache->count <= 0 || !state || !thread_handle ||
        *thread_handle || state->running) {
        return;
    }

    min_index = selected - UI_SHELF_COVER_DOWNLOAD_RADIUS;
    max_index = selected + UI_SHELF_COVER_DOWNLOAD_RADIUS;
    if (min_index < 0) {
        min_index = 0;
    }
    if (max_index >= cache->count) {
        max_index = cache->count - 1;
    }

    for (int distance = 0; distance <= UI_SHELF_COVER_DOWNLOAD_RADIUS; distance++) {
        int candidates[2] = { selected + distance, selected - distance };

        for (int pass = 0; pass < (distance == 0 ? 1 : 2); pass++) {
            int index = candidates[pass];
            ShelfCoverEntry *entry;

            if (index < min_index || index > max_index) {
                continue;
            }
            entry = &cache->entries[index];
            if (!entry->cover_url || !entry->cover_url[0] ||
                !entry->cache_path[0] || entry->texture ||
                entry->attempted || entry->download_failed) {
                continue;
            }

            ui_shelf_flow_cover_download_state_reset(state);
            snprintf(state->data_dir, sizeof(state->data_dir), "%s", ctx->data_dir);
            snprintf(state->ca_file, sizeof(state->ca_file), "%s", ctx->ca_file);
            snprintf(state->cover_url, sizeof(state->cover_url), "%s", entry->cover_url);
            snprintf(state->cache_path, sizeof(state->cache_path), "%s", entry->cache_path);
            state->entry_index = index;
            state->running = 1;
            entry->attempted = 1;
            *thread_handle = SDL_CreateThread(ui_shelf_flow_cover_download_thread,
                                              "weread-cover-download", state);
            if (!*thread_handle) {
                state->running = 0;
                state->failed = 1;
                entry->attempted = 0;
                entry->download_failed = 1;
            }
            return;
        }
    }
}

void ui_shelf_flow_cover_download_poll(ShelfCoverCache *cache,
                                       ShelfCoverDownloadState *state,
                                       SDL_Thread **thread_handle) {
    ShelfCoverEntry *entry = NULL;

    if (!cache || !state || !thread_handle || !*thread_handle || state->running) {
        return;
    }

    SDL_WaitThread(*thread_handle, NULL);
    *thread_handle = NULL;

    if (state->entry_index >= 0 && state->entry_index < cache->count) {
        entry = &cache->entries[state->entry_index];
    }
    if (entry) {
        entry->attempted = 0;
        entry->download_failed = state->ready ? 0 : 1;
    }

    ui_shelf_flow_cover_download_state_reset(state);
}

void ui_shelf_flow_cover_download_stop(ShelfCoverDownloadState *state,
                                       SDL_Thread **thread_handle) {
    if (!state || !thread_handle) {
        return;
    }
    if (*thread_handle) {
        SDL_WaitThread(*thread_handle, NULL);
        *thread_handle = NULL;
    }
    ui_shelf_flow_cover_download_state_reset(state);
}

int ui_shelf_flow_prepare_resume(ApiContext *ctx, char *target, size_t target_size,
                                 int *font_size, char *loading_title,
                                 size_t loading_title_size, char *status,
                                 size_t status_size) {
    return shelf_service_prepare_resume(ctx, target, target_size, font_size,
                                        loading_title, loading_title_size,
                                        status, status_size);
}

int ui_shelf_flow_prepare_selected_open(cJSON *shelf_nuxt, int selected,
                                        char *target, size_t target_size,
                                        char *book_id, size_t book_id_size,
                                        char *loading_title, size_t loading_title_size,
                                        char *status, size_t status_size,
                                        char *shelf_status,
                                        size_t shelf_status_size) {
    return shelf_service_prepare_selected_open(shelf_nuxt, selected, target, target_size,
                                               book_id, book_id_size,
                                               loading_title, loading_title_size,
                                               status, status_size,
                                               shelf_status, shelf_status_size);
}

int ui_shelf_flow_prepare_article_open(ApiContext *ctx, cJSON *shelf_nuxt, int font_size,
                                       char *target, size_t target_size,
                                       char *loading_title, size_t loading_title_size,
                                       char *status, size_t status_size,
                                       char *shelf_status, size_t shelf_status_size) {
    return shelf_service_prepare_article_open(ctx, shelf_nuxt, font_size,
                                              target, target_size,
                                              loading_title, loading_title_size,
                                              status, status_size,
                                              shelf_status, shelf_status_size);
}

#endif
