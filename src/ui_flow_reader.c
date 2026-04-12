/*
 * ui_flow_reader.c - Reader flow ownership for open, prefetch, and progress
 */
#include "ui_internal.h"

#if HAVE_SDL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reader_service.h"

static int ui_reader_flow_progress_report_thread(void *userdata) {
    ProgressReportState *state = (ProgressReportState *)userdata;

    state->result = reader_service_report_progress_with_retry(state->data_dir,
                                                              state->ca_file,
                                                              &state->doc,
                                                              state->current_page,
                                                              state->total_pages,
                                                              state->chapter_offset,
                                                              state->reading_seconds,
                                                              state->page_summary,
                                                              state->compute_progress);
    state->running = 0;
    return state->result;
}

static int ui_reader_flow_open_thread(void *userdata) {
    ReaderOpenState *state = (ReaderOpenState *)userdata;
    ApiContext ctx;
    ReaderOpenResult result;

    if (!state) {
        return -1;
    }
    if (api_init(&ctx, state->data_dir) != 0) {
        fprintf(stderr, "reader-open-thread: api_init failed source=%s\n",
                state->source_target);
        state->failed = 1;
        state->running = 0;
        return -1;
    }
    snprintf(ctx.ca_file, sizeof(ctx.ca_file), "%s", state->ca_file);

    memset(&result, 0, sizeof(result));
    if (reader_service_prepare_open_document(&ctx,
                                             state->source_target,
                                             state->book_id[0] ? state->book_id : NULL,
                                             state->font_size,
                                             &result) == 0) {
        state->doc = result.doc;
        memset(&result.doc, 0, sizeof(result.doc));
        ui_copy_string(state->source_target, sizeof(state->source_target),
                       result.source_target);
        state->content_font_size = result.content_font_size;
        state->initial_page = result.initial_page;
        state->initial_offset = result.initial_offset;
        state->honor_saved_position = result.honor_saved_position;
        state->ready = 1;
        fprintf(stderr,
                "reader-open-thread: ready source=%s finalSource=%s docTarget=%s kind=%s bookId=%s initialPage=%d initialOffset=%d honorSaved=%d\n",
                state->source_target,
                result.source_target,
                state->doc.target ? state->doc.target : "(null)",
                state->doc.kind == READER_DOCUMENT_KIND_ARTICLE ? "article" : "book",
                state->doc.book_id ? state->doc.book_id : "(null)",
                state->initial_page,
                state->initial_offset,
                state->honor_saved_position);
    } else {
        fprintf(stderr,
                "reader-open-thread: failed source=%s bookId=%s\n",
                state->source_target,
                state->book_id[0] ? state->book_id : "(null)");
        state->failed = 1;
    }

    state->poor_network = ctx.poor_network;
    api_cleanup(&ctx);
    state->running = 0;
    return state->ready ? 0 : -1;
}

static int ui_reader_flow_prefetch_thread(void *userdata) {
    ChapterPrefetchState *state = (ChapterPrefetchState *)userdata;
    ApiContext ctx;

    if (!state) {
        return -1;
    }
    if (api_init(&ctx, state->data_dir) != 0) {
        state->failed = 1;
        state->running = 0;
        return -1;
    }
    snprintf(ctx.ca_file, sizeof(ctx.ca_file), "%s", state->ca_file);

    if (reader_prefetch(&ctx, state->target, state->font_size, &state->doc) == 0) {
        state->ready = 1;
    } else {
        state->failed = 1;
    }

    api_cleanup(&ctx);
    state->running = 0;
    return state->ready ? 0 : -1;
}

static int ui_reader_flow_catalog_hydration_thread(void *userdata) {
    CatalogHydrationState *state = (CatalogHydrationState *)userdata;
    ApiContext ctx;
    int type;

    if (!state) {
        return -1;
    }
    if (api_init(&ctx, state->data_dir) != 0) {
        state->failed = 1;
        state->running = 0;
        return -1;
    }
    snprintf(ctx.ca_file, sizeof(ctx.ca_file), "%s", state->ca_file);

    type = state->direction < 0 ? 1 : 2;
    if (reader_fetch_catalog_chunk(&ctx, state->book_id, type,
                                   state->range_start, state->range_end,
                                   state->chapter_uid[0] ? state->chapter_uid : NULL,
                                   &state->items, &state->item_count,
                                   NULL, NULL) == 0) {
        state->ready = 1;
    } else {
        state->failed = 1;
    }

    api_cleanup(&ctx);
    state->running = 0;
    return state->ready ? 0 : -1;
}

static void ui_reader_flow_build_page_summary(ReaderViewState *state, char *out,
                                              size_t out_size) {
    int total_pages;
    int start_line;
    int end_line;
    size_t len = 0;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!state || !state->lines || state->line_count <= 0) {
        return;
    }

    total_pages = ui_reader_view_total_pages(state);
    start_line = state->current_page * state->lines_per_page;
    end_line = start_line + state->lines_per_page;
    if (end_line > state->line_count) {
        end_line = state->line_count;
    }

    for (int i = start_line; i < end_line && len + 1 < out_size; i++) {
        const char *p = state->lines[i];
        while (p && *p && len + 1 < out_size) {
            int ch_len = utf8_char_len((unsigned char)*p);
            if (len + (size_t)ch_len >= out_size) {
                break;
            }
            memcpy(out + len, p, (size_t)ch_len);
            len += (size_t)ch_len;
            p += ch_len;
            if (len >= 20) {
                break;
            }
        }
        if (len >= 20 || state->current_page + 1 < total_pages) {
            break;
        }
    }
    out[len] = '\0';
}

static int ui_reader_flow_progress_finalize(ReaderViewState *state,
                                            ProgressReportState *report_state,
                                            SDL_Thread **report_thread) {
    Uint32 now;
    int result;

    if (!state || !report_state || !report_thread || !*report_thread ||
        report_state->running) {
        return 0;
    }

    SDL_WaitThread(*report_thread, NULL);
    *report_thread = NULL;
    result = report_state->result;
    now = SDL_GetTicks();
    if (result == READER_REPORT_OK) {
        state->progress_start_tick = now;
        if (!state->progress_paused) {
            state->progress_report_due_tick = now + UI_PROGRESS_REPORT_INTERVAL_MS;
        }
    } else if (!state->progress_paused) {
        state->progress_report_due_tick = now + UI_PROGRESS_REPORT_INTERVAL_MS;
    }
    ui_reader_flow_progress_report_state_reset(report_state);
    return result;
}

static void ui_reader_flow_queue_report(ApiContext *ctx, ReaderViewState *state,
                                        ProgressReportState *report_state,
                                        SDL_Thread **report_thread, int reading_seconds,
                                        int compute_progress) {
    if (!ctx || !state || !report_state || !report_thread ||
        !state->doc.book_id || !state->doc.token || !state->doc.chapter_uid) {
        return;
    }

    ui_reader_flow_progress_finalize(state, report_state, report_thread);
    if (*report_thread || report_state->running) {
        return;
    }

    memset(report_state, 0, sizeof(*report_state));
    snprintf(report_state->data_dir, sizeof(report_state->data_dir), "%s", ctx->data_dir);
    snprintf(report_state->ca_file, sizeof(report_state->ca_file), "%s", ctx->ca_file);
    if (reader_service_copy_report_document(&report_state->doc, &state->doc) != 0) {
        ui_reader_flow_progress_report_state_reset(report_state);
        return;
    }
    report_state->current_page = state->current_page;
    report_state->total_pages = ui_reader_view_total_pages(state);
    report_state->chapter_offset = ui_reader_view_current_page_offset(state);
    report_state->reading_seconds = reading_seconds > 0 ? reading_seconds : 0;
    report_state->compute_progress = compute_progress;
    ui_reader_flow_build_page_summary(state, report_state->page_summary,
                                      sizeof(report_state->page_summary));
    report_state->running = 1;
    *report_thread = SDL_CreateThread(ui_reader_flow_progress_report_thread,
                                      "weread-progress-report", report_state);
    if (!*report_thread) {
        ui_reader_flow_progress_report_state_reset(report_state);
    }
}

static void ui_reader_flow_prefetch_maybe_start(ApiContext *ctx, ChapterPrefetchState *state,
                                                SDL_Thread **thread_handle,
                                                const char *target, int font_size) {
    if (!ctx || !state || !thread_handle || !target || !target[0]) {
        return;
    }
    if (state->running) {
        return;
    }
    if (state->ready && strcmp(state->target, target) == 0 && state->font_size == font_size) {
        return;
    }
    if (strlen(target) >= sizeof(state->target)) {
        return;
    }
    if (*thread_handle) {
        SDL_WaitThread(*thread_handle, NULL);
        *thread_handle = NULL;
    }
    reader_document_free(&state->doc);
    memset(state, 0, sizeof(*state));
    snprintf(state->data_dir, sizeof(state->data_dir), "%s", ctx->data_dir);
    snprintf(state->ca_file, sizeof(state->ca_file), "%s", ctx->ca_file);
    snprintf(state->target, sizeof(state->target), "%s", target);
    state->font_size = font_size;
    state->running = 1;
    *thread_handle = SDL_CreateThread(ui_reader_flow_prefetch_thread,
                                      "weread-prefetch", state);
    if (!*thread_handle) {
        state->running = 0;
        state->failed = 1;
    }
}

static void ui_reader_flow_prefetch_poll(ChapterPrefetchState *state,
                                         SDL_Thread **thread_handle) {
    if (!state || !thread_handle || !*thread_handle || state->running) {
        return;
    }
    SDL_WaitThread(*thread_handle, NULL);
    *thread_handle = NULL;
    if (state->failed) {
        reader_document_free(&state->doc);
        memset(state, 0, sizeof(*state));
    }
}

static void ui_reader_flow_prefetch_slot_reset(ChapterPrefetchSlot *slot) {
    if (!slot) {
        return;
    }
    if (slot->thread) {
        SDL_WaitThread(slot->thread, NULL);
        slot->thread = NULL;
    }
    reader_document_free(&slot->state.doc);
    memset(&slot->state, 0, sizeof(slot->state));
}

static int ui_reader_flow_target_in_list(const char *target, char targets[][2048],
                                         int target_count) {
    if (!target || !target[0]) {
        return 0;
    }
    for (int i = 0; i < target_count; i++) {
        if (strcmp(targets[i], target) == 0) {
            return 1;
        }
    }
    return 0;
}

static void ui_reader_flow_prefetch_request(ApiContext *ctx, ChapterPrefetchCache *cache,
                                            const char *target, int font_size) {
    ChapterPrefetchSlot *free_slot = NULL;
    int running_count = 0;

    if (!ctx || !cache || !target || !target[0]) {
        return;
    }

    for (int i = 0; i < (int)(sizeof(cache->slots) / sizeof(cache->slots[0])); i++) {
        ChapterPrefetchSlot *slot = &cache->slots[i];
        if ((slot->state.running || slot->state.ready) &&
            strcmp(slot->state.target, target) == 0 &&
            slot->state.font_size == font_size) {
            return;
        }
        if (slot->state.running || slot->thread) {
            running_count++;
        }
        if (!free_slot && !slot->state.running && !slot->thread && !slot->state.ready) {
            free_slot = slot;
        }
    }

    if (running_count >= UI_CHAPTER_PREFETCH_MAX_RUNNING) {
        return;
    }

    if (!free_slot) {
        for (int i = 0; i < (int)(sizeof(cache->slots) / sizeof(cache->slots[0])); i++) {
            ChapterPrefetchSlot *slot = &cache->slots[i];
            if (!slot->state.running && !slot->thread) {
                reader_document_free(&slot->state.doc);
                memset(&slot->state, 0, sizeof(slot->state));
                free_slot = slot;
                break;
            }
        }
    }

    if (!free_slot) {
        return;
    }

    ui_reader_flow_prefetch_maybe_start(ctx, &free_slot->state, &free_slot->thread,
                                        target, font_size);
}

void ui_reader_flow_progress_report_state_reset(ProgressReportState *state) {
    if (!state) {
        return;
    }
    reader_document_free(&state->doc);
    memset(state, 0, sizeof(*state));
}

void ui_reader_flow_reader_open_state_reset(ReaderOpenState *state) {
    if (!state) {
        return;
    }
    reader_document_free(&state->doc);
    memset(state, 0, sizeof(*state));
}

void ui_reader_flow_catalog_hydration_state_reset(CatalogHydrationState *state) {
    int last_direction = 0;

    if (!state) {
        return;
    }
    last_direction = state->last_requested_direction;
    reader_catalog_items_free(state->items, state->item_count);
    memset(state, 0, sizeof(*state));
    state->last_requested_direction = last_direction;
}

void ui_reader_flow_chapter_prefetch_cache_reset(ChapterPrefetchCache *cache) {
    if (!cache) {
        return;
    }
    for (int i = 0; i < (int)(sizeof(cache->slots) / sizeof(cache->slots[0])); i++) {
        ui_reader_flow_prefetch_slot_reset(&cache->slots[i]);
    }
    cache->last_update_index = -1;
}

int ui_reader_flow_chapter_prefetch_has_running_work(const ChapterPrefetchCache *cache) {
    if (!cache) {
        return 0;
    }
    for (int i = 0; i < (int)(sizeof(cache->slots) / sizeof(cache->slots[0])); i++) {
        if (cache->slots[i].thread || cache->slots[i].state.running) {
            return 1;
        }
    }
    return 0;
}

int ui_reader_flow_catalog_hydration_has_running_work(const CatalogHydrationState *state,
                                                     SDL_Thread *thread_handle) {
    if (!state) {
        return thread_handle != NULL;
    }
    return thread_handle != NULL || state->running;
}

int ui_reader_flow_chapter_prefetch_cache_adopt(ChapterPrefetchCache *cache,
                                                const char *target, TTF_Font *body_font,
                                                ReaderViewState *reader_state,
                                                const UiLayout *current_layout) {
    if (!cache || !target || !target[0] || !body_font || !reader_state || !current_layout) {
        return -1;
    }
    for (int i = 0; i < (int)(sizeof(cache->slots) / sizeof(cache->slots[0])); i++) {
        ChapterPrefetchSlot *slot = &cache->slots[i];
        if (slot->state.ready &&
            strcmp(slot->state.target, target) == 0 &&
            ui_reader_view_adopt_document(body_font, &slot->state.doc,
                                          current_layout->reader_content_w,
                                          current_layout->reader_content_h, 0,
                                          reader_state) == 0) {
            ui_reader_flow_prefetch_slot_reset(slot);
            return 0;
        }
    }
    return -1;
}

static int ui_reader_flow_catalog_hydration_poll(ReaderViewState *reader_state,
                                                 CatalogHydrationState *state,
                                                 SDL_Thread **thread_handle) {
    int added_count = 0;
    int render_requested = 0;
    char selected_chapter_uid[128];
    char selected_target[2048];
    int selected_chapter_idx = 0;
    int preserve_selection = 0;
    int preserved_index = -1;

    if (!state || !thread_handle || !*thread_handle || state->running) {
        return 0;
    }

    selected_chapter_uid[0] = '\0';
    selected_target[0] = '\0';
    if (reader_state && reader_state->catalog_open &&
        reader_state->doc.catalog_items &&
        reader_state->catalog_selected >= 0 &&
        reader_state->catalog_selected < reader_state->doc.catalog_count) {
        ReaderCatalogItem *selected_item =
            &reader_state->doc.catalog_items[reader_state->catalog_selected];

        if (selected_item->chapter_uid && selected_item->chapter_uid[0]) {
            snprintf(selected_chapter_uid, sizeof(selected_chapter_uid), "%s",
                     selected_item->chapter_uid);
        }
        if (selected_item->target && selected_item->target[0]) {
            snprintf(selected_target, sizeof(selected_target), "%s",
                     selected_item->target);
        }
        selected_chapter_idx = selected_item->chapter_idx;
        preserve_selection = 1;
    }

    SDL_WaitThread(*thread_handle, NULL);
    *thread_handle = NULL;
    if (!state->failed && state->ready && reader_state &&
        reader_state->doc.kind == READER_DOCUMENT_KIND_BOOK &&
        reader_state->doc.book_id && state->book_id[0] &&
        strcmp(reader_state->doc.book_id, state->book_id) == 0 &&
        state->items && state->item_count > 0 &&
        reader_merge_catalog_chunk(&reader_state->doc, state->items,
                                   state->item_count, &added_count) == 0) {
        if (preserve_selection) {
            for (int i = 0; i < reader_state->doc.catalog_count; i++) {
                ReaderCatalogItem *item = &reader_state->doc.catalog_items[i];

                if (selected_chapter_uid[0] && item->chapter_uid &&
                    strcmp(item->chapter_uid, selected_chapter_uid) == 0) {
                    preserved_index = i;
                    break;
                }
                if (selected_target[0] && item->target &&
                    strcmp(item->target, selected_target) == 0) {
                    preserved_index = i;
                    break;
                }
                if (selected_chapter_idx > 0 && item->chapter_idx == selected_chapter_idx) {
                    preserved_index = i;
                    break;
                }
            }
        }
        if (preserved_index >= 0) {
            reader_state->catalog_selected = preserved_index;
        } else {
            int index = ui_reader_view_current_catalog_index(reader_state);
            reader_state->catalog_selected = index >= 0 ? index : 0;
        }
        render_requested = reader_state->catalog_open && added_count > 0;
        fprintf(stderr,
                "reader-catalog-bg: merged bookId=%s direction=%d added=%d count=%d total=%d\n",
                reader_state->doc.book_id,
                state->direction,
                added_count,
                reader_state->doc.catalog_count,
                reader_state->doc.catalog_total_count);
    }
    ui_reader_flow_catalog_hydration_state_reset(state);
    return render_requested;
}

static void ui_reader_flow_catalog_hydration_maybe_start(ApiContext *ctx,
                                                         ReaderViewState *reader_state,
                                                         CatalogHydrationState *state,
                                                         SDL_Thread **thread_handle) {
    int first_idx;
    int last_idx;
    int missing_before;
    int missing_after;
    int direction;

    if (!ctx || !reader_state || !state || !thread_handle || *thread_handle ||
        state->running || !reader_state->catalog_open ||
        reader_state->doc.kind != READER_DOCUMENT_KIND_BOOK ||
        !reader_state->doc.book_id || !reader_state->doc.catalog_items ||
        reader_state->doc.catalog_count <= 0 || reader_state->doc.catalog_total_count <= 0 ||
        reader_state->doc.catalog_count >= reader_state->doc.catalog_total_count) {
        return;
    }

    first_idx = reader_state->doc.catalog_items[0].chapter_idx;
    last_idx = reader_state->doc.catalog_items[reader_state->doc.catalog_count - 1].chapter_idx;
    missing_before = first_idx > 1 ? first_idx - 1 : 0;
    missing_after = last_idx < reader_state->doc.catalog_total_count ?
        (reader_state->doc.catalog_total_count - last_idx) : 0;
    if (missing_before <= 0 && missing_after <= 0) {
        return;
    }

    if (missing_before > 0 && missing_after > 0) {
        direction = missing_before >= missing_after ? -1 : 1;
        if (state->last_requested_direction == direction) {
            direction = -direction;
        }
    } else {
        direction = missing_before > 0 ? -1 : 1;
    }

    ui_reader_flow_catalog_hydration_state_reset(state);
    snprintf(state->data_dir, sizeof(state->data_dir), "%s", ctx->data_dir);
    snprintf(state->ca_file, sizeof(state->ca_file), "%s", ctx->ca_file);
    snprintf(state->book_id, sizeof(state->book_id), "%s", reader_state->doc.book_id);
    if (reader_state->doc.chapter_uid && reader_state->doc.chapter_uid[0]) {
        snprintf(state->chapter_uid, sizeof(state->chapter_uid), "%s",
                 reader_state->doc.chapter_uid);
    }
    state->direction = direction;
    state->range_start = first_idx;
    state->range_end = last_idx;
    state->last_requested_direction = direction;
    state->running = 1;
    *thread_handle = SDL_CreateThread(ui_reader_flow_catalog_hydration_thread,
                                      "weread-catalog-hydrate", state);
    if (!*thread_handle) {
        state->running = 0;
        state->failed = 1;
        fprintf(stderr,
                "reader-catalog-bg: SDL_CreateThread failed bookId=%s direction=%d range=%d-%d\n",
                state->book_id,
                state->direction,
                state->range_start,
                state->range_end);
        return;
    }

    fprintf(stderr,
            "reader-catalog-bg: begin bookId=%s direction=%d range=%d-%d count=%d total=%d\n",
            state->book_id,
            state->direction,
            state->range_start,
            state->range_end,
            reader_state->doc.catalog_count,
            reader_state->doc.catalog_total_count);
}

int ui_reader_flow_tick_reader(ApiContext *ctx, ReaderViewState *reader_state,
                               ProgressReportState *progress_report,
                               SDL_Thread **progress_report_thread_handle,
                               ChapterPrefetchCache *chapter_prefetch_cache,
                               CatalogHydrationState *catalog_hydration,
                               SDL_Thread **catalog_hydration_thread_handle) {
    Uint32 now;
    Uint32 elapsed_ms;
    int elapsed_seconds;
    char targets[UI_CHAPTER_PREFETCH_RADIUS * 2][2048];
    int target_count = 0;
    int current_index;
    int render_requested = 0;

    if (!ctx || !reader_state || !progress_report || !progress_report_thread_handle ||
        !chapter_prefetch_cache || !catalog_hydration || !catalog_hydration_thread_handle) {
        return 0;
    }

    if (reader_state->doc.book_id && reader_state->doc.token && reader_state->doc.chapter_uid) {
        now = SDL_GetTicks();
        if (!reader_state->progress_initialized) {
            reader_state->progress_initialized = 1;
            reader_state->progress_paused = 0;
            reader_state->progress_initial_report_pending = 1;
            reader_state->progress_start_tick = now;
            reader_state->progress_pause_tick = 0;
            reader_state->progress_pause_deadline_tick = now + UI_PROGRESS_PAUSE_TIMEOUT_MS;
            reader_state->progress_report_due_tick = now + UI_PROGRESS_REPORT_INTERVAL_MS;
        }
        ui_reader_flow_progress_finalize(reader_state, progress_report,
                                         progress_report_thread_handle);

        if (!reader_state->progress_paused &&
            now >= reader_state->progress_pause_deadline_tick) {
            reader_state->progress_paused = 1;
            reader_state->progress_pause_tick = now;
            reader_state->progress_report_due_tick = 0;
        }

        if (!*progress_report_thread_handle && !progress_report->running &&
            !reader_state->progress_paused) {
            if (reader_state->progress_initial_report_pending) {
                reader_state->progress_initial_report_pending = 0;
                ui_reader_flow_queue_report(ctx, reader_state, progress_report,
                                            progress_report_thread_handle, 0, 0);
            } else if (reader_state->progress_report_due_tick > 0 &&
                       now >= reader_state->progress_report_due_tick) {
                elapsed_ms = reader_state->progress_start_tick > 0 &&
                    now > reader_state->progress_start_tick ?
                    (now - reader_state->progress_start_tick) : 0;
                elapsed_seconds = (int)(elapsed_ms / 1000);
                ui_reader_flow_queue_report(ctx, reader_state, progress_report,
                                            progress_report_thread_handle,
                                            elapsed_seconds, 1);
            }
        }
    }

    for (int i = 0; i < (int)(sizeof(chapter_prefetch_cache->slots) /
                              sizeof(chapter_prefetch_cache->slots[0])); i++) {
        ui_reader_flow_prefetch_poll(&chapter_prefetch_cache->slots[i].state,
                                     &chapter_prefetch_cache->slots[i].thread);
    }
    render_requested |=
        ui_reader_flow_catalog_hydration_poll(reader_state, catalog_hydration,
                                              catalog_hydration_thread_handle);
    ui_reader_flow_catalog_hydration_maybe_start(ctx, reader_state, catalog_hydration,
                                                 catalog_hydration_thread_handle);

    current_index = ui_reader_view_current_catalog_index(reader_state);
    if (current_index >= 0 && current_index == chapter_prefetch_cache->last_update_index) {
        return render_requested;
    }
    chapter_prefetch_cache->last_update_index = current_index;
    if (reader_state->doc.catalog_items && reader_state->doc.catalog_count > 0 &&
        current_index >= 0) {
        for (int distance = 1; distance <= UI_CHAPTER_PREFETCH_RADIUS; distance++) {
            int indexes[2] = { current_index - distance, current_index + distance };

            for (int j = 0; j < 2; j++) {
                int index = indexes[j];
                ReaderCatalogItem *item;
                if (index < 0 || index >= reader_state->doc.catalog_count) {
                    continue;
                }
                item = &reader_state->doc.catalog_items[index];
                if (!item->target || !item->target[0] ||
                    ui_reader_flow_target_in_list(item->target, targets, target_count)) {
                    continue;
                }
                snprintf(targets[target_count++], sizeof(targets[0]), "%s", item->target);
            }
        }
    } else {
        if (reader_state->doc.prev_target && reader_state->doc.prev_target[0]) {
            snprintf(targets[target_count++], sizeof(targets[0]), "%s",
                     reader_state->doc.prev_target);
        }
        if (reader_state->doc.next_target && reader_state->doc.next_target[0] &&
            !ui_reader_flow_target_in_list(reader_state->doc.next_target, targets,
                                           target_count)) {
            snprintf(targets[target_count++], sizeof(targets[0]), "%s",
                     reader_state->doc.next_target);
        }
    }

    for (int i = 0; i < (int)(sizeof(chapter_prefetch_cache->slots) /
                              sizeof(chapter_prefetch_cache->slots[0])); i++) {
        ChapterPrefetchSlot *slot = &chapter_prefetch_cache->slots[i];
        if ((slot->state.ready || slot->state.running) &&
            !ui_reader_flow_target_in_list(slot->state.target, targets, target_count) &&
            !slot->state.running) {
            ui_reader_flow_prefetch_slot_reset(slot);
        }
    }

    for (int i = 0; i < target_count; i++) {
        ui_reader_flow_prefetch_request(ctx, chapter_prefetch_cache, targets[i],
                                        reader_state->doc.font_size);
    }
    return render_requested;
}

void ui_reader_flow_poll_background(ChapterPrefetchCache *chapter_prefetch_cache,
                                    CatalogHydrationState *catalog_hydration,
                                    SDL_Thread **catalog_hydration_thread_handle,
                                    ReaderViewState *reader_state) {
    if (chapter_prefetch_cache) {
        for (int i = 0; i < (int)(sizeof(chapter_prefetch_cache->slots) /
                                  sizeof(chapter_prefetch_cache->slots[0])); i++) {
            ui_reader_flow_prefetch_poll(&chapter_prefetch_cache->slots[i].state,
                                         &chapter_prefetch_cache->slots[i].thread);
        }
    }
    (void)ui_reader_flow_catalog_hydration_poll(reader_state, catalog_hydration,
                                                catalog_hydration_thread_handle);
}

void ui_reader_flow_begin_reader_open(ApiContext *ctx, ReaderOpenState *reader_open,
                                      SDL_Thread **reader_open_thread_handle,
                                      const char *source_target, const char *book_id,
                                      int font_size) {
    if (!ctx || !reader_open || !reader_open_thread_handle || !source_target ||
        !*source_target || *reader_open_thread_handle || reader_open->running) {
        return;
    }

    ui_reader_flow_reader_open_state_reset(reader_open);
    snprintf(reader_open->data_dir, sizeof(reader_open->data_dir), "%s", ctx->data_dir);
    snprintf(reader_open->ca_file, sizeof(reader_open->ca_file), "%s", ctx->ca_file);
    snprintf(reader_open->source_target, sizeof(reader_open->source_target), "%s", source_target);
    if (book_id && *book_id) {
        snprintf(reader_open->book_id, sizeof(reader_open->book_id), "%s", book_id);
    }
    reader_open->font_size = font_size;
    reader_open->content_font_size = UI_READER_CONTENT_FONT_SIZE;
    fprintf(stderr,
            "reader-open-begin: source=%s bookId=%s font=%d\n",
            reader_open->source_target,
            reader_open->book_id[0] ? reader_open->book_id : "(null)",
            reader_open->font_size);
    reader_open->running = 1;
    *reader_open_thread_handle =
        SDL_CreateThread(ui_reader_flow_open_thread, "weread-reader-open", reader_open);
    if (!*reader_open_thread_handle) {
        reader_open->running = 0;
        reader_open->failed = 1;
        fprintf(stderr,
                "reader-open-begin: SDL_CreateThread failed source=%s\n",
                reader_open->source_target);
    }
}

int ui_reader_flow_finish_open(ApiContext *ctx, TTF_Font *body_font,
                               ReaderOpenState *reader_open,
                               SDL_Thread **reader_open_thread_handle,
                               ReaderViewState *reader_state, UiView *view,
                               char *status, size_t status_size,
                               char *shelf_status, size_t shelf_status_size,
                               Uint32 *poor_network_toast_until,
                               const UiLayout *current_layout, int shelf_available) {
    if (!ctx || !body_font || !reader_open || !reader_open_thread_handle ||
        !*reader_open_thread_handle || reader_open->running || !reader_state ||
        !view || !status || !shelf_status || !poor_network_toast_until ||
        !current_layout) {
        return 0;
    }

    SDL_WaitThread(*reader_open_thread_handle, NULL);
    *reader_open_thread_handle = NULL;
    if (reader_open->poor_network) {
        *poor_network_toast_until = SDL_GetTicks() + 3000;
    }
    fprintf(stderr,
            "reader-open-finish: begin source=%s ready=%d failed=%d kind=%s docTarget=%s initialPage=%d initialOffset=%d honorSaved=%d contentFont=%d\n",
            reader_open->source_target,
            reader_open->ready,
            reader_open->failed,
            reader_open->doc.kind == READER_DOCUMENT_KIND_ARTICLE ? "article" : "book",
            reader_open->doc.target ? reader_open->doc.target : "(null)",
            reader_open->initial_page,
            reader_open->initial_offset,
            reader_open->honor_saved_position,
            reader_open->content_font_size);
    reader_state->content_font_size = reader_open->content_font_size;
    if (reader_open->ready &&
        ui_reader_view_adopt_document(body_font, &reader_open->doc,
                                      current_layout->reader_content_w,
                                      current_layout->reader_content_h,
                                      reader_open->honor_saved_position,
                                      reader_state) == 0) {
        if (!reader_open->honor_saved_position) {
            if (reader_open->initial_offset > 0) {
                reader_state->current_page =
                    ui_reader_view_find_page_for_offset(reader_state,
                                                        reader_open->initial_offset);
            } else {
                reader_state->current_page = reader_open->initial_page;
            }
            ui_reader_view_clamp_current_page(reader_state);
        }
        ui_reader_view_set_source_target(reader_state, reader_open->source_target);
        ui_reader_view_save_local_position(ctx, reader_state);
        shelf_status[0] = '\0';
        status[0] = '\0';
        *view = VIEW_READER;
        fprintf(stderr,
                "reader-open-finish: adopted source=%s docTarget=%s view=reader currentPage=%d totalLines=%d\n",
                reader_open->source_target,
                reader_state->doc.target ? reader_state->doc.target : "(null)",
                reader_state->current_page,
                reader_state->line_count);
    } else if (reader_open->failed || !reader_open->ready) {
        fprintf(stderr,
                "reader-open-finish: open failed source=%s shelfAvailable=%d ready=%d failed=%d\n",
                reader_open->source_target,
                shelf_available,
                reader_open->ready,
                reader_open->failed);
        if (shelf_available) {
            *view = VIEW_SHELF;
            snprintf(shelf_status, shelf_status_size,
                     "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE6\x89\x80\xE9\x80\x89\xE4\xB9\xA6\xE7\xB1\x8D");
            ui_reader_flow_reader_open_state_reset(reader_open);
        } else {
            char retry_target[2048];
            char retry_book_id[256];
            int retry_font_size = reader_open->font_size;
            ui_copy_string(retry_target, sizeof(retry_target), reader_open->source_target);
            ui_copy_string(retry_book_id, sizeof(retry_book_id), reader_open->book_id);
            ui_reader_flow_reader_open_state_reset(reader_open);
            ui_copy_string(reader_open->source_target, sizeof(reader_open->source_target),
                           retry_target);
            ui_copy_string(reader_open->book_id, sizeof(reader_open->book_id), retry_book_id);
            reader_open->font_size = retry_font_size;
            *view = VIEW_OPENING;
            snprintf(status, status_size,
                     "\xE6\x89\x93\xE5\xBC\x80\xE5\xA4\xB1\xE8\xB4\xA5\xEF\xBC\x8C\xE6\x8C\x89 A \xE9\x87\x8D\xE8\xAF\x95");
        }
    } else {
        fprintf(stderr,
                "reader-open-finish: ready but adopt failed source=%s\n",
                reader_open->source_target);
        ui_reader_flow_reader_open_state_reset(reader_open);
    }

    return 1;
}

void ui_reader_flow_shutdown(ReaderOpenState *reader_open,
                             SDL_Thread **reader_open_thread_handle,
                             ProgressReportState *progress_report,
                             SDL_Thread **progress_report_thread_handle,
                             ChapterPrefetchCache *chapter_prefetch_cache,
                             CatalogHydrationState *catalog_hydration,
                             SDL_Thread **catalog_hydration_thread_handle) {
    if (reader_open_thread_handle && *reader_open_thread_handle) {
        SDL_WaitThread(*reader_open_thread_handle, NULL);
        *reader_open_thread_handle = NULL;
    }
    if (progress_report_thread_handle && *progress_report_thread_handle) {
        SDL_WaitThread(*progress_report_thread_handle, NULL);
        *progress_report_thread_handle = NULL;
    }
    if (catalog_hydration_thread_handle && *catalog_hydration_thread_handle) {
        SDL_WaitThread(*catalog_hydration_thread_handle, NULL);
        *catalog_hydration_thread_handle = NULL;
    }
    ui_reader_flow_chapter_prefetch_cache_reset(chapter_prefetch_cache);
    ui_reader_flow_catalog_hydration_state_reset(catalog_hydration);
    ui_reader_flow_progress_report_state_reset(progress_report);
    ui_reader_flow_reader_open_state_reset(reader_open);
}

#endif
