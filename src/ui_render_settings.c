#include "ui_render_internal.h"

#if HAVE_SDL

#include <math.h>
#include <stdio.h>

const char *ui_rotation_label(UiRotation rotation) {
    switch (rotation) {
    case UI_ROTATE_RIGHT_PORTRAIT:
        return "Portrait Right";
    case UI_ROTATE_LEFT_PORTRAIT:
        return "Portrait Left";
    case UI_ROTATE_LANDSCAPE:
    default:
        return "Landscape";
    }
}

int ui_settings_effective_font_size(const SettingsFlowState *settings_state,
                                    const ReaderViewState *reader_state,
                                    int preferred_reader_font_size) {
    if (settings_state &&
        settings_state->origin == UI_SETTINGS_ORIGIN_READER &&
        reader_state &&
        reader_state->content_font_size > 0) {
        return reader_state->content_font_size;
    }
    if (preferred_reader_font_size > 0) {
        return preferred_reader_font_size;
    }
    return UI_READER_CONTENT_FONT_SIZE;
}

static void ui_settings_value_text(char *out, size_t out_size, UiSettingsItem item,
                                   const SettingsFlowState *settings_state,
                                   const ReaderViewState *reader_state,
                                   int preferred_reader_font_size, int brightness_level,
                                   UiRotation rotation) {
    if (!out || out_size == 0) {
        return;
    }

    switch (item) {
    case UI_SETTINGS_ITEM_READER_FONT_SIZE:
        snprintf(out, out_size, "%d", ui_settings_effective_font_size(settings_state,
                                                                      reader_state,
                                                                      preferred_reader_font_size));
        break;
    case UI_SETTINGS_ITEM_DARK_MODE:
        snprintf(out, out_size, "%s", ui_dark_mode_enabled() ? "On" : "Off");
        break;
    case UI_SETTINGS_ITEM_BRIGHTNESS:
        if (brightness_level < UI_BRIGHTNESS_MIN ||
            brightness_level > UI_BRIGHTNESS_MAX) {
            brightness_level = UI_BRIGHTNESS_DEFAULT;
        }
        snprintf(out, out_size, "%d/10", brightness_level);
        break;
    case UI_SETTINGS_ITEM_ROTATION:
        snprintf(out, out_size, "%s", ui_rotation_label(rotation));
        break;
    case UI_SETTINGS_ITEM_LOGOUT:
    default:
        out[0] = '\0';
        break;
    }
}

void render_settings(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
                     const SettingsFlowState *settings_state,
                     const UiUserProfile *user_profile,
                     const ReaderViewState *reader_state,
                     int preferred_reader_font_size, int brightness_level,
                     UiRotation rotation, const char *status,
                     const char *battery_text, const UiLayout *layout,
                     float progress) {
    const UiTheme *theme = ui_current_theme();
    SDL_Color ink = theme->ink;
    SDL_Color muted = theme->muted;
    SDL_Color line = theme->line;
    const UiSettingsItemSpec *items = NULL;
    int item_count = UI_SETTINGS_ITEM_COUNT;
    int canvas_w = layout ? layout->canvas_w : UI_CANVAS_WIDTH;
    int canvas_h = layout ? layout->canvas_h : UI_CANVAS_HEIGHT;
    int cw = layout ? layout->content_w : canvas_w;
    int cx = layout ? layout->content_x : 0;
    int panel_w = cw < 760 ? cw : 760;
    SDL_Rect backdrop = { 0, 0, canvas_w, canvas_h };
    SDL_Rect panel = { cx + cw - panel_w, 0, panel_w, canvas_h };
    SDL_Rect header = { panel.x, 0, panel_w, 84 };
    SDL_Rect profile_card;
    SDL_Rect avatar_rect;
    SDL_Rect status_bar;
    int title_y;
    int status_h = body_font ? TTF_FontHeight(body_font) : 28;
    int list_top;
    int list_bottom;
    int list_gap = 12;
    int row_h;
    char status_buf[96];
    float eased;
    int panel_offset;
    Uint8 backdrop_alpha;
    Uint8 panel_alpha;

    if (progress <= 0.0f) {
        return;
    }
    eased = ui_ease_out_cubic(progress);
    if (panel.x < cx) {
        panel.x = cx;
        panel.w = cw;
        header.x = panel.x;
        header.w = panel.w;
    }
    panel_offset = (int)lroundf((1.0f - eased) * 72.0f);
    panel.x += panel_offset;
    header.x = panel.x;
    profile_card.x = panel.x + 16;
    profile_card.y = header.y + header.h + 14;
    profile_card.w = panel.w - 32;
    profile_card.h = 108;
    avatar_rect = (SDL_Rect){ profile_card.x + 16, profile_card.y + 16, 76, 76 };
    status_bar.x = panel.x;
    status_bar.w = panel.w;
    status_bar.h = 72;
    status_bar.y = panel.y + panel.h - status_bar.h;
    title_y = header.y + 14;

    backdrop_alpha = (Uint8)(theme->backdrop_a * eased);
    panel_alpha = (Uint8)(255.0f * (0.82f + 0.18f * eased));
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, theme->backdrop_r, theme->backdrop_g,
                           theme->backdrop_b, backdrop_alpha);
    SDL_RenderFillRect(renderer, &backdrop);
    SDL_SetRenderDrawColor(renderer, theme->catalog_panel_r, theme->catalog_panel_g,
                           theme->catalog_panel_b, panel_alpha);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, theme->catalog_header_r, theme->catalog_header_g,
                           theme->catalog_header_b, panel_alpha);
    SDL_RenderFillRect(renderer, &header);
    SDL_SetRenderDrawColor(renderer, theme->card_r, theme->card_g, theme->card_b, panel_alpha);
    SDL_RenderFillRect(renderer, &profile_card);
    SDL_RenderFillRect(renderer, &status_bar);
    draw_rect_outline(renderer, &panel, line, 1);
    draw_rect_outline(renderer, &profile_card, line, 1);

    draw_text(renderer, title_font, header.x + 24, title_y, ink, "Settings");
    render_header_status(renderer, body_font, battery_text, layout);

    {
        char name_buf[128];
        int avatar_text_y;
        int title_text_y;
        int avatar_font_h = body_font ? TTF_FontHeight(body_font) : 28;
        int name_max_w = profile_card.w - 144;

        SDL_SetRenderDrawColor(renderer, theme->qr_slot_r, theme->qr_slot_g,
                               theme->qr_slot_b, 255);
        SDL_RenderFillRect(renderer, &avatar_rect);
        draw_rect_outline(renderer, &avatar_rect, line, 1);
        if (user_profile && user_profile->avatar_texture) {
            ui_draw_texture_cover(renderer, user_profile->avatar_texture,
                                  user_profile->avatar_w, user_profile->avatar_h,
                                  &avatar_rect);
        } else {
            char fallback_buf[8];
            SDL_Rect glyph_rect = {
                avatar_rect.x + 10,
                avatar_rect.y + 10,
                avatar_rect.w - 20,
                avatar_rect.h - 20
            };

            ui_user_profile_placeholder_text(user_profile, fallback_buf, sizeof(fallback_buf));
            SDL_SetRenderDrawColor(renderer, theme->card_r, theme->card_g, theme->card_b, 255);
            SDL_RenderFillRect(renderer, &glyph_rect);
            avatar_text_y = avatar_rect.y + (avatar_rect.h - avatar_font_h) / 2;
            draw_text(renderer, body_font, avatar_rect.x + 22, avatar_text_y, ink, fallback_buf);
        }

        fit_text_ellipsis(body_font,
                          user_profile && user_profile->nickname[0] ?
                          user_profile->nickname :
                          "\xE5\xBE\xAE\xE4\xBF\xA1\xE8\xAF\xBB\xE4\xB9\xA6\xE7\x94\xA8\xE6\x88\xB7",
                          name_max_w, name_buf, sizeof(name_buf));
        title_text_y = profile_card.y + (profile_card.h - avatar_font_h) / 2;
        draw_text(renderer, body_font, avatar_rect.x + avatar_rect.w + 20, title_text_y,
                  ink, name_buf);
    }

    list_top = profile_card.y + profile_card.h + 18;
    list_bottom = status_bar.y - 16;
    row_h = (list_bottom - list_top - list_gap * (item_count - 1)) / item_count;
    if (row_h > 72) {
        row_h = 72;
    }
    if (row_h < 54) {
        row_h = 54;
    }

    items = ui_settings_flow_items(NULL);
    for (int i = 0; items && i < item_count; i++) {
        SDL_Rect row = {
            panel.x + 16,
            list_top + i * (row_h + list_gap),
            panel.w - 32,
            row_h
        };
        char value_buf[64];
        char title_buf[64];
        char value_fit_buf[64];
        int value_w = 0;
        int font_h = body_font ? TTF_FontHeight(body_font) : 28;
        int text_y = row.y + (row.h - font_h) / 2;
        int value_max_w = row.w / 3;
        int title_max_w;

        if (settings_state && settings_state->selected == i) {
            SDL_SetRenderDrawColor(renderer, theme->catalog_current_r,
                                   theme->catalog_current_g,
                                   theme->catalog_current_b, 255);
            SDL_RenderFillRect(renderer, &row);
        }
        draw_rect_outline(renderer, &row, line, 1);
        ui_settings_value_text(value_buf, sizeof(value_buf), items[i].item,
                               settings_state, reader_state,
                               preferred_reader_font_size, brightness_level, rotation);
        if (value_buf[0]) {
            fit_text_ellipsis(body_font, value_buf, value_max_w, value_fit_buf,
                              sizeof(value_fit_buf));
            TTF_SizeUTF8(body_font, value_fit_buf, &value_w, NULL);
        } else {
            value_fit_buf[0] = '\0';
        }
        title_max_w = row.w - 32 - (value_w > 0 ? value_w + 20 : 0);
        fit_text_ellipsis(body_font, items[i].title, title_max_w, title_buf, sizeof(title_buf));
        draw_text(renderer, body_font, row.x + 16, text_y, ink, title_buf);
        if (value_buf[0]) {
            draw_text(renderer, body_font, row.x + row.w - 16 - value_w, text_y,
                      settings_state && settings_state->selected == i ? ink : muted,
                      value_fit_buf);
        }
    }

    fit_text_ellipsis(body_font,
                      status && status[0] ? status : "Left/Right adjust, A next, B back",
                      status_bar.w - 48, status_buf, sizeof(status_buf));
    draw_text(renderer, body_font, status_bar.x + 24,
              status_bar.y + (status_bar.h - status_h) / 2, muted, status_buf);
}

#endif
