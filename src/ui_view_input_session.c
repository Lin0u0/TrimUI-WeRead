#include "ui_internal.h"

#if HAVE_SDL

#include <stdio.h>
#include <string.h>

static void ui_begin_login_flow(ApiContext *ctx, LoginStartState *login_start,
                                SDL_Thread **login_thread_handle, UiView *view,
                                char *status, size_t status_size,
                                SDL_Texture **qr_texture, const char *qr_path,
                                UiHapticState *haptic_state) {
    if (!ctx || !login_start || !login_thread_handle || !view || !status || !qr_path) {
        return;
    }

    if (qr_texture && *qr_texture) {
        SDL_DestroyTexture(*qr_texture);
        *qr_texture = NULL;
    }
    ui_startup_login_begin_login_flow(ctx, login_start, login_thread_handle, view,
                                      status, status_size, qr_path);
    ui_platform_haptic_pulse(haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
}

void ui_handle_login_view_action(ApiContext *ctx, UiInputAction action,
                                 LoginStartState *login_start,
                                 SDL_Thread **login_thread_handle, UiView *view,
                                 int *login_active, char *status, size_t status_size,
                                 SDL_Texture **qr_texture, const char *qr_path,
                                 UiHapticState *haptic_state) {
    if (!login_start || !login_active) {
        return;
    }
    if (action == UI_INPUT_ACTION_LOGIN_CONFIRM &&
        !atomic_load(&login_start->running) &&
        !*login_active) {
        ui_begin_login_flow(ctx, login_start, login_thread_handle, view, status, status_size,
                            qr_texture, qr_path, haptic_state);
    }
}

void ui_handle_login_view_event(ApiContext *ctx, const SDL_Event *event, int tg5040_input,
                                const UiInputSuppression *input_suppression,
                                LoginStartState *login_start,
                                SDL_Thread **login_thread_handle, UiView *view,
                                int *login_active, char *status, size_t status_size,
                                SDL_Texture **qr_texture, const char *qr_path,
                                UiHapticState *haptic_state) {
    ui_handle_login_view_action(
        ctx,
        ui_input_action_for_event(UI_INPUT_SCOPE_LOGIN, event, tg5040_input,
                                  input_suppression),
        login_start, login_thread_handle, view, login_active, status, status_size,
        qr_texture, qr_path, haptic_state);
}

void ui_handle_bootstrap_view_action(ApiContext *ctx, UiInputAction action,
                                     StartupState *startup_state,
                                     SDL_Thread **startup_thread_handle,
                                     char *loading_title, size_t loading_title_size,
                                     char *status, size_t status_size,
                                     UiHapticState *haptic_state) {
    if (!startup_state || !startup_thread_handle || !loading_title || !status) {
        return;
    }
    if (action == UI_INPUT_ACTION_BOOTSTRAP_RETRY &&
        !atomic_load(&startup_state->running) &&
        !*startup_thread_handle) {
        snprintf(loading_title, loading_title_size,
                 "\xE5\xBE\xAE\xE4\xBF\xA1\xE8\xAF\xBB\xE4\xB9\xA6");
        snprintf(status, status_size, "\xE6\xAD\xA3\xE5\x9C\xA8\xE9\x87\x8D\xE8\xAF\x95...");
        ui_startup_login_begin_startup_refresh(ctx, startup_state, startup_thread_handle);
        ui_platform_haptic_pulse(haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
    }
}

void ui_handle_bootstrap_view_event(ApiContext *ctx, const SDL_Event *event, int tg5040_input,
                                    const UiInputSuppression *input_suppression,
                                    StartupState *startup_state,
                                    SDL_Thread **startup_thread_handle,
                                    char *loading_title, size_t loading_title_size,
                                    char *status, size_t status_size,
                                    UiHapticState *haptic_state) {
    ui_handle_bootstrap_view_action(
        ctx,
        ui_input_action_for_event(UI_INPUT_SCOPE_BOOTSTRAP, event, tg5040_input,
                                  input_suppression),
        startup_state, startup_thread_handle, loading_title, loading_title_size,
        status, status_size, haptic_state);
}

void ui_handle_opening_view_action(ApiContext *ctx, UiInputAction action,
                                   ReaderOpenState *reader_open,
                                   SDL_Thread **reader_open_thread_handle,
                                   char *status, size_t status_size,
                                   UiHapticState *haptic_state) {
    if (!reader_open || !reader_open_thread_handle || !status) {
        return;
    }
    if (action == UI_INPUT_ACTION_OPENING_RETRY &&
        !atomic_load(&reader_open->running) &&
        !*reader_open_thread_handle &&
        reader_open->source_target[0]) {
        snprintf(status, status_size, "\xE6\xAD\xA3\xE5\x9C\xA8\xE9\x87\x8D\xE8\xAF\x95...");
        ui_reader_flow_begin_reader_open(ctx, reader_open, reader_open_thread_handle,
                                         reader_open->source_target,
                                         reader_open->book_id[0] ? reader_open->book_id : NULL,
                                         reader_open->font_size);
        ui_platform_haptic_pulse(haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
    }
}

void ui_handle_opening_view_event(ApiContext *ctx, const SDL_Event *event, int tg5040_input,
                                  const UiInputSuppression *input_suppression,
                                  ReaderOpenState *reader_open,
                                  SDL_Thread **reader_open_thread_handle,
                                  char *status, size_t status_size,
                                  UiHapticState *haptic_state) {
    ui_handle_opening_view_action(
        ctx,
        ui_input_action_for_event(UI_INPUT_SCOPE_OPENING, event, tg5040_input,
                                  input_suppression),
        reader_open, reader_open_thread_handle, status, status_size, haptic_state);
}

int ui_handle_settings_view_action(UiViewInputContext *context, UiInputAction action,
                                   int *render_requested) {
    int item_count = UI_SETTINGS_ITEM_COUNT;

    if (!context || !render_requested || !context->settings_state ||
        !context->renderer || !context->body_font || !context->reader_state ||
        !context->preferred_reader_font_size || !context->brightness_level ||
        !context->rotation || !context->current_layout || !context->scene_texture ||
        !context->shelf_status || !context->view) {
        return 0;
    }

    if (action == UI_INPUT_ACTION_SETTINGS_NEXT &&
        context->settings_state->selected + 1 < item_count) {
        context->settings_state->selected++;
        ui_settings_clear_logout_confirm(context->settings_state);
        *render_requested = 1;
        ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 25);
    } else if (action == UI_INPUT_ACTION_SETTINGS_PREV &&
               context->settings_state->selected > 0) {
        context->settings_state->selected--;
        ui_settings_clear_logout_confirm(context->settings_state);
        *render_requested = 1;
        ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 25);
    } else if (action == UI_INPUT_ACTION_SETTINGS_CONFIRM &&
               context->settings_state->selected == UI_SETTINGS_ITEM_LOGOUT) {
        if (!context->settings_state->logout_confirm_armed) {
            context->settings_state->logout_confirm_armed = 1;
            snprintf(context->shelf_status, context->shelf_status_size,
                     "Press A again to log out");
            *render_requested = 1;
            ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 35);
        } else {
            SessionLogoutResult logout_result = {0};
            int logout_rc = session_service_logout(context->ctx, &logout_result);

            ui_settings_clear_logout_confirm(context->settings_state);
            if (logout_rc == 0 && logout_result.local_cleanup_ok) {
                context->shelf_status[0] = '\0';
                ui_transition_to_login_required(context->view, context->session,
                                                context->login_active,
                                                context->settings_state,
                                                context->reader_state,
                                                context->shelf_nuxt_ref,
                                                context->shelf_covers,
                                                context->shelf_cover_download,
                                                context->shelf_cover_download_thread_handle,
                                                context->qr_texture,
                                                context->selected,
                                                context->exit_confirm_until,
                                                context->reader_exit_confirm_until,
                                                context->status,
                                                context->status_size,
                                                ui_logout_status_text(logout_result.outcome));
            } else {
                snprintf(context->shelf_status, context->shelf_status_size, "%s",
                         ui_logout_status_text(SESSION_LOGOUT_LOCAL_FAILED));
            }
            *render_requested = 1;
            ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_CONFIRM_MS, 45);
        }
    } else if ((action == UI_INPUT_ACTION_SETTINGS_ADJUST_PREV ||
                action == UI_INPUT_ACTION_SETTINGS_ADJUST_NEXT) &&
               context->settings_state->selected != UI_SETTINGS_ITEM_LOGOUT) {
        int apply_result = ui_settings_apply(context->settings_state, context->ctx,
                                             context->renderer, context->body_font,
                                             context->reader_state,
                                             context->preferred_reader_font_size,
                                             context->tg5040_input,
                                             context->brightness_level,
                                             context->rotation,
                                             context->current_layout,
                                             context->scene_texture,
                                             context->shelf_status,
                                             context->shelf_status_size,
                                             action == UI_INPUT_ACTION_SETTINGS_ADJUST_PREV ?
                                             -1 : 1);

        ui_settings_clear_logout_confirm(context->settings_state);
        if (apply_result < 0) {
            return -1;
        }
        if (apply_result > 0) {
            *render_requested = 1;
            ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_CONFIRM_MS, 45);
        }
    }

    return 0;
}

int ui_handle_settings_view_event(UiViewInputContext *context, const SDL_Event *event,
                                  int *render_requested) {
    return ui_handle_settings_view_action(
        context,
        ui_input_action_for_event(UI_INPUT_SCOPE_SETTINGS, event,
                                  context ? context->tg5040_input : 0,
                                  context ? context->input_suppression : NULL),
        render_requested);
}

#endif
