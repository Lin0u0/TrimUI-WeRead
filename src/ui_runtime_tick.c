#include "ui_runtime_internal.h"

#if HAVE_SDL

#include <math.h>
#include <string.h>
#include "json.h"
#include "shelf.h"

static const char *ui_runtime_session_expired_text(void) {
    return "Reading session expired. Press A to generate QR code";
}

static Uint32 ui_runtime_repeat_initial_delay_ms(UiInputAction action) {
    if (action == UI_INPUT_ACTION_SHELF_NEXT ||
        action == UI_INPUT_ACTION_SHELF_PREV) {
        return UI_SHELF_REPEAT_DELAY_MS;
    }
    if (action == UI_INPUT_ACTION_READER_PAGE_NEXT ||
        action == UI_INPUT_ACTION_READER_PAGE_PREV) {
        return UI_READER_PAGE_REPEAT_DELAY_MS;
    }
    return UI_INPUT_REPEAT_DELAY_MS;
}

static Uint32 ui_runtime_repeat_interval_ms(UiInputAction action) {
    if (action == UI_INPUT_ACTION_SHELF_NEXT ||
        action == UI_INPUT_ACTION_SHELF_PREV) {
        return UI_SHELF_REPEAT_INTERVAL_MS;
    }
    if (action == UI_INPUT_ACTION_READER_PAGE_NEXT ||
        action == UI_INPUT_ACTION_READER_PAGE_PREV) {
        return UI_READER_PAGE_REPEAT_INTERVAL_MS;
    }
    return UI_INPUT_REPEAT_INTERVAL_MS;
}

static void ui_runtime_reload_shelf_after_login(UiRuntime *runtime, ApiContext *ctx) {
    cJSON_Delete(runtime->shelf_nuxt);
    runtime->shelf_nuxt = shelf_load(ctx, 1, NULL);
    ui_user_profile_sync(&runtime->user_profile, runtime->shelf_nuxt, ctx->data_dir);
    ui_shelf_flow_cover_download_stop(&runtime->shelf_cover_download,
                                      &runtime->shelf_cover_download_thread_handle);
    shelf_cover_cache_build(ctx, runtime->shelf_nuxt, &runtime->shelf_covers);
    runtime->selected = shelf_ui_clamp_selection(runtime->shelf_nuxt,
                                                 shelf_ui_default_selection(runtime->shelf_nuxt));
    runtime->shelf_start = 0;
    runtime->shelf_status[0] = '\0';
    runtime->status[0] = '\0';
    runtime->login_active = 0;
    runtime->view = VIEW_SHELF;
}

void ui_runtime_tick_after_input(UiRuntime *runtime, ApiContext *ctx,
                                 UiRuntimeFrame *frame) {
    UiInputAction repeat_action;
    Uint32 now;

    if (!runtime || !ctx || !frame) {
        return;
    }

    if (runtime->view == VIEW_SHELF) {
        repeat_action = ui_input_repeat_action_for_mask(UI_INPUT_SCOPE_SHELF,
                                                        runtime->input_state.current_mask);
    } else if (runtime->view == VIEW_READER && runtime->reader_state.catalog_open) {
        repeat_action = ui_input_repeat_action_for_mask(UI_INPUT_SCOPE_READER_CATALOG,
                                                        runtime->input_state.current_mask);
    } else if (runtime->view == VIEW_READER) {
        repeat_action = ui_input_repeat_action_for_mask(UI_INPUT_SCOPE_READER,
                                                        runtime->input_state.current_mask);
    } else {
        repeat_action = UI_INPUT_ACTION_NONE;
    }
    now = SDL_GetTicks();
    if (repeat_action != runtime->repeat_state.action) {
        runtime->repeat_state.action = repeat_action;
        runtime->repeat_state.next_tick = repeat_action != UI_INPUT_ACTION_NONE ?
            now + ui_runtime_repeat_initial_delay_ms(repeat_action) : 0;
    } else if (repeat_action != UI_INPUT_ACTION_NONE &&
               now >= runtime->repeat_state.next_tick) {
        char repeat_reader_target_before[2048];

        ui_copy_string(repeat_reader_target_before,
                       sizeof(repeat_reader_target_before),
                       runtime->reader_state.doc.target ?
                       runtime->reader_state.doc.target : "");
        ui_view_input_apply_repeat_action(repeat_action, ctx, runtime->body_font,
                                          &runtime->reader_state,
                                          &runtime->motion_state,
                                          runtime->shelf_nuxt,
                                          &runtime->selected,
                                          runtime->shelf_status,
                                          sizeof(runtime->shelf_status),
                                          &runtime->current_layout,
                                          &runtime->chapter_prefetch_cache);
        if (runtime->view == VIEW_READER) {
            frame->render_requested = 1;
        }
        if (ui_should_begin_input_transition_after_repeat(
                repeat_action, repeat_reader_target_before,
                &runtime->reader_state)) {
            ui_start_input_transition(&frame->transition_input);
        } else {
            runtime->repeat_state.next_tick =
                now + ui_runtime_repeat_interval_ms(repeat_action);
        }
    }

    if (runtime->view == VIEW_READER) {
        frame->render_requested |=
            ui_reader_flow_tick_reader(ctx, &runtime->reader_state,
                                       &runtime->progress_report,
                                       &runtime->progress_report_thread_handle,
                                       &runtime->chapter_prefetch_cache,
                                       &runtime->catalog_hydration,
                                       &runtime->catalog_hydration_thread_handle);
    } else if (ui_reader_flow_chapter_prefetch_has_running_work(
                   &runtime->chapter_prefetch_cache) ||
               ui_reader_flow_catalog_hydration_has_running_work(
                   &runtime->catalog_hydration,
                   runtime->catalog_hydration_thread_handle)) {
        ui_reader_flow_poll_background(&runtime->chapter_prefetch_cache,
                                       &runtime->catalog_hydration,
                                       &runtime->catalog_hydration_thread_handle,
                                       &runtime->reader_state);
    }
    if (runtime->reader_state.progress_session_expired &&
        runtime->view != VIEW_LOGIN &&
        runtime->view != VIEW_BOOTSTRAP &&
        runtime->view != VIEW_OPENING) {
        ui_reader_view_save_local_position(ctx, &runtime->reader_state);
        runtime->shelf_status[0] = '\0';
        ui_transition_to_login_required(
            &runtime->view, &runtime->session, &runtime->login_active,
            &runtime->settings_state, &runtime->reader_state,
            &runtime->shelf_nuxt, &runtime->shelf_covers,
            &runtime->shelf_cover_download,
            &runtime->shelf_cover_download_thread_handle,
            &runtime->qr_texture, &runtime->selected,
            &runtime->exit_confirm_until,
            &runtime->reader_exit_confirm_until,
            runtime->status, sizeof(runtime->status),
            ui_runtime_session_expired_text());
        frame->render_requested = 1;
    }

    if (runtime->shelf_cover_download_thread_handle) {
        ui_shelf_flow_cover_download_poll(&runtime->shelf_covers,
                                          &runtime->shelf_cover_download,
                                          &runtime->shelf_cover_download_thread_handle);
    }
    if (runtime->view == VIEW_SHELF) {
        int direction = 0;
        int shelf_animating = 0;

        if (runtime->motion_state.shelf_initialized) {
            float delta = (float)runtime->selected -
                runtime->motion_state.shelf_selected_visual;

            if (delta > 0.05f) {
                direction = 1;
            } else if (delta < -0.05f) {
                direction = -1;
            }
            shelf_animating = fabsf(delta) >= 0.001f;
        }
        if (direction == 0) {
            if (repeat_action == UI_INPUT_ACTION_SHELF_NEXT) {
                direction = 1;
            } else if (repeat_action == UI_INPUT_ACTION_SHELF_PREV) {
                direction = -1;
            }
        }

        ui_shelf_flow_cover_download_maybe_start(
            ctx, runtime->shelf_nuxt,
            &runtime->shelf_covers, &runtime->shelf_cover_download,
            &runtime->shelf_cover_download_thread_handle, runtime->selected, direction);
        if (runtime->motion_state.shelf_initialized) {
            int prepare_limit = shelf_animating ?
                UI_SHELF_COVER_PREPARE_ANIM_LIMIT :
                UI_SHELF_COVER_PREPARE_IDLE_LIMIT;

            if (shelf_cover_prepare_nearby(
                    ctx, runtime->renderer, runtime->shelf_nuxt,
                    &runtime->shelf_covers,
                    runtime->motion_state.shelf_selected_visual,
                    runtime->selected, direction, prepare_limit) > 0) {
                frame->render_requested = 1;
                runtime->motion_state.shelf_cover_warmup_active = 1;
            } else {
                runtime->motion_state.shelf_cover_warmup_active = 0;
            }
        }
    } else {
        runtime->motion_state.shelf_cover_warmup_active = 0;
    }

    if (ui_startup_login_finish_startup(&runtime->startup_thread_handle,
                                        &runtime->startup_state)) {
        frame->render_requested = 1;
        if (runtime->startup_state.session_ok == 1 &&
            runtime->startup_state.shelf_nuxt) {
            cJSON_Delete(runtime->shelf_nuxt);
            runtime->shelf_nuxt = runtime->startup_state.shelf_nuxt;
            runtime->startup_state.shelf_nuxt = NULL;
            ui_user_profile_sync(&runtime->user_profile, runtime->shelf_nuxt,
                                 ctx->data_dir);
            ui_shelf_flow_cover_download_stop(
                &runtime->shelf_cover_download,
                &runtime->shelf_cover_download_thread_handle);
            shelf_cover_cache_build(ctx, runtime->shelf_nuxt,
                                    &runtime->shelf_covers);
            if (shelf_books(runtime->shelf_nuxt) &&
                cJSON_IsArray(shelf_books(runtime->shelf_nuxt))) {
                runtime->selected =
                    shelf_ui_clamp_selection(runtime->shelf_nuxt,
                                             runtime->selected);
                if (runtime->view != VIEW_READER &&
                    runtime->view != VIEW_LOGIN &&
                    runtime->view != VIEW_OPENING) {
                    runtime->shelf_status[0] = '\0';
                } else if (runtime->view == VIEW_BOOTSTRAP) {
                    runtime->view = VIEW_SHELF;
                }
            }
        } else if (runtime->startup_state.session_ok == 0) {
            runtime->view = VIEW_LOGIN;
            runtime->login_active = 0;
            memset(&runtime->session, 0, sizeof(runtime->session));
            ui_user_profile_clear(&runtime->user_profile);
            snprintf(runtime->status, sizeof(runtime->status),
                     "\xE6\x8C\x89 A \xE7\x94\x9F\xE6\x88\x90\xE4\xBA\x8C\xE7\xBB\xB4\xE7\xA0\x81");
            if (runtime->qr_texture) {
                SDL_DestroyTexture(runtime->qr_texture);
                runtime->qr_texture = NULL;
            }
        } else if (!runtime->shelf_nuxt &&
                   runtime->view != VIEW_READER &&
                   runtime->view != VIEW_LOGIN) {
            snprintf(runtime->status, sizeof(runtime->status),
                     "\xE7\xBD\x91\xE7\xBB\x9C\xE9\x94\x99\xE8\xAF\xAF\xEF\xBC\x8C\xE6\x8C\x89 A \xE9\x87\x8D\xE8\xAF\x95");
        }
        if (runtime->startup_state.poor_network) {
            runtime->poor_network_toast_until = SDL_GetTicks() + 3000;
        }
        ui_startup_login_startup_state_reset(&runtime->startup_state);
    }

    if (runtime->reader_open_thread_handle && !runtime->reader_open.running) {
        frame->render_requested = 1;
        (void)ui_reader_flow_finish_open(
            ctx, runtime->body_font, &runtime->reader_open,
            &runtime->reader_open_thread_handle,
            &runtime->reader_state, &runtime->view,
            runtime->status, sizeof(runtime->status),
            runtime->shelf_status, sizeof(runtime->shelf_status),
            &runtime->poor_network_toast_until,
            &runtime->current_layout,
            runtime->shelf_nuxt != NULL);
    }

    if (!runtime->motion_state.shelf_initialized) {
        runtime->motion_state.shelf_selected_visual = (float)runtime->selected;
        runtime->motion_state.shelf_initialized = 1;
    } else {
        runtime->motion_state.shelf_selected_visual =
            ui_motion_step(runtime->motion_state.shelf_selected_visual,
                           (float)runtime->selected,
                           UI_SHELF_SELECTION_SPEED, frame->dt_seconds);
        if (fabsf(runtime->motion_state.shelf_selected_visual -
                  (float)runtime->selected) < 0.001f) {
            runtime->motion_state.shelf_selected_visual =
                (float)runtime->selected;
        }
    }
    runtime->motion_state.catalog_selected_visual =
        (float)runtime->reader_state.catalog_selected;
    runtime->motion_state.catalog_selection_initialized = 1;
    runtime->motion_state.catalog_selection_animating_active = 0;
    runtime->motion_state.catalog_progress =
        ui_motion_step(runtime->motion_state.catalog_progress,
                       runtime->reader_state.catalog_open ? 1.0f : 0.0f,
                       runtime->reader_state.catalog_open ?
                       UI_CATALOG_ANIMATION_SPEED :
                       UI_CATALOG_CLOSE_ANIMATION_SPEED,
                       frame->dt_seconds);
    runtime->motion_state.catalog_animating_active =
        fabsf(runtime->motion_state.catalog_progress -
              (runtime->reader_state.catalog_open ? 1.0f : 0.0f)) >= 0.001f;
    if (!runtime->motion_state.catalog_animating_active) {
        runtime->motion_state.catalog_progress =
            runtime->reader_state.catalog_open ? 1.0f : 0.0f;
    }
    runtime->motion_state.settings_progress =
        ui_motion_step(runtime->motion_state.settings_progress,
                       (runtime->view == VIEW_SETTINGS &&
                        runtime->settings_state.open) ? 1.0f : 0.0f,
                       (runtime->view == VIEW_SETTINGS &&
                        runtime->settings_state.open) ?
                       UI_CATALOG_ANIMATION_SPEED :
                       UI_CATALOG_CLOSE_ANIMATION_SPEED,
                       frame->dt_seconds);
    runtime->motion_state.settings_animating_active =
        fabsf(runtime->motion_state.settings_progress -
              ((runtime->view == VIEW_SETTINGS &&
                runtime->settings_state.open) ? 1.0f : 0.0f)) >= 0.001f;
    if (!runtime->motion_state.settings_animating_active) {
        runtime->motion_state.settings_progress =
            (runtime->view == VIEW_SETTINGS &&
             runtime->settings_state.open) ? 1.0f : 0.0f;
    }
    if (ui_should_begin_input_transition_after_frame(
            frame->frame_view_before_updates, runtime->view,
            frame->catalog_open_before_updates,
            &runtime->reader_state,
            frame->frame_reader_target_before)) {
        runtime->motion_state.view_fade_active = 1;
        runtime->motion_state.view_fade_start_tick = SDL_GetTicks();
        runtime->motion_state.view_fade_duration_ms = UI_VIEW_FADE_DURATION_MS;
        ui_start_input_transition(&frame->transition_input);
    }

    if (runtime->view == VIEW_SETTINGS &&
        !runtime->settings_state.open &&
        !runtime->motion_state.settings_animating_active) {
        if (ui_settings_flow_finish_close(&runtime->settings_state,
                                          &runtime->view,
                                          &runtime->selected) == 0) {
            ui_start_input_transition(&frame->transition_input);
            frame->render_requested = 1;
        }
    }

    if (fabsf(runtime->motion_state.shelf_selected_visual -
              frame->shelf_selected_visual_before_updates) >= 0.001f ||
        fabsf(runtime->motion_state.catalog_selected_visual -
              frame->catalog_selected_visual_before_updates) >= 0.001f ||
        fabsf(runtime->motion_state.catalog_progress -
              frame->catalog_progress_before_updates) >= 0.001f ||
        fabsf(runtime->motion_state.settings_progress -
              frame->settings_progress_before_updates) >= 0.001f) {
        frame->render_requested = 1;
    }

    if (runtime->view == VIEW_LOGIN) {
        (void)ui_startup_login_finish_login_start(
            ctx, &runtime->login_start, &runtime->login_thread,
            &runtime->login_poll, &runtime->login_poll_thread_handle,
            &runtime->session, &runtime->last_poll, &runtime->login_active,
            runtime->status, sizeof(runtime->status),
            &frame->render_requested);
    }

    if (runtime->view == VIEW_LOGIN && runtime->login_active) {
        int login_result =
            ui_startup_login_poll_login(&runtime->login_poll,
                                        &runtime->login_poll_thread_handle,
                                        &runtime->last_poll,
                                        runtime->status,
                                        sizeof(runtime->status),
                                        &frame->render_requested);

        if (login_result == 1) {
            ui_runtime_reload_shelf_after_login(runtime, ctx);
        } else if (login_result < 0) {
            runtime->login_active = 0;
        }
    }
}

#endif
