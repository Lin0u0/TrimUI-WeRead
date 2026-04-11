#include "ui.h"
#include "ui_internal.h"

#if HAVE_SDL

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "stb_image.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "auth.h"
#include "json.h"
#include "preferences_state.h"
#include "reader.h"
#include "reader_state.h"
#include "session_service.h"
#include "shelf.h"
#include "shelf_state.h"
#include "state.h"

static void render_header_status(SDL_Renderer *renderer, TTF_Font *body_font,
                                 const char *text, const UiLayout *layout);
static void render_confirm_hint(SDL_Renderer *renderer, TTF_Font *body_font,
                                Uint32 hint_until, const char *msg, const UiLayout *layout);
static int shelf_ui_has_article_slot(cJSON *nuxt, ShelfArticleSlotInfo *info_out);
static int shelf_ui_normal_count(cJSON *nuxt);
static int shelf_ui_default_selection(cJSON *nuxt);
static int shelf_ui_clamp_selection(cJSON *nuxt, int selected);
static cJSON *shelf_ui_selected_book(cJSON *nuxt, int selected);

int ui_join_path_checked(char *dst, size_t dst_size, const char *dir, const char *name) {
    size_t dir_len;
    size_t name_len;

    if (!dst || dst_size == 0 || !dir || !name) {
        return -1;
    }

    dir_len = strlen(dir);
    name_len = strlen(name);
    if (dir_len + 1 + name_len + 1 > dst_size) {
        dst[0] = '\0';
        return -1;
    }

    memcpy(dst, dir, dir_len);
    dst[dir_len] = '/';
    memcpy(dst + dir_len + 1, name, name_len + 1);
    return 0;
}

void ui_copy_string(char *dst, size_t dst_size, const char *src) {
    size_t copy_len;

    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    if (dst == src) {
        return;
    }

    copy_len = strlen(src);
    if (copy_len >= dst_size) {
        copy_len = dst_size - 1;
    }
    memmove(dst, src, copy_len);
    dst[copy_len] = '\0';
}

enum {
    UI_HAPTIC_CONFIRM_MS = 58,
    UI_HAPTIC_EMPHASIS_MS = 78
};

static UiLayout ui_layout_for_rotation(UiRotation rotation) {
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

static int reader_top_inset_for_font_size(int font_size) {
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

typedef struct {
    SDL_Color ink;
    SDL_Color muted;
    SDL_Color accent;
    SDL_Color line;
    SDL_Color dim;
    Uint8 bg_r, bg_g, bg_b;
    Uint8 header_r, header_g, header_b;
    Uint8 card_r, card_g, card_b;
    Uint8 qr_slot_r, qr_slot_g, qr_slot_b;
    Uint8 cover_bg_r, cover_bg_g, cover_bg_b;
    Uint8 cover_empty_r, cover_empty_g, cover_empty_b;
    Uint8 shadow_r, shadow_g, shadow_b, shadow_a;
    Uint8 selection_border_r, selection_border_g, selection_border_b;
    Uint8 catalog_panel_r, catalog_panel_g, catalog_panel_b;
    Uint8 catalog_header_r, catalog_header_g, catalog_header_b;
    Uint8 catalog_highlight_r, catalog_highlight_g, catalog_highlight_b;
    Uint8 catalog_current_r, catalog_current_g, catalog_current_b;
    Uint8 backdrop_r, backdrop_g, backdrop_b, backdrop_a;
} UiTheme;

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

static const UiTheme *ui_current_theme(void) {
    return ui_dark_mode ? &ui_theme_dark : &ui_theme_light;
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

float ui_motion_step(float current, float target, float speed, float dt_seconds) {
    float factor;

    if (dt_seconds <= 0.0f) {
        return current;
    }
    factor = ui_clamp01f(speed * dt_seconds);
    return current + (target - current) * factor;
}

static void ui_reset_transient_input_state(UiRepeatState *repeat_state,
                                           int *tg5040_select_pressed,
                                           int *tg5040_start_pressed,
                                           Uint32 *exit_confirm_until,
                                           Uint32 *reader_exit_confirm_until) {
    if (repeat_state) {
        repeat_state->action = UI_REPEAT_NONE;
        repeat_state->next_tick = 0;
    }
    if (tg5040_select_pressed) {
        *tg5040_select_pressed = 0;
    }
    if (tg5040_start_pressed) {
        *tg5040_start_pressed = 0;
    }
    if (exit_confirm_until) {
        *exit_confirm_until = 0;
    }
    if (reader_exit_confirm_until) {
        *reader_exit_confirm_until = 0;
    }
}

static void ui_reset_view_input_latches(int *login_back_latch,
                                        int *login_confirm_latch,
                                        int *shelf_back_latch,
                                        int *shelf_confirm_latch,
                                        int *shelf_resume_latch,
                                        int *shelf_next_latch,
                                        int *shelf_prev_latch) {
    if (login_back_latch) {
        *login_back_latch = 0;
    }
    if (login_confirm_latch) {
        *login_confirm_latch = 0;
    }
    if (shelf_back_latch) {
        *shelf_back_latch = 0;
    }
    if (shelf_confirm_latch) {
        *shelf_confirm_latch = 0;
    }
    if (shelf_resume_latch) {
        *shelf_resume_latch = 0;
    }
    if (shelf_next_latch) {
        *shelf_next_latch = 0;
    }
    if (shelf_prev_latch) {
        *shelf_prev_latch = 0;
    }
}

static int ui_login_back_is_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                                 const UiInputSuppression *input_suppression) {
    return ui_input_mask_is_held(UI_INPUT_MASK_LOGIN_BACK, tg5040_input, joysticks,
                                 joystick_count, input_suppression);
}

static int ui_login_confirm_is_held(int tg5040_input, SDL_Joystick **joysticks,
                                    int joystick_count,
                                    const UiInputSuppression *input_suppression) {
    return ui_input_mask_is_held(UI_INPUT_MASK_LOGIN_CONFIRM, tg5040_input, joysticks,
                                 joystick_count, input_suppression);
}

static int ui_shelf_resume_is_held(int tg5040_input, SDL_Joystick **joysticks,
                                   int joystick_count,
                                   const UiInputSuppression *input_suppression) {
    return ui_input_mask_is_held(UI_INPUT_MASK_SHELF_RESUME, tg5040_input, joysticks,
                                 joystick_count, input_suppression);
}

static int ui_reader_doc_target_changed(const char *before, const ReaderViewState *state) {
    const char *after = state && state->doc.target ? state->doc.target : "";

    if (!before) {
        before = "";
    }
    return strcmp(before, after) != 0;
}

static int ui_reader_should_skip_transition_for_page_turn(const SDL_Event *event,
                                                          int tg5040_input,
                                                          const UiInputSuppression *suppression,
                                                          UiView event_view_before,
                                                          UiView current_view,
                                                          int event_catalog_before,
                                                          const ReaderViewState *reader_state,
                                                          const char *event_reader_target_before) {
    if (!event || event_view_before != VIEW_READER || current_view != VIEW_READER ||
        event_catalog_before || !reader_state) {
        return 0;
    }
    if (!ui_reader_doc_target_changed(event_reader_target_before, reader_state)) {
        return 0;
    }
    return ui_event_is_page_next(event, tg5040_input, suppression) ||
           ui_event_is_page_prev(event, tg5040_input, suppression);
}

static void ui_flush_transition_input_events(void) {
    SDL_FlushEvent(SDL_KEYDOWN);
    SDL_FlushEvent(SDL_KEYUP);
    SDL_FlushEvent(SDL_JOYBUTTONDOWN);
    SDL_FlushEvent(SDL_JOYBUTTONUP);
    SDL_FlushEvent(SDL_JOYHATMOTION);
    SDL_FlushEvent(SDL_JOYAXISMOTION);
}

static void ui_begin_input_transition(UiRepeatState *repeat_state,
                                      UiInputSuppression *input_suppression,
                                      UiInputMask *observed_input_mask,
                                      int tg5040_input,
                                      SDL_Joystick **joysticks,
                                      int joystick_count,
                                      int *tg5040_select_pressed,
                                      int *tg5040_start_pressed,
                                      Uint32 *exit_confirm_until,
                                      Uint32 *reader_exit_confirm_until,
                                      int *login_back_latch,
                                      int *login_confirm_latch,
                                      int *shelf_back_latch,
                                      int *shelf_confirm_latch,
                                      int *shelf_resume_latch,
                                      int *shelf_next_latch,
                                      int *shelf_prev_latch) {
    UiInputMask observed_mask = observed_input_mask ? *observed_input_mask : 0;
    UiInputMask live_mask = ui_input_current_mask(tg5040_input, joysticks, joystick_count);

    ui_reset_transient_input_state(repeat_state,
                                   tg5040_select_pressed,
                                   tg5040_start_pressed,
                                   exit_confirm_until,
                                   reader_exit_confirm_until);
    ui_reset_view_input_latches(login_back_latch, login_confirm_latch,
                                shelf_back_latch, shelf_confirm_latch,
                                shelf_resume_latch, shelf_next_latch,
                                shelf_prev_latch);
    if (input_suppression) {
        ui_input_suppression_begin(input_suppression, live_mask | observed_mask);
    }
    if (observed_input_mask) {
        *observed_input_mask = live_mask;
    }
    ui_flush_transition_input_events();
}

static void ui_force_exit_from_login(UiHapticState *haptic_state) {
    ui_platform_shutdown_haptics(haptic_state);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    _Exit(0);
}


static void ui_apply_repeat_action(UiRepeatAction action, ApiContext *ctx, TTF_Font *body_font,
                                   ReaderViewState *reader_state, cJSON *shelf_nuxt, int *selected,
                                   char *shelf_status, size_t shelf_status_size,
                                   const UiLayout *current_layout,
                                   ChapterPrefetchCache *chapter_prefetch_cache) {
    if (action == UI_REPEAT_NONE) {
        return;
    }
    if (action == UI_REPEAT_SHELF_PREV || action == UI_REPEAT_SHELF_NEXT) {
        int count = shelf_ui_normal_count(shelf_nuxt);
        int has_article = shelf_ui_has_article_slot(shelf_nuxt, NULL);
        int min_selected = has_article ? -1 : 0;

        if (!selected) {
            return;
        }
        if (action == UI_REPEAT_SHELF_NEXT && *selected + 1 < count) {
            (*selected)++;
        } else if (action == UI_REPEAT_SHELF_PREV && *selected > min_selected) {
            (*selected)--;
        }
        return;
    }
    if (!reader_state) {
        return;
    }
    if (action == UI_REPEAT_CATALOG_UP) {
        if (reader_state->catalog_selected > 0) {
            reader_state->catalog_selected--;
        } else if (ui_reader_view_expand_catalog_for_selection(ctx, reader_state, -1,
                                                               shelf_status, shelf_status_size) > 0) {
            reader_state->catalog_selected--;
        }
        return;
    }
    if (action == UI_REPEAT_CATALOG_DOWN) {
        if (reader_state->catalog_selected + 1 < reader_state->doc.catalog_count) {
            reader_state->catalog_selected++;
        } else if (ui_reader_view_expand_catalog_for_selection(ctx, reader_state, 1,
                                                               shelf_status, shelf_status_size) > 0 &&
                   reader_state->catalog_selected + 1 < reader_state->doc.catalog_count) {
            reader_state->catalog_selected++;
        }
        return;
    }
    if (action == UI_REPEAT_CATALOG_PAGE_PREV) {
        if (reader_state->catalog_selected > 0) {
            reader_state->catalog_selected -= 10;
            if (reader_state->catalog_selected < 0) {
                reader_state->catalog_selected = 0;
            }
        } else if (ui_reader_view_expand_catalog_for_selection(ctx, reader_state, -1,
                                                               shelf_status, shelf_status_size) > 0) {
            reader_state->catalog_selected -= 10;
            if (reader_state->catalog_selected < 0) {
                reader_state->catalog_selected = 0;
            }
        }
        return;
    }
    if (action == UI_REPEAT_CATALOG_PAGE_NEXT) {
        if (reader_state->catalog_selected + 1 < reader_state->doc.catalog_count) {
            reader_state->catalog_selected += 10;
            if (reader_state->catalog_selected >= reader_state->doc.catalog_count) {
                reader_state->catalog_selected = reader_state->doc.catalog_count - 1;
            }
        } else if (ui_reader_view_expand_catalog_for_selection(ctx, reader_state, 1,
                                                               shelf_status, shelf_status_size) > 0) {
            reader_state->catalog_selected += 10;
            if (reader_state->catalog_selected >= reader_state->doc.catalog_count) {
                reader_state->catalog_selected = reader_state->doc.catalog_count - 1;
            }
        }
        return;
    }
    if (action == UI_REPEAT_PAGE_NEXT) {
        int total_pages = ui_reader_view_total_pages(reader_state);

        ui_reader_view_note_progress_activity(reader_state, SDL_GetTicks());
        if (reader_state->current_page + 1 < total_pages) {
            reader_state->current_page++;
            ui_reader_view_save_local_position(ctx, reader_state);
        } else if (reader_state->doc.next_target && reader_state->doc.next_target[0]) {
            char *target = strdup(reader_state->doc.next_target);
            char source_target[2048];
            int font_size = reader_state->doc.font_size;

            if (!target) {
                return;
            }
            snprintf(source_target, sizeof(source_target), "%s", reader_state->source_target);
            ui_reader_view_flush_progress_blocking(ctx, reader_state, 1);
            if (chapter_prefetch_cache &&
                ui_reader_flow_chapter_prefetch_cache_adopt(chapter_prefetch_cache, target,
                                                            body_font, reader_state,
                                                            current_layout) == 0) {
                ui_reader_view_set_source_target(reader_state, source_target);
                ui_reader_view_save_local_position(ctx, reader_state);
                shelf_status[0] = '\0';
            } else if (ui_reader_view_load(ctx, body_font, target, font_size,
                                           current_layout->reader_content_w,
                                           current_layout->reader_content_h, 0,
                                           reader_state) == 0) {
                ui_reader_view_set_source_target(reader_state, source_target);
                ui_reader_view_save_local_position(ctx, reader_state);
                shelf_status[0] = '\0';
            } else {
                snprintf(shelf_status, shelf_status_size,
                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8B\xE4\xB8\x80\xE7\xAB\xA0");  /* 无法打开下一章 */
            }
            free(target);
        }
        return;
    }
    if (action == UI_REPEAT_PAGE_PREV) {
        ui_reader_view_note_progress_activity(reader_state, SDL_GetTicks());
        if (reader_state->current_page > 0) {
            reader_state->current_page--;
            ui_reader_view_save_local_position(ctx, reader_state);
        } else if (reader_state->doc.prev_target && reader_state->doc.prev_target[0]) {
            char *target = strdup(reader_state->doc.prev_target);
            char source_target[2048];
            int font_size = reader_state->doc.font_size;

            if (!target) {
                return;
            }
            snprintf(source_target, sizeof(source_target), "%s", reader_state->source_target);
            ui_reader_view_flush_progress_blocking(ctx, reader_state, 1);
            if (chapter_prefetch_cache &&
                ui_reader_flow_chapter_prefetch_cache_adopt(chapter_prefetch_cache, target,
                                                            body_font, reader_state,
                                                            current_layout) == 0) {
                int new_total_pages;

                ui_reader_view_set_source_target(reader_state, source_target);
                new_total_pages = ui_reader_view_total_pages(reader_state);
                reader_state->current_page = new_total_pages > 0 ? new_total_pages - 1 : 0;
                ui_reader_view_save_local_position(ctx, reader_state);
                shelf_status[0] = '\0';
            } else if (ui_reader_view_load(ctx, body_font, target, font_size,
                                           current_layout->reader_content_w,
                                           current_layout->reader_content_h, 0,
                                           reader_state) == 0) {
                int new_total_pages;

                ui_reader_view_set_source_target(reader_state, source_target);
                new_total_pages = ui_reader_view_total_pages(reader_state);
                reader_state->current_page = new_total_pages > 0 ? new_total_pages - 1 : 0;
                ui_reader_view_save_local_position(ctx, reader_state);
                shelf_status[0] = '\0';
            } else {
                snprintf(shelf_status, shelf_status_size,
                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8A\xE4\xB8\x80\xE7\xAB\xA0");  /* 无法打开上一章 */
            }
            free(target);
        }
    }
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

int ui_recreate_scene_texture(SDL_Renderer *renderer, SDL_Texture **scene_texture,
                                     const UiLayout *layout) {
    SDL_Texture *new_texture;

    if (!renderer || !scene_texture || !layout) {
        return -1;
    }

    new_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                    SDL_TEXTUREACCESS_TARGET,
                                    layout->canvas_w, layout->canvas_h);
    if (!new_texture) {
        return -1;
    }

    if (*scene_texture) {
        SDL_DestroyTexture(*scene_texture);
    }
    *scene_texture = new_texture;
    return 0;
}

static int ui_present_scene(SDL_Renderer *renderer, SDL_Texture *scene, UiRotation rotation,
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

    /* Portrait: scene texture is output_h x output_w (e.g. 768x1024).
     * Place dst at (0,0) with those dimensions and use a custom rotation
     * center so the rotated result fills the screen exactly. */
    dst.x = 0;
    dst.y = 0;
    dst.w = output_h;
    dst.h = output_w;

    if (rotation == UI_ROTATE_RIGHT_PORTRAIT) {
        center.x = output_w / 2;
        center.y = output_w / 2;
        return SDL_RenderCopyEx(renderer, scene, NULL, &dst, 90.0, &center, SDL_FLIP_NONE);
    }
    /* UI_ROTATE_LEFT_PORTRAIT */
    center.x = output_h / 2;
    center.y = output_h / 2;
    return SDL_RenderCopyEx(renderer, scene, NULL, &dst, 270.0, &center, SDL_FLIP_NONE);
}

static void reader_format_chapter_heading(const ReaderViewState *state, char *out, size_t out_size) {
    char prefix[64];

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!state || !state->doc.chapter_title || !state->doc.chapter_title[0]) {
        return;
    }
    if (state->doc.chapter_idx <= 0) {
        snprintf(out, out_size, "%s", state->doc.chapter_title);
        return;
    }

    snprintf(prefix, sizeof(prefix), "第%d章 ", state->doc.chapter_idx);
    if (strncmp(state->doc.chapter_title, prefix, strlen(prefix)) == 0) {
        snprintf(out, out_size, "%s", state->doc.chapter_title);
        return;
    }
    snprintf(out, out_size, "%s%s", prefix, state->doc.chapter_title);
}

static char *dup_or_null(const char *s) {
    return s ? strdup(s) : NULL;
}

static void shelf_cover_entry_reset(ShelfCoverEntry *entry) {
    if (!entry) {
        return;
    }
    free(entry->book_id);
    free(entry->cover_url);
    if (entry->texture) {
        SDL_DestroyTexture(entry->texture);
    }
    memset(entry, 0, sizeof(*entry));
}

static void shelf_cover_entry_release_texture(ShelfCoverEntry *entry) {
    if (!entry || !entry->texture) {
        return;
    }
    SDL_DestroyTexture(entry->texture);
    entry->texture = NULL;
}

static void shelf_cover_cache_reset(ShelfCoverCache *cache) {
    if (!cache) {
        return;
    }
    if (cache->entries) {
        for (int i = 0; i < cache->count; i++) {
            shelf_cover_entry_reset(&cache->entries[i]);
        }
    }
    free(cache->entries);
    memset(cache, 0, sizeof(*cache));
}

static void shelf_cover_cache_trim(ShelfCoverCache *cache, int selected, int keep_radius) {
    if (!cache || !cache->entries) {
        return;
    }

    for (int i = 0; i < cache->count; i++) {
        if (i < selected - keep_radius || i > selected + keep_radius) {
            shelf_cover_entry_release_texture(&cache->entries[i]);
        }
    }
}

static void draw_text(SDL_Renderer *renderer, TTF_Font *font, int x, int y,
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

static void draw_rect_outline(SDL_Renderer *renderer, const SDL_Rect *rect,
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

static void fit_text_ellipsis(TTF_Font *font, const char *text, int max_width,
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

static const char *shelf_cover_url(cJSON *book) {
    static const char *paths[] = {
        "cover",
        "coverUrl",
        "cover_url",
        "coverMiddle",
        "coverMid",
        "coverLarge",
        "coverSmall",
        "bookCover",
        "bookInfo.cover",
        "bookInfo.coverUrl",
        "bookInfo.coverLarge",
        "bookInfo.coverMiddle",
        "bookInfo.coverSmall",
        "bookInfo.bookCover",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        const char *value = json_get_string(book, paths[i]);
        if (value && *value) {
            return value;
        }
    }
    return NULL;
}

static int ensure_dir(const char *path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static SDL_Surface *load_image_stb(const char *path) {
    FILE *fp;
    long file_size;
    unsigned char *file_data;
    int w, h, channels;
    unsigned char *pixels;
    SDL_Surface *surface;

    fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (file_size <= 0 || file_size > 16 * 1024 * 1024) {
        fclose(fp);
        return NULL;
    }
    file_data = malloc((size_t)file_size);
    if (!file_data) {
        fclose(fp);
        return NULL;
    }
    if ((long)fread(file_data, 1, (size_t)file_size, fp) != file_size) {
        free(file_data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    pixels = stbi_load_from_memory(file_data, (int)file_size, &w, &h, &channels, 4);
    free(file_data);
    if (!pixels) {
        return NULL;
    }

    surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, 32, w * 4,
                                                  SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        stbi_image_free(pixels);
        return NULL;
    }
    /* SDL_FreeSurface won't free stb pixels, so we mark it for manual cleanup.
     * The caller must copy to texture before freeing the surface. */
    return surface;
}

static int shelf_cover_prepare(ApiContext *ctx, SDL_Renderer *renderer, ShelfCoverEntry *entry) {
    SDL_Surface *surface;

    if (!ctx || !renderer || !entry) {
        return -1;
    }
    if (entry->texture) {
        return entry && entry->texture ? 0 : -1;
    }
    if (!entry->cover_url || !entry->cache_path[0]) {
        return -1;
    }

    if (access(entry->cache_path, F_OK) != 0) {
        entry->attempted = 1;
        return -1;
    }

    surface = load_image_stb(entry->cache_path);
    if (!surface) {
        return -1;
    }
    entry->texture = SDL_CreateTextureFromSurface(renderer, surface);
    {
        void *pixels = surface->pixels;
        SDL_FreeSurface(surface);
        stbi_image_free(pixels);
    }
    return entry->texture ? 0 : -1;
}

static int shelf_cover_prepare_nearest(ApiContext *ctx, SDL_Renderer *renderer,
                                       ShelfCoverCache *cache, float selected_pos) {
    int center;

    if (!ctx || !renderer || !cache || !cache->entries || cache->count <= 0) {
        return 0;
    }

    center = (int)lroundf(selected_pos);
    for (int distance = 0; distance <= 3; distance++) {
        int candidates[2] = { center - distance, center + distance };
        int candidate_count = distance == 0 ? 1 : 2;

        for (int i = 0; i < candidate_count; i++) {
            int index = candidates[i];
            ShelfCoverEntry *entry;

            if (index < 0 || index >= cache->count) {
                continue;
            }

            entry = &cache->entries[index];
            if (entry->texture || !entry->cache_path[0] || access(entry->cache_path, F_OK) != 0) {
                continue;
            }

            return shelf_cover_prepare(ctx, renderer, entry) == 0;
        }
    }

    return 0;
}

static void shelf_cover_cache_build(ApiContext *ctx, cJSON *nuxt, ShelfCoverCache *cache) {
    char covers_dir[1024];
    char file_name[256];
    int count;

    shelf_cover_cache_reset(cache);
    if (!ctx) {
        return;
    }

    snprintf(covers_dir, sizeof(covers_dir), "%s/covers", ctx->state_dir);
    if (ensure_dir(covers_dir) != 0) {
        return;
    }

    count = shelf_normal_book_count(nuxt);
    if (count <= 0) {
        return;
    }
    cache->entries = calloc((size_t)count, sizeof(ShelfCoverEntry));
    if (!cache->entries) {
        cache->count = 0;
        return;
    }
    cache->count = count;

    for (int i = 0; i < count; i++) {
        cJSON *book = shelf_normal_book_at(nuxt, i, NULL);
        ShelfCoverEntry *entry = &cache->entries[i];
        const char *book_id = json_get_string(book, "bookId");
        const char *cover = shelf_cover_url(book);

        entry->book_id = dup_or_null(book_id);
        entry->cover_url = dup_or_null(cover);
        if (book_id && *book_id) {
            if (strlen(book_id) + strlen(".img") + 1 > sizeof(file_name) ||
                snprintf(file_name, sizeof(file_name), "%s.img", book_id) < 0 ||
                ui_join_path_checked(entry->cache_path, sizeof(entry->cache_path),
                                     covers_dir, file_name) != 0) {
                entry->cache_path[0] = '\0';
            }
        } else {
            if (snprintf(file_name, sizeof(file_name), "book-%d.img", i) < 0 ||
                ui_join_path_checked(entry->cache_path, sizeof(entry->cache_path),
                                     covers_dir, file_name) != 0) {
                entry->cache_path[0] = '\0';
            }
        }
    }
}

static int shelf_ui_has_article_slot(cJSON *nuxt, ShelfArticleSlotInfo *info_out) {
    return shelf_article_slot_info(nuxt, info_out);
}

static int shelf_ui_normal_count(cJSON *nuxt) {
    return shelf_normal_book_count(nuxt);
}

static int shelf_ui_default_selection(cJSON *nuxt) {
    if (shelf_ui_normal_count(nuxt) > 0) {
        return 0;
    }
    return shelf_ui_has_article_slot(nuxt, NULL) ? -1 : 0;
}

static int shelf_ui_clamp_selection(cJSON *nuxt, int selected) {
    int has_article = shelf_ui_has_article_slot(nuxt, NULL);
    int count = shelf_ui_normal_count(nuxt);
    int min_selected = has_article ? -1 : 0;
    int max_selected = count - 1;

    if (max_selected < min_selected) {
        return min_selected;
    }
    if (selected < min_selected) {
        return min_selected;
    }
    if (selected > max_selected) {
        return max_selected;
    }
    return selected;
}

static cJSON *shelf_ui_selected_book(cJSON *nuxt, int selected) {
    if (selected < 0) {
        return NULL;
    }
    return shelf_normal_book_at(nuxt, selected, NULL);
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
            if (cached_w) *cached_w = tex_w;
            if (cached_h) *cached_h = tex_h;
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

static void render_header_status(SDL_Renderer *renderer, TTF_Font *body_font,
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

static void render_confirm_hint(SDL_Renderer *renderer, TTF_Font *body_font,
                                Uint32 hint_until, const char *msg, const UiLayout *layout) {
    Uint32 now = SDL_GetTicks();
    int tw = 0, th = 0;
    int pad_x = 18, pad_y = 9;
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
    elapsed = UI_EXIT_CONFIRM_DURATION_MS > remaining ? UI_EXIT_CONFIRM_DURATION_MS - remaining : 0;
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

static void render_login(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
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
    SDL_Rect card;
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
    int title_y;
    int footer_text_y;
    int card_w = cw >= 900 ? 500 : cw - 88;
    int card_h = canvas_h >= 900 ? 600 : content_h - 44;
    int qr_size = card_w - 120;
    int status_width = 0;

    if (card_w < 360) {
        card_w = 360;
    }
    if (card_h < 460) {
        card_h = 460;
    }
    if (card_h > content_h - 24) {
        card_h = content_h - 24;
    }
    if (qr_size > 360) {
        qr_size = 360;
    }
    if (qr_size < 220) {
        qr_size = 220;
    }
    header_band = (SDL_Rect){ 0, 0, canvas_w, header_h };
    header_line = (SDL_Rect){ 0, header_h, canvas_w, 1 };
    footer_line = (SDL_Rect){ 0, canvas_h - footer_h, canvas_w, 1 };
    card = (SDL_Rect){ cx + (cw - card_w) / 2, content_top + (content_h - card_h) / 2, card_w, card_h };
    qr_slot = (SDL_Rect){ card.x + (card.w - qr_size) / 2, card.y + 70, qr_size, qr_size };
    title_y = (header_h - (title_font ? TTF_FontHeight(title_font) : 36)) / 2;
    footer_text_y = footer_line.y + (footer_h - (body_font ? TTF_FontHeight(body_font) : 28)) / 2;

    SDL_SetRenderDrawColor(renderer, theme->bg_r, theme->bg_g, theme->bg_b, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, theme->header_r, theme->header_g, theme->header_b, 255);
    SDL_RenderFillRect(renderer, &header_band);
    SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
    SDL_RenderFillRect(renderer, &header_line);
    SDL_RenderFillRect(renderer, &footer_line);
    SDL_SetRenderDrawColor(renderer, theme->card_r, theme->card_g, theme->card_b, 255);
    SDL_RenderFillRect(renderer, &card);
    draw_rect_outline(renderer, &card, line, 1);

    draw_text(renderer, title_font, cx + margin, title_y, ink, "\xE5\xBE\xAE\xE4\xBF\xA1\xE8\xAF\xBB\xE4\xB9\xA6");  /* 微信读书 */

    SDL_SetRenderDrawColor(renderer, theme->qr_slot_r, theme->qr_slot_g, theme->qr_slot_b, 255);
    SDL_RenderFillRect(renderer, &qr_slot);
    draw_rect_outline(renderer, &qr_slot, line, 1);
    render_header_status(renderer, body_font, battery_text, layout);

    if (session && session->qr_png_path[0]) {
        draw_qr(renderer, session->qr_png_path, &qr_slot, qr_texture, qr_w, qr_h);
    }

    if (body_font) {
        /* 扫码登录 */
        const char *status_text = (status && status[0]) ? status : "\xE6\x89\xAB\xE7\xA0\x81\xE7\x99\xBB\xE5\xBD\x95";
        char status_buf[256];
        fit_text_ellipsis(body_font, status_text, cw - margin * 2, status_buf, sizeof(status_buf));
        TTF_SizeUTF8(body_font, status_buf, &status_width, NULL);
        if (status_width <= cw - margin * 2) {
            draw_text(renderer, body_font, cx + (cw - status_width) / 2, footer_text_y, muted, status_buf);
        }
    }
}

static void render_poor_network_toast(SDL_Renderer *renderer, TTF_Font *body_font,
                                      Uint32 toast_until, const UiLayout *layout) {
    Uint32 now = SDL_GetTicks();
    /* Localized message: "网络不佳" (Poor network in Chinese) */
    const char *msg = "\xE7\xBD\x91\xE7\xBB\x9C\xE4\xB8\x8D\xE4\xBD\xB3";
    int tw = 0, th = 0;
    int pad_x = 20, pad_y = 10;
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

static void render_loading(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
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

static void render_shelf_cover(SDL_Renderer *renderer, TTF_Font *body_font, SDL_Color ink,
                               SDL_Rect cover_rect, ShelfCoverEntry *entry,
                               const char *title, float emphasis) {
    const float clamped_emphasis = ui_clamp01f(emphasis);
    const float scale = 1.0f + 0.18f * clamped_emphasis;
    SDL_Rect scaled_rect = {
        cover_rect.x - (int)((cover_rect.w * scale - cover_rect.w) / 2.0f),
        cover_rect.y - (int)((cover_rect.h * scale - cover_rect.h) / 2.0f),
        (int)(cover_rect.w * scale),
        (int)(cover_rect.h * scale)
    };
    SDL_Rect border = {
        scaled_rect.x - 6,
        scaled_rect.y - 6,
        scaled_rect.w + 12,
        scaled_rect.h + 12
    };

    const UiTheme *theme = ui_current_theme();
    Uint8 border_alpha = (Uint8)(255.0f * clamped_emphasis);
    Uint8 shadow_alpha = (Uint8)(theme->shadow_a * (0.7f + clamped_emphasis * 0.3f));
    (void)title;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, theme->shadow_r, theme->shadow_g, theme->shadow_b, shadow_alpha);
    SDL_Rect shadow = {
        scaled_rect.x + 8,
        scaled_rect.y + 10,
        scaled_rect.w,
        scaled_rect.h
    };
    SDL_RenderFillRect(renderer, &shadow);

    if (border_alpha > 0) {
        SDL_SetRenderDrawColor(renderer, theme->selection_border_r, theme->selection_border_g,
                               theme->selection_border_b, border_alpha);
        SDL_RenderFillRect(renderer, &border);
    }

    SDL_SetRenderDrawColor(renderer, theme->cover_bg_r, theme->cover_bg_g, theme->cover_bg_b, 255);
    SDL_RenderFillRect(renderer, &scaled_rect);

    if (entry && entry->texture) {
        SDL_RenderCopy(renderer, entry->texture, NULL, &scaled_rect);
    } else {
        SDL_SetRenderDrawColor(renderer, theme->cover_empty_r, theme->cover_empty_g, theme->cover_empty_b, 255);
        SDL_RenderFillRect(renderer, &scaled_rect);
        draw_text(renderer, body_font, scaled_rect.x + 22, scaled_rect.y + scaled_rect.h / 2 - 10,
                  ink, "\xE6\x97\xA0\xE5\xB0\x81\xE9\x9D\xA2");  /* 无封面 */
    }
}

static void render_shelf_article_slot(SDL_Renderer *renderer, TTF_Font *title_font,
                                      TTF_Font *body_font, SDL_Rect cover_rect,
                                      const ShelfArticleSlotInfo *article_slot,
                                      float emphasis) {
    const UiTheme *theme = ui_current_theme();
    const float clamped_emphasis = ui_clamp01f(emphasis);
    const float scale = 1.0f + 0.18f * clamped_emphasis;
    SDL_Rect scaled_rect = {
        cover_rect.x - (int)((cover_rect.w * scale - cover_rect.w) / 2.0f),
        cover_rect.y - (int)((cover_rect.h * scale - cover_rect.h) / 2.0f),
        (int)(cover_rect.w * scale),
        (int)(cover_rect.h * scale)
    };
    SDL_Rect border = {
        scaled_rect.x - 6,
        scaled_rect.y - 6,
        scaled_rect.w + 12,
        scaled_rect.h + 12
    };
    SDL_Rect accent_bar = {
        scaled_rect.x + 22,
        scaled_rect.y + 28,
        scaled_rect.w - 44,
        6
    };
    SDL_Color accent = theme->accent;
    SDL_Color ink = theme->ink;
    Uint8 border_alpha = (Uint8)(255.0f * clamped_emphasis);
    Uint8 shadow_alpha = (Uint8)(theme->shadow_a * (0.7f + clamped_emphasis * 0.3f));
    char title_buf[160];
    const char *article_title =
        (article_slot && article_slot->title && article_slot->title[0]) ?
        article_slot->title :
        "\xE5\x85\xAC\xE4\xBC\x97\xE5\x8F\xB7\xE6\x96\x87\xE7\xAB\xA0";

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, theme->shadow_r, theme->shadow_g, theme->shadow_b, shadow_alpha);
    SDL_Rect shadow = {
        scaled_rect.x + 8,
        scaled_rect.y + 10,
        scaled_rect.w,
        scaled_rect.h
    };
    SDL_RenderFillRect(renderer, &shadow);

    if (border_alpha > 0) {
        SDL_SetRenderDrawColor(renderer, theme->selection_border_r, theme->selection_border_g,
                               theme->selection_border_b, border_alpha);
        SDL_RenderFillRect(renderer, &border);
    }

    SDL_SetRenderDrawColor(renderer, theme->card_r, theme->card_g, theme->card_b, 255);
    SDL_RenderFillRect(renderer, &scaled_rect);
    draw_rect_outline(renderer, &scaled_rect, theme->line, 2);

    SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, 255);
    SDL_RenderFillRect(renderer, &accent_bar);

    draw_text(renderer, title_font, scaled_rect.x + 22, scaled_rect.y + 56, accent,
              "\xE6\x96\x87\xE7\xAB\xA0");  /* 文章 */
    fit_text_ellipsis(body_font, article_title, scaled_rect.w - 44, title_buf, sizeof(title_buf));
    draw_text(renderer, body_font, scaled_rect.x + 22, scaled_rect.y + 128, ink, title_buf);
}

static void render_shelf(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
                         ApiContext *ctx, cJSON *nuxt, ShelfCoverCache *cover_cache,
                         int selected, float selected_pos, int start,
                         const char *status, const char *battery_text, const UiLayout *layout) {
    const int margin = 32;
    const int cover_w = 272;
    const int cover_h = 382;
    const int card_gap = 64;
    const int header_h = 60;
    const int canvas_w = layout ? layout->canvas_w : UI_CANVAS_WIDTH;
    const int window_w = layout ? layout->content_w : canvas_w;
    const int window_x = layout ? layout->content_x : 0;
    const int window_h = layout ? layout->canvas_h : UI_CANVAS_HEIGHT;
    const UiTheme *theme = ui_current_theme();
    SDL_Color ink = theme->ink;
    SDL_Color muted = theme->muted;
    SDL_Color line = theme->line;
    ShelfArticleSlotInfo article_slot;
    int has_article = shelf_ui_has_article_slot(nuxt, &article_slot);
    int count = shelf_ui_normal_count(nuxt);
    int min_selected = has_article ? -1 : 0;
    int start_y;
    int content_top = header_h;
    int content_bottom = window_h - 56;
    int content_h = content_bottom - content_top;
    cJSON *selected_book = NULL;
    const char *selected_title = NULL;
    time_t now = time(NULL);
    struct tm *local_tm = localtime(&now);
    char time_buf[32] = "";
    char position_buf[48];
    char title_buf[256];
    SDL_Rect header_band = { 0, 0, canvas_w, header_h };
    SDL_Rect header_line = { 0, header_h, canvas_w, 1 };
    SDL_Rect footer_line = { 0, window_h - 56, canvas_w, 1 };
    int position_width = 0;
    int position_x;
    int info_h = window_h - footer_line.y;
    int footer_text_y;
    int title_y;
    int nav_y;
    (void)status;
    (void)start;

    start_y = content_top + (content_h - cover_h) / 2;
    if (start_y < content_top) {
        start_y = content_top;
    }

    SDL_SetRenderDrawColor(renderer, theme->bg_r, theme->bg_g, theme->bg_b, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, theme->header_r, theme->header_g, theme->header_b, 255);
    SDL_RenderFillRect(renderer, &header_band);
    SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
    SDL_RenderFillRect(renderer, &header_line);
    SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
    SDL_RenderFillRect(renderer, &footer_line);

    if (local_tm) {
        strftime(time_buf, sizeof(time_buf), "%H:%M", local_tm);
    }
    title_y = (header_h - (title_font ? TTF_FontHeight(title_font) : 36)) / 2;
    nav_y = content_top + (content_h - (title_font ? TTF_FontHeight(title_font) : 36)) / 2;

    if (count == 0 && !has_article) {
        int empty_w = 0;
        /* 书架为空 */
        const char *empty_text = "\xE4\xB9\xA6\xE6\x9E\xB6\xE4\xB8\xBA\xE7\xA9\xBA";
        render_header_status(renderer, body_font, battery_text, layout);
        TTF_SizeUTF8(title_font, empty_text, &empty_w, NULL);
        draw_text(renderer, title_font, window_x + window_w / 2 - empty_w / 2, window_h / 2 - 40, ink, empty_text);
        return;
    }

    shelf_cover_cache_trim(cover_cache, selected, UI_SHELF_COVER_TEXTURE_KEEP_RADIUS);
    selected_book = shelf_ui_selected_book(nuxt, selected);
    if (selected < 0 && has_article) {
        selected_title = article_slot.title;
    } else {
        selected_title = json_get_string(selected_book, "title");
    }
    /* 微信读书 */
    fit_text_ellipsis(title_font, selected_title ? selected_title : "\xE5\xBE\xAE\xE4\xBF\xA1\xE8\xAF\xBB\xE4\xB9\xA6",
                      window_w - margin * 2 - 140,
                      title_buf, sizeof(title_buf));
    draw_text(renderer, title_font, window_x + margin, title_y, ink, title_buf);
    render_header_status(renderer, body_font, battery_text, layout);

    for (int i = (int)floorf(selected_pos) - 3; i <= (int)ceilf(selected_pos) + 3; i++) {
        float offset;
        float emphasis;
        SDL_Rect cover_rect = {
            0,
            start_y,
            cover_w,
            cover_h
        };

        if (i < min_selected || i >= count) {
            continue;
        }
        offset = (float)i - selected_pos;
        emphasis = 1.0f - fabsf(offset);
        cover_rect.x = window_x + (window_w - cover_w) / 2 +
                       (int)lroundf(offset * (float)(cover_w + card_gap));

        if (i < 0) {
            render_shelf_article_slot(renderer, title_font, body_font, cover_rect,
                                      &article_slot, emphasis);
            continue;
        }

        {
            cJSON *book = shelf_normal_book_at(nuxt, i, NULL);
            const char *title = json_get_string(book, "title");

            if (!book) {
                continue;
            }
            render_shelf_cover(renderer, body_font, ink, cover_rect,
                               cover_cache && i < cover_cache->count ? &cover_cache->entries[i] : NULL,
                               title, emphasis);
        }
    }

    if (selected > min_selected) {
        draw_text(renderer, title_font, window_x + 18, nav_y, ink, "<");
    }
    if (selected + 1 < count) {
        draw_text(renderer, title_font, window_x + window_w - 42, nav_y,
                  ink, ">");
    }

    if (selected < 0 && has_article) {
        snprintf(position_buf, sizeof(position_buf), "\xE5\x85\xAC\xE4\xBC\x97\xE5\x8F\xB7 1 / 1");
    } else {
        snprintf(position_buf, sizeof(position_buf),
                 "\xE4\xB9\xA6 %d / %d", selected + 1, count);
    }
    TTF_SizeUTF8(body_font, position_buf, &position_width, NULL);
    position_x = window_x + window_w - margin - position_width;
    footer_text_y = footer_line.y + (info_h - (body_font ? TTF_FontHeight(body_font) : 28)) / 2;
    draw_text(renderer, body_font, window_x + margin, footer_text_y, muted, time_buf[0] ? time_buf : "--:--");
    draw_text(renderer, body_font, position_x, footer_text_y, muted, position_buf);
}

static void reader_view_free(ReaderViewState *state) {
    char fallback_font_path[512];

    if (!state) {
        return;
    }
    fallback_font_path[0] = '\0';
    if (state->fallback_font_path[0]) {
        snprintf(fallback_font_path, sizeof(fallback_font_path), "%s",
                 state->fallback_font_path);
    }
    if (state->lines) {
        for (int i = 0; i < state->line_count; i++) {
            free(state->lines[i]);
        }
    }
    if (state->content_font) {
        TTF_CloseFont(state->content_font);
    }
    free(state->lines);
    free(state->line_offsets);
    reader_document_free(&state->doc);
    memset(state, 0, sizeof(*state));
    if (fallback_font_path[0]) {
        snprintf(state->fallback_font_path, sizeof(state->fallback_font_path), "%s",
                 fallback_font_path);
    }
}

static int reader_is_catalog_item_current(const ReaderViewState *state, const ReaderCatalogItem *item) {
    if (!state || !item) {
        return 0;
    }
    if (item->is_current) {
        return 1;
    }
    if (state->doc.chapter_uid && item->chapter_uid &&
        strcmp(state->doc.chapter_uid, item->chapter_uid) == 0) {
        return 1;
    }
    if (state->doc.chapter_idx > 0 && item->chapter_idx == state->doc.chapter_idx) {
        return 1;
    }
    return 0;
}

static void render_reader(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
                          ReaderViewState *state, const char *battery_text,
                          const UiLayout *layout) {
    static const int margin = 32;
    static const int header_h = 60;
    static const int footer_h = 56;
    const UiTheme *theme = ui_current_theme();
    SDL_Color ink = theme->ink;
    SDL_Color muted = theme->muted;
    SDL_Color line = theme->line;
    TTF_Font *content_font = state->content_font ? state->content_font : body_font;
    int canvas_w = layout ? layout->canvas_w : UI_CANVAS_WIDTH;
    int canvas_h = layout ? layout->canvas_h : UI_CANVAS_HEIGHT;
    int cw = layout ? layout->content_w : canvas_w;
    int cx = layout ? layout->content_x : 0;
    int start_line = state->current_page * state->lines_per_page;
    int end_line = start_line + state->lines_per_page;
    int line_h = state->line_height > 0 ? state->line_height : TTF_FontLineSkip(content_font);
    int total_pages = state->line_count > 0 ?
        (state->line_count + state->lines_per_page - 1) / state->lines_per_page : 1;
    time_t now = time(NULL);
    struct tm *local_tm = localtime(&now);
    char time_buf[32];
    char footer[256];
    char title_buf[256];
    SDL_Rect header_band = { 0, 0, canvas_w, header_h };
    SDL_Rect header_line = { 0, header_h, canvas_w, 1 };
    SDL_Rect footer_line = { 0, canvas_h - footer_h, canvas_w, 1 };
    char chapter_heading[256];
    int info_h = canvas_h - footer_line.y;
    int footer_text_y = footer_line.y + (info_h - (body_font ? TTF_FontHeight(body_font) : 28)) / 2;
    int title_y = (header_h - (title_font ? TTF_FontHeight(title_font) : 36)) / 2;

    /* Content area: top-aligned with a font-size-aware top inset */
    int content_top = header_h + reader_top_inset_for_font_size(state->content_font_size);
    int content_bottom = canvas_h - footer_h - 4;
    int content_x = cx + margin;
    int y = content_top;

    SDL_SetRenderDrawColor(renderer, theme->bg_r, theme->bg_g, theme->bg_b, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, theme->header_r, theme->header_g, theme->header_b, 255);
    SDL_RenderFillRect(renderer, &header_band);
    SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
    SDL_RenderFillRect(renderer, &header_line);
    SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
    SDL_RenderFillRect(renderer, &footer_line);

    reader_format_chapter_heading(state, chapter_heading, sizeof(chapter_heading));
    fit_text_ellipsis(title_font,
                      chapter_heading[0] ? chapter_heading :
                      (state->doc.book_title ? state->doc.book_title : ""),
                      cw - 2 * margin - 140,
                      title_buf, sizeof(title_buf));
    draw_text(renderer, title_font, cx + margin, title_y, muted, title_buf);
    render_header_status(renderer, body_font, battery_text, layout);

    if (end_line > state->line_count) {
        end_line = state->line_count;
    }
    {
        int visible_lines = 0;
        int content_area_h = content_bottom - content_top;
        int block_offset_y = 0;

        for (int i = start_line; i < end_line; i++) {
            if (content_top + visible_lines * line_h + line_h > content_bottom) {
                break;
            }
            visible_lines++;
        }

        if (visible_lines > 0 &&
            visible_lines >= state->lines_per_page &&
            end_line < state->line_count) {
            int visible_text_h = visible_lines * line_h;
            block_offset_y = (content_area_h - visible_text_h) / 2;
            if (block_offset_y < 0) {
                block_offset_y = 0;
            }
        }

        y = content_top + block_offset_y;
        for (int i = start_line; i < start_line + visible_lines; i++) {
            draw_text(renderer, content_font, content_x, y, ink, state->lines[i]);
            y += line_h;
        }
    }

    if (local_tm) {
        strftime(time_buf, sizeof(time_buf), "%H:%M", local_tm);
        draw_text(renderer, body_font, cx + margin, footer_text_y, muted, time_buf);
    }
    snprintf(footer, sizeof(footer), "%d/%d", state->current_page + 1, total_pages);
    {
        int fw = 0, fh = 0;
        TTF_SizeUTF8(body_font, footer, &fw, &fh);
        draw_text(renderer, body_font, cx + cw - margin - fw, footer_text_y, muted, footer);
    }
}

static int ui_settings_visible_item_count(void) {
    return UI_SETTINGS_ITEM_COUNT;
}

static void ui_settings_clear_logout_confirm(SettingsFlowState *settings_state) {
    if (!settings_state) {
        return;
    }
    settings_state->logout_confirm_armed = 0;
}

static const char *ui_logout_status_text(SessionLogoutOutcome outcome) {
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

static void ui_transition_to_login_required(UiView *view, AuthSession *session, int *login_active,
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
        reader_view_free(reader_state);
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

static const char *ui_rotation_label(UiRotation rotation) {
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

static int ui_settings_effective_font_size(const SettingsFlowState *settings_state,
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
        snprintf(out, out_size, "%s", ui_dark_mode ? "On" : "Off");
        break;
    case UI_SETTINGS_ITEM_BRIGHTNESS:
        if (brightness_level < UI_BRIGHTNESS_MIN || brightness_level > UI_BRIGHTNESS_MAX) {
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

static void render_settings(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
                            const SettingsFlowState *settings_state,
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
    int item_count = ui_settings_visible_item_count();
    int canvas_w = layout ? layout->canvas_w : UI_CANVAS_WIDTH;
    int canvas_h = layout ? layout->canvas_h : UI_CANVAS_HEIGHT;
    int cw = layout ? layout->content_w : canvas_w;
    int cx = layout ? layout->content_x : 0;
    int panel_w = cw < 760 ? cw : 760;
    SDL_Rect backdrop = { 0, 0, canvas_w, canvas_h };
    SDL_Rect panel = { cx + cw - panel_w, 0, panel_w, canvas_h };
    SDL_Rect header = { panel.x, 0, panel_w, 84 };
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
    SDL_RenderFillRect(renderer, &status_bar);
    draw_rect_outline(renderer, &panel, line, 1);

    draw_text(renderer, title_font, header.x + 24, title_y, ink, "Settings");
    render_header_status(renderer, body_font, battery_text, layout);

    list_top = header.y + header.h + 14;
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
        SDL_Rect row = { panel.x + 16, list_top + i * (row_h + list_gap), panel.w - 32, row_h };
        char value_buf[64];
        char title_buf[64];
        char value_fit_buf[64];
        int value_w = 0;
        int font_h = body_font ? TTF_FontHeight(body_font) : 28;
        int text_y = row.y + (row.h - font_h) / 2;
        int value_max_w = row.w / 3;
        int title_max_w;

        if (settings_state && settings_state->selected == i) {
            SDL_SetRenderDrawColor(renderer, theme->catalog_current_r, theme->catalog_current_g,
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

static int ui_settings_apply(SettingsFlowState *settings_state, ApiContext *ctx,
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
        ui_dark_mode = !ui_dark_mode;
        preferences_state_save_dark_mode(ctx, ui_dark_mode);
        snprintf(status, status_size, "Dark mode %s", ui_dark_mode ? "on" : "off");
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

static void render_catalog_overlay(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
                                   ReaderViewState *state, float progress, float selected_pos,
                                   const UiLayout *layout) {
    const UiTheme *theme = ui_current_theme();
    SDL_Color ink = theme->ink;
    SDL_Color dim = theme->dim;
    SDL_Color line = theme->line;
    int canvas_w = layout ? layout->canvas_w : UI_CANVAS_WIDTH;
    int canvas_h = layout ? layout->canvas_h : UI_CANVAS_HEIGHT;
    int cw = layout ? layout->content_w : canvas_w;
    int cx = layout ? layout->content_x : 0;
    int panel_w = cw < 760 ? cw : 760;
    SDL_Rect backdrop = { 0, 0, canvas_w, canvas_h };
    SDL_Rect panel = { cx + cw - panel_w, 0, panel_w, canvas_h };
    SDL_Rect header = { panel.x, 0, panel_w, 84 };
    int line_height = body_font ? TTF_FontLineSkip(body_font) + 10 : 38;
    int list_top;
    int visible;
    int cursor_slot;
    int first_visible;
    int last_visible;
    char title_buf[256];
    int header_title_y;
    float eased = ui_ease_out_cubic(progress);
    float row_eased = ui_ease_out_cubic(progress);
    float selected_clamped;
    float scroll_origin;
    int panel_offset;
    Uint8 backdrop_alpha;
    Uint8 panel_alpha;
    SDL_Rect selected_row;

    if (!state || !state->doc.catalog_items || state->doc.catalog_count <= 0) {
        return;
    }
    if (progress <= 0.0f) {
        return;
    }
    if (panel.x < cx) {
        panel.x = cx;
        panel.w = cw;
        header.x = panel.x;
        header.w = panel.w;
    }
    panel_offset = (int)lroundf((1.0f - eased) * 72.0f);
    panel.x += panel_offset;
    header.x = panel.x;
    list_top = header.y + header.h + 12;
    visible = line_height > 0 ? (panel.h - 116) / line_height : 15;
    if (visible < 6) {
        visible = 6;
    }
    selected_clamped = selected_pos;
    if (selected_clamped < 0.0f) {
        selected_clamped = 0.0f;
    }
    if (selected_clamped > (float)(state->doc.catalog_count - 1)) {
        selected_clamped = (float)(state->doc.catalog_count - 1);
    }
    cursor_slot = visible / 2;
    if (cursor_slot < 0) {
        cursor_slot = 0;
    }
    scroll_origin = selected_clamped - (float)cursor_slot;
    first_visible = (int)floorf(scroll_origin) - 1;
    last_visible = (int)ceilf(scroll_origin + (float)visible) + 1;
    if (first_visible < 0) {
        first_visible = 0;
    }
    if (last_visible > state->doc.catalog_count) {
        last_visible = state->doc.catalog_count;
    }

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
    draw_rect_outline(renderer, &panel, line, 1);

    fit_text_ellipsis(title_font, state->doc.book_title ? state->doc.book_title : "目录",
                      header.w - 120, title_buf, sizeof(title_buf));
    header_title_y = header.y + (header.h - (title_font ? TTF_FontHeight(title_font) : 36)) / 2;
    draw_text(renderer, title_font, header.x + 24, header_title_y, ink, title_buf);

    selected_row.x = panel.x + 16;
    selected_row.y = list_top + cursor_slot * line_height;
    selected_row.w = panel.w - 32;
    selected_row.h = line_height - 4;
    if (selected_row.y < list_top) {
        selected_row.y = list_top;
    }
    if (selected_row.y + selected_row.h > panel.y + panel.h - 20) {
        selected_row.y = panel.y + panel.h - 20 - selected_row.h;
    }
    SDL_SetRenderDrawColor(renderer, theme->catalog_highlight_r, theme->catalog_highlight_g,
                           theme->catalog_highlight_b, 255);
    SDL_RenderFillRect(renderer, &selected_row);

    for (int i = first_visible; i < last_visible; i++) {
        ReaderCatalogItem *item = &state->doc.catalog_items[i];
        int row_y = list_top +
            (int)lroundf(((float)i - scroll_origin) * (float)line_height);
        SDL_Rect row = { panel.x + 16, row_y, panel.w - 32, line_height - 4 };
        char row_buf[256];
        int indent = item->level > 1 ? (item->level - 1) * 20 : 0;
        int text_x_shift =
            (int)lroundf((1.0f - row_eased) *
                         (14.0f + fminf(fabsf((float)i - selected_clamped), 4.0f) * 5.0f));
        SDL_Color color = item->is_lock ? dim : ink;

        if (row.y + row.h < list_top || row.y > panel.y + panel.h - 20) {
            continue;
        }

        if (reader_is_catalog_item_current(state, item) &&
            fabsf((float)i - selected_clamped) >= 0.45f) {
            SDL_SetRenderDrawColor(renderer, theme->catalog_current_r, theme->catalog_current_g,
                                   theme->catalog_current_b, 255);
            SDL_RenderFillRect(renderer, &row);
        }

        fit_text_ellipsis(body_font, item->title ? item->title : "(untitled)",
                          row.w - 72 - indent, row_buf, sizeof(row_buf));
        draw_text(renderer, body_font, row.x + 16 + indent + text_x_shift, row.y + 6, color, row_buf);
    }
}

int ui_is_available(void) {
    return 1;
}

int ui_run(ApiContext *ctx, const char *font_path, const char *platform) {
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Joystick **joysticks = NULL;
    TTF_Font *title_font = NULL;
    TTF_Font *body_font = NULL;
    cJSON *shelf_nuxt = NULL;
    ReaderViewState reader_state;
    StartupState startup_state;
    ReaderOpenState reader_open;
    UiView view = VIEW_SHELF;
    int selected = 0;
    int shelf_start = 0;
    int running = 1;
    Uint32 last_poll = 0;
    AuthSession session;
    LoginStartState login_start;
    LoginPollState login_poll;
    ProgressReportState progress_report;
    ShelfCoverCache shelf_covers;
    ShelfCoverDownloadState shelf_cover_download;
    ChapterPrefetchCache chapter_prefetch_cache;
    CatalogHydrationState catalog_hydration;
    SDL_Thread *login_thread = NULL;
    SDL_Thread *startup_thread_handle = NULL;
    SDL_Thread *reader_open_thread_handle = NULL;
    SDL_Thread *shelf_cover_download_thread_handle = NULL;
    SDL_Thread *login_poll_thread_handle = NULL;
    SDL_Thread *progress_report_thread_handle = NULL;
    SDL_Thread *catalog_hydration_thread_handle = NULL;
    SDL_Texture *scene_texture = NULL;
    SDL_Texture *qr_texture = NULL;
    int qr_tex_w = 0, qr_tex_h = 0;
    int login_active = 0;
    char status[256] = "";
    char shelf_status[256] = "";
    /* 微信读书 */
    char loading_title[128] = "\xE5\xBE\xAE\xE4\xBF\xA1\xE8\xAF\xBB\xE4\xB9\xA6";
    char qr_path[1024];
    int tg5040_input = ui_is_tg5040_platform(platform);
    int tg5040_select_pressed = 0;
    int tg5040_start_pressed = 0;
    int brightness_level = -1;
    int joystick_count = 0;
    UiRotation rotation = UI_ROTATE_LANDSCAPE;
    UiLayout current_layout = ui_layout_for_rotation(UI_ROTATE_LANDSCAPE);
    UiRepeatState repeat_state;
    UiInputSuppression input_suppression;
    UiMotionState motion_state;
    UiHapticState haptic_state;
    UiBatteryState battery_state;
    SettingsFlowState settings_state;
    int preferred_reader_font_size = UI_READER_CONTENT_FONT_SIZE;
    Uint32 poor_network_toast_until = 0;
    Uint32 exit_confirm_until = 0;
    Uint32 reader_exit_confirm_until = 0;
    UiInputMask observed_input_mask = 0;
    Uint32 lock_button_ignore_until = 0;
    Uint32 last_lock_trigger_tick = 0;
    time_t last_clock_minute = 0;
    int login_back_latch = 0;
    int login_confirm_latch = 0;
    int shelf_back_latch = 0;
    int shelf_confirm_latch = 0;
    int shelf_resume_latch = 0;
    int shelf_next_latch = 0;
    int shelf_prev_latch = 0;
    int rc = -1;

    memset(&session, 0, sizeof(session));
    memset(&login_start, 0, sizeof(login_start));
    memset(&login_poll, 0, sizeof(login_poll));
    memset(&startup_state, 0, sizeof(startup_state));
    memset(&reader_open, 0, sizeof(reader_open));
    memset(&progress_report, 0, sizeof(progress_report));
    memset(&shelf_covers, 0, sizeof(shelf_covers));
    ui_shelf_flow_cover_download_state_reset(&shelf_cover_download);
    memset(&chapter_prefetch_cache, 0, sizeof(chapter_prefetch_cache));
    memset(&catalog_hydration, 0, sizeof(catalog_hydration));
    memset(&reader_state, 0, sizeof(reader_state));
    memset(&repeat_state, 0, sizeof(repeat_state));
    ui_input_suppression_reset(&input_suppression);
    memset(&motion_state, 0, sizeof(motion_state));
    memset(&haptic_state, 0, sizeof(haptic_state));
    memset(&battery_state, 0, sizeof(battery_state));
    ui_settings_flow_state_reset(&settings_state);
    snprintf(qr_path, sizeof(qr_path), "%s/weread-login-qr.png", ctx->data_dir);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_JoystickEventState(SDL_ENABLE);
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        goto cleanup;
    }
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
        goto cleanup;
    }

    window = SDL_CreateWindow("WeRead", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              1024, 768, SDL_WINDOW_FULLSCREEN_DESKTOP);
    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_PRESENTVSYNC |
                                  SDL_RENDERER_TARGETTEXTURE);
    if (window && !renderer) {
        renderer = SDL_CreateRenderer(window, -1,
                                      SDL_RENDERER_SOFTWARE |
                                      SDL_RENDERER_TARGETTEXTURE);
    }
    if (!window || !renderer) {
        fprintf(stderr, "SDL window setup failed: %s\n", SDL_GetError());
        goto cleanup;
    }
    if (ui_recreate_scene_texture(renderer, &scene_texture, &current_layout) != 0) {
        fprintf(stderr, "Failed to create scene texture: %s\n", SDL_GetError());
        goto cleanup;
    }

    joystick_count = SDL_NumJoysticks();
    if (joystick_count > 0) {
        joysticks = calloc((size_t)joystick_count, sizeof(*joysticks));
        if (!joysticks) {
            fprintf(stderr, "Failed to allocate joystick handles.\n");
            goto cleanup;
        }
        for (int i = 0; i < joystick_count; i++) {
            joysticks[i] = SDL_JoystickOpen(i);
        }
    }
    observed_input_mask = ui_input_current_mask(tg5040_input, joysticks, joystick_count);

    if (font_path && *font_path) {
        title_font = TTF_OpenFont(font_path, UI_TITLE_FONT_SIZE);
        body_font = TTF_OpenFont(font_path, UI_BODY_FONT_SIZE);
    }
    if (!title_font || !body_font) {
        fprintf(stderr, "Failed to open font: %s\n", font_path ? font_path : "(null)");
        goto cleanup;
    }
    if (font_path && *font_path) {
        snprintf(reader_state.fallback_font_path, sizeof(reader_state.fallback_font_path), "%s",
                 font_path);
    }
    (void)ui_platform_init_haptics(tg5040_input, &haptic_state);

    ui_dark_mode = preferences_state_load_dark_mode(ctx);
    if (preferences_state_load_brightness_level(ctx, &brightness_level) == 0) {
        ui_platform_apply_brightness_level(tg5040_input, brightness_level);
    }
    (void)preferences_state_load_reader_font_size(ctx, &preferred_reader_font_size);
    {
        int saved_rotation = 0;

        if (preferences_state_load_rotation(ctx, &saved_rotation) == 0) {
            rotation = (UiRotation)saved_rotation;
            current_layout = ui_layout_for_rotation(rotation);
            if (ui_recreate_scene_texture(renderer, &scene_texture, &current_layout) != 0) {
                fprintf(stderr, "Failed to apply saved rotation: %s\n", SDL_GetError());
                goto cleanup;
            }
        }
    }

    shelf_nuxt = shelf_state_load_cache(ctx);
    if (shelf_nuxt && shelf_books(shelf_nuxt) && cJSON_IsArray(shelf_books(shelf_nuxt))) {
        shelf_status[0] = '\0';
        ui_shelf_flow_cover_download_stop(&shelf_cover_download,
                                          &shelf_cover_download_thread_handle);
        shelf_cover_cache_build(ctx, shelf_nuxt, &shelf_covers);
        selected = shelf_ui_clamp_selection(shelf_nuxt, shelf_ui_default_selection(shelf_nuxt));
        view = VIEW_SHELF;
    } else {
        cJSON_Delete(shelf_nuxt);
        shelf_nuxt = NULL;
        view = VIEW_BOOTSTRAP;
        /* 正在检查书架... */
        snprintf(status, sizeof(status), "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\xA3\x80\xE6\x9F\xA5\xE4\xB9\xA6\xE6\x9E\xB6...");
    }
    ui_startup_login_begin_startup_refresh(ctx, &startup_state, &startup_thread_handle);

    while (running) {
        SDL_Event event;
        Uint32 frame_now = SDL_GetTicks();
        time_t wall_now = time(NULL);
        float dt_seconds = motion_state.last_tick > 0 && frame_now > motion_state.last_tick ?
            (float)(frame_now - motion_state.last_tick) / 1000.0f : 0.0f;
        UiView frame_view_before_updates = view;
        int catalog_open_before_updates = reader_state.catalog_open;
        float shelf_selected_visual_before_updates = motion_state.shelf_selected_visual;
        float catalog_selected_visual_before_updates = motion_state.catalog_selected_visual;
        float catalog_progress_before_updates = motion_state.catalog_progress;
        float settings_progress_before_updates = motion_state.settings_progress;
        char frame_reader_target_before[2048];
        int input_transition_started = 0;
        int reader_input_seen = 0;
        int render_requested = 0;
        int shelf_back_event_seen = 0;
        int shelf_confirm_event_seen = 0;
        int shelf_resume_event_seen = 0;
        int shelf_nav_event_seen = 0;
        time_t current_clock_minute = wall_now / 60;

        ui_copy_string(frame_reader_target_before, sizeof(frame_reader_target_before),
                       reader_state.doc.target ? reader_state.doc.target : "");
        motion_state.last_tick = frame_now;
        if (last_clock_minute == 0) {
            last_clock_minute = current_clock_minute;
        } else if (current_clock_minute != last_clock_minute) {
            last_clock_minute = current_clock_minute;
            render_requested = 1;
        }
        ui_battery_state_update(&battery_state, frame_now);
        ui_platform_haptic_poll(&haptic_state, frame_now);
        ui_input_suppression_refresh(&input_suppression, observed_input_mask);
        while (SDL_PollEvent(&event)) {
            UiView event_view_before = view;
            int event_catalog_before = reader_state.catalog_open;
            char event_reader_target_before[2048];

            ui_copy_string(event_reader_target_before, sizeof(event_reader_target_before),
                           reader_state.doc.target ? reader_state.doc.target : "");

            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_RENDER_TARGETS_RESET ||
                       event.type == SDL_RENDER_DEVICE_RESET) {
                if (ui_recreate_scene_texture(renderer, &scene_texture, &current_layout) != 0) {
                    fprintf(stderr, "Failed to recover renderer after reset: %s\n", SDL_GetError());
                    running = 0;
                    break;
                }
                motion_state.last_tick = SDL_GetTicks();
                repeat_state.action = UI_REPEAT_NONE;
                repeat_state.next_tick = 0;
            } else if (event.type == SDL_JOYBUTTONUP) {
                if (tg5040_input && event.jbutton.button == TG5040_JOY_SELECT) {
                    tg5040_select_pressed = 0;
                } else if (tg5040_input && event.jbutton.button == TG5040_JOY_START) {
                    tg5040_start_pressed = 0;
                }
            }
            observed_input_mask = ui_input_apply_event(observed_input_mask, &event, tg5040_input);
            ui_input_suppression_refresh(&input_suppression, observed_input_mask);

            if (event.type == SDL_KEYDOWN ||
                event.type == SDL_JOYBUTTONDOWN ||
                event.type == SDL_JOYHATMOTION ||
                event.type == SDL_JOYAXISMOTION) {
                if (view == VIEW_READER) {
                    reader_input_seen = 1;
                }
                if (tg5040_input && event.type == SDL_JOYBUTTONDOWN) {
                    if (event.jbutton.button == TG5040_JOY_SELECT) {
                        tg5040_select_pressed = 1;
                    } else if (event.jbutton.button == TG5040_JOY_START) {
                        tg5040_start_pressed = 1;
                    }
                }
                if (view == VIEW_SHELF) {
                    if (ui_event_is_back(&event, tg5040_input, &input_suppression)) {
                        shelf_back_event_seen = 1;
                    }
                    if (ui_event_is_confirm(&event, tg5040_input, &input_suppression)) {
                        shelf_confirm_event_seen = 1;
                    }
                    if (ui_event_is_shelf_resume(&event, tg5040_input,
                                                 &input_suppression)) {
                        shelf_resume_event_seen = 1;
                    }
                    if (ui_event_is_up(&event, tg5040_input, &input_suppression) ||
                        ui_event_is_down(&event, tg5040_input, &input_suppression) ||
                        ui_event_is_left(&event, tg5040_input, &input_suppression) ||
                        ui_event_is_right(&event, tg5040_input, &input_suppression)) {
                        shelf_nav_event_seen = 1;
                    }
                }
                if (ui_event_is_back(&event, tg5040_input, &input_suppression) ||
                    (view == VIEW_SETTINGS &&
                     ui_event_is_settings_open(&event, tg5040_input,
                                               &input_suppression))) {
                    if (view == VIEW_LOGIN) {
                        login_active = 0;
                        login_poll.stop = 1;
                        ui_force_exit_from_login(&haptic_state);
                    } else if (view == VIEW_SETTINGS) {
                        ui_settings_clear_logout_confirm(&settings_state);
                        if (ui_settings_flow_begin_close(&settings_state) == 0) {
                            render_requested = 1;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 35);
                        }
                    } else if (view == VIEW_READER) {
                        if (reader_state.catalog_open) {
                            reader_state.catalog_open = 0;
                            render_requested = 1;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 35);
                        } else if (reader_exit_confirm_until > frame_now) {
                            ui_reader_view_flush_progress_blocking(ctx, &reader_state, 1);
                            ui_reader_view_save_local_position(ctx, &reader_state);
                            reader_exit_confirm_until = 0;
                            exit_confirm_until = 0;
                            view = VIEW_SHELF;
                            render_requested = 1;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 50);
                        } else {
                            reader_exit_confirm_until = frame_now + UI_EXIT_CONFIRM_DURATION_MS;
                            render_requested = 1;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 35);
                        }
                    } else if (view == VIEW_OPENING && reader_open.running) {
                        /* 正在打开书籍... */
                        snprintf(status, sizeof(status), "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x89\x93\xE5\xBC\x80\xE4\xB9\xA6\xE7\xB1\x8D...");
                    } else {
                        if (exit_confirm_until > frame_now) {
                            running = 0;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 50);
                        } else {
                            exit_confirm_until = frame_now + UI_EXIT_CONFIRM_DURATION_MS;
                            render_requested = 1;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 35);
                        }
                    }
                } else if (ui_event_is_lock_button(&event, &input_suppression) &&
                           frame_now >= lock_button_ignore_until &&
                           (last_lock_trigger_tick == 0 ||
                            frame_now - last_lock_trigger_tick >= 1000)) {
                    last_lock_trigger_tick = frame_now;
                    if (view == VIEW_READER) {
                        ui_reader_view_flush_progress_blocking(ctx, &reader_state, 1);
                        ui_reader_view_save_local_position(ctx, &reader_state);
                    }
                    if (ui_platform_lock_screen(tg5040_input) == 0) {
                        tg5040_select_pressed = 0;
                        tg5040_start_pressed = 0;
                        repeat_state.action = UI_REPEAT_NONE;
                        repeat_state.next_tick = 0;
                        motion_state.last_tick = SDL_GetTicks();
                        lock_button_ignore_until = motion_state.last_tick + 1200;
                        if (ui_platform_restore_after_sleep(renderer, &scene_texture,
                                                            &current_layout, tg5040_input,
                                                            brightness_level) != 0) {
                            running = 0;
                            break;
                        }
                    }
                } else if (view == VIEW_SHELF) {
                    int count = shelf_ui_normal_count(shelf_nuxt);
                    int has_article = shelf_ui_has_article_slot(shelf_nuxt, NULL);
                    int min_selected = has_article ? -1 : 0;
                    if (ui_event_is_settings_open(&event, tg5040_input,
                                                  &input_suppression)) {
                        if (ui_settings_flow_open_from_shelf(&settings_state, &view, 1,
                                                             selected) == 0) {
                            exit_confirm_until = 0;
                            render_requested = 1;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 45);
                        }
                    } else if ((ui_event_is_down(&event, tg5040_input, &input_suppression) ||
                                ui_event_is_right(&event, tg5040_input, &input_suppression)) &&
                                selected + 1 < count) {
                        selected++;
                        render_requested = 1;
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                    } else if ((ui_event_is_up(&event, tg5040_input, &input_suppression) ||
                                ui_event_is_left(&event, tg5040_input, &input_suppression)) &&
                               selected > min_selected) {
                        selected--;
                        render_requested = 1;
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                    } else if (ui_event_is_shelf_resume(&event, tg5040_input,
                                                        &input_suppression)) {
                        char target[2048];
                        int font_size = 3;
                        if (ui_shelf_flow_prepare_resume(ctx, target, sizeof(target), &font_size,
                                                         loading_title, sizeof(loading_title),
                                                         status, sizeof(status))) {
                            ui_reader_flow_begin_reader_open(ctx, &reader_open,
                                                             &reader_open_thread_handle,
                                                             target, NULL, font_size);
                            if (reader_open.running || reader_open_thread_handle) {
                                view = VIEW_OPENING;
                                render_requested = 1;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                            }
                        }
                    } else if (ui_event_is_confirm(&event, tg5040_input,
                                                   &input_suppression) &&
                               selected < 0 && has_article) {
                        char target[2048];

                        if (ui_shelf_flow_prepare_article_open(ctx, shelf_nuxt, 3,
                                                               target, sizeof(target),
                                                               loading_title, sizeof(loading_title),
                                                               status, sizeof(status),
                                                               shelf_status,
                                                               sizeof(shelf_status))) {
                            ui_reader_flow_begin_reader_open(ctx, &reader_open,
                                                             &reader_open_thread_handle,
                                                             target, NULL, 3);
                            if (reader_open.running || reader_open_thread_handle) {
                                view = VIEW_OPENING;
                                render_requested = 1;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                            } else {
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE5\x90\xAF\xE5\x8A\xA8\xE5\x8A\xA0\xE8\xBD\xBD\xE4\xBB\xBB\xE5\x8A\xA1");
                            }
                        }
                    } else if (ui_event_is_confirm(&event, tg5040_input,
                                                   &input_suppression) && count > 0) {
                        char target[2048];
                        char book_id[256];

                        if (ui_shelf_flow_prepare_selected_open(shelf_nuxt, selected,
                                                                target, sizeof(target),
                                                                book_id, sizeof(book_id),
                                                                loading_title, sizeof(loading_title),
                                                                status, sizeof(status),
                                                                shelf_status, sizeof(shelf_status))) {
                            ui_reader_flow_begin_reader_open(ctx, &reader_open,
                                                             &reader_open_thread_handle,
                                                             target, book_id[0] ? book_id : NULL,
                                                             3);
                            if (reader_open.running || reader_open_thread_handle) {
                                view = VIEW_OPENING;
                                render_requested = 1;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                            } else {
                                /* 无法启动加载任务 */
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE5\x90\xAF\xE5\x8A\xA8\xE5\x8A\xA0\xE8\xBD\xBD\xE4\xBB\xBB\xE5\x8A\xA1");
                            }
                        }
                    }
                } else if (view == VIEW_LOGIN) {
                    if ((ui_event_is_confirm(&event, tg5040_input, &input_suppression) ||
                         ui_event_is_keydown(&event, SDLK_l, SDL_SCANCODE_L,
                                             &input_suppression)) &&
                        !login_start.running && !login_active) {
                        if (qr_texture) { SDL_DestroyTexture(qr_texture); qr_texture = NULL; }
                        ui_startup_login_begin_login_flow(ctx, &login_start, &login_thread, &view,
                                                          status, sizeof(status), qr_path);
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                    }
                } else if (view == VIEW_BOOTSTRAP) {
                    if (ui_event_is_confirm(&event, tg5040_input, &input_suppression) &&
                        !startup_state.running && !startup_thread_handle) {
                        /* 微信读书 */
                        snprintf(loading_title, sizeof(loading_title), "\xE5\xBE\xAE\xE4\xBF\xA1\xE8\xAF\xBB\xE4\xB9\xA6");
                        /* 正在重试... */
                        snprintf(status, sizeof(status), "\xE6\xAD\xA3\xE5\x9C\xA8\xE9\x87\x8D\xE8\xAF\x95...");
                        ui_startup_login_begin_startup_refresh(ctx, &startup_state,
                                                               &startup_thread_handle);
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                    }
                } else if (view == VIEW_OPENING) {
                    if (ui_event_is_confirm(&event, tg5040_input, &input_suppression) &&
                        !reader_open.running && !reader_open_thread_handle &&
                        reader_open.source_target[0]) {
                        /* 正在重试... */
                        snprintf(status, sizeof(status), "\xE6\xAD\xA3\xE5\x9C\xA8\xE9\x87\x8D\xE8\xAF\x95...");
                        ui_reader_flow_begin_reader_open(ctx, &reader_open,
                                                         &reader_open_thread_handle,
                                                         reader_open.source_target,
                                                         reader_open.book_id[0] ?
                                                         reader_open.book_id : NULL,
                                                         reader_open.font_size);
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                    }
                } else if (view == VIEW_SETTINGS) {
                    int item_count = ui_settings_visible_item_count();

                    if (ui_event_is_down(&event, tg5040_input, &input_suppression) &&
                        settings_state.selected + 1 < item_count) {
                        settings_state.selected++;
                        ui_settings_clear_logout_confirm(&settings_state);
                        render_requested = 1;
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                    } else if (ui_event_is_up(&event, tg5040_input, &input_suppression) &&
                               settings_state.selected > 0) {
                        settings_state.selected--;
                        ui_settings_clear_logout_confirm(&settings_state);
                        render_requested = 1;
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                    } else if (ui_event_is_confirm(&event, tg5040_input, &input_suppression) &&
                               settings_state.selected == UI_SETTINGS_ITEM_LOGOUT) {
                        if (!settings_state.logout_confirm_armed) {
                            settings_state.logout_confirm_armed = 1;
                            snprintf(shelf_status, sizeof(shelf_status),
                                     "Press A again to log out");
                            render_requested = 1;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 35);
                        } else {
                            SessionLogoutResult logout_result = {0};
                            int logout_rc = session_service_logout(ctx, &logout_result);

                            ui_settings_clear_logout_confirm(&settings_state);
                            if (logout_rc == 0 && logout_result.local_cleanup_ok) {
                                shelf_status[0] = '\0';
                                ui_transition_to_login_required(&view, &session, &login_active,
                                                                &settings_state, &reader_state,
                                                                &shelf_nuxt, &shelf_covers,
                                                                &shelf_cover_download,
                                                                &shelf_cover_download_thread_handle,
                                                                &qr_texture, &selected,
                                                                &exit_confirm_until,
                                                                &reader_exit_confirm_until,
                                                                status, sizeof(status),
                                                                ui_logout_status_text(
                                                                    logout_result.outcome));
                            } else {
                                snprintf(shelf_status, sizeof(shelf_status), "%s",
                                         ui_logout_status_text(SESSION_LOGOUT_LOCAL_FAILED));
                            }
                            render_requested = 1;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 45);
                        }
                    } else if ((ui_event_is_left(&event, tg5040_input, &input_suppression) ||
                                ui_event_is_right(&event, tg5040_input, &input_suppression)) &&
                               settings_state.selected != UI_SETTINGS_ITEM_LOGOUT) {
                        int apply_result = ui_settings_apply(&settings_state, ctx,
                                                             renderer, body_font,
                                                             &reader_state,
                                                             &preferred_reader_font_size,
                                                             tg5040_input,
                                                             &brightness_level,
                                                             &rotation,
                                                             &current_layout,
                                                             &scene_texture,
                                                             shelf_status,
                                                             sizeof(shelf_status),
                                                             ui_event_is_left(&event, tg5040_input,
                                                                              &input_suppression) ? -1 : 1);

                        ui_settings_clear_logout_confirm(&settings_state);
                        if (apply_result < 0) {
                            running = 0;
                            break;
                        }
                        if (apply_result > 0) {
                            render_requested = 1;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 45);
                        }
                    }
                } else if (view == VIEW_READER) {
                    int total_pages = ui_reader_view_total_pages(&reader_state);
                    ui_reader_view_note_progress_activity(&reader_state, SDL_GetTicks());
                    if (ui_event_is_settings_open(&event, tg5040_input,
                                                  &input_suppression) &&
                        !reader_state.catalog_open) {
                        if (ui_settings_flow_open_from_reader(&settings_state, &view, 1) == 0) {
                            reader_exit_confirm_until = 0;
                            render_requested = 1;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 45);
                        }
                    } else if (reader_state.catalog_open) {
                        if (ui_event_is_back(&event, tg5040_input, &input_suppression) ||
                            ui_event_is_catalog_toggle(&event, tg5040_input,
                                                       &input_suppression)) {
                            reader_state.catalog_open = 0;
                            render_requested = 1;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 35);
                        } else if (ui_event_is_up(&event, tg5040_input,
                                                  &input_suppression)) {
                            if (reader_state.catalog_selected > 0) {
                                reader_state.catalog_selected--;
                                render_requested = 1;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            } else if (ui_reader_view_expand_catalog_for_selection(ctx, &reader_state, -1,
                                                                                   shelf_status, sizeof(shelf_status)) > 0) {
                                reader_state.catalog_selected--;
                                render_requested = 1;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            }
                        } else if (ui_event_is_down(&event, tg5040_input,
                                                    &input_suppression)) {
                            if (reader_state.catalog_selected + 1 < reader_state.doc.catalog_count) {
                                reader_state.catalog_selected++;
                                render_requested = 1;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            } else if (ui_reader_view_expand_catalog_for_selection(ctx, &reader_state, 1,
                                                                                   shelf_status, sizeof(shelf_status)) > 0 &&
                                       reader_state.catalog_selected + 1 < reader_state.doc.catalog_count) {
                                reader_state.catalog_selected++;
                                render_requested = 1;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            }
                        } else if (ui_event_is_left(&event, tg5040_input,
                                                    &input_suppression)) {
                            if (reader_state.catalog_selected > 0) {
                                reader_state.catalog_selected -= 10;
                                if (reader_state.catalog_selected < 0) {
                                    reader_state.catalog_selected = 0;
                                }
                                render_requested = 1;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            } else if (ui_reader_view_expand_catalog_for_selection(ctx, &reader_state, -1,
                                                                                   shelf_status, sizeof(shelf_status)) > 0) {
                                reader_state.catalog_selected -= 10;
                                if (reader_state.catalog_selected < 0) {
                                    reader_state.catalog_selected = 0;
                                }
                                render_requested = 1;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            }
                        } else if (ui_event_is_right(&event, tg5040_input,
                                                     &input_suppression)) {
                            if (reader_state.catalog_selected + 1 < reader_state.doc.catalog_count) {
                                reader_state.catalog_selected += 10;
                                if (reader_state.catalog_selected >= reader_state.doc.catalog_count) {
                                    reader_state.catalog_selected = reader_state.doc.catalog_count - 1;
                                }
                                render_requested = 1;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            } else if (ui_reader_view_expand_catalog_for_selection(ctx, &reader_state, 1,
                                                                                   shelf_status, sizeof(shelf_status)) > 0) {
                                reader_state.catalog_selected += 10;
                                if (reader_state.catalog_selected >= reader_state.doc.catalog_count) {
                                    reader_state.catalog_selected = reader_state.doc.catalog_count - 1;
                                }
                                render_requested = 1;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            }
                        } else if (ui_event_is_confirm(&event, tg5040_input,
                                                       &input_suppression) &&
                                   reader_state.doc.catalog_items &&
                                   reader_state.catalog_selected >= 0 &&
                                   reader_state.catalog_selected < reader_state.doc.catalog_count) {
                            ReaderCatalogItem *item =
                                &reader_state.doc.catalog_items[reader_state.catalog_selected];
                            char source_target[2048];
                            int font_size = reader_state.doc.font_size;

                            if (item->target && item->target[0]) {
                                snprintf(source_target, sizeof(source_target), "%s",
                                         reader_state.source_target);
                                ui_reader_view_flush_progress_blocking(ctx, &reader_state, 1);
                                if (ui_reader_view_load(ctx, body_font, item->target, font_size,
                                                        current_layout.reader_content_w,
                                                        current_layout.reader_content_h, 0,
                                                        &reader_state) == 0) {
                                    ui_reader_view_set_source_target(&reader_state, source_target);
                                    ui_reader_view_save_local_position(ctx, &reader_state);
                                    reader_state.catalog_open = 0;
                                    render_requested = 1;
                                    shelf_status[0] = '\0';
                                    ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                                } else {
                                    /* 无法打开所选章节 */
                                    snprintf(shelf_status, sizeof(shelf_status),
                                             "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE6\x89\x80\xE9\x80\x89\xE7\xAB\xA0\xE8\x8A\x82");
                                }
                            }
                        }
                    } else if (ui_event_is_catalog_toggle(&event, tg5040_input,
                                                          &input_suppression)) {
                        fprintf(stderr,
                                "reader-catalog-toggle: target=%s kind=%s count=%d items=%p open=%d\n",
                                reader_state.doc.target ? reader_state.doc.target : "(null)",
                                reader_state.doc.kind == READER_DOCUMENT_KIND_ARTICLE ? "article" : "book",
                                reader_state.doc.catalog_count,
                                (void *)reader_state.doc.catalog_items,
                                reader_state.catalog_open);
                        if (reader_state.doc.catalog_items && reader_state.doc.catalog_count > 0) {
                            ui_reader_view_open_catalog(ctx, &reader_state, shelf_status, sizeof(shelf_status));
                            if (reader_state.catalog_open) {
                                render_requested = 1;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 50);
                            }
                        } else if (reader_state.doc.kind == READER_DOCUMENT_KIND_ARTICLE) {
                            snprintf(shelf_status, sizeof(shelf_status),
                                     "\xE5\xBD\x93\xE5\x89\x8D\xE6\x96\x87\xE7\xAB\xA0\xE6\x97\xA0\xE7\x9B\xAE\xE5\xBD\x95");
                            render_requested = 1;
                        }
                    } else if (ui_event_is_page_next(&event, tg5040_input,
                                                     &input_suppression) &&
                        reader_state.current_page + 1 < total_pages) {
                        reader_state.current_page++;
                        ui_reader_view_save_local_position(ctx, &reader_state);
                    } else if (ui_event_is_page_next(&event, tg5040_input,
                                                     &input_suppression) &&
                               reader_state.doc.next_target && reader_state.doc.next_target[0]) {
                        char *target = strdup(reader_state.doc.next_target);
                        char source_target[2048];
                        int font_size = reader_state.doc.font_size;
                        if (target) {
                            snprintf(source_target, sizeof(source_target), "%s", reader_state.source_target);
                            ui_reader_view_flush_progress_blocking(ctx, &reader_state, 1);
                            if (ui_reader_flow_chapter_prefetch_cache_adopt(
                                    &chapter_prefetch_cache, target, body_font,
                                    &reader_state, &current_layout) == 0) {
                                ui_reader_view_set_source_target(&reader_state, source_target);
                                ui_reader_view_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                            } else if (ui_reader_view_load(ctx, body_font, target, font_size,
                                                           current_layout.reader_content_w,
                                                           current_layout.reader_content_h, 0,
                                                           &reader_state) == 0) {
                                ui_reader_view_set_source_target(&reader_state, source_target);
                                ui_reader_view_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                            } else {
                                /* 无法打开下一章 */
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8B\xE4\xB8\x80\xE7\xAB\xA0");
                            }
                            free(target);
                        }
                    } else if (ui_event_is_page_prev(&event, tg5040_input,
                                                     &input_suppression) &&
                               reader_state.current_page > 0) {
                        reader_state.current_page--;
                        ui_reader_view_save_local_position(ctx, &reader_state);
                    } else if (ui_event_is_page_prev(&event, tg5040_input,
                                                     &input_suppression) &&
                               reader_state.doc.prev_target && reader_state.doc.prev_target[0]) {
                        char *target = strdup(reader_state.doc.prev_target);
                        char source_target[2048];
                        int font_size = reader_state.doc.font_size;
                        if (target) {
                            snprintf(source_target, sizeof(source_target), "%s", reader_state.source_target);
                            ui_reader_view_flush_progress_blocking(ctx, &reader_state, 1);
                            if (ui_reader_flow_chapter_prefetch_cache_adopt(
                                    &chapter_prefetch_cache, target, body_font,
                                    &reader_state, &current_layout) == 0) {
                                ui_reader_view_set_source_target(&reader_state, source_target);
                                int new_total_pages = ui_reader_view_total_pages(&reader_state);
                                reader_state.current_page = new_total_pages > 0 ? new_total_pages - 1 : 0;
                                ui_reader_view_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                            } else if (ui_reader_view_load(ctx, body_font, target, font_size,
                                                            current_layout.reader_content_w,
                                                            current_layout.reader_content_h, 0,
                                                            &reader_state) == 0) {
                                ui_reader_view_set_source_target(&reader_state, source_target);
                                int new_total_pages = ui_reader_view_total_pages(&reader_state);
                                reader_state.current_page = new_total_pages > 0 ? new_total_pages - 1 : 0;
                                ui_reader_view_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                            } else {
                                /* 无法打开上一章 */
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8A\xE4\xB8\x80\xE7\xAB\xA0");
                            }
                            free(target);
                        }
                    } else if ((ui_event_is_chapter_prev(&event, tg5040_input,
                                                         &input_suppression) ||
                                ui_event_is_chapter_next(&event, tg5040_input,
                                                         &input_suppression)) &&
                               reader_state.current_page > 0) {
                        reader_state.current_page = 0;
                        ui_reader_view_save_local_position(ctx, &reader_state);
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 50);
                    } else if (ui_event_is_chapter_prev(&event, tg5040_input,
                                                        &input_suppression) &&
                               reader_state.doc.prev_target && reader_state.doc.prev_target[0]) {
                        char *target = strdup(reader_state.doc.prev_target);
                        char source_target[2048];
                        int font_size = reader_state.doc.font_size;
                        if (target) {
                            snprintf(source_target, sizeof(source_target), "%s", reader_state.source_target);
                            ui_reader_view_flush_progress_blocking(ctx, &reader_state, 1);
                            if (ui_reader_flow_chapter_prefetch_cache_adopt(
                                    &chapter_prefetch_cache, target, body_font,
                                    &reader_state, &current_layout) == 0) {
                                ui_reader_view_set_source_target(&reader_state, source_target);
                                ui_reader_view_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
                            } else if (ui_reader_view_load(ctx, body_font, target, font_size,
                                                            current_layout.reader_content_w,
                                                            current_layout.reader_content_h, 0,
                                                            &reader_state) == 0) {
                                ui_reader_view_set_source_target(&reader_state, source_target);
                                ui_reader_view_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
                            } else {
                                /* 无法打开上一章 */
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8A\xE4\xB8\x80\xE7\xAB\xA0");
                            }
                            free(target);
                        }
                    } else if (ui_event_is_chapter_next(&event, tg5040_input,
                                                        &input_suppression) &&
                               reader_state.doc.next_target && reader_state.doc.next_target[0]) {
                        char *target = strdup(reader_state.doc.next_target);
                        char source_target[2048];
                        int font_size = reader_state.doc.font_size;
                        if (target) {
                            snprintf(source_target, sizeof(source_target), "%s", reader_state.source_target);
                            ui_reader_view_flush_progress_blocking(ctx, &reader_state, 1);
                            if (ui_reader_flow_chapter_prefetch_cache_adopt(
                                    &chapter_prefetch_cache, target, body_font,
                                    &reader_state, &current_layout) == 0) {
                                ui_reader_view_set_source_target(&reader_state, source_target);
                                ui_reader_view_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
                            } else if (ui_reader_view_load(ctx, body_font, target, font_size,
                                                           current_layout.reader_content_w,
                                                           current_layout.reader_content_h, 0,
                                                           &reader_state) == 0) {
                                ui_reader_view_set_source_target(&reader_state, source_target);
                                ui_reader_view_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
                            } else {
                                /* 无法打开下一章 */
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8B\xE4\xB8\x80\xE7\xAB\xA0");
                            }
                            free(target);
                        }
                    }
                }
            }

            if (event_view_before != view ||
                event_catalog_before != reader_state.catalog_open ||
                (ui_reader_doc_target_changed(event_reader_target_before, &reader_state) &&
                 !ui_reader_should_skip_transition_for_page_turn(&event, tg5040_input,
                                                                 &input_suppression,
                                                                 event_view_before, view,
                                                                 event_catalog_before,
                                                                 &reader_state,
                                                                 event_reader_target_before))) {
                ui_begin_input_transition(&repeat_state,
                                          &input_suppression,
                                          &observed_input_mask,
                                          tg5040_input,
                                          joysticks,
                                          joystick_count,
                                          &tg5040_select_pressed,
                                          &tg5040_start_pressed,
                                          &exit_confirm_until,
                                          &reader_exit_confirm_until,
                                          &login_back_latch,
                                          &login_confirm_latch,
                                          &shelf_back_latch,
                                          &shelf_confirm_latch,
                                          &shelf_resume_latch,
                                          &shelf_next_latch,
                                          &shelf_prev_latch);
                input_transition_started = 1;
                /* Any remaining queued input belonged to the previous view.
                 * ui_begin_input_transition flushes it and re-seeds
                 * observed_input_mask from the live physical state. */
                break;
            }
        }

        {
            UiRepeatAction repeat_action =
                ui_repeat_action_current(view, &reader_state, tg5040_input,
                                         joysticks, joystick_count,
                                         &input_suppression);
            Uint32 now = SDL_GetTicks();

            if (repeat_action != repeat_state.action) {
                repeat_state.action = repeat_action;
                if (repeat_action == UI_REPEAT_PAGE_NEXT ||
                    repeat_action == UI_REPEAT_PAGE_PREV) {
                    repeat_state.next_tick = now + UI_PAGE_REPEAT_DELAY_MS;
                } else {
                    repeat_state.next_tick = repeat_action != UI_REPEAT_NONE ?
                        now + UI_INPUT_REPEAT_DELAY_MS : 0;
                }
            } else if (repeat_action != UI_REPEAT_NONE && now >= repeat_state.next_tick) {
                char repeat_reader_target_before[2048];

                ui_copy_string(repeat_reader_target_before,
                               sizeof(repeat_reader_target_before),
                               reader_state.doc.target ? reader_state.doc.target : "");
                ui_apply_repeat_action(repeat_action, ctx, body_font, &reader_state,
                                       shelf_nuxt, &selected, shelf_status,
                                       sizeof(shelf_status), &current_layout,
                                       &chapter_prefetch_cache);
                if (view == VIEW_READER) {
                    render_requested = 1;
                }
                if (ui_reader_doc_target_changed(repeat_reader_target_before,
                                                 &reader_state) &&
                    !(repeat_action == UI_REPEAT_PAGE_NEXT ||
                      repeat_action == UI_REPEAT_PAGE_PREV)) {
                    ui_begin_input_transition(&repeat_state,
                                              &input_suppression,
                                              &observed_input_mask,
                                              tg5040_input,
                                              joysticks,
                                              joystick_count,
                                              &tg5040_select_pressed,
                                              &tg5040_start_pressed,
                                              &exit_confirm_until,
                                              &reader_exit_confirm_until,
                                              &login_back_latch,
                                              &login_confirm_latch,
                                              &shelf_back_latch,
                                              &shelf_confirm_latch,
                                              &shelf_resume_latch,
                                              &shelf_next_latch,
                                              &shelf_prev_latch);
                    input_transition_started = 1;
                } else {
                    repeat_state.next_tick = now + UI_INPUT_REPEAT_INTERVAL_MS;
                }
            }
        }

        if (view == VIEW_LOGIN) {
            int login_confirm_held =
                ui_login_confirm_is_held(tg5040_input, joysticks, joystick_count,
                                         &input_suppression);
            int login_back_held = ui_login_back_is_held(tg5040_input, joysticks, joystick_count,
                                                        &input_suppression);

            if (login_confirm_held &&
                !login_confirm_latch &&
                !login_start.running &&
                !login_active) {
                if (qr_texture) {
                    SDL_DestroyTexture(qr_texture);
                    qr_texture = NULL;
                }
                ui_startup_login_begin_login_flow(ctx, &login_start, &login_thread, &view,
                                                  status, sizeof(status), qr_path);
                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
            }
            if (login_back_held && !login_back_latch) {
                login_active = 0;
                login_poll.stop = 1;
                ui_force_exit_from_login(&haptic_state);
            }
            login_confirm_latch = login_confirm_held;
            login_back_latch = login_back_held;
        } else {
            login_confirm_latch = 0;
            login_back_latch = 0;
        }

        if (view == VIEW_SHELF) {
            int count = shelf_ui_normal_count(shelf_nuxt);
            int has_article = shelf_ui_has_article_slot(shelf_nuxt, NULL);
            int min_selected = has_article ? -1 : 0;
            int shelf_back_held =
                ui_login_back_is_held(tg5040_input, joysticks, joystick_count,
                                      &input_suppression);
            int shelf_confirm_held =
                ui_login_confirm_is_held(tg5040_input, joysticks, joystick_count,
                                         &input_suppression);
            int shelf_resume_held =
                ui_shelf_resume_is_held(tg5040_input, joysticks, joystick_count,
                                        &input_suppression);
            int shelf_next_held =
                ui_input_is_down_held(tg5040_input, joysticks, joystick_count,
                                      &input_suppression) ||
                ui_input_is_right_held(tg5040_input, joysticks, joystick_count,
                                       &input_suppression);
            int shelf_prev_held =
                ui_input_is_up_held(tg5040_input, joysticks, joystick_count,
                                    &input_suppression) ||
                ui_input_is_left_held(tg5040_input, joysticks, joystick_count,
                                      &input_suppression);

            if (!shelf_nav_event_seen && shelf_next_held && !shelf_next_latch &&
                selected + 1 < count) {
                selected++;
                render_requested = 1;
                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
            } else if (!shelf_nav_event_seen && shelf_prev_held && !shelf_prev_latch &&
                       selected > min_selected) {
                selected--;
                render_requested = 1;
                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
            }
            if (!shelf_resume_event_seen && shelf_resume_held && !shelf_resume_latch) {
                char target[2048];
                int font_size = 3;

                if (ui_shelf_flow_prepare_resume(ctx, target, sizeof(target), &font_size,
                                                 loading_title, sizeof(loading_title),
                                                 status, sizeof(status))) {
                    ui_reader_flow_begin_reader_open(ctx, &reader_open,
                                                     &reader_open_thread_handle,
                                                     target, NULL, font_size);
                    if (reader_open.running || reader_open_thread_handle) {
                        view = VIEW_OPENING;
                        render_requested = 1;
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                    }
                }
            } else if (!shelf_confirm_event_seen && shelf_confirm_held &&
                       !shelf_confirm_latch && selected < 0 && has_article) {
                char target[2048];

                if (ui_shelf_flow_prepare_article_open(ctx, shelf_nuxt, 3,
                                                       target, sizeof(target),
                                                       loading_title, sizeof(loading_title),
                                                       status, sizeof(status),
                                                       shelf_status, sizeof(shelf_status))) {
                    ui_reader_flow_begin_reader_open(ctx, &reader_open,
                                                     &reader_open_thread_handle,
                                                     target, NULL, 3);
                    if (reader_open.running || reader_open_thread_handle) {
                        view = VIEW_OPENING;
                        render_requested = 1;
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                    }
                }
            } else if (!shelf_confirm_event_seen && shelf_confirm_held &&
                       !shelf_confirm_latch && count > 0) {
                char target[2048];
                char book_id[256];

                if (ui_shelf_flow_prepare_selected_open(shelf_nuxt, selected,
                                                        target, sizeof(target),
                                                        book_id, sizeof(book_id),
                                                        loading_title, sizeof(loading_title),
                                                        status, sizeof(status),
                                                        shelf_status, sizeof(shelf_status))) {
                    ui_reader_flow_begin_reader_open(ctx, &reader_open,
                                                     &reader_open_thread_handle,
                                                     target, book_id[0] ? book_id : NULL,
                                                     3);
                    if (reader_open.running || reader_open_thread_handle) {
                        view = VIEW_OPENING;
                        render_requested = 1;
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                    } else {
                        snprintf(shelf_status, sizeof(shelf_status),
                                 "\xE6\x97\xA0\xE6\xB3\x95\xE5\x90\xAF\xE5\x8A\xA8\xE5\x8A\xA0\xE8\xBD\xBD\xE4\xBB\xBB\xE5\x8A\xA1");
                    }
                }
            }
            if (!shelf_back_event_seen && shelf_back_held && !shelf_back_latch) {
                exit_confirm_until = SDL_GetTicks() + UI_EXIT_CONFIRM_DURATION_MS;
                render_requested = 1;
                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 35);
            }
            shelf_back_latch = shelf_back_held;
            shelf_confirm_latch = shelf_confirm_held;
            shelf_resume_latch = shelf_resume_held;
            shelf_next_latch = shelf_next_held;
            shelf_prev_latch = shelf_prev_held;
        } else {
            shelf_back_latch = 0;
            shelf_confirm_latch = 0;
            shelf_resume_latch = 0;
            shelf_next_latch = 0;
            shelf_prev_latch = 0;
        }

        if (view == VIEW_READER) {
            render_requested |=
                ui_reader_flow_tick_reader(ctx, &reader_state, &progress_report,
                                           &progress_report_thread_handle,
                                           &chapter_prefetch_cache,
                                           &catalog_hydration,
                                           &catalog_hydration_thread_handle);
        } else if (ui_reader_flow_chapter_prefetch_has_running_work(&chapter_prefetch_cache) ||
                   ui_reader_flow_catalog_hydration_has_running_work(&catalog_hydration,
                                                                     catalog_hydration_thread_handle)) {
            ui_reader_flow_poll_background(&chapter_prefetch_cache,
                                           &catalog_hydration,
                                           &catalog_hydration_thread_handle,
                                           &reader_state);
        }

        if (shelf_cover_download_thread_handle) {
            ui_shelf_flow_cover_download_poll(&shelf_covers, &shelf_cover_download,
                                              &shelf_cover_download_thread_handle);
        }
        if (view == VIEW_SHELF) {
            ui_shelf_flow_cover_download_maybe_start(ctx, &shelf_covers, &shelf_cover_download,
                                                     &shelf_cover_download_thread_handle, selected);
            if (shelf_cover_prepare_nearest(ctx, renderer, &shelf_covers,
                                            motion_state.shelf_selected_visual)) {
                render_requested = 1;
            }
        }

        if (ui_startup_login_finish_startup(&startup_thread_handle, &startup_state)) {
            render_requested = 1;
            if (startup_state.session_ok == 1 && startup_state.shelf_nuxt) {
                cJSON_Delete(shelf_nuxt);
                shelf_nuxt = startup_state.shelf_nuxt;
                startup_state.shelf_nuxt = NULL;
                ui_shelf_flow_cover_download_stop(&shelf_cover_download,
                                                  &shelf_cover_download_thread_handle);
                shelf_cover_cache_build(ctx, shelf_nuxt, &shelf_covers);
                if (shelf_books(shelf_nuxt) && cJSON_IsArray(shelf_books(shelf_nuxt))) {
                    selected = shelf_ui_clamp_selection(shelf_nuxt, selected);
                    if (view != VIEW_READER && view != VIEW_LOGIN && view != VIEW_OPENING) {
                        shelf_status[0] = '\0';
                    } else if (view == VIEW_BOOTSTRAP) {
                        view = VIEW_SHELF;
                    }
                }
            } else if (startup_state.session_ok == 0) {
                view = VIEW_LOGIN;
                login_active = 0;
                memset(&session, 0, sizeof(session));
                /* 按 A 生成二维码 */
                snprintf(status, sizeof(status), "\xE6\x8C\x89 A \xE7\x94\x9F\xE6\x88\x90\xE4\xBA\x8C\xE7\xBB\xB4\xE7\xA0\x81");
                if (qr_texture) {
                    SDL_DestroyTexture(qr_texture);
                    qr_texture = NULL;
                }
            } else if (!shelf_nuxt && view != VIEW_READER && view != VIEW_LOGIN) {
                /* 网络错误，按 A 重试 */
                snprintf(status, sizeof(status), "\xE7\xBD\x91\xE7\xBB\x9C\xE9\x94\x99\xE8\xAF\xAF\xEF\xBC\x8C\xE6\x8C\x89 A \xE9\x87\x8D\xE8\xAF\x95");
            }
            if (startup_state.poor_network) {
                poor_network_toast_until = SDL_GetTicks() + 3000;
            }
            ui_startup_login_startup_state_reset(&startup_state);
        }

        if (reader_open_thread_handle && !reader_open.running) {
            render_requested = 1;
            (void)ui_reader_flow_finish_open(ctx, body_font, &reader_open,
                                             &reader_open_thread_handle,
                                             &reader_state, &view,
                                             status, sizeof(status),
                                             shelf_status, sizeof(shelf_status),
                                             &poor_network_toast_until,
                                             &current_layout, shelf_nuxt != NULL);
        }

        if (!motion_state.shelf_initialized) {
            motion_state.shelf_selected_visual = (float)selected;
            motion_state.shelf_initialized = 1;
        } else {
            motion_state.shelf_selected_visual =
                ui_motion_step(motion_state.shelf_selected_visual, (float)selected,
                               UI_SHELF_SELECTION_SPEED, dt_seconds);
            if (fabsf(motion_state.shelf_selected_visual - (float)selected) < 0.001f) {
                motion_state.shelf_selected_visual = (float)selected;
            }
        }
        if (!motion_state.catalog_selection_initialized) {
            motion_state.catalog_selected_visual = (float)reader_state.catalog_selected;
            motion_state.catalog_selection_initialized = 1;
        } else {
            motion_state.catalog_selected_visual =
                ui_motion_step(motion_state.catalog_selected_visual,
                               (float)reader_state.catalog_selected,
                               UI_CATALOG_SELECTION_SPEED, dt_seconds);
            if (fabsf(motion_state.catalog_selected_visual -
                      (float)reader_state.catalog_selected) < 0.001f) {
                motion_state.catalog_selected_visual = (float)reader_state.catalog_selected;
            }
        }
        motion_state.catalog_selection_animating_active =
            reader_state.catalog_open &&
            fabsf(motion_state.catalog_selected_visual -
                  (float)reader_state.catalog_selected) >= 0.001f;
        motion_state.catalog_progress =
            ui_motion_step(motion_state.catalog_progress, reader_state.catalog_open ? 1.0f : 0.0f,
                           reader_state.catalog_open ?
                           UI_CATALOG_ANIMATION_SPEED :
                           UI_CATALOG_CLOSE_ANIMATION_SPEED,
                           dt_seconds);
        motion_state.catalog_animating_active =
            fabsf(motion_state.catalog_progress - (reader_state.catalog_open ? 1.0f : 0.0f)) >= 0.001f;
        if (!motion_state.catalog_animating_active) {
            motion_state.catalog_progress = reader_state.catalog_open ? 1.0f : 0.0f;
        }
        motion_state.settings_progress =
            ui_motion_step(motion_state.settings_progress,
                           (view == VIEW_SETTINGS && settings_state.open) ? 1.0f : 0.0f,
                           (view == VIEW_SETTINGS && settings_state.open) ?
                           UI_CATALOG_ANIMATION_SPEED :
                           UI_CATALOG_CLOSE_ANIMATION_SPEED,
                           dt_seconds);
        motion_state.settings_animating_active =
            fabsf(motion_state.settings_progress -
                  ((view == VIEW_SETTINGS && settings_state.open) ? 1.0f : 0.0f)) >= 0.001f;
        if (!motion_state.settings_animating_active) {
            motion_state.settings_progress =
                (view == VIEW_SETTINGS && settings_state.open) ? 1.0f : 0.0f;
        }
        if (frame_view_before_updates != view ||
            (view == VIEW_READER && catalog_open_before_updates != reader_state.catalog_open)) {
            motion_state.view_fade_active = 1;
            motion_state.view_fade_start_tick = SDL_GetTicks();
            motion_state.view_fade_duration_ms = UI_VIEW_FADE_DURATION_MS;
            if (!input_transition_started) {
                ui_begin_input_transition(&repeat_state,
                                          &input_suppression,
                                          &observed_input_mask,
                                          tg5040_input,
                                          joysticks,
                                          joystick_count,
                                          &tg5040_select_pressed,
                                          &tg5040_start_pressed,
                                          &exit_confirm_until,
                                          &reader_exit_confirm_until,
                                          &login_back_latch,
                                          &login_confirm_latch,
                                          &shelf_back_latch,
                                          &shelf_confirm_latch,
                                          &shelf_resume_latch,
                                          &shelf_next_latch,
                                          &shelf_prev_latch);
                input_transition_started = 1;
            }
        } else if (!input_transition_started &&
                   ui_reader_doc_target_changed(frame_reader_target_before,
                                                &reader_state)) {
            ui_begin_input_transition(&repeat_state,
                                      &input_suppression,
                                      &observed_input_mask,
                                      tg5040_input,
                                      joysticks,
                                      joystick_count,
                                      &tg5040_select_pressed,
                                      &tg5040_start_pressed,
                                      &exit_confirm_until,
                                      &reader_exit_confirm_until,
                                      &login_back_latch,
                                      &login_confirm_latch,
                                      &shelf_back_latch,
                                      &shelf_confirm_latch,
                                      &shelf_resume_latch,
                                      &shelf_next_latch,
                                      &shelf_prev_latch);
            input_transition_started = 1;
        }

        if (view == VIEW_SETTINGS &&
            !settings_state.open &&
            !motion_state.settings_animating_active) {
            if (ui_settings_flow_finish_close(&settings_state, &view, &selected) == 0) {
                ui_begin_input_transition(&repeat_state,
                                          &input_suppression,
                                          &observed_input_mask,
                                          tg5040_input,
                                          joysticks,
                                          joystick_count,
                                          &tg5040_select_pressed,
                                          &tg5040_start_pressed,
                                          &exit_confirm_until,
                                          &reader_exit_confirm_until,
                                          &login_back_latch,
                                          &login_confirm_latch,
                                          &shelf_back_latch,
                                          &shelf_confirm_latch,
                                          &shelf_resume_latch,
                                          &shelf_next_latch,
                                          &shelf_prev_latch);
                input_transition_started = 1;
                render_requested = 1;
            }
        }

        if (fabsf(motion_state.shelf_selected_visual -
                  shelf_selected_visual_before_updates) >= 0.001f ||
            fabsf(motion_state.catalog_selected_visual -
                  catalog_selected_visual_before_updates) >= 0.001f ||
            fabsf(motion_state.catalog_progress -
                  catalog_progress_before_updates) >= 0.001f ||
            fabsf(motion_state.settings_progress -
                  settings_progress_before_updates) >= 0.001f) {
            render_requested = 1;
        }

        if (view == VIEW_LOGIN) {
            (void)ui_startup_login_finish_login_start(ctx, &login_start, &login_thread,
                                                      &login_poll, &login_poll_thread_handle,
                                                      &session, &last_poll, &login_active,
                                                      status, sizeof(status),
                                                      &render_requested);
        }

        if (view == VIEW_LOGIN && login_active) {
            int login_result =
                ui_startup_login_poll_login(&login_poll, &login_poll_thread_handle,
                                            &last_poll, status, sizeof(status),
                                            &render_requested);

            if (login_result == 1) {
                cJSON_Delete(shelf_nuxt);
                shelf_nuxt = shelf_load(ctx, 1, NULL);
                ui_shelf_flow_cover_download_stop(&shelf_cover_download,
                                                  &shelf_cover_download_thread_handle);
                shelf_cover_cache_build(ctx, shelf_nuxt, &shelf_covers);
                selected = shelf_ui_clamp_selection(shelf_nuxt,
                                                    shelf_ui_default_selection(shelf_nuxt));
                shelf_start = 0;
                shelf_status[0] = '\0';
                status[0] = '\0';
                login_active = 0;
                view = VIEW_SHELF;
            } else if (login_result < 0) {
                login_active = 0;
            }
        }

        {
            Uint32 now = SDL_GetTicks();
            int toast_visible = poor_network_toast_until > now;
            int catalog_animating = motion_state.catalog_animating_active;
            int catalog_selection_animating =
                motion_state.catalog_selection_animating_active;
            int should_render = 1;

            if (view == VIEW_READER &&
                !reader_input_seen &&
                !render_requested &&
                !motion_state.view_fade_active &&
                !catalog_animating &&
                !catalog_selection_animating &&
                !toast_visible) {
                should_render = 0;
            }
            if (view == VIEW_SHELF &&
                !render_requested &&
                !motion_state.view_fade_active &&
                fabsf(motion_state.shelf_selected_visual - (float)selected) < 0.001f &&
                !toast_visible &&
                exit_confirm_until <= now) {
                should_render = 0;
            }

            if (should_render) {
                SDL_SetRenderTarget(renderer, scene_texture);

                if (view == VIEW_LOGIN) {
                    render_login(renderer, title_font, body_font, &session, status,
                                 battery_state.text, &current_layout,
                                 &qr_texture, &qr_tex_w, &qr_tex_h);
                    } else if (view == VIEW_SETTINGS) {
                        if (settings_state.origin == UI_SETTINGS_ORIGIN_READER) {
                            render_reader(renderer, title_font, body_font, &reader_state,
                                          battery_state.text, &current_layout);
                        } else {
                        render_shelf(renderer, title_font, body_font, ctx, shelf_nuxt, &shelf_covers,
                                     selected, motion_state.shelf_selected_visual,
                                     shelf_start, shelf_status, battery_state.text, &current_layout);
                        }
                        render_settings(renderer, title_font, body_font, &settings_state,
                                        &reader_state, preferred_reader_font_size,
                                        brightness_level, rotation, shelf_status,
                                        battery_state.text, &current_layout,
                                        motion_state.settings_progress);
                } else if (view == VIEW_READER) {
                    render_reader(renderer, title_font, body_font, &reader_state,
                                  battery_state.text, &current_layout);
                    if (motion_state.catalog_progress > 0.001f) {
                        render_catalog_overlay(renderer, title_font, body_font, &reader_state,
                                               motion_state.catalog_progress,
                                               motion_state.catalog_selected_visual,
                                               &current_layout);
                    }
                } else if (view == VIEW_BOOTSTRAP || view == VIEW_OPENING) {
                    render_loading(renderer, title_font, body_font, loading_title, status,
                                   battery_state.text, &current_layout);
                } else {
                    render_shelf(renderer, title_font, body_font, ctx, shelf_nuxt, &shelf_covers,
                                 selected, motion_state.shelf_selected_visual,
                                 shelf_start, shelf_status, battery_state.text, &current_layout);
                }
                if (ctx->poor_network) {
                    ctx->poor_network = 0;
                    poor_network_toast_until = SDL_GetTicks() + 3000;
                    toast_visible = 1;
                }
                if (toast_visible) {
                    render_poor_network_toast(renderer, body_font, poor_network_toast_until,
                                              &current_layout);
                }
                if (reader_exit_confirm_until > SDL_GetTicks()) {
                    render_confirm_hint(renderer, body_font, reader_exit_confirm_until,
                                        "\xE5\x86\x8D\xE6\xAC\xA1\xE6\x8C\x89 B \xE8\xBF\x94\xE5\x9B\x9E\xE4\xB9\xA6\xE6\x9E\xB6",
                                        &current_layout);
                } else if (exit_confirm_until > SDL_GetTicks()) {
                    render_confirm_hint(renderer, body_font, exit_confirm_until,
                                        "\xE5\x86\x8D\xE6\xAC\xA1\xE6\x8C\x89 B \xE9\x80\x80\xE5\x87\xBA",
                                        &current_layout);
                }
                {
                    Uint8 scene_alpha = 255;

                    if (motion_state.view_fade_active) {
                        Uint32 elapsed = SDL_GetTicks() - motion_state.view_fade_start_tick;
                        float progress = motion_state.view_fade_duration_ms > 0 ?
                            (float)elapsed / (float)motion_state.view_fade_duration_ms : 1.0f;

                        if (progress >= 1.0f) {
                            motion_state.view_fade_active = 0;
                        } else {
                            scene_alpha = ui_view_fade_alpha(progress);
                        }
                    }
                    ui_present_scene(renderer, scene_texture, rotation, scene_alpha);
                }
                SDL_RenderPresent(renderer);
            }
        }

        {
            Uint32 sleep_budget = ui_frame_interval_ms(view, &motion_state,
                                                       poor_network_toast_until,
                                                       exit_confirm_until,
                                                       SDL_GetTicks());
            Uint32 frame_elapsed = SDL_GetTicks() - frame_now;

            if (frame_elapsed < sleep_budget) {
                SDL_Delay(sleep_budget - frame_elapsed);
            } else if (sleep_budget == 0) {
                Uint32 now = SDL_GetTicks();
                time_t wait_wall_now = time(NULL);
                int ms_until_next_minute =
                    (int)(((wait_wall_now / 60) + 1) * 60 - wait_wall_now) * 1000;
                int background_busy =
                    startup_thread_handle ||
                    reader_open_thread_handle ||
                    login_thread ||
                    login_poll_thread_handle ||
                    shelf_cover_download_thread_handle ||
                    progress_report_thread_handle ||
                    ui_reader_flow_chapter_prefetch_has_running_work(&chapter_prefetch_cache) ||
                    ui_reader_flow_catalog_hydration_has_running_work(&catalog_hydration,
                                                                      catalog_hydration_thread_handle);
                int wait_timeout_ms = 0;
                SDL_Event waited_event;

                if (motion_state.view_fade_active || poor_network_toast_until > now) {
                    wait_timeout_ms = 0;
                } else {
                    Uint32 next_deadline = now + 5000;

                    if (ms_until_next_minute > 0 &&
                        now + (Uint32)ms_until_next_minute < next_deadline) {
                        next_deadline = now + (Uint32)ms_until_next_minute;
                    }
                    if (battery_state.next_poll_tick > now &&
                        battery_state.next_poll_tick < next_deadline) {
                        next_deadline = battery_state.next_poll_tick;
                    }
                    if (view == VIEW_READER &&
                        reader_state.progress_report_due_tick > now &&
                        reader_state.progress_report_due_tick < next_deadline) {
                        next_deadline = reader_state.progress_report_due_tick;
                    }
                    if (haptic_state.stop_tick > 0 &&
                        haptic_state.stop_tick < next_deadline) {
                        next_deadline = haptic_state.stop_tick;
                    }
                    if (reader_exit_confirm_until > now &&
                        reader_exit_confirm_until < next_deadline) {
                        next_deadline = reader_exit_confirm_until;
                    }
                    if (exit_confirm_until > now &&
                        exit_confirm_until < next_deadline) {
                        next_deadline = exit_confirm_until;
                    }
                    if (background_busy) {
                        Uint32 bg_poll = now + 100;
                        if (bg_poll < next_deadline) {
                            next_deadline = bg_poll;
                        }
                    }

                    wait_timeout_ms = (int)(next_deadline - now);
                    if (wait_timeout_ms < 1) {
                        wait_timeout_ms = 1;
                    }
                }

                if (wait_timeout_ms > 0 &&
                    SDL_WaitEventTimeout(&waited_event, wait_timeout_ms)) {
                    SDL_PushEvent(&waited_event);
                }
            }
        }
    }

    rc = 0;

cleanup:
    ui_platform_shutdown_haptics(&haptic_state);
    if (view == VIEW_READER) {
        ui_reader_view_flush_progress_blocking(ctx, &reader_state, 1);
        ui_reader_view_save_local_position(ctx, &reader_state);
    }
    ui_startup_login_shutdown(&login_poll, &login_thread, &startup_thread_handle,
                              &login_poll_thread_handle);
    ui_shelf_flow_cover_download_stop(&shelf_cover_download,
                                      &shelf_cover_download_thread_handle);
    ui_reader_flow_shutdown(&reader_open, &reader_open_thread_handle,
                            &progress_report, &progress_report_thread_handle,
                            &chapter_prefetch_cache,
                            &catalog_hydration, &catalog_hydration_thread_handle);
    reader_view_free(&reader_state);
    ui_startup_login_startup_state_reset(&startup_state);
    shelf_cover_cache_reset(&shelf_covers);
    cJSON_Delete(shelf_nuxt);
    if (body_font) {
        TTF_CloseFont(body_font);
    }
    if (title_font) {
        TTF_CloseFont(title_font);
    }
    if (qr_texture) {
        SDL_DestroyTexture(qr_texture);
    }
    if (scene_texture) {
        SDL_DestroyTexture(scene_texture);
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    if (joysticks) {
        for (int i = 0; i < joystick_count; i++) {
            if (joysticks[i]) {
                SDL_JoystickClose(joysticks[i]);
            }
        }
    }
    free(joysticks);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return rc;
}

#else

#include <stdio.h>

int ui_is_available(void) {
    return 0;
}

int ui_run(ApiContext *ctx, const char *font_path, const char *platform) {
    (void)ctx;
    (void)font_path;
    (void)platform;
    fprintf(stderr, "UI support is unavailable in this build.\n");
    return -1;
}

#endif
