#include "ui_runtime_internal.h"

#if HAVE_SDL

#include <string.h>
#include <time.h>

static void ui_runtime_begin_frame(UiRuntime *runtime, UiRuntimeFrame *frame) {
    if (!runtime || !frame) {
        return;
    }

    memset(frame, 0, sizeof(*frame));
    frame->frame_now = SDL_GetTicks();
    frame->wall_now = time(NULL);
    frame->dt_seconds = runtime->motion_state.last_tick > 0 &&
        frame->frame_now > runtime->motion_state.last_tick ?
        (float)(frame->frame_now - runtime->motion_state.last_tick) / 1000.0f :
        0.0f;
    if (frame->dt_seconds > (float)UI_MOTION_DT_CAP_MS / 1000.0f) {
        frame->dt_seconds = (float)UI_MOTION_DT_CAP_MS / 1000.0f;
    }
    frame->frame_view_before_updates = runtime->view;
    frame->catalog_open_before_updates = runtime->reader_state.catalog_open;
    frame->shelf_selected_visual_before_updates =
        runtime->motion_state.shelf_selected_visual;
    frame->catalog_selected_visual_before_updates =
        runtime->motion_state.catalog_selected_visual;
    frame->catalog_progress_before_updates =
        runtime->motion_state.catalog_progress;
    frame->settings_progress_before_updates =
        runtime->motion_state.settings_progress;
    frame->current_clock_minute = frame->wall_now / 60;
    frame->transition_input =
        ui_make_input_transition_context(
            &runtime->repeat_state, &runtime->input_suppression,
            &runtime->input_state, runtime->tg5040_input,
            runtime->joysticks, runtime->joystick_count,
            &runtime->exit_confirm_until,
            &runtime->reader_exit_confirm_until);

    ui_copy_string(frame->frame_reader_target_before,
                   sizeof(frame->frame_reader_target_before),
                   runtime->reader_state.doc.target ?
                   runtime->reader_state.doc.target : "");
    runtime->motion_state.last_tick = frame->frame_now;
    if (runtime->last_clock_minute == 0) {
        runtime->last_clock_minute = frame->current_clock_minute;
    } else if (frame->current_clock_minute != runtime->last_clock_minute) {
        runtime->last_clock_minute = frame->current_clock_minute;
        frame->render_requested = 1;
    }

    ui_battery_state_update(&runtime->battery_state, frame->frame_now);
    ui_platform_haptic_poll(&runtime->haptic_state, frame->frame_now);
    ui_input_suppression_refresh(
        &runtime->input_suppression,
        ui_input_current_mask(runtime->tg5040_input, runtime->joysticks,
                              runtime->joystick_count));
}

int ui_runtime_exec(ApiContext *ctx, const char *font_path, const char *platform) {
    UiRuntime runtime;

    if (ui_runtime_boot(&runtime, ctx, font_path, platform) != 0) {
        ui_runtime_shutdown(&runtime, ctx);
        return runtime.rc;
    }

    while (runtime.running) {
        UiRuntimeFrame frame;
        ui_runtime_begin_frame(&runtime, &frame);
        ui_runtime_process_events(&runtime, ctx, &frame);
        ui_runtime_tick_after_input(&runtime, ctx, &frame);
        ui_runtime_render_frame(&runtime, ctx, &frame);
        ui_runtime_wait_for_next_frame(&runtime, frame.frame_now);
    }

    runtime.rc = 0;
    ui_runtime_shutdown(&runtime, ctx);
    return runtime.rc;
}

#endif
