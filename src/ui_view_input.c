#include "ui_internal.h"

#if HAVE_SDL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shelf.h"

static void ui_view_input_sync_catalog_selection_visual(UiMotionState *motion_state,
                                                        const ReaderViewState *reader_state) {
    if (!motion_state || !reader_state) {
        return;
    }
    motion_state->catalog_selected_visual = (float)reader_state->catalog_selected;
    motion_state->catalog_selection_initialized = 1;
}

static int ui_view_input_expand_catalog_for_selection(UiMotionState *motion_state,
                                                      ApiContext *ctx,
                                                      ReaderViewState *reader_state,
                                                      int direction,
                                                      char *shelf_status,
                                                      size_t shelf_status_size) {
    int added_count = ui_reader_view_expand_catalog_for_selection(ctx, reader_state, direction,
                                                                  shelf_status,
                                                                  shelf_status_size);

    if (added_count > 0 && direction < 0) {
        reader_state->catalog_scroll_top += added_count;
        ui_view_input_sync_catalog_selection_visual(motion_state, reader_state);
    }
    return added_count;
}

static int ui_view_input_move_catalog_selection(UiMotionState *motion_state, ApiContext *ctx,
                                                ReaderViewState *reader_state, int delta,
                                                char *shelf_status,
                                                size_t shelf_status_size) {
    int target;
    int expanded = 0;

    if (!reader_state || !reader_state->doc.catalog_items ||
        reader_state->doc.catalog_count <= 0 || delta == 0) {
        return 0;
    }

    target = reader_state->catalog_selected + delta;
    while (target < 0) {
        int added = ui_view_input_expand_catalog_for_selection(motion_state, ctx, reader_state,
                                                               -1, shelf_status,
                                                               shelf_status_size);
        if (added <= 0) {
            target = 0;
            break;
        }
        expanded = 1;
        target += added;
    }
    while (target >= reader_state->doc.catalog_count) {
        int added = ui_view_input_expand_catalog_for_selection(motion_state, ctx, reader_state,
                                                               1, shelf_status,
                                                               shelf_status_size);
        if (added <= 0) {
            target = reader_state->doc.catalog_count - 1;
            break;
        }
        expanded = 1;
    }

    if (target < 0) {
        target = 0;
    }
    if (target >= reader_state->doc.catalog_count) {
        target = reader_state->doc.catalog_count - 1;
    }
    if (target == reader_state->catalog_selected) {
        return 0;
    }

    reader_state->catalog_selected = target;
    if (expanded) {
        ui_view_input_sync_catalog_selection_visual(motion_state, reader_state);
    }
    return 1;
}

void ui_view_input_apply_repeat_action(UiInputAction action, ApiContext *ctx,
                                       TTF_Font *body_font, ReaderViewState *reader_state,
                                       UiMotionState *motion_state, cJSON *shelf_nuxt,
                                       int *selected, char *shelf_status,
                                       size_t shelf_status_size,
                                       const UiLayout *current_layout,
                                       ChapterPrefetchCache *chapter_prefetch_cache) {
    if (action == UI_INPUT_ACTION_NONE) {
        return;
    }
    if (action == UI_INPUT_ACTION_SHELF_PREV ||
        action == UI_INPUT_ACTION_SHELF_NEXT) {
        int count = shelf_normal_book_count(shelf_nuxt);
        int min_selected = shelf_article_count(shelf_nuxt) > 0 ?
            -shelf_article_count(shelf_nuxt) : 0;

        if (!selected) {
            return;
        }
        if (action == UI_INPUT_ACTION_SHELF_NEXT && *selected + 1 < count) {
            (*selected)++;
        } else if (action == UI_INPUT_ACTION_SHELF_PREV && *selected > min_selected) {
            (*selected)--;
        }
        return;
    }
    if (!reader_state) {
        return;
    }
    if (action == UI_INPUT_ACTION_READER_CATALOG_UP) {
        (void)ui_view_input_move_catalog_selection(motion_state, ctx, reader_state, -1,
                                                   shelf_status, shelf_status_size);
        return;
    }
    if (action == UI_INPUT_ACTION_READER_CATALOG_DOWN) {
        (void)ui_view_input_move_catalog_selection(motion_state, ctx, reader_state, 1,
                                                   shelf_status, shelf_status_size);
        return;
    }
    if (action == UI_INPUT_ACTION_READER_CATALOG_PAGE_PREV) {
        (void)ui_view_input_move_catalog_selection(motion_state, ctx, reader_state, -10,
                                                   shelf_status, shelf_status_size);
        return;
    }
    if (action == UI_INPUT_ACTION_READER_CATALOG_PAGE_NEXT) {
        (void)ui_view_input_move_catalog_selection(motion_state, ctx, reader_state, 10,
                                                   shelf_status, shelf_status_size);
        return;
    }
    if (action == UI_INPUT_ACTION_READER_PAGE_NEXT) {
        int total_pages = ui_reader_view_total_pages(reader_state);

        ui_reader_view_note_progress_activity(reader_state, SDL_GetTicks());
        if (reader_state->current_page + 1 < total_pages) {
            reader_state->current_page++;
            ui_reader_view_save_local_position(ctx, reader_state);
        } else if (reader_state->doc.next_target && reader_state->doc.next_target[0]) {
            char *target = strdup(reader_state->doc.next_target);
            char source_target[2048];
            int font_size = reader_state->doc.font_size;

            if (!target) {
                return;
            }
            snprintf(source_target, sizeof(source_target), "%s", reader_state->source_target);
            ui_reader_view_flush_progress_blocking(ctx, reader_state, 1);
            if (chapter_prefetch_cache &&
                ui_reader_flow_chapter_prefetch_cache_adopt(chapter_prefetch_cache, target,
                                                            body_font, reader_state,
                                                            current_layout) == 0) {
                ui_reader_view_set_source_target(reader_state, source_target);
                ui_reader_view_save_local_position(ctx, reader_state);
                shelf_status[0] = '\0';
            } else if (ui_reader_view_load(ctx, body_font, target, font_size,
                                           current_layout->reader_content_w,
                                           current_layout->reader_content_h, 0,
                                           reader_state) == 0) {
                ui_reader_view_set_source_target(reader_state, source_target);
                ui_reader_view_save_local_position(ctx, reader_state);
                shelf_status[0] = '\0';
            } else {
                snprintf(shelf_status, shelf_status_size,
                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8B\xE4\xB8\x80\xE7\xAB\xA0");  /* 无法打开下一章 */
            }
            free(target);
        }
        return;
    }
    if (action == UI_INPUT_ACTION_READER_PAGE_PREV) {
        ui_reader_view_note_progress_activity(reader_state, SDL_GetTicks());
        if (reader_state->current_page > 0) {
            reader_state->current_page--;
            ui_reader_view_save_local_position(ctx, reader_state);
        } else if (reader_state->doc.prev_target && reader_state->doc.prev_target[0]) {
            char *target = strdup(reader_state->doc.prev_target);
            char source_target[2048];
            int font_size = reader_state->doc.font_size;

            if (!target) {
                return;
            }
            snprintf(source_target, sizeof(source_target), "%s", reader_state->source_target);
            ui_reader_view_flush_progress_blocking(ctx, reader_state, 1);
            if (chapter_prefetch_cache &&
                ui_reader_flow_chapter_prefetch_cache_adopt(chapter_prefetch_cache, target,
                                                            body_font, reader_state,
                                                            current_layout) == 0) {
                int new_total_pages;

                ui_reader_view_set_source_target(reader_state, source_target);
                new_total_pages = ui_reader_view_total_pages(reader_state);
                reader_state->current_page = new_total_pages > 0 ? new_total_pages - 1 : 0;
                ui_reader_view_save_local_position(ctx, reader_state);
                shelf_status[0] = '\0';
            } else if (ui_reader_view_load(ctx, body_font, target, font_size,
                                           current_layout->reader_content_w,
                                           current_layout->reader_content_h, 0,
                                           reader_state) == 0) {
                int new_total_pages;

                ui_reader_view_set_source_target(reader_state, source_target);
                new_total_pages = ui_reader_view_total_pages(reader_state);
                reader_state->current_page = new_total_pages > 0 ? new_total_pages - 1 : 0;
                ui_reader_view_save_local_position(ctx, reader_state);
                shelf_status[0] = '\0';
            } else {
                snprintf(shelf_status, shelf_status_size,
                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8A\xE4\xB8\x80\xE7\xAB\xA0");  /* 无法打开上一章 */
            }
            free(target);
        }
    }
}

static int ui_view_input_catalog_item_matches_document(const ReaderCatalogItem *item,
                                                       const ReaderDocument *doc) {
    if (!item || !doc) {
        return 0;
    }
    if (item->chapter_uid && doc->chapter_uid &&
        strcmp(item->chapter_uid, doc->chapter_uid) == 0) {
        return 1;
    }
    if (item->chapter_idx > 0 && doc->chapter_idx > 0 &&
        item->chapter_idx == doc->chapter_idx) {
        return 1;
    }
    if (item->target && doc->target && strcmp(item->target, doc->target) == 0) {
        return 1;
    }
    return 0;
}

static int ui_catalog_document_has_content(const ReaderDocument *doc) {
    return doc && doc->target && doc->target[0] &&
        doc->content_text && doc->content_text[0];
}

static int ui_catalog_load_document(ApiContext *ctx, const char *target, int font_size,
                                    ReaderDocument *doc_out) {
    if (!ctx || !target || !target[0] || !doc_out) {
        return -1;
    }

    memset(doc_out, 0, sizeof(*doc_out));
    if (reader_load(ctx, target, font_size, doc_out) != 0) {
        return -1;
    }
    if (!ui_catalog_document_has_content(doc_out)) {
        fprintf(stderr,
                "reader-catalog-jump: empty document target=%s loadedTarget=%s\n",
                target,
                doc_out->target ? doc_out->target : "(null)");
        reader_document_free(doc_out);
        return -1;
    }
    return 0;
}

static int ui_catalog_selection_direction(const ReaderViewState *reader_state,
                                          const ReaderCatalogItem *item,
                                          int selected_index) {
    int current_index;

    if (!reader_state || !item) {
        return 0;
    }
    if (item->chapter_idx > 0 && reader_state->doc.chapter_idx > 0 &&
        item->chapter_idx != reader_state->doc.chapter_idx) {
        return item->chapter_idx > reader_state->doc.chapter_idx ? 1 : -1;
    }

    current_index = ui_reader_view_current_catalog_index((ReaderViewState *)reader_state);
    if (current_index >= 0 && selected_index >= 0 && selected_index != current_index) {
        return selected_index > current_index ? 1 : -1;
    }
    if (item->target && reader_state->doc.next_target &&
        strcmp(item->target, reader_state->doc.next_target) == 0) {
        return 1;
    }
    if (item->target && reader_state->doc.prev_target &&
        strcmp(item->target, reader_state->doc.prev_target) == 0) {
        return -1;
    }
    return 0;
}

static int ui_catalog_walk_document(ApiContext *ctx, const ReaderViewState *reader_state,
                                    const ReaderCatalogItem *item, int selected_index,
                                    int font_size, ReaderDocument *doc_out) {
    int direction;
    int steps = 0;
    int last_chapter_idx;
    char *target = NULL;

    if (!ctx || !reader_state || !item || !doc_out) {
        return -1;
    }

    direction = ui_catalog_selection_direction(reader_state, item, selected_index);
    if (direction == 0) {
        return -1;
    }

    if (direction > 0) {
        if (!reader_state->doc.next_target || !reader_state->doc.next_target[0]) {
            return -1;
        }
        target = strdup(reader_state->doc.next_target);
    } else {
        if (!reader_state->doc.prev_target || !reader_state->doc.prev_target[0]) {
            return -1;
        }
        target = strdup(reader_state->doc.prev_target);
    }
    if (!target) {
        return -1;
    }

    last_chapter_idx = reader_state->doc.chapter_idx;
    while (target[0] && steps++ < 128) {
        ReaderDocument doc = {0};
        char *next_target = NULL;

        if (ui_catalog_load_document(ctx, target, font_size, &doc) != 0) {
            break;
        }
        fprintf(stderr,
                "reader-catalog-jump: walk step=%d direction=%d target=%s loadedUid=%s loadedIdx=%d loadedTarget=%s\n",
                steps,
                direction,
                target,
                doc.chapter_uid ? doc.chapter_uid : "(null)",
                doc.chapter_idx,
                doc.target ? doc.target : "(null)");
        if (ui_view_input_catalog_item_matches_document(item, &doc)) {
            free(target);
            *doc_out = doc;
            return 0;
        }

        if (direction > 0) {
            next_target = doc.next_target ? strdup(doc.next_target) : NULL;
        } else {
            next_target = doc.prev_target ? strdup(doc.prev_target) : NULL;
        }
        if (!next_target || !next_target[0]) {
            free(next_target);
            reader_document_free(&doc);
            break;
        }
        if (doc.chapter_idx > 0 && last_chapter_idx > 0) {
            if ((direction > 0 && doc.chapter_idx <= last_chapter_idx) ||
                (direction < 0 && doc.chapter_idx >= last_chapter_idx)) {
                free(next_target);
                reader_document_free(&doc);
                break;
            }
            if (item->chapter_idx > 0 &&
                ((direction > 0 && doc.chapter_idx > item->chapter_idx) ||
                 (direction < 0 && doc.chapter_idx < item->chapter_idx))) {
                free(next_target);
                reader_document_free(&doc);
                break;
            }
            last_chapter_idx = doc.chapter_idx;
        }

        free(target);
        target = next_target;
        reader_document_free(&doc);
    }

    free(target);
    return -1;
}

static int ui_view_input_catalog_resolve_document(ApiContext *ctx,
                                                  const ReaderViewState *reader_state,
                                                  const ReaderCatalogItem *item,
                                                  int selected_index,
                                                  int font_size,
                                                  ReaderDocument *doc_out) {
    ReaderDocument doc = {0};

    if (!ctx || !reader_state || !item || !doc_out) {
        return -1;
    }

    memset(doc_out, 0, sizeof(*doc_out));
    if (item->target && item->target[0] &&
        ui_catalog_load_document(ctx, item->target, font_size, &doc) == 0) {
        if (ui_view_input_catalog_item_matches_document(item, &doc)) {
            *doc_out = doc;
            return 0;
        }
        fprintf(stderr,
                "reader-catalog-jump: direct mismatch selectedUid=%s selectedIdx=%d selectedTarget=%s loadedUid=%s loadedIdx=%d loadedTarget=%s\n",
                item->chapter_uid ? item->chapter_uid : "(null)",
                item->chapter_idx,
                item->target,
                doc.chapter_uid ? doc.chapter_uid : "(null)",
                doc.chapter_idx,
                doc.target ? doc.target : "(null)");
        reader_document_free(&doc);
    }

    return ui_catalog_walk_document(ctx, reader_state, item, selected_index,
                                    font_size, doc_out);
}

int ui_view_input_reader_doc_target_changed(const char *before, const ReaderViewState *state) {
    const char *after = state && state->doc.target ? state->doc.target : "";

    if (!before) {
        before = "";
    }
    return strcmp(before, after) != 0;
}

int ui_view_input_should_skip_transition_for_page_turn(UiInputAction action,
                                                       UiView event_view_before,
                                                       UiView current_view,
                                                       int event_catalog_before,
                                                       const ReaderViewState *reader_state,
                                                       const char *event_reader_target_before) {
    if (event_view_before != VIEW_READER || current_view != VIEW_READER ||
        event_catalog_before || !reader_state) {
        return 0;
    }
    if (!ui_view_input_reader_doc_target_changed(event_reader_target_before, reader_state)) {
        return 0;
    }
    return action == UI_INPUT_ACTION_READER_PAGE_NEXT ||
           action == UI_INPUT_ACTION_READER_PAGE_PREV;
}

static void ui_reader_load_adjacent_document(UiViewInputContext *context, const char *target,
                                             const char *error_message, int place_at_end,
                                             int *render_requested) {
    char source_target[2048];
    int font_size;

    if (!context || !target || !target[0] || !context->reader_state) {
        return;
    }

    snprintf(source_target, sizeof(source_target), "%s", context->reader_state->source_target);
    font_size = context->reader_state->doc.font_size;
    ui_reader_view_flush_progress_blocking(context->ctx, context->reader_state, 1);
    if (context->chapter_prefetch_cache &&
        ui_reader_flow_chapter_prefetch_cache_adopt(context->chapter_prefetch_cache, target,
                                                    context->body_font, context->reader_state,
                                                    context->current_layout) == 0) {
        ui_reader_view_set_source_target(context->reader_state, source_target);
        if (place_at_end) {
            int new_total_pages = ui_reader_view_total_pages(context->reader_state);
            context->reader_state->current_page = new_total_pages > 0 ? new_total_pages - 1 : 0;
        }
        ui_reader_view_save_local_position(context->ctx, context->reader_state);
        context->shelf_status[0] = '\0';
    } else if (ui_reader_view_load(context->ctx, context->body_font, target, font_size,
                                   context->current_layout->reader_content_w,
                                   context->current_layout->reader_content_h, 0,
                                   context->reader_state) == 0) {
        ui_reader_view_set_source_target(context->reader_state, source_target);
        if (place_at_end) {
            int new_total_pages = ui_reader_view_total_pages(context->reader_state);
            context->reader_state->current_page = new_total_pages > 0 ? new_total_pages - 1 : 0;
        }
        ui_reader_view_save_local_position(context->ctx, context->reader_state);
        context->shelf_status[0] = '\0';
    } else {
        snprintf(context->shelf_status, context->shelf_status_size, "%s", error_message);
    }
    if (render_requested) {
        *render_requested = 1;
    }
}

void ui_handle_reader_view_action(UiViewInputContext *context, UiInputAction action,
                                  int *render_requested) {
    ReaderViewState *reader_state;

    if (!context || !render_requested || !context->reader_state ||
        action == UI_INPUT_ACTION_NONE) {
        return;
    }

    reader_state = context->reader_state;

    ui_reader_view_note_progress_activity(reader_state, SDL_GetTicks());
    if (action == UI_INPUT_ACTION_READER_SETTINGS_OPEN) {
        if (ui_settings_flow_open_from_reader(context->settings_state, context->view, 1) == 0) {
            *context->reader_exit_confirm_until = 0;
            *render_requested = 1;
            ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_CONFIRM_MS, 45);
        }
        return;
    }

    if (reader_state->catalog_open) {
        if (action == UI_INPUT_ACTION_READER_CATALOG_CLOSE) {
            reader_state->catalog_open = 0;
            *render_requested = 1;
            ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 35);
        } else if (action == UI_INPUT_ACTION_READER_CATALOG_UP) {
            if (ui_view_input_move_catalog_selection(context->motion_state, context->ctx,
                                                     reader_state, -1,
                                                     context->shelf_status,
                                                     context->shelf_status_size)) {
                *render_requested = 1;
                ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 25);
            }
        } else if (action == UI_INPUT_ACTION_READER_CATALOG_DOWN) {
            if (ui_view_input_move_catalog_selection(context->motion_state, context->ctx,
                                                     reader_state, 1,
                                                     context->shelf_status,
                                                     context->shelf_status_size)) {
                *render_requested = 1;
                ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 25);
            }
        } else if (action == UI_INPUT_ACTION_READER_CATALOG_PAGE_PREV) {
            if (ui_view_input_move_catalog_selection(context->motion_state, context->ctx,
                                                     reader_state, -10,
                                                     context->shelf_status,
                                                     context->shelf_status_size)) {
                *render_requested = 1;
                ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 25);
            }
        } else if (action == UI_INPUT_ACTION_READER_CATALOG_PAGE_NEXT) {
            if (ui_view_input_move_catalog_selection(context->motion_state, context->ctx,
                                                     reader_state, 10,
                                                     context->shelf_status,
                                                     context->shelf_status_size)) {
                *render_requested = 1;
                ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 25);
            }
        } else if (action == UI_INPUT_ACTION_READER_CATALOG_CONFIRM &&
                   reader_state->doc.catalog_items &&
                   reader_state->catalog_selected >= 0 &&
                   reader_state->catalog_selected < reader_state->doc.catalog_count) {
            ReaderCatalogItem *item =
                &reader_state->doc.catalog_items[reader_state->catalog_selected];
            ReaderDocument selected_doc = {0};
            char source_target[2048];
            int font_size = reader_state->doc.font_size;

            if (ui_view_input_catalog_item_matches_document(item, &reader_state->doc)) {
                reader_state->catalog_open = 0;
                *render_requested = 1;
                context->shelf_status[0] = '\0';
                ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_CONFIRM_MS, 45);
            } else if (item->target && item->target[0]) {
                snprintf(source_target, sizeof(source_target), "%s",
                         reader_state->source_target);
                ui_reader_view_flush_progress_blocking(context->ctx, reader_state, 1);
                if (ui_view_input_catalog_resolve_document(context->ctx, reader_state, item,
                                                           reader_state->catalog_selected,
                                                           font_size, &selected_doc) == 0 &&
                    ui_reader_view_adopt_document(context->body_font, &selected_doc,
                                                  context->current_layout->reader_content_w,
                                                  context->current_layout->reader_content_h,
                                                  0, reader_state) == 0) {
                    ui_reader_view_set_source_target(reader_state, source_target);
                    ui_reader_view_save_local_position(context->ctx, reader_state);
                    reader_state->catalog_open = 0;
                    *render_requested = 1;
                    context->shelf_status[0] = '\0';
                    ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                } else {
                    reader_document_free(&selected_doc);
                    snprintf(context->shelf_status, context->shelf_status_size,
                             "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE6\x89\x80\xE9\x80\x89\xE7\xAB\xA0\xE8\x8A\x82");
                    *render_requested = 1;
                }
            }
        }
        return;
    }

    if (action == UI_INPUT_ACTION_READER_CATALOG_TOGGLE) {
        fprintf(stderr,
                "reader-catalog-toggle: target=%s kind=%s count=%d items=%p open=%d\n",
                reader_state->doc.target ? reader_state->doc.target : "(null)",
                reader_state->doc.kind == READER_DOCUMENT_KIND_ARTICLE ? "article" : "book",
                reader_state->doc.catalog_count,
                (void *)reader_state->doc.catalog_items,
                reader_state->catalog_open);
        if (reader_state->doc.catalog_items && reader_state->doc.catalog_count > 0) {
            ui_reader_view_open_catalog(context->ctx, reader_state,
                                        context->shelf_status, context->shelf_status_size);
            if (reader_state->catalog_open) {
                ui_view_input_sync_catalog_selection_visual(context->motion_state, reader_state);
                *render_requested = 1;
                ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_CONFIRM_MS, 50);
            }
        } else if (reader_state->doc.kind == READER_DOCUMENT_KIND_ARTICLE) {
            snprintf(context->shelf_status, context->shelf_status_size,
                     "\xE5\xBD\x93\xE5\x89\x8D\xE6\x96\x87\xE7\xAB\xA0\xE6\x97\xA0\xE7\x9B\xAE\xE5\xBD\x95");
            *render_requested = 1;
        }
    } else if (action == UI_INPUT_ACTION_READER_PAGE_NEXT &&
               reader_state->current_page + 1 < ui_reader_view_total_pages(reader_state)) {
        reader_state->current_page++;
        ui_reader_view_save_local_position(context->ctx, reader_state);
    } else if (action == UI_INPUT_ACTION_READER_PAGE_NEXT &&
               reader_state->doc.next_target && reader_state->doc.next_target[0]) {
        ui_reader_load_adjacent_document(context, reader_state->doc.next_target,
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8B\xE4\xB8\x80\xE7\xAB\xA0",
                                         0, render_requested);
    } else if (action == UI_INPUT_ACTION_READER_PAGE_PREV &&
               reader_state->current_page > 0) {
        reader_state->current_page--;
        ui_reader_view_save_local_position(context->ctx, reader_state);
    } else if (action == UI_INPUT_ACTION_READER_PAGE_PREV &&
               reader_state->doc.prev_target && reader_state->doc.prev_target[0]) {
        ui_reader_load_adjacent_document(context, reader_state->doc.prev_target,
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8A\xE4\xB8\x80\xE7\xAB\xA0",
                                         1, render_requested);
    } else if ((action == UI_INPUT_ACTION_READER_CHAPTER_PREV ||
                action == UI_INPUT_ACTION_READER_CHAPTER_NEXT) &&
               reader_state->current_page > 0) {
        reader_state->current_page = 0;
        ui_reader_view_save_local_position(context->ctx, reader_state);
        ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_CONFIRM_MS, 50);
    } else if (action == UI_INPUT_ACTION_READER_CHAPTER_PREV &&
               reader_state->doc.prev_target && reader_state->doc.prev_target[0]) {
        ui_reader_load_adjacent_document(context, reader_state->doc.prev_target,
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8A\xE4\xB8\x80\xE7\xAB\xA0",
                                         0, render_requested);
        ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
    } else if (action == UI_INPUT_ACTION_READER_CHAPTER_NEXT &&
               reader_state->doc.next_target && reader_state->doc.next_target[0]) {
        ui_reader_load_adjacent_document(context, reader_state->doc.next_target,
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8B\xE4\xB8\x80\xE7\xAB\xA0",
                                         0, render_requested);
        ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
    }
}

void ui_handle_reader_view_event(UiViewInputContext *context, const SDL_Event *event,
                                 int *render_requested) {
    UiInputScope scope;

    if (!context || !context->reader_state) {
        return;
    }
    scope = context->reader_state->catalog_open ?
        UI_INPUT_SCOPE_READER_CATALOG : UI_INPUT_SCOPE_READER;
    ui_handle_reader_view_action(
        context,
        ui_input_action_for_event(scope, event, context->tg5040_input,
                                  context->input_suppression),
        render_requested);
}

#endif
