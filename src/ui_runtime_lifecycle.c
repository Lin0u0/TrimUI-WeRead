#include "ui_runtime_internal.h"

#if HAVE_SDL

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "preferences_state.h"
#include "shelf.h"
#include "shelf_state.h"

static void ui_runtime_reset(UiRuntime *runtime, ApiContext *ctx, const char *platform) {
    if (!runtime || !ctx) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->view = VIEW_SHELF;
    runtime->running = 1;
    runtime->brightness_level = -1;
    runtime->rotation = UI_ROTATE_LANDSCAPE;
    runtime->current_layout = ui_layout_for_rotation(UI_ROTATE_LANDSCAPE);
    runtime->preferred_reader_font_size = UI_READER_CONTENT_FONT_SIZE;
    runtime->rc = -1;

    ui_shelf_flow_cover_download_state_reset(&runtime->shelf_cover_download);
    ui_input_suppression_reset(&runtime->input_suppression);
    ui_settings_flow_state_reset(&runtime->settings_state);

    snprintf(runtime->loading_title, sizeof(runtime->loading_title), "%s",
             "\xE5\xBE\xAE\xE4\xBF\xA1\xE8\xAF\xBB\xE4\xB9\xA6");
    snprintf(runtime->qr_path, sizeof(runtime->qr_path), "%s/weread-login-qr.png",
             ctx->data_dir);
    runtime->tg5040_input = ui_is_tg5040_platform(platform);
}

int ui_runtime_boot(UiRuntime *runtime, ApiContext *ctx,
                    const char *font_path, const char *platform) {
    int saved_rotation = 0;

    if (!runtime || !ctx) {
        return -1;
    }

    ui_runtime_reset(runtime, ctx, platform);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_JoystickEventState(SDL_ENABLE);
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return -1;
    }
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
        return -1;
    }

    runtime->window = SDL_CreateWindow("WeRead", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       1024, 768, SDL_WINDOW_FULLSCREEN_DESKTOP);
    runtime->renderer = SDL_CreateRenderer(runtime->window, -1,
                                           SDL_RENDERER_ACCELERATED |
                                           SDL_RENDERER_PRESENTVSYNC |
                                           SDL_RENDERER_TARGETTEXTURE);
    if (runtime->window && !runtime->renderer) {
        runtime->renderer = SDL_CreateRenderer(runtime->window, -1,
                                               SDL_RENDERER_SOFTWARE |
                                               SDL_RENDERER_TARGETTEXTURE);
    }
    if (!runtime->window || !runtime->renderer) {
        fprintf(stderr, "SDL window setup failed: %s\n", SDL_GetError());
        return -1;
    }
    if (ui_recreate_scene_texture(runtime->renderer, &runtime->scene_texture,
                                  &runtime->current_layout) != 0) {
        fprintf(stderr, "Failed to create scene texture: %s\n", SDL_GetError());
        return -1;
    }

    runtime->joystick_count = SDL_NumJoysticks();
    if (runtime->joystick_count > 0) {
        runtime->joysticks = calloc((size_t)runtime->joystick_count, sizeof(*runtime->joysticks));
        if (!runtime->joysticks) {
            fprintf(stderr, "Failed to allocate joystick handles.\n");
            return -1;
        }
        for (int i = 0; i < runtime->joystick_count; i++) {
            runtime->joysticks[i] = SDL_JoystickOpen(i);
        }
    }
    ui_input_state_reset(
        &runtime->input_state,
        ui_input_current_mask(runtime->tg5040_input, runtime->joysticks,
                              runtime->joystick_count),
        &runtime->input_suppression);

    if (font_path && *font_path) {
        runtime->title_font = TTF_OpenFont(font_path, UI_TITLE_FONT_SIZE);
        runtime->body_font = TTF_OpenFont(font_path, UI_BODY_FONT_SIZE);
    }
    if (!runtime->title_font || !runtime->body_font) {
        fprintf(stderr, "Failed to open font: %s\n", font_path ? font_path : "(null)");
        return -1;
    }
    if (font_path && *font_path) {
        snprintf(runtime->reader_state.fallback_font_path,
                 sizeof(runtime->reader_state.fallback_font_path), "%s", font_path);
    }
    (void)ui_platform_init_haptics(runtime->tg5040_input, &runtime->haptic_state);

    ui_dark_mode_set(preferences_state_load_dark_mode(ctx));
    if (preferences_state_load_brightness_level(ctx, &runtime->brightness_level) == 0) {
        ui_platform_apply_brightness_level(runtime->tg5040_input, runtime->brightness_level);
    }
    (void)preferences_state_load_reader_font_size(ctx, &runtime->preferred_reader_font_size);
    if (preferences_state_load_rotation(ctx, &saved_rotation) == 0) {
        runtime->rotation = (UiRotation)saved_rotation;
        runtime->current_layout = ui_layout_for_rotation(runtime->rotation);
        if (ui_recreate_scene_texture(runtime->renderer, &runtime->scene_texture,
                                      &runtime->current_layout) != 0) {
            fprintf(stderr, "Failed to apply saved rotation: %s\n", SDL_GetError());
            return -1;
        }
    }

    runtime->shelf_nuxt = shelf_state_load_cache(ctx);
    ui_user_profile_sync(&runtime->user_profile, runtime->shelf_nuxt, ctx->data_dir);
    if (runtime->shelf_nuxt &&
        shelf_books(runtime->shelf_nuxt) &&
        cJSON_IsArray(shelf_books(runtime->shelf_nuxt))) {
        runtime->shelf_status[0] = '\0';
        ui_shelf_flow_cover_download_stop(&runtime->shelf_cover_download,
                                          &runtime->shelf_cover_download_thread_handle);
        shelf_cover_cache_build(ctx, runtime->shelf_nuxt, &runtime->shelf_covers);
        runtime->selected = shelf_ui_clamp_selection(
            runtime->shelf_nuxt, shelf_ui_default_selection(runtime->shelf_nuxt));
        runtime->motion_state.shelf_selected_visual = (float)runtime->selected;
        runtime->motion_state.shelf_initialized = 1;
        runtime->view = VIEW_SHELF;
    } else {
        cJSON_Delete(runtime->shelf_nuxt);
        runtime->shelf_nuxt = NULL;
        runtime->view = VIEW_BOOTSTRAP;
        snprintf(runtime->status, sizeof(runtime->status),
                 "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\xA3\x80\xE6\x9F\xA5\xE4\xB9\xA6\xE6\x9E\xB6...");
    }
    ui_startup_login_begin_startup_refresh(ctx, &runtime->startup_state,
                                           &runtime->startup_thread_handle);
    return 0;
}

void ui_runtime_shutdown(UiRuntime *runtime, ApiContext *ctx) {
    if (!runtime || !ctx) {
        return;
    }

    ui_platform_shutdown_haptics(&runtime->haptic_state);
    if (runtime->view == VIEW_READER) {
        ui_reader_view_flush_progress_blocking(ctx, &runtime->reader_state, 1);
        ui_reader_view_save_local_position(ctx, &runtime->reader_state);
    }
    ui_startup_login_shutdown(&runtime->login_poll, &runtime->login_thread,
                              &runtime->startup_thread_handle,
                              &runtime->login_poll_thread_handle);
    ui_shelf_flow_cover_download_stop(&runtime->shelf_cover_download,
                                      &runtime->shelf_cover_download_thread_handle);
    ui_reader_flow_shutdown(&runtime->reader_open, &runtime->reader_open_thread_handle,
                            &runtime->progress_report,
                            &runtime->progress_report_thread_handle,
                            &runtime->chapter_prefetch_cache,
                            &runtime->catalog_hydration,
                            &runtime->catalog_hydration_thread_handle);
    ui_reader_view_free(&runtime->reader_state);
    ui_startup_login_startup_state_reset(&runtime->startup_state);
    shelf_cover_cache_reset(&runtime->shelf_covers);
    ui_user_profile_clear(&runtime->user_profile);
    cJSON_Delete(runtime->shelf_nuxt);
    if (runtime->body_font) {
        TTF_CloseFont(runtime->body_font);
    }
    if (runtime->title_font) {
        TTF_CloseFont(runtime->title_font);
    }
    if (runtime->qr_texture) {
        SDL_DestroyTexture(runtime->qr_texture);
    }
    if (runtime->scene_texture) {
        SDL_DestroyTexture(runtime->scene_texture);
    }
    if (runtime->renderer) {
        SDL_DestroyRenderer(runtime->renderer);
    }
    if (runtime->window) {
        SDL_DestroyWindow(runtime->window);
    }
    if (runtime->joysticks) {
        for (int i = 0; i < runtime->joystick_count; i++) {
            if (runtime->joysticks[i]) {
                SDL_JoystickClose(runtime->joysticks[i]);
            }
        }
    }
    free(runtime->joysticks);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
}

#endif
