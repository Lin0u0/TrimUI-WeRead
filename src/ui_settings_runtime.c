#include "ui_internal.h"

#if HAVE_SDL

#include <stdio.h>
#include <string.h>
#include "preferences_state.h"

UiLayout ui_layout_for_rotation(UiRotation rotation) {
    UiLayout layout;
    int is_portrait = rotation == UI_ROTATE_LEFT_PORTRAIT ||
                      rotation == UI_ROTATE_RIGHT_PORTRAIT;

    layout.canvas_w = is_portrait ? UI_CANVAS_PORTRAIT_WIDTH : UI_CANVAS_WIDTH;
    layout.canvas_h = is_portrait ? UI_CANVAS_PORTRAIT_HEIGHT : UI_CANVAS_HEIGHT;
    layout.content_x = 0;
    layout.content_w = layout.canvas_w;
    layout.reader_content_w = layout.content_w - 64;
    layout.reader_content_h = layout.canvas_h - 128;
    if (layout.content_w < 320) {
        layout.content_w = 320;
        layout.content_x = (layout.canvas_w - layout.content_w) / 2;
    }
    if (layout.reader_content_w < 320) {
        layout.reader_content_w = 320;
    }
    if (layout.reader_content_h < 240) {
        layout.reader_content_h = 240;
    }
    return layout;
}

static UiRotation ui_rotation_next(UiRotation rotation) {
    switch (rotation) {
    case UI_ROTATE_LEFT_PORTRAIT:
        return UI_ROTATE_LANDSCAPE;
    case UI_ROTATE_LANDSCAPE:
        return UI_ROTATE_RIGHT_PORTRAIT;
    case UI_ROTATE_RIGHT_PORTRAIT:
    default:
        return UI_ROTATE_LEFT_PORTRAIT;
    }
}

static int ui_rotation_prev(UiRotation rotation) {
    switch (rotation) {
    case UI_ROTATE_RIGHT_PORTRAIT:
        return UI_ROTATE_LANDSCAPE;
    case UI_ROTATE_LANDSCAPE:
        return UI_ROTATE_LEFT_PORTRAIT;
    case UI_ROTATE_LEFT_PORTRAIT:
    default:
        return UI_ROTATE_RIGHT_PORTRAIT;
    }
}

static int ui_reader_font_size_step(int current, int delta) {
    static const int sizes[] = { 24, 28, 32, 36, 40, 44 };
    int index = 3;
    int i;

    if (delta == 0) {
        delta = 1;
    }
    for (i = 0; i < (int)(sizeof(sizes) / sizeof(sizes[0])); i++) {
        if (sizes[i] == current) {
            index = i;
            break;
        }
    }
    index += delta > 0 ? 1 : -1;
    if (index < 0) {
        index = (int)(sizeof(sizes) / sizeof(sizes[0])) - 1;
    } else if (index >= (int)(sizeof(sizes) / sizeof(sizes[0]))) {
        index = 0;
    }
    return sizes[index];
}

void ui_settings_clear_logout_confirm(SettingsFlowState *settings_state) {
    if (!settings_state) {
        return;
    }
    settings_state->logout_confirm_armed = 0;
}

const char *ui_logout_status_text(SessionLogoutOutcome outcome) {
    switch (outcome) {
    case SESSION_LOGOUT_SUCCESS:
        return "Logged out locally and remotely";
    case SESSION_LOGOUT_REMOTE_FAILED:
        return "Logged out locally, but remote logout failed";
    case SESSION_LOGOUT_LOCAL_FAILED:
    default:
        return "Local logout failed, still logged in locally";
    }
}

void ui_transition_to_login_required(UiView *view, AuthSession *session, int *login_active,
                                     SettingsFlowState *settings_state,
                                     ReaderViewState *reader_state, cJSON **shelf_nuxt,
                                     ShelfCoverCache *shelf_covers,
                                     ShelfCoverDownloadState *cover_download_state,
                                     SDL_Thread **cover_download_thread_handle,
                                     SDL_Texture **qr_texture, int *selected,
                                     Uint32 *exit_confirm_until,
                                     Uint32 *reader_exit_confirm_until,
                                     char *status, size_t status_size,
                                     const char *message) {
    if (cover_download_state && cover_download_thread_handle) {
        ui_shelf_flow_cover_download_stop(cover_download_state, cover_download_thread_handle);
    }
    if (shelf_covers) {
        shelf_cover_cache_reset(shelf_covers);
    }
    if (shelf_nuxt && *shelf_nuxt) {
        cJSON_Delete(*shelf_nuxt);
        *shelf_nuxt = NULL;
    }
    if (reader_state) {
        ui_reader_view_free(reader_state);
    }
    if (settings_state) {
        ui_settings_flow_state_reset(settings_state);
    }
    if (session) {
        memset(session, 0, sizeof(*session));
    }
    if (login_active) {
        *login_active = 0;
    }
    if (selected) {
        *selected = 0;
    }
    if (exit_confirm_until) {
        *exit_confirm_until = 0;
    }
    if (reader_exit_confirm_until) {
        *reader_exit_confirm_until = 0;
    }
    if (qr_texture && *qr_texture) {
        SDL_DestroyTexture(*qr_texture);
        *qr_texture = NULL;
    }
    if (status && status_size > 0) {
        snprintf(status, status_size, "%s",
                 message ? message : "Press A to generate QR code");
    }
    if (view) {
        *view = VIEW_LOGIN;
    }
}

int ui_settings_apply(SettingsFlowState *settings_state, ApiContext *ctx,
                      SDL_Renderer *renderer, TTF_Font *body_font,
                      ReaderViewState *reader_state,
                      int *preferred_reader_font_size,
                      int tg5040_input, int *brightness_level, UiRotation *rotation,
                      UiLayout *current_layout, SDL_Texture **scene_texture,
                      char *status, size_t status_size, int direction) {
    if (!settings_state || !ctx || !status || status_size == 0) {
        return 0;
    }

    switch ((UiSettingsItem)settings_state->selected) {
    case UI_SETTINGS_ITEM_READER_FONT_SIZE: {
        int next_font_size = ui_reader_font_size_step(
            ui_settings_effective_font_size(settings_state, reader_state,
                                            preferred_reader_font_size ?
                                            *preferred_reader_font_size :
                                            UI_READER_CONTENT_FONT_SIZE),
            direction);

        if (preferred_reader_font_size) {
            *preferred_reader_font_size = next_font_size;
        }
        preferences_state_save_reader_font_size(ctx, next_font_size);
        if (settings_state->origin == UI_SETTINGS_ORIGIN_READER && reader_state) {
            reader_state->content_font_size = next_font_size;
            if (ui_reader_view_reset_content_font(body_font, reader_state) != 0) {
                snprintf(status, status_size, "Unable to apply font size");
                return 0;
            }
            ui_reader_view_rewrap(body_font, current_layout->reader_content_w,
                                  current_layout->reader_content_h, reader_state);
            ui_reader_view_save_local_position(ctx, reader_state);
        }
        snprintf(status, status_size, "Font size %d", next_font_size);
        return 1;
    }
    case UI_SETTINGS_ITEM_DARK_MODE:
        ui_dark_mode_set(!ui_dark_mode_enabled());
        preferences_state_save_dark_mode(ctx, ui_dark_mode_enabled());
        snprintf(status, status_size, "Dark mode %s",
                 ui_dark_mode_enabled() ? "on" : "off");
        return 1;
    case UI_SETTINGS_ITEM_BRIGHTNESS:
        if (!brightness_level) {
            return 0;
        }
        if (ui_platform_step_brightness(ctx, tg5040_input, direction < 0 ? -1 : 1,
                                        brightness_level) != 0) {
            snprintf(status, status_size, "Unable to adjust brightness");
            return 0;
        }
        snprintf(status, status_size, "Brightness %d/10", *brightness_level);
        return 1;
    case UI_SETTINGS_ITEM_ROTATION: {
        UiRotation next_rotation = direction < 0 ?
            (UiRotation)ui_rotation_prev(*rotation) :
            ui_rotation_next(*rotation);

        *rotation = next_rotation;
        *current_layout = ui_layout_for_rotation(*rotation);
        if (ui_recreate_scene_texture(renderer, scene_texture, current_layout) != 0) {
            snprintf(status, status_size, "Unable to rotate view");
            return -1;
        }
        if (settings_state->origin == UI_SETTINGS_ORIGIN_READER && reader_state) {
            ui_reader_view_rewrap(body_font, current_layout->reader_content_w,
                                  current_layout->reader_content_h, reader_state);
        }
        preferences_state_save_rotation(ctx, *rotation);
        snprintf(status, status_size, "%s", ui_rotation_label(*rotation));
        return 1;
    }
    case UI_SETTINGS_ITEM_LOGOUT:
    default:
        return 0;
    }
}

#endif
