#include "ui_internal.h"

#if HAVE_SDL

#include <stdio.h>
#include <string.h>

static void ui_reset_transient_input_state(UiRepeatState *repeat_state,
                                           Uint32 *exit_confirm_until,
                                           Uint32 *reader_exit_confirm_until) {
    if (repeat_state) {
        repeat_state->action = UI_INPUT_ACTION_NONE;
        repeat_state->next_tick = 0;
    }
    if (exit_confirm_until) {
        *exit_confirm_until = 0;
    }
    if (reader_exit_confirm_until) {
        *reader_exit_confirm_until = 0;
    }
}

static void ui_begin_input_transition(UiInputTransitionContext *context) {
    UiInputMask live_mask;

    if (!context) {
        return;
    }

    live_mask = ui_input_current_mask(context->tg5040_input,
                                      context->joysticks,
                                      context->joystick_count);

    ui_reset_transient_input_state(context->repeat_state,
                                   context->exit_confirm_until,
                                   context->reader_exit_confirm_until);
    if (context->input_suppression) {
        ui_input_suppression_begin(context->input_suppression, live_mask);
    }
    if (context->input_state) {
        ui_input_state_reset(context->input_state, live_mask, context->input_suppression);
    }
}

UiInputTransitionContext ui_make_input_transition_context(
    UiRepeatState *repeat_state,
    UiInputSuppression *input_suppression,
    UiInputState *input_state,
    int tg5040_input,
    SDL_Joystick **joysticks,
    int joystick_count,
    Uint32 *exit_confirm_until,
    Uint32 *reader_exit_confirm_until) {
    UiInputTransitionContext context;

    memset(&context, 0, sizeof(context));
    context.repeat_state = repeat_state;
    context.input_suppression = input_suppression;
    context.input_state = input_state;
    context.tg5040_input = tg5040_input;
    context.joysticks = joysticks;
    context.joystick_count = joystick_count;
    context.exit_confirm_until = exit_confirm_until;
    context.reader_exit_confirm_until = reader_exit_confirm_until;
    return context;
}

UiGlobalInputContext ui_make_global_input_context(
    ApiContext *ctx,
    SDL_Renderer *renderer,
    SDL_Texture **scene_texture,
    UiLayout *current_layout,
    UiView *view,
    SettingsFlowState *settings_state,
    ReaderViewState *reader_state,
    ReaderOpenState *reader_open,
    LoginPollState *login_poll,
    AuthSession *session,
    int *login_active,
    int *running,
    int tg5040_input,
    int *brightness_level,
    UiHapticState *haptic_state,
    UiRepeatState *repeat_state,
    UiMotionState *motion_state,
    int *tg5040_select_pressed,
    int *tg5040_start_pressed,
    Uint32 *lock_button_ignore_until,
    Uint32 *last_lock_trigger_tick,
    Uint32 *exit_confirm_until,
    Uint32 *reader_exit_confirm_until,
    cJSON **shelf_nuxt,
    ShelfCoverCache *shelf_covers,
    ShelfCoverDownloadState *shelf_cover_download,
    SDL_Thread **shelf_cover_download_thread_handle,
    SDL_Texture **qr_texture,
    int *selected,
    char *status,
    size_t status_size,
    int *render_requested) {
    UiGlobalInputContext context;

    memset(&context, 0, sizeof(context));
    context.ctx = ctx;
    context.renderer = renderer;
    context.scene_texture = scene_texture;
    context.current_layout = current_layout;
    context.view = view;
    context.settings_state = settings_state;
    context.reader_state = reader_state;
    context.reader_open = reader_open;
    context.login_poll = login_poll;
    context.session = session;
    context.login_active = login_active;
    context.running = running;
    context.tg5040_input = tg5040_input;
    context.brightness_level = brightness_level;
    context.haptic_state = haptic_state;
    context.repeat_state = repeat_state;
    context.motion_state = motion_state;
    context.tg5040_select_pressed = tg5040_select_pressed;
    context.tg5040_start_pressed = tg5040_start_pressed;
    context.lock_button_ignore_until = lock_button_ignore_until;
    context.last_lock_trigger_tick = last_lock_trigger_tick;
    context.exit_confirm_until = exit_confirm_until;
    context.reader_exit_confirm_until = reader_exit_confirm_until;
    context.shelf_nuxt = shelf_nuxt;
    context.shelf_covers = shelf_covers;
    context.shelf_cover_download = shelf_cover_download;
    context.shelf_cover_download_thread_handle = shelf_cover_download_thread_handle;
    context.qr_texture = qr_texture;
    context.selected = selected;
    context.status = status;
    context.status_size = status_size;
    context.render_requested = render_requested;
    return context;
}

void ui_start_input_transition(UiInputTransitionContext *context) {
    if (!context) {
        return;
    }
    ui_begin_input_transition(context);
}

int ui_should_begin_input_transition_after_action(UiInputAction action,
                                                 UiView event_view_before,
                                                 UiView current_view,
                                                 int event_catalog_before,
                                                 const ReaderViewState *reader_state,
                                                 const char *event_reader_target_before) {
    return event_view_before != current_view ||
           event_catalog_before != (reader_state ? reader_state->catalog_open : 0) ||
           (ui_view_input_reader_doc_target_changed(event_reader_target_before, reader_state) &&
            !ui_view_input_should_skip_transition_for_page_turn(
                action, event_view_before, current_view,
                event_catalog_before, reader_state, event_reader_target_before));
}

int ui_should_begin_input_transition_after_repeat(UiInputAction repeat_action,
                                                  const char *reader_target_before,
                                                  const ReaderViewState *reader_state) {
    return ui_view_input_reader_doc_target_changed(reader_target_before, reader_state) &&
           !(repeat_action == UI_INPUT_ACTION_READER_PAGE_NEXT ||
             repeat_action == UI_INPUT_ACTION_READER_PAGE_PREV);
}

int ui_should_begin_input_transition_after_frame(UiView frame_view_before,
                                                 UiView current_view,
                                                 int catalog_before,
                                                 const ReaderViewState *reader_state,
                                                 const char *frame_reader_target_before) {
    if (frame_view_before != current_view ||
        (current_view == VIEW_READER &&
         catalog_before != (reader_state ? reader_state->catalog_open : 0))) {
        return 1;
    }
    return ui_view_input_reader_doc_target_changed(frame_reader_target_before, reader_state);
}

UiViewInputContext ui_make_view_input_context(ApiContext *ctx, UiView *view,
                                              UiHapticState *haptic_state,
                                              SettingsFlowState *settings_state,
                                              ReaderViewState *reader_state,
                                              ReaderOpenState *reader_open,
                                              SDL_Thread **reader_open_thread_handle,
                                              cJSON *shelf_nuxt, int *selected,
                                              int tg5040_input,
                                              const UiInputSuppression *input_suppression,
                                              char *loading_title,
                                              size_t loading_title_size,
                                              char *status, size_t status_size,
                                              char *shelf_status,
                                              size_t shelf_status_size,
                                              Uint32 *exit_confirm_until,
                                              Uint32 *reader_exit_confirm_until) {
    UiViewInputContext context;

    memset(&context, 0, sizeof(context));
    context.ctx = ctx;
    context.view = view;
    context.haptic_state = haptic_state;
    context.settings_state = settings_state;
    context.reader_state = reader_state;
    context.reader_open = reader_open;
    context.reader_open_thread_handle = reader_open_thread_handle;
    context.shelf_nuxt = shelf_nuxt;
    context.selected = selected;
    context.tg5040_input = tg5040_input;
    context.input_suppression = input_suppression;
    context.loading_title = loading_title;
    context.loading_title_size = loading_title_size;
    context.status = status;
    context.status_size = status_size;
    context.shelf_status = shelf_status;
    context.shelf_status_size = shelf_status_size;
    context.exit_confirm_until = exit_confirm_until;
    context.reader_exit_confirm_until = reader_exit_confirm_until;
    return context;
}

UiViewInputContext ui_make_shelf_view_input_context(ApiContext *ctx, UiView *view,
                                                    UiHapticState *haptic_state,
                                                    UiMotionState *motion_state,
                                                    UiLayout *current_layout,
                                                    SettingsFlowState *settings_state,
                                                    ReaderViewState *reader_state,
                                                    ReaderOpenState *reader_open,
                                                    SDL_Thread **reader_open_thread_handle,
                                                    ChapterPrefetchCache *chapter_prefetch_cache,
                                                    cJSON *shelf_nuxt, int *selected,
                                                    int tg5040_input,
                                                    const UiInputSuppression *input_suppression,
                                                    char *loading_title,
                                                    size_t loading_title_size,
                                                    char *status, size_t status_size,
                                                    char *shelf_status,
                                                    size_t shelf_status_size,
                                                    Uint32 *exit_confirm_until,
                                                    Uint32 *reader_exit_confirm_until,
                                                    TTF_Font *body_font) {
    UiViewInputContext context =
        ui_make_view_input_context(ctx, view, haptic_state, settings_state, reader_state,
                                   reader_open, reader_open_thread_handle, shelf_nuxt,
                                   selected, tg5040_input, input_suppression,
                                   loading_title, loading_title_size, status, status_size,
                                   shelf_status, shelf_status_size, exit_confirm_until,
                                   reader_exit_confirm_until);

    context.motion_state = motion_state;
    context.current_layout = current_layout;
    context.chapter_prefetch_cache = chapter_prefetch_cache;
    context.body_font = body_font;
    return context;
}

UiViewInputContext ui_make_reader_view_input_context(ApiContext *ctx, UiView *view,
                                                     UiHapticState *haptic_state,
                                                     UiMotionState *motion_state,
                                                     UiLayout *current_layout,
                                                     SettingsFlowState *settings_state,
                                                     ReaderViewState *reader_state,
                                                     ReaderOpenState *reader_open,
                                                     SDL_Thread **reader_open_thread_handle,
                                                     ChapterPrefetchCache *chapter_prefetch_cache,
                                                     cJSON *shelf_nuxt, int *selected,
                                                     int tg5040_input,
                                                     const UiInputSuppression *input_suppression,
                                                     char *loading_title,
                                                     size_t loading_title_size,
                                                     char *status, size_t status_size,
                                                     char *shelf_status,
                                                     size_t shelf_status_size,
                                                     Uint32 *exit_confirm_until,
                                                     Uint32 *reader_exit_confirm_until,
                                                     TTF_Font *body_font) {
    return ui_make_shelf_view_input_context(ctx, view, haptic_state, motion_state,
                                            current_layout, settings_state, reader_state,
                                            reader_open, reader_open_thread_handle,
                                            chapter_prefetch_cache, shelf_nuxt, selected,
                                            tg5040_input, input_suppression, loading_title,
                                            loading_title_size, status, status_size,
                                            shelf_status, shelf_status_size,
                                            exit_confirm_until, reader_exit_confirm_until,
                                            body_font);
}

UiViewInputContext ui_make_settings_view_input_context(
    ApiContext *ctx,
    UiView *view,
    UiHapticState *haptic_state,
    SettingsFlowState *settings_state,
    ReaderViewState *reader_state,
    AuthSession *session,
    cJSON *shelf_nuxt,
    cJSON **shelf_nuxt_ref,
    ShelfCoverCache *shelf_covers,
    ShelfCoverDownloadState *shelf_cover_download,
    SDL_Thread **shelf_cover_download_thread_handle,
    int *selected,
    int *login_active,
    int *preferred_reader_font_size,
    int *brightness_level,
    int tg5040_input,
    const UiInputSuppression *input_suppression,
    char *status,
    size_t status_size,
    char *shelf_status,
    size_t shelf_status_size,
    SDL_Renderer *renderer,
    SDL_Texture **scene_texture,
    UiLayout *current_layout,
    TTF_Font *body_font,
    UiRotation *rotation,
    SDL_Texture **qr_texture,
    Uint32 *exit_confirm_until,
    Uint32 *reader_exit_confirm_until) {
    UiViewInputContext context =
        ui_make_view_input_context(ctx, view, haptic_state, settings_state, reader_state,
                                   NULL, NULL, shelf_nuxt, selected, tg5040_input,
                                   input_suppression, NULL, 0, status, status_size,
                                   shelf_status, shelf_status_size, exit_confirm_until,
                                   reader_exit_confirm_until);

    context.session = session;
    context.shelf_nuxt_ref = shelf_nuxt_ref;
    context.shelf_covers = shelf_covers;
    context.shelf_cover_download = shelf_cover_download;
    context.shelf_cover_download_thread_handle = shelf_cover_download_thread_handle;
    context.login_active = login_active;
    context.preferred_reader_font_size = preferred_reader_font_size;
    context.brightness_level = brightness_level;
    context.renderer = renderer;
    context.scene_texture = scene_texture;
    context.current_layout = current_layout;
    context.body_font = body_font;
    context.rotation = rotation;
    context.qr_texture = qr_texture;
    return context;
}

int ui_handle_global_action(UiGlobalInputContext *context, UiInputAction action,
                            Uint32 frame_now) {
    if (!context) {
        return 0;
    }

    if (action == UI_INPUT_ACTION_GLOBAL_BACK ||
        action == UI_INPUT_ACTION_SETTINGS_CLOSE) {
        if (*context->view == VIEW_LOGIN) {
            *context->login_active = 0;
            atomic_store(&context->login_poll->stop, 1);
            ui_force_exit_from_login(context->haptic_state);
        } else if (*context->view == VIEW_SETTINGS) {
            ui_settings_clear_logout_confirm(context->settings_state);
            if (ui_settings_flow_begin_close(context->settings_state) == 0) {
                *context->render_requested = 1;
                ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 35);
            }
        } else if (*context->view == VIEW_READER) {
            if (context->reader_state->catalog_open) {
                context->reader_state->catalog_open = 0;
                *context->render_requested = 1;
                ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 35);
            } else if (*context->reader_exit_confirm_until > frame_now) {
                ui_reader_view_flush_progress_blocking(context->ctx, context->reader_state, 1);
                ui_reader_view_save_local_position(context->ctx, context->reader_state);
                *context->reader_exit_confirm_until = 0;
                *context->exit_confirm_until = 0;
                *context->view = VIEW_SHELF;
                *context->render_requested = 1;
                ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_CONFIRM_MS, 50);
            } else {
                *context->reader_exit_confirm_until = frame_now + UI_EXIT_CONFIRM_DURATION_MS;
                *context->render_requested = 1;
                ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 35);
            }
        } else if (*context->view == VIEW_OPENING &&
                   atomic_load(&context->reader_open->running)) {
            snprintf(context->status, context->status_size,
                     "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x89\x93\xE5\xBC\x80\xE4\xB9\xA6\xE7\xB1\x8D...");
        } else {
            if (*context->exit_confirm_until > frame_now) {
                *context->running = 0;
                ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_CONFIRM_MS, 50);
            } else {
                *context->exit_confirm_until = frame_now + UI_EXIT_CONFIRM_DURATION_MS;
                *context->render_requested = 1;
                ui_platform_haptic_pulse(context->haptic_state, UI_HAPTIC_NAV_MS, 35);
            }
        }
        return 1;
    }

    if (action == UI_INPUT_ACTION_GLOBAL_LOCK &&
        frame_now >= *context->lock_button_ignore_until &&
        (*context->last_lock_trigger_tick == 0 ||
         frame_now - *context->last_lock_trigger_tick >= 1000)) {
        *context->last_lock_trigger_tick = frame_now;
        if (*context->view == VIEW_READER) {
            ui_reader_view_flush_progress_blocking(context->ctx, context->reader_state, 1);
            ui_reader_view_save_local_position(context->ctx, context->reader_state);
        }
        if (ui_platform_lock_screen(context->tg5040_input) == 0) {
            context->repeat_state->action = UI_INPUT_ACTION_NONE;
            context->repeat_state->next_tick = 0;
            context->motion_state->last_tick = SDL_GetTicks();
            *context->lock_button_ignore_until = context->motion_state->last_tick + 1200;
            if (ui_platform_restore_after_sleep(context->renderer, context->scene_texture,
                                                context->current_layout,
                                                context->tg5040_input,
                                                context->brightness_level ?
                                                *context->brightness_level :
                                                UI_BRIGHTNESS_DEFAULT) != 0) {
                *context->running = 0;
                return -1;
            }
        }
        return 1;
    }

    return 0;
}

#endif
