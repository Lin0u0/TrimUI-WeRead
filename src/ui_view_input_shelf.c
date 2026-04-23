#include "ui_internal.h"

#if HAVE_SDL

#include <stdio.h>
#include "shelf.h"

static void ui_handle_shelf_open(UiViewInputContext *context, const char *target,
                                 const char *book_id, int font_size,
                                 int *render_requested) {
    if (!context || !target || !target[0] || !render_requested) {
        return;
    }

    ui_reader_flow_begin_reader_open(context->ctx, context->reader_open,
                                     context->reader_open_thread_handle,
                                     target, book_id, font_size);
    if (atomic_load(&context->reader_open->running) || *context->reader_open_thread_handle) {
        *context->view = VIEW_OPENING;
        *render_requested = 1;
    } else if (context->shelf_status && context->shelf_status_size > 0) {
        snprintf(context->shelf_status, context->shelf_status_size,
                 "\xE6\x97\xA0\xE6\xB3\x95\xE5\x90\xAF\xE5\x8A\xA8\xE5\x8A\xA0\xE8\xBD\xBD\xE4\xBB\xBB\xE5\x8A\xA1");
    }
}

void ui_handle_shelf_view_action(UiViewInputContext *context, UiInputAction action,
                                 int *render_requested) {
    int article_count;
    int count;
    int total_count;
    int min_selected;

    if (!context || !render_requested || !context->selected ||
        action == UI_INPUT_ACTION_NONE) {
        return;
    }

    article_count = shelf_article_count(context->shelf_nuxt);
    count = shelf_normal_book_count(context->shelf_nuxt);
    total_count = article_count + count;
    min_selected = article_count > 0 ? -article_count : 0;

    if (action == UI_INPUT_ACTION_SHELF_SETTINGS_OPEN) {
        if (ui_settings_flow_open_from_shelf(context->settings_state, context->view, 1,
                                             *context->selected) == 0) {
            *context->exit_confirm_until = 0;
            *render_requested = 1;
            ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_CONFIRM_MS, 45);
        }
    } else if (action == UI_INPUT_ACTION_SHELF_NEXT && *context->selected + 1 < count) {
        (*context->selected)++;
        *render_requested = 1;
        ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 25);
    } else if (action == UI_INPUT_ACTION_SHELF_PREV && *context->selected > min_selected) {
        (*context->selected)--;
        *render_requested = 1;
        ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 25);
    } else if (action == UI_INPUT_ACTION_SHELF_RESUME) {
        char target[2048];
        int font_size = 3;

        if (ui_shelf_flow_prepare_resume(context->ctx, target, sizeof(target), &font_size,
                                         context->loading_title, context->loading_title_size,
                                         context->status, context->status_size)) {
            ui_handle_shelf_open(context, target, NULL, font_size, render_requested);
        }
    } else if (action == UI_INPUT_ACTION_SHELF_OPEN_SELECTED && total_count > 0) {
        char target[2048];
        char book_id[256];

        if (ui_shelf_flow_prepare_selected_open(context->shelf_nuxt, *context->selected,
                                                target, sizeof(target),
                                                book_id, sizeof(book_id),
                                                context->loading_title, context->loading_title_size,
                                                context->status, context->status_size,
                                                context->shelf_status,
                                                context->shelf_status_size)) {
            ui_handle_shelf_open(context, target, book_id[0] ? book_id : NULL, 3,
                                 render_requested);
        }
    }
}

void ui_handle_shelf_view_event(UiViewInputContext *context, const SDL_Event *event,
                                int *render_requested) {
    ui_handle_shelf_view_action(
        context,
        ui_input_action_for_event(UI_INPUT_SCOPE_SHELF, event,
                                  context ? context->tg5040_input : 0,
                                  context ? context->input_suppression : NULL),
        render_requested);
}

#endif
