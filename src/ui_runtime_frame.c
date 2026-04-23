#include "ui_runtime_internal.h"

#if HAVE_SDL

#include <math.h>
#include <time.h>

void ui_runtime_render_frame(UiRuntime *runtime, ApiContext *ctx,
                             const UiRuntimeFrame *frame) {
    Uint32 now;
    int toast_visible;
    int catalog_animating;
    int catalog_selection_animating;
    int should_render = 1;

    if (!runtime || !ctx || !frame) {
        return;
    }

    now = SDL_GetTicks();
    toast_visible = runtime->poor_network_toast_until > now;
    catalog_animating = runtime->motion_state.catalog_animating_active;
    catalog_selection_animating =
        runtime->motion_state.catalog_selection_animating_active;

    if (runtime->view == VIEW_READER &&
        !frame->reader_input_seen &&
        !frame->render_requested &&
        !runtime->motion_state.view_fade_active &&
        !catalog_animating &&
        !catalog_selection_animating &&
        !toast_visible) {
        should_render = 0;
    }
    if (runtime->view == VIEW_SHELF &&
        !frame->render_requested &&
        !runtime->motion_state.view_fade_active &&
        fabsf(runtime->motion_state.shelf_selected_visual -
              (float)runtime->selected) < 0.001f &&
        !toast_visible &&
        runtime->exit_confirm_until <= now) {
        should_render = 0;
    }

    if (!should_render) {
        return;
    }

    SDL_SetRenderTarget(runtime->renderer, runtime->scene_texture);

    if (runtime->view == VIEW_LOGIN) {
        render_login(runtime->renderer, runtime->title_font, runtime->body_font,
                     &runtime->session, runtime->status,
                     runtime->battery_state.text, &runtime->current_layout,
                     &runtime->qr_texture, &runtime->qr_tex_w,
                     &runtime->qr_tex_h);
    } else if (runtime->view == VIEW_SETTINGS) {
        ui_user_profile_prepare_avatar_texture(ctx, runtime->renderer,
                                               &runtime->user_profile);
        if (runtime->settings_state.origin == UI_SETTINGS_ORIGIN_READER) {
            render_reader(runtime->renderer, runtime->title_font,
                          runtime->body_font, &runtime->reader_state,
                          runtime->battery_state.text,
                          &runtime->current_layout);
        } else {
            render_shelf(runtime->renderer, runtime->title_font,
                         runtime->body_font, ctx, runtime->shelf_nuxt,
                         &runtime->shelf_covers, runtime->selected,
                         runtime->motion_state.shelf_selected_visual,
                         runtime->shelf_start, runtime->shelf_status,
                         runtime->battery_state.text,
                         &runtime->current_layout);
        }
        render_settings(runtime->renderer, runtime->title_font,
                        runtime->body_font, &runtime->settings_state,
                        &runtime->user_profile, &runtime->reader_state,
                        runtime->preferred_reader_font_size,
                        runtime->brightness_level, runtime->rotation,
                        runtime->shelf_status, runtime->battery_state.text,
                        &runtime->current_layout,
                        runtime->motion_state.settings_progress);
    } else if (runtime->view == VIEW_READER) {
        render_reader(runtime->renderer, runtime->title_font,
                      runtime->body_font, &runtime->reader_state,
                      runtime->battery_state.text,
                      &runtime->current_layout);
        if (runtime->motion_state.catalog_progress > 0.001f) {
            render_catalog_overlay(runtime->renderer, runtime->title_font,
                                   runtime->body_font, &runtime->reader_state,
                                   runtime->motion_state.catalog_progress,
                                   runtime->motion_state.catalog_selected_visual,
                                   &runtime->current_layout);
        }
    } else if (runtime->view == VIEW_BOOTSTRAP ||
               runtime->view == VIEW_OPENING) {
        render_loading(runtime->renderer, runtime->title_font,
                       runtime->body_font, runtime->loading_title,
                       runtime->status, runtime->battery_state.text,
                       &runtime->current_layout);
    } else {
        render_shelf(runtime->renderer, runtime->title_font, runtime->body_font,
                     ctx, runtime->shelf_nuxt, &runtime->shelf_covers,
                     runtime->selected,
                     runtime->motion_state.shelf_selected_visual,
                     runtime->shelf_start, runtime->shelf_status,
                     runtime->battery_state.text,
                     &runtime->current_layout);
    }
    if (ctx->poor_network) {
        ctx->poor_network = 0;
        runtime->poor_network_toast_until = SDL_GetTicks() + 3000;
        toast_visible = 1;
    }
    if (toast_visible) {
        render_poor_network_toast(runtime->renderer, runtime->body_font,
                                  runtime->poor_network_toast_until,
                                  &runtime->current_layout);
    }
    if (runtime->reader_exit_confirm_until > SDL_GetTicks()) {
        render_confirm_hint(runtime->renderer, runtime->body_font,
                            runtime->reader_exit_confirm_until,
                            "\xE5\x86\x8D\xE6\xAC\xA1\xE6\x8C\x89 B \xE8\xBF\x94\xE5\x9B\x9E\xE4\xB9\xA6\xE6\x9E\xB6",
                            &runtime->current_layout);
    } else if (runtime->exit_confirm_until > SDL_GetTicks()) {
        render_confirm_hint(runtime->renderer, runtime->body_font,
                            runtime->exit_confirm_until,
                            "\xE5\x86\x8D\xE6\xAC\xA1\xE6\x8C\x89 B \xE9\x80\x80\xE5\x87\xBA",
                            &runtime->current_layout);
    }
    {
        Uint8 scene_alpha = 255;

        if (runtime->motion_state.view_fade_active) {
            Uint32 elapsed = SDL_GetTicks() -
                runtime->motion_state.view_fade_start_tick;
            float progress = runtime->motion_state.view_fade_duration_ms > 0 ?
                (float)elapsed /
                (float)runtime->motion_state.view_fade_duration_ms : 1.0f;

            if (progress >= 1.0f) {
                runtime->motion_state.view_fade_active = 0;
            } else {
                scene_alpha = ui_view_fade_alpha(progress);
            }
        }
        ui_present_scene(runtime->renderer, runtime->scene_texture,
                         runtime->rotation, scene_alpha);
    }
    SDL_RenderPresent(runtime->renderer);
}

void ui_runtime_wait_for_next_frame(UiRuntime *runtime, Uint32 frame_now) {
    Uint32 sleep_budget;
    Uint32 frame_elapsed;

    if (!runtime) {
        return;
    }

    sleep_budget = ui_frame_interval_ms(runtime->view, &runtime->motion_state,
                                        runtime->poor_network_toast_until,
                                        runtime->exit_confirm_until,
                                        SDL_GetTicks());
    frame_elapsed = SDL_GetTicks() - frame_now;

    if (frame_elapsed < sleep_budget) {
        SDL_Delay(sleep_budget - frame_elapsed);
    } else if (sleep_budget == 0) {
        Uint32 now = SDL_GetTicks();
        time_t wait_wall_now = time(NULL);
        int ms_until_next_minute =
            (int)(((wait_wall_now / 60) + 1) * 60 - wait_wall_now) * 1000;
        int background_busy =
            runtime->startup_thread_handle ||
            runtime->reader_open_thread_handle ||
            runtime->login_thread ||
            runtime->login_poll_thread_handle ||
            runtime->shelf_cover_download_thread_handle ||
            runtime->progress_report_thread_handle ||
            ui_reader_flow_chapter_prefetch_has_running_work(
                &runtime->chapter_prefetch_cache) ||
            ui_reader_flow_catalog_hydration_has_running_work(
                &runtime->catalog_hydration,
                runtime->catalog_hydration_thread_handle);
        int wait_timeout_ms = 0;

        if (runtime->motion_state.view_fade_active ||
            runtime->poor_network_toast_until > now) {
            wait_timeout_ms = 0;
        } else {
            Uint32 next_deadline = now + 5000;

            if (ms_until_next_minute > 0 &&
                now + (Uint32)ms_until_next_minute < next_deadline) {
                next_deadline = now + (Uint32)ms_until_next_minute;
            }
            if (runtime->battery_state.next_poll_tick > now &&
                runtime->battery_state.next_poll_tick < next_deadline) {
                next_deadline = runtime->battery_state.next_poll_tick;
            }
            if (runtime->view == VIEW_READER &&
                runtime->reader_state.progress_report_due_tick > now &&
                runtime->reader_state.progress_report_due_tick < next_deadline) {
                next_deadline = runtime->reader_state.progress_report_due_tick;
            }
            if (runtime->haptic_state.stop_tick > 0 &&
                runtime->haptic_state.stop_tick < next_deadline) {
                next_deadline = runtime->haptic_state.stop_tick;
            }
            if (runtime->reader_exit_confirm_until > now &&
                runtime->reader_exit_confirm_until < next_deadline) {
                next_deadline = runtime->reader_exit_confirm_until;
            }
            if (runtime->exit_confirm_until > now &&
                runtime->exit_confirm_until < next_deadline) {
                next_deadline = runtime->exit_confirm_until;
            }
            if (background_busy) {
                Uint32 bg_poll = now + 100;
                if (bg_poll < next_deadline) {
                    next_deadline = bg_poll;
                }
            }
            if (runtime->repeat_state.action != UI_INPUT_ACTION_NONE &&
                runtime->repeat_state.next_tick > now &&
                runtime->repeat_state.next_tick < next_deadline) {
                next_deadline = runtime->repeat_state.next_tick;
            }
            if (runtime->motion_state.catalog_animating_active ||
                runtime->motion_state.settings_animating_active ||
                runtime->motion_state.catalog_selection_animating_active ||
                runtime->motion_state.shelf_cover_warmup_active ||
                (runtime->motion_state.shelf_initialized &&
                 fabsf(runtime->motion_state.shelf_selected_visual -
                       (float)runtime->selected) >= 0.001f)) {
                Uint32 anim_tick = now + 16;
                if (anim_tick < next_deadline) {
                    next_deadline = anim_tick;
                }
            }

            wait_timeout_ms = (int)(next_deadline - now);
            if (wait_timeout_ms < 1) {
                wait_timeout_ms = 1;
            }
        }

        if (wait_timeout_ms > 0) {
            SDL_Event waited_event;

            if (SDL_WaitEventTimeout(&waited_event, wait_timeout_ms) == 1) {
                SDL_PushEvent(&waited_event);
            }
        }
    }
}

#endif
