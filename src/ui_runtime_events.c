#include "ui_runtime_internal.h"

#if HAVE_SDL

#include <stdio.h>

static UiInputScope ui_runtime_input_scope(const UiRuntime *runtime) {
    if (!runtime) {
        return UI_INPUT_SCOPE_NONE;
    }
    switch (runtime->view) {
    case VIEW_SHELF:
        return UI_INPUT_SCOPE_SHELF;
    case VIEW_LOGIN:
        return UI_INPUT_SCOPE_LOGIN;
    case VIEW_BOOTSTRAP:
        return UI_INPUT_SCOPE_BOOTSTRAP;
    case VIEW_OPENING:
        return UI_INPUT_SCOPE_OPENING;
    case VIEW_SETTINGS:
        return UI_INPUT_SCOPE_SETTINGS;
    case VIEW_READER:
        return runtime->reader_state.catalog_open ?
            UI_INPUT_SCOPE_READER_CATALOG : UI_INPUT_SCOPE_READER;
    default:
        return UI_INPUT_SCOPE_NONE;
    }
}

static int ui_runtime_dispatch_view_action(UiRuntime *runtime, ApiContext *ctx,
                                           UiRuntimeFrame *frame,
                                           UiInputAction action) {
    if (!runtime || !ctx || !frame || action == UI_INPUT_ACTION_NONE) {
        return 0;
    }

    if (runtime->view == VIEW_SHELF) {
        UiViewInputContext view_input =
            ui_make_shelf_view_input_context(
                ctx, &runtime->view, &runtime->haptic_state,
                &runtime->motion_state, &runtime->current_layout,
                &runtime->settings_state, &runtime->reader_state,
                &runtime->reader_open, &runtime->reader_open_thread_handle,
                &runtime->chapter_prefetch_cache, runtime->shelf_nuxt,
                &runtime->selected, runtime->tg5040_input,
                &runtime->input_suppression, runtime->loading_title,
                sizeof(runtime->loading_title), runtime->status,
                sizeof(runtime->status), runtime->shelf_status,
                sizeof(runtime->shelf_status), &runtime->exit_confirm_until,
                &runtime->reader_exit_confirm_until, runtime->body_font);

        ui_handle_shelf_view_action(&view_input, action, &frame->render_requested);
        return 0;
    }

    if (runtime->view == VIEW_LOGIN) {
        ui_handle_login_view_action(ctx, action, &runtime->login_start,
                                    &runtime->login_thread, &runtime->view,
                                    &runtime->login_active, runtime->status,
                                    sizeof(runtime->status),
                                    &runtime->qr_texture, runtime->qr_path,
                                    &runtime->haptic_state);
        return 0;
    }

    if (runtime->view == VIEW_BOOTSTRAP) {
        ui_handle_bootstrap_view_action(ctx, action, &runtime->startup_state,
                                        &runtime->startup_thread_handle,
                                        runtime->loading_title,
                                        sizeof(runtime->loading_title),
                                        runtime->status,
                                        sizeof(runtime->status),
                                        &runtime->haptic_state);
        return 0;
    }

    if (runtime->view == VIEW_OPENING) {
        ui_handle_opening_view_action(ctx, action, &runtime->reader_open,
                                      &runtime->reader_open_thread_handle,
                                      runtime->status,
                                      sizeof(runtime->status),
                                      &runtime->haptic_state);
        return 0;
    }

    if (runtime->view == VIEW_SETTINGS) {
        UiViewInputContext view_input =
            ui_make_settings_view_input_context(
                ctx, &runtime->view, &runtime->haptic_state,
                &runtime->settings_state, &runtime->reader_state,
                &runtime->session, runtime->shelf_nuxt,
                &runtime->shelf_nuxt, &runtime->shelf_covers,
                &runtime->shelf_cover_download,
                &runtime->shelf_cover_download_thread_handle,
                &runtime->selected, &runtime->login_active,
                &runtime->preferred_reader_font_size,
                &runtime->brightness_level, runtime->tg5040_input,
                &runtime->input_suppression, runtime->status,
                sizeof(runtime->status), runtime->shelf_status,
                sizeof(runtime->shelf_status), runtime->renderer,
                &runtime->scene_texture, &runtime->current_layout,
                runtime->body_font, &runtime->rotation,
                &runtime->qr_texture, &runtime->exit_confirm_until,
                &runtime->reader_exit_confirm_until);

        if (ui_handle_settings_view_action(&view_input, action,
                                           &frame->render_requested) < 0) {
            runtime->running = 0;
            return -1;
        }
        return 0;
    }

    if (runtime->view == VIEW_READER) {
        UiViewInputContext view_input =
            ui_make_reader_view_input_context(
                ctx, &runtime->view, &runtime->haptic_state,
                &runtime->motion_state, &runtime->current_layout,
                &runtime->settings_state, &runtime->reader_state,
                &runtime->reader_open, &runtime->reader_open_thread_handle,
                &runtime->chapter_prefetch_cache, runtime->shelf_nuxt,
                &runtime->selected, runtime->tg5040_input,
                &runtime->input_suppression, runtime->loading_title,
                sizeof(runtime->loading_title), runtime->status,
                sizeof(runtime->status), runtime->shelf_status,
                sizeof(runtime->shelf_status), &runtime->exit_confirm_until,
                &runtime->reader_exit_confirm_until, runtime->body_font);

        ui_handle_reader_view_action(&view_input, action, &frame->render_requested);
    }

    return 0;
}

static int ui_runtime_dispatch_action(UiRuntime *runtime, ApiContext *ctx,
                                      UiRuntimeFrame *frame,
                                      UiGlobalInputContext *global_input,
                                      UiInputAction action) {
    int global_route_result;

    if (!runtime || !ctx || !frame || !global_input ||
        action == UI_INPUT_ACTION_NONE) {
        return 0;
    }

    if (runtime->view == VIEW_READER) {
        frame->reader_input_seen = 1;
    }

    global_route_result =
        ui_handle_global_action(global_input, action, frame->frame_now);
    if (global_route_result != 0) {
        return global_route_result < 0 ? -1 : 0;
    }

    return ui_runtime_dispatch_view_action(runtime, ctx, frame, action);
}

void ui_runtime_process_events(UiRuntime *runtime, ApiContext *ctx,
                               UiRuntimeFrame *frame) {
    SDL_Event event;
    UiGlobalInputContext global_input;
    UiInputMask event_press_mask = 0;
    UiInputMask live_mask;
    UiInputMask pressed_mask;
    UiInputAction action;

    if (!runtime || !ctx || !frame) {
        return;
    }

    global_input =
        ui_make_global_input_context(
            ctx, runtime->renderer, &runtime->scene_texture,
            &runtime->current_layout, &runtime->view,
            &runtime->settings_state, &runtime->reader_state,
            &runtime->reader_open, &runtime->login_poll,
            &runtime->session, &runtime->login_active,
            &runtime->running, runtime->tg5040_input,
            &runtime->brightness_level, &runtime->haptic_state,
            &runtime->repeat_state, &runtime->motion_state,
            NULL, NULL,
            &runtime->lock_button_ignore_until,
            &runtime->last_lock_trigger_tick,
            &runtime->exit_confirm_until,
            &runtime->reader_exit_confirm_until,
            &runtime->shelf_nuxt, &runtime->shelf_covers,
            &runtime->shelf_cover_download,
            &runtime->shelf_cover_download_thread_handle,
            &runtime->qr_texture, &runtime->selected,
            runtime->status, sizeof(runtime->status),
            &frame->render_requested);

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            runtime->running = 0;
        } else if (event.type == SDL_RENDER_TARGETS_RESET ||
                   event.type == SDL_RENDER_DEVICE_RESET) {
            if (ui_recreate_scene_texture(runtime->renderer,
                                          &runtime->scene_texture,
                                          &runtime->current_layout) != 0) {
                fprintf(stderr,
                        "Failed to recover renderer after reset: %s\n",
                        SDL_GetError());
                runtime->running = 0;
                break;
            }
            runtime->motion_state.last_tick = SDL_GetTicks();
            runtime->repeat_state.action = UI_INPUT_ACTION_NONE;
            runtime->repeat_state.next_tick = 0;
        } else if (event.type == SDL_KEYDOWN ||
                   event.type == SDL_JOYBUTTONDOWN ||
                   event.type == SDL_JOYHATMOTION ||
                   event.type == SDL_JOYAXISMOTION) {
            event_press_mask |= ui_input_event_mask(&event, runtime->tg5040_input);
        }
    }

    live_mask = ui_input_current_mask(runtime->tg5040_input, runtime->joysticks,
                                      runtime->joystick_count);
    ui_input_suppression_refresh(&runtime->input_suppression, live_mask);
    pressed_mask = ui_input_state_update(&runtime->input_state, live_mask,
                                         event_press_mask,
                                         &runtime->input_suppression);
    action = ui_input_action_for_mask(ui_runtime_input_scope(runtime), pressed_mask);
    if (action != UI_INPUT_ACTION_NONE) {
        UiView action_view_before = runtime->view;
        int action_catalog_before = runtime->reader_state.catalog_open;
        char action_reader_target_before[2048];

        ui_copy_string(action_reader_target_before,
                       sizeof(action_reader_target_before),
                       runtime->reader_state.doc.target ?
                       runtime->reader_state.doc.target : "");

        if (ui_runtime_dispatch_action(runtime, ctx, frame, &global_input, action) < 0) {
            return;
        }

        if (ui_should_begin_input_transition_after_action(
                action, action_view_before, runtime->view,
                action_catalog_before, &runtime->reader_state,
                action_reader_target_before)) {
            ui_start_input_transition(&frame->transition_input);
        }
    }
}

#endif
