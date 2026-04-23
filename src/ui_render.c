#include "ui_render_internal.h"

#if HAVE_SDL

#include <SDL_image.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const UiTheme ui_theme_light = {
    .ink    = { 30, 29, 26, 255 },
    .muted  = { 116, 106, 88, 255 },
    .accent = { 191, 155, 76, 255 },
    .line   = { 221, 210, 188, 255 },
    .dim    = { 118, 108, 92, 255 },
    .bg_r = 246, .bg_g = 242, .bg_b = 230,
    .header_r = 248, .header_g = 244, .header_b = 234,
    .card_r = 252, .card_g = 249, .card_b = 242,
    .qr_slot_r = 245, .qr_slot_g = 240, .qr_slot_b = 229,
    .cover_bg_r = 236, .cover_bg_g = 228, .cover_bg_b = 204,
    .cover_empty_r = 222, .cover_empty_g = 212, .cover_empty_b = 188,
    .shadow_r = 201, .shadow_g = 191, .shadow_b = 166, .shadow_a = 180,
    .selection_border_r = 214, .selection_border_g = 189, .selection_border_b = 121,
    .catalog_panel_r = 250, .catalog_panel_g = 246, .catalog_panel_b = 237,
    .catalog_header_r = 244, .catalog_header_g = 239, .catalog_header_b = 228,
    .catalog_highlight_r = 228, .catalog_highlight_g = 216, .catalog_highlight_b = 187,
    .catalog_current_r = 242, .catalog_current_g = 236, .catalog_current_b = 223,
    .backdrop_r = 18, .backdrop_g = 16, .backdrop_b = 12, .backdrop_a = 108,
};

static const UiTheme ui_theme_dark = {
    .ink    = { 210, 206, 197, 255 },
    .muted  = { 148, 140, 124, 255 },
    .accent = { 191, 155, 76, 255 },
    .line   = { 58, 54, 46, 255 },
    .dim    = { 120, 112, 98, 255 },
    .bg_r = 30, .bg_g = 28, .bg_b = 24,
    .header_r = 36, .header_g = 34, .header_b = 28,
    .card_r = 42, .card_g = 39, .card_b = 33,
    .qr_slot_r = 48, .qr_slot_g = 44, .qr_slot_b = 38,
    .cover_bg_r = 50, .cover_bg_g = 46, .cover_bg_b = 38,
    .cover_empty_r = 58, .cover_empty_g = 54, .cover_empty_b = 46,
    .shadow_r = 10, .shadow_g = 9, .shadow_b = 8, .shadow_a = 200,
    .selection_border_r = 180, .selection_border_g = 155, .selection_border_b = 90,
    .catalog_panel_r = 38, .catalog_panel_g = 35, .catalog_panel_b = 30,
    .catalog_header_r = 44, .catalog_header_g = 40, .catalog_header_b = 34,
    .catalog_highlight_r = 56, .catalog_highlight_g = 52, .catalog_highlight_b = 42,
    .catalog_current_r = 48, .catalog_current_g = 44, .catalog_current_b = 36,
    .backdrop_r = 0, .backdrop_g = 0, .backdrop_b = 0, .backdrop_a = 160,
};

static int ui_dark_mode = 0;

const UiTheme *ui_current_theme(void) {
    return ui_dark_mode ? &ui_theme_dark : &ui_theme_light;
}

int ui_dark_mode_enabled(void) {
    return ui_dark_mode;
}

void ui_dark_mode_set(int enabled) {
    ui_dark_mode = enabled ? 1 : 0;
}

int reader_top_inset_for_font_size(int font_size) {
    switch (font_size) {
    case 24: return 12;
    case 28: return 14;
    case 32: return 16;
    case 36: return 18;
    case 40: return 20;
    case 44: return 22;
    default:
        if (font_size <= 0) {
            font_size = UI_READER_CONTENT_FONT_SIZE;
        }
        return font_size / 2;
    }
}

float ui_clamp01f(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float ui_ease_out_cubic(float value) {
    float t = ui_clamp01f(value);
    float inv = 1.0f - t;

    return 1.0f - inv * inv * inv;
}

float ui_ease_in_out_cubic(float value) {
    float t = ui_clamp01f(value);

    if (t < 0.5f) {
        return 4.0f * t * t * t;
    }
    t = -2.0f * t + 2.0f;
    return 1.0f - (t * t * t) / 2.0f;
}

Uint8 ui_view_fade_alpha(float progress) {
    float eased;

    if (ui_dark_mode) {
        eased = ui_ease_out_cubic(progress);
        return (Uint8)(112.0f + eased * 143.0f);
    }

    eased = ui_ease_in_out_cubic(progress);
    return (Uint8)(228.0f + eased * 27.0f);
}

int ui_present_scene(SDL_Renderer *renderer, SDL_Texture *scene, UiRotation rotation,
                     Uint8 alpha) {
    int output_w, output_h;
    SDL_Rect dst;
    SDL_Point center;

    if (!renderer || !scene) {
        return -1;
    }

    SDL_SetRenderTarget(renderer, NULL);
    SDL_GetRendererOutputSize(renderer, &output_w, &output_h);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetTextureBlendMode(scene, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(scene, alpha);

    if (rotation == UI_ROTATE_LANDSCAPE) {
        return SDL_RenderCopy(renderer, scene, NULL, NULL);
    }

    dst.x = 0;
    dst.y = 0;
    dst.w = output_h;
    dst.h = output_w;

    if (rotation == UI_ROTATE_RIGHT_PORTRAIT) {
        center.x = output_w / 2;
        center.y = output_w / 2;
        return SDL_RenderCopyEx(renderer, scene, NULL, &dst, 90.0, &center, SDL_FLIP_NONE);
    }

    center.x = output_h / 2;
    center.y = output_h / 2;
    return SDL_RenderCopyEx(renderer, scene, NULL, &dst, 270.0, &center, SDL_FLIP_NONE);
}

void draw_text(SDL_Renderer *renderer, TTF_Font *font, int x, int y,
               SDL_Color color, const char *text) {
    SDL_Surface *surface;
    SDL_Texture *texture;
    SDL_Rect dst;

    if (!font || !text || !*text) {
        return;
    }
    surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface) {
        return;
    }
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }
    dst.x = x;
    dst.y = y;
    dst.w = surface->w;
    dst.h = surface->h;
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

void draw_rect_outline(SDL_Renderer *renderer, const SDL_Rect *rect,
                       SDL_Color color, int thickness) {
    if (!renderer || !rect || thickness <= 0) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; i < thickness; i++) {
        SDL_Rect outline = {
            rect->x - i,
            rect->y - i,
            rect->w + i * 2,
            rect->h + i * 2
        };
        SDL_RenderDrawRect(renderer, &outline);
    }
}

void fit_text_ellipsis(TTF_Font *font, const char *text, int max_width,
                       char *out, size_t out_size) {
    size_t len;
    int width = 0;
    int ellipsis_width = 0;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!text || !*text) {
        return;
    }

    snprintf(out, out_size, "%s", text);
    if (!font || TTF_SizeUTF8(font, out, &width, NULL) != 0 || width <= max_width) {
        return;
    }

    TTF_SizeUTF8(font, "...", &ellipsis_width, NULL);
    len = strlen(text);
    while (len > 0) {
        if (len + 4 > out_size) {
            len--;
            continue;
        }
        memcpy(out, text, len);
        memcpy(out + len, "...", 4);
        if (TTF_SizeUTF8(font, out, &width, NULL) == 0 && width <= max_width) {
            return;
        }
        len--;
    }

    snprintf(out, out_size, "...");
}

static void draw_qr(SDL_Renderer *renderer, const char *path, const SDL_Rect *slot,
                    SDL_Texture **cached_texture, int *cached_w, int *cached_h) {
    SDL_Texture *texture;
    int tex_w, tex_h;
    SDL_Rect dst;
    int max_w;
    int max_h;
    float scale;

    if (cached_texture && *cached_texture) {
        texture = *cached_texture;
        tex_w = cached_w ? *cached_w : 0;
        tex_h = cached_h ? *cached_h : 0;
    } else {
        SDL_Surface *surface = IMG_Load(path);
        if (!surface) {
            return;
        }
        texture = SDL_CreateTextureFromSurface(renderer, surface);
        tex_w = surface->w;
        tex_h = surface->h;
        SDL_FreeSurface(surface);
        if (!texture) {
            return;
        }
        if (cached_texture) {
            *cached_texture = texture;
            if (cached_w) {
                *cached_w = tex_w;
            }
            if (cached_h) {
                *cached_h = tex_h;
            }
        }
    }

    max_w = slot ? slot->w - 24 : tex_w * 2;
    max_h = slot ? slot->h - 24 : tex_h * 2;
    scale = 2.0f;
    if (tex_w > 0 && tex_h > 0) {
        float scale_w = (float)max_w / (float)tex_w;
        float scale_h = (float)max_h / (float)tex_h;

        if (scale_w < scale) {
            scale = scale_w;
        }
        if (scale_h < scale) {
            scale = scale_h;
        }
    }
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    dst.w = (int)(tex_w * scale);
    dst.h = (int)(tex_h * scale);
    if (slot) {
        dst.x = slot->x + (slot->w - dst.w) / 2;
        dst.y = slot->y + (slot->h - dst.h) / 2;
    } else {
        dst.x = 512 - dst.w / 2;
        dst.y = 180;
    }
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    if (!cached_texture) {
        SDL_DestroyTexture(texture);
    }
}

void ui_draw_texture_cover(SDL_Renderer *renderer, SDL_Texture *texture,
                           int tex_w, int tex_h, const SDL_Rect *dst) {
    SDL_Rect src = { 0, 0, tex_w, tex_h };

    if (!renderer || !texture || !dst || tex_w <= 0 || tex_h <= 0) {
        return;
    }
    if (tex_w * dst->h > tex_h * dst->w) {
        src.w = (int)((float)tex_h * (float)dst->w / (float)dst->h);
        src.x = (tex_w - src.w) / 2;
    } else {
        src.h = (int)((float)tex_w * (float)dst->h / (float)dst->w);
        src.y = (tex_h - src.h) / 2;
    }
    SDL_RenderCopy(renderer, texture, &src, dst);
}

void ui_draw_texture_contain(SDL_Renderer *renderer, SDL_Texture *texture,
                             int tex_w, int tex_h, const SDL_Rect *dst) {
    SDL_Rect fitted;
    float scale_w;
    float scale_h;
    float scale;

    if (!renderer || !texture || !dst || tex_w <= 0 || tex_h <= 0) {
        return;
    }

    scale_w = (float)dst->w / (float)tex_w;
    scale_h = (float)dst->h / (float)tex_h;
    scale = scale_w < scale_h ? scale_w : scale_h;
    if (scale <= 0.0f) {
        return;
    }

    fitted.w = (int)lroundf((float)tex_w * scale);
    fitted.h = (int)lroundf((float)tex_h * scale);
    fitted.x = dst->x + (dst->w - fitted.w) / 2;
    fitted.y = dst->y + (dst->h - fitted.h) / 2;
    SDL_RenderCopy(renderer, texture, NULL, &fitted);
}

void ui_user_profile_placeholder_text(const UiUserProfile *profile,
                                      char *out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (profile && profile->nickname[0]) {
        unsigned char ch = (unsigned char)profile->nickname[0];

        if (ch < 0x80) {
            out[0] = (char)toupper(ch);
            out[1] = '\0';
            return;
        }
    }
    snprintf(out, out_size, "\xE8\xAF\xBB");
}

void render_header_status(SDL_Renderer *renderer, TTF_Font *body_font,
                          const char *text, const UiLayout *layout) {
    const UiTheme *theme = ui_current_theme();
    int canvas_w = layout ? layout->canvas_w : UI_CANVAS_WIDTH;
    int cw = layout ? layout->content_w : canvas_w;
    int cx = layout ? layout->content_x : 0;
    int text_w = 0;
    int text_h = 0;
    int y;

    if (!renderer || !body_font || !text || !text[0]) {
        return;
    }
    TTF_SizeUTF8(body_font, text, &text_w, &text_h);
    y = (60 - text_h) / 2;
    draw_text(renderer, body_font, cx + cw - 32 - text_w, y, theme->muted, text);
}

void render_confirm_hint(SDL_Renderer *renderer, TTF_Font *body_font,
                         Uint32 hint_until, const char *msg, const UiLayout *layout) {
    Uint32 now = SDL_GetTicks();
    int tw = 0;
    int th = 0;
    int pad_x = 18;
    int pad_y = 9;
    int canvas_w = layout ? layout->canvas_w : UI_CANVAS_WIDTH;
    int canvas_h = layout ? layout->canvas_h : UI_CANVAS_HEIGHT;
    SDL_Rect bg;
    Uint8 alpha;
    Uint32 remaining;
    Uint32 elapsed;
    float phase = 1.0f;
    float slide_progress = 1.0f;
    int base_y;

    if (!renderer || !body_font || !msg || !msg[0] || now >= hint_until) {
        return;
    }

    remaining = hint_until - now;
    elapsed = UI_EXIT_CONFIRM_DURATION_MS > remaining ?
        UI_EXIT_CONFIRM_DURATION_MS - remaining : 0;
    if (elapsed < UI_TOAST_FADE_MS) {
        phase = ui_ease_out_cubic((float)elapsed / (float)UI_TOAST_FADE_MS);
    } else if (remaining < UI_TOAST_FADE_MS) {
        phase = ui_clamp01f((float)remaining / (float)UI_TOAST_FADE_MS);
    }
    alpha = (Uint8)(215.0f * phase);
    slide_progress = ui_ease_out_cubic(phase);

    TTF_SizeUTF8(body_font, msg, &tw, &th);
    bg.w = tw + pad_x * 2;
    bg.h = th + pad_y * 2;
    bg.x = (canvas_w - bg.w) / 2;
    base_y = canvas_h - bg.h - 24;
    bg.y = base_y + (int)((1.0f - slide_progress) * 18.0f);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);
    SDL_RenderFillRect(renderer, &bg);
    {
        SDL_Color white = { 255, 255, 255, alpha };
        draw_text(renderer, body_font, bg.x + pad_x, bg.y + pad_y, white, msg);
    }
}

void render_login(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
                  AuthSession *session, const char *status, const char *battery_text,
                  const UiLayout *layout,
                  SDL_Texture **qr_texture, int *qr_w, int *qr_h) {
    const UiTheme *theme = ui_current_theme();
    SDL_Color ink = theme->ink;
    SDL_Color muted = theme->muted;
    SDL_Color line = theme->line;
    SDL_Rect header_band;
    SDL_Rect header_line;
    SDL_Rect footer_line;
    SDL_Rect intro_rect;
    SDL_Rect frame_rect;
    SDL_Rect qr_slot;
    int canvas_w = layout ? layout->canvas_w : UI_CANVAS_WIDTH;
    int canvas_h = layout ? layout->canvas_h : UI_CANVAS_HEIGHT;
    int cw = layout ? layout->content_w : canvas_w;
    int cx = layout ? layout->content_x : 0;
    const int header_h = 60;
    const int footer_h = 56;
    const int margin = 32;
    int content_top = header_h;
    int content_bottom = canvas_h - footer_h;
    int content_h = content_bottom - content_top;
    int center_x = cx + cw / 2;
    int title_y;
    int footer_text_y;
    int intro_w = cw > 560 ? 560 : cw - 48;
    int qr_frame_size = cw > 720 ? 420 : cw - 160;
    int qr_size;
    int intro_h = 86;
    int stack_gap = 24;
    int total_h;
    int status_width = 0;

    if (intro_w < 320) {
        intro_w = 320;
    }
    if (qr_frame_size > content_h - 160) {
        qr_frame_size = content_h - 160;
    }
    if (qr_frame_size < 260) {
        qr_frame_size = 260;
    }
    qr_size = qr_frame_size - 60;
    header_band = (SDL_Rect){ 0, 0, canvas_w, header_h };
    header_line = (SDL_Rect){ 0, header_h, canvas_w, 1 };
    footer_line = (SDL_Rect){ 0, canvas_h - footer_h, canvas_w, 1 };
    total_h = intro_h + stack_gap + qr_frame_size;
    intro_rect = (SDL_Rect){ center_x - intro_w / 2,
                             content_top + (content_h - total_h) / 2,
                             intro_w,
                             intro_h };
    frame_rect = (SDL_Rect){ center_x - qr_frame_size / 2,
                             intro_rect.y + intro_rect.h + stack_gap,
                             qr_frame_size,
                             qr_frame_size };
    qr_slot = (SDL_Rect){ frame_rect.x + (frame_rect.w - qr_size) / 2,
                          frame_rect.y + (frame_rect.h - qr_size) / 2,
                          qr_size,
                          qr_size };
    title_y = (header_h - (title_font ? TTF_FontHeight(title_font) : 36)) / 2;
    footer_text_y = footer_line.y +
        (footer_h - (body_font ? TTF_FontHeight(body_font) : 28)) / 2;

    SDL_SetRenderDrawColor(renderer, theme->bg_r, theme->bg_g, theme->bg_b, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, theme->header_r, theme->header_g, theme->header_b, 255);
    SDL_RenderFillRect(renderer, &header_band);
    SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
    SDL_RenderFillRect(renderer, &header_line);
    SDL_RenderFillRect(renderer, &footer_line);
    draw_text(renderer, title_font, cx + margin, title_y, ink,
              "\xE5\xBE\xAE\xE4\xBF\xA1\xE8\xAF\xBB\xE4\xB9\xA6");

    SDL_SetRenderDrawColor(renderer, theme->card_r, theme->card_g, theme->card_b, 255);
    SDL_RenderFillRect(renderer, &intro_rect);
    draw_rect_outline(renderer, &intro_rect, line, 1);
    SDL_RenderFillRect(renderer, &frame_rect);
    draw_rect_outline(renderer, &frame_rect, line, 1);
    SDL_SetRenderDrawColor(renderer, theme->qr_slot_r, theme->qr_slot_g, theme->qr_slot_b, 255);
    SDL_RenderFillRect(renderer, &qr_slot);
    draw_rect_outline(renderer, &qr_slot, line, 1);
    render_header_status(renderer, body_font, battery_text, layout);

    if (session && session->qr_png_path[0]) {
        draw_qr(renderer, session->qr_png_path, &qr_slot, qr_texture, qr_w, qr_h);
    }

    if (body_font) {
        const char *status_text = (status && status[0]) ?
            status : "\xE6\x89\xAB\xE7\xA0\x81\xE7\x99\xBB\xE5\xBD\x95";
        char status_buf[256];
        const char *hint_text =
            "\xE6\x89\x8B\xE6\x9C\xBA\xE5\xBE\xAE\xE4\xBF\xA1\xE6\x89\xAB\xE7\xA0\x81\xE5\x90\x8E\xE5\x9C\xA8\xE6\x89\x8B\xE6\x9C\xBA\xE4\xB8\x8A\xE7\xA1\xAE\xE8\xAE\xA4";

        fit_text_ellipsis(body_font, hint_text, intro_rect.w - 48, status_buf, sizeof(status_buf));
        TTF_SizeUTF8(body_font, status_buf, &status_width, NULL);
        draw_text(renderer, body_font, intro_rect.x + (intro_rect.w - status_width) / 2,
                  intro_rect.y + 18, muted, status_buf);

        fit_text_ellipsis(body_font, status_text, cw - margin * 2, status_buf, sizeof(status_buf));
        TTF_SizeUTF8(body_font, status_buf, &status_width, NULL);
        if (status_width <= cw - margin * 2) {
            draw_text(renderer, body_font, center_x - status_width / 2,
                      footer_text_y, muted, status_buf);
        }
    }
}

void render_poor_network_toast(SDL_Renderer *renderer, TTF_Font *body_font,
                               Uint32 toast_until, const UiLayout *layout) {
    Uint32 now = SDL_GetTicks();
    const char *msg = "\xE7\xBD\x91\xE7\xBB\x9C\xE4\xB8\x8D\xE4\xBD\xB3";
    int tw = 0;
    int th = 0;
    int pad_x = 20;
    int pad_y = 10;
    int canvas_w = layout ? layout->canvas_w : UI_CANVAS_WIDTH;
    int canvas_h = layout ? layout->canvas_h : UI_CANVAS_HEIGHT;
    SDL_Rect bg;
    Uint8 alpha;
    Uint32 remaining;
    Uint32 elapsed;
    float phase = 1.0f;
    float slide_progress = 1.0f;
    int base_y;

    if (!body_font || now >= toast_until) {
        return;
    }

    remaining = toast_until - now;
    elapsed = UI_TOAST_DURATION_MS > remaining ? UI_TOAST_DURATION_MS - remaining : 0;
    if (elapsed < UI_TOAST_FADE_MS) {
        phase = ui_ease_out_cubic((float)elapsed / (float)UI_TOAST_FADE_MS);
    } else if (remaining < UI_TOAST_FADE_MS) {
        phase = ui_clamp01f((float)remaining / (float)UI_TOAST_FADE_MS);
    }
    alpha = (Uint8)(210.0f * phase);
    slide_progress = ui_ease_out_cubic(phase);

    TTF_SizeUTF8(body_font, msg, &tw, &th);
    bg.w = tw + pad_x * 2;
    bg.h = th + pad_y * 2;
    bg.x = (canvas_w - bg.w) / 2;
    base_y = canvas_h - bg.h - 24;
    bg.y = base_y + (int)((1.0f - slide_progress) * 18.0f);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);
    SDL_RenderFillRect(renderer, &bg);
    {
        SDL_Color white = { 255, 255, 255, alpha };
        draw_text(renderer, body_font, bg.x + pad_x, bg.y + pad_y, white, msg);
    }
}

void render_loading(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
                    const char *title, const char *status, const char *battery_text,
                    const UiLayout *layout) {
    const UiTheme *theme = ui_current_theme();
    SDL_Color ink = theme->ink;
    SDL_Color muted = theme->muted;
    SDL_Color accent = theme->accent;
    int canvas_w = layout ? layout->canvas_w : UI_CANVAS_WIDTH;
    int canvas_h = layout ? layout->canvas_h : UI_CANVAS_HEIGHT;
    int cw = layout ? layout->content_w : canvas_w;
    int coff = layout ? layout->content_x : 0;
    int title_w = 0;
    int status_w = 0;
    int cx = coff + cw / 2;
    int cy = canvas_h / 2 - 36;
    Uint32 tick = SDL_GetTicks() / 120;

    SDL_SetRenderDrawColor(renderer, theme->bg_r, theme->bg_g, theme->bg_b, 255);
    SDL_RenderClear(renderer);

    for (int i = 0; i < 8; i++) {
        static const int offsets[8][2] = {
            { 0, -32 }, { 22, -22 }, { 32, 0 }, { 22, 22 },
            { 0, 32 }, { -22, 22 }, { -32, 0 }, { -22, -22 }
        };
        Uint8 alpha = (Uint8)((i == (int)(tick % 8)) ? 255 : (90 + i * 12));
        SDL_Rect dot = {
            cx + offsets[i][0] - 6,
            cy + offsets[i][1] - 6,
            12,
            12
        };
        SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, alpha);
        SDL_RenderFillRect(renderer, &dot);
    }

    if (title_font && title && *title) {
        TTF_SizeUTF8(title_font, title, &title_w, NULL);
        draw_text(renderer, title_font, cx - title_w / 2, cy + 64, ink, title);
    }
    if (body_font && status && *status) {
        TTF_SizeUTF8(body_font, status, &status_w, NULL);
        draw_text(renderer, body_font, cx - status_w / 2, cy + 122, muted, status);
    }
    render_header_status(renderer, body_font, battery_text, layout);
}

#endif
