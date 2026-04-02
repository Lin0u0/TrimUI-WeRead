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
#include "reader.h"
#include "shelf.h"
#include "state.h"

typedef struct {
    char data_dir[512];
    char ca_file[512];
    char qr_path[1024];
    AuthSession session;
    int running;
    int success;
    int failed;
} LoginStartState;

typedef struct {
    char data_dir[512];
    char ca_file[512];
    AuthSession session;
    int running;
    int completed;
    int stop;
    AuthPollStatus last_status;
} LoginPollState;

typedef struct {
    char data_dir[512];
    char ca_file[512];
    cJSON *shelf_nuxt;
    int session_ok;
    int running;
    int completed;
    int poor_network;
} StartupState;

typedef struct {
    char data_dir[512];
    char ca_file[512];
    char source_target[2048];
    char book_id[256];
    int font_size;
    int content_font_size;
    int initial_page;
    int initial_offset;
    int honor_saved_position;
    int running;
    int ready;
    int failed;
    int poor_network;
    ReaderDocument doc;
} ReaderOpenState;

typedef struct {
    char data_dir[512];
    char ca_file[512];
    ReaderDocument doc;
    int current_page;
    int total_pages;
    int chapter_offset;
    int reading_seconds;
    int compute_progress;
    char page_summary[128];
    int running;
    int result;
} ProgressReportState;

typedef struct {
    char *book_id;
    char *cover_url;
    char cache_path[1024];
    SDL_Texture *texture;
    int attempted;
    int download_failed;
} ShelfCoverEntry;

typedef struct {
    ShelfCoverEntry *entries;
    int count;
} ShelfCoverCache;

typedef struct {
    char data_dir[512];
    char ca_file[512];
    char target[2048];
    int font_size;
    int running;
    int ready;
    int failed;
    ReaderDocument doc;
} ChapterPrefetchState;

typedef struct {
    ChapterPrefetchState state;
    SDL_Thread *thread;
} ChapterPrefetchSlot;

typedef struct {
    ChapterPrefetchSlot slots[10];
} ChapterPrefetchCache;

typedef struct {
    char data_dir[512];
    char ca_file[512];
    char cover_url[2048];
    char cache_path[1024];
    int running;
    int ready;
    int failed;
    int entry_index;
} ShelfCoverDownloadState;

static int reader_total_pages(ReaderViewState *state);
static int reader_find_page_for_offset(const ReaderViewState *state, int target_offset);
static int reader_reset_content_font(TTF_Font *fallback_font, ReaderViewState *state);
static int reader_has_cloud_position(const ReaderDocument *doc);
static const char *reader_find_progress_target(const ReaderViewState *state);
static int reader_view_adopt_document(TTF_Font *font, ReaderDocument *doc,
                                      int content_width, int content_height,
                                      int honor_saved_position, ReaderViewState *state);
static int reader_view_load(ApiContext *ctx, TTF_Font *font, const char *target, int font_size,
                            int content_width, int content_height, int honor_saved_position,
                            ReaderViewState *state);
static void reader_set_source_target(ReaderViewState *state, const char *source_target);
static void reader_save_local_position(ApiContext *ctx, ReaderViewState *state);
static int reader_anchor_offset(const ReaderViewState *state);
static void reader_progress_note_activity(ReaderViewState *state, Uint32 now);
static void reader_progress_flush_blocking(ApiContext *ctx, ReaderViewState *state,
                                           int compute_progress);
static void chapter_prefetch_reset(ChapterPrefetchState *state);
static void shelf_cover_download_state_reset(ShelfCoverDownloadState *state);
static int shelf_cover_download_thread(void *userdata);
static int chapter_prefetch_cache_adopt(ChapterPrefetchCache *cache, const char *target,
                                        TTF_Font *body_font, ReaderViewState *reader_state,
                                        const UiLayout *current_layout);
static void chapter_prefetch_maybe_start(ApiContext *ctx, ChapterPrefetchState *state,
                                         SDL_Thread **thread_handle,
                                         const char *target, int font_size);
static void render_header_status(SDL_Renderer *renderer, TTF_Font *body_font,
                                 const char *text, const UiLayout *layout);
static void render_confirm_hint(SDL_Renderer *renderer, TTF_Font *body_font,
                                Uint32 hint_until, const char *msg, const UiLayout *layout);

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

static int reader_reset_content_font(TTF_Font *fallback_font, ReaderViewState *state) {
    int font_size;

    if (!state) {
        return -1;
    }

    if (state->content_font) {
        TTF_CloseFont(state->content_font);
        state->content_font = NULL;
    }

    font_size = state->content_font_size > 0 ? state->content_font_size : UI_READER_CONTENT_FONT_SIZE;
    state->content_font_size = font_size;

    if (state->doc.use_content_font && state->doc.content_font_path[0]) {
        state->content_font = TTF_OpenFont(state->doc.content_font_path, font_size);
        if (!state->content_font) {
            return fallback_font ? 0 : -1;
        }
    }

    /* Clear character width cache to force recalculation with new font size.
     * TTF_OpenFont may reuse the same memory address as the closed font,
     * causing get_char_width_fast to incorrectly use cached widths from
     * the previous font size. */
    char_width_cache_reset();

    return 0;
}

static int reader_line_height_for_font_size(int font_size) {
    switch (font_size) {
        case 24: return 36;
        case 28: return 40;
        case 32: return 45;
        case 36: return 50;
        case 40: return 54;
        case 44: return 59;
        default:
            if (font_size <= 0) {
                font_size = UI_READER_CONTENT_FONT_SIZE;
            }
            return font_size + UI_READER_EXTRA_LEADING + 4;
    }
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

static void reader_sync_catalog_selection(ReaderViewState *state);
static void reader_open_catalog(ApiContext *ctx, ReaderViewState *state, char *status, size_t status_size);
static int reader_expand_catalog_for_selection(ApiContext *ctx, ReaderViewState *state,
                                               int direction, char *status, size_t status_size);
static int reader_find_page_for_offset(const ReaderViewState *state, int target_offset);

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


static void ui_apply_repeat_action(UiRepeatAction action, ApiContext *ctx, TTF_Font *body_font,
                                   ReaderViewState *reader_state, cJSON *shelf_nuxt, int *selected,
                                   char *shelf_status, size_t shelf_status_size,
                                   const UiLayout *current_layout,
                                   ChapterPrefetchCache *chapter_prefetch_cache) {
    if (action == UI_REPEAT_NONE) {
        return;
    }
    if (action == UI_REPEAT_SHELF_PREV || action == UI_REPEAT_SHELF_NEXT) {
        cJSON *books = shelf_nuxt ? shelf_books(shelf_nuxt) : NULL;
        int count = books && cJSON_IsArray(books) ? cJSON_GetArraySize(books) : 0;

        if (!selected || count <= 0) {
            return;
        }
        if (action == UI_REPEAT_SHELF_NEXT && *selected + 1 < count) {
            (*selected)++;
        } else if (action == UI_REPEAT_SHELF_PREV && *selected > 0) {
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
        } else if (reader_expand_catalog_for_selection(ctx, reader_state, -1,
                                                       shelf_status, shelf_status_size) > 0) {
            reader_state->catalog_selected--;
        }
        return;
    }
    if (action == UI_REPEAT_CATALOG_DOWN) {
        if (reader_state->catalog_selected + 1 < reader_state->doc.catalog_count) {
            reader_state->catalog_selected++;
        } else if (reader_expand_catalog_for_selection(ctx, reader_state, 1,
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
        } else if (reader_expand_catalog_for_selection(ctx, reader_state, -1,
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
        } else if (reader_expand_catalog_for_selection(ctx, reader_state, 1,
                                                       shelf_status, shelf_status_size) > 0) {
            reader_state->catalog_selected += 10;
            if (reader_state->catalog_selected >= reader_state->doc.catalog_count) {
                reader_state->catalog_selected = reader_state->doc.catalog_count - 1;
            }
        }
        return;
    }
    if (action == UI_REPEAT_PAGE_NEXT) {
        int total_pages = reader_total_pages(reader_state);

        reader_progress_note_activity(reader_state, SDL_GetTicks());
        if (reader_state->current_page + 1 < total_pages) {
            reader_state->current_page++;
            reader_save_local_position(ctx, reader_state);
        } else if (reader_state->doc.next_target && reader_state->doc.next_target[0]) {
            char *target = strdup(reader_state->doc.next_target);
            char source_target[2048];
            int font_size = reader_state->doc.font_size;

            if (!target) {
                return;
            }
            snprintf(source_target, sizeof(source_target), "%s", reader_state->source_target);
            reader_progress_flush_blocking(ctx, reader_state, 1);
            if (chapter_prefetch_cache &&
                chapter_prefetch_cache_adopt(chapter_prefetch_cache, target, body_font,
                                             reader_state, current_layout) == 0) {
                reader_set_source_target(reader_state, source_target);
                reader_save_local_position(ctx, reader_state);
                shelf_status[0] = '\0';
            } else if (reader_view_load(ctx, body_font, target, font_size,
                                        current_layout->reader_content_w,
                                        current_layout->reader_content_h, 0,
                                        reader_state) == 0) {
                reader_set_source_target(reader_state, source_target);
                reader_save_local_position(ctx, reader_state);
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
        reader_progress_note_activity(reader_state, SDL_GetTicks());
        if (reader_state->current_page > 0) {
            reader_state->current_page--;
            reader_save_local_position(ctx, reader_state);
        } else if (reader_state->doc.prev_target && reader_state->doc.prev_target[0]) {
            char *target = strdup(reader_state->doc.prev_target);
            char source_target[2048];
            int font_size = reader_state->doc.font_size;

            if (!target) {
                return;
            }
            snprintf(source_target, sizeof(source_target), "%s", reader_state->source_target);
            reader_progress_flush_blocking(ctx, reader_state, 1);
            if (chapter_prefetch_cache &&
                chapter_prefetch_cache_adopt(chapter_prefetch_cache, target, body_font,
                                             reader_state, current_layout) == 0) {
                int new_total_pages;

                reader_set_source_target(reader_state, source_target);
                new_total_pages = reader_total_pages(reader_state);
                reader_state->current_page = new_total_pages > 0 ? new_total_pages - 1 : 0;
                reader_save_local_position(ctx, reader_state);
                shelf_status[0] = '\0';
            } else if (reader_view_load(ctx, body_font, target, font_size,
                                        current_layout->reader_content_w,
                                        current_layout->reader_content_h, 0,
                                        reader_state) == 0) {
                int new_total_pages;

                reader_set_source_target(reader_state, source_target);
                new_total_pages = reader_total_pages(reader_state);
                reader_state->current_page = new_total_pages > 0 ? new_total_pages - 1 : 0;
                reader_save_local_position(ctx, reader_state);
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

static void progress_report_state_reset(ProgressReportState *state) {
    if (!state) {
        return;
    }
    reader_document_free(&state->doc);
    memset(state, 0, sizeof(*state));
}

static void startup_state_reset(StartupState *state) {
    if (!state) {
        return;
    }
    cJSON_Delete(state->shelf_nuxt);
    memset(state, 0, sizeof(*state));
}

static void reader_open_state_reset(ReaderOpenState *state) {
    if (!state) {
        return;
    }
    reader_document_free(&state->doc);
    memset(state, 0, sizeof(*state));
}

static int copy_reader_report_document(ReaderDocument *dst, const ReaderDocument *src) {
    memset(dst, 0, sizeof(*dst));
    dst->book_id = dup_or_null(src->book_id);
    dst->token = dup_or_null(src->token);
    dst->chapter_uid = dup_or_null(src->chapter_uid);
    dst->progress_chapter_uid = dup_or_null(src->progress_chapter_uid);
    dst->progress_summary = dup_or_null(src->progress_summary);
    dst->chapter_idx = src->chapter_idx;
    dst->progress_chapter_idx = src->progress_chapter_idx;
    dst->total_words = src->total_words;
    dst->chapter_word_count = src->chapter_word_count;
    dst->prev_chapters_word_count = src->prev_chapters_word_count;
    dst->saved_chapter_offset = src->saved_chapter_offset;
    dst->chapter_max_offset = src->chapter_max_offset;
    dst->last_reported_progress = src->last_reported_progress;
    dst->chapter_offset_count = src->chapter_offset_count;
    if (src->chapter_offset_count > 0 && src->chapter_offsets) {
        dst->chapter_offsets = malloc(sizeof(int) * src->chapter_offset_count);
        if (!dst->chapter_offsets) {
            reader_document_free(dst);
            return -1;
        }
        memcpy(dst->chapter_offsets, src->chapter_offsets, sizeof(int) * src->chapter_offset_count);
    }

    if ((src->book_id && !dst->book_id) ||
        (src->token && !dst->token) ||
        (src->chapter_uid && !dst->chapter_uid) ||
        (src->progress_chapter_uid && !dst->progress_chapter_uid) ||
        (src->progress_summary && !dst->progress_summary)) {
        reader_document_free(dst);
        return -1;
    }
    return 0;
}

static int progress_report_thread(void *userdata) {
    ProgressReportState *state = (ProgressReportState *)userdata;
    ApiContext ctx;

    if (api_init(&ctx, state->data_dir) != 0) {
        state->result = -1;
        state->running = 0;
        return -1;
    }
    snprintf(ctx.ca_file, sizeof(ctx.ca_file), "%s", state->ca_file);

    state->result = reader_report_progress_at_offset(&ctx, &state->doc, state->current_page,
                                                     state->total_pages, state->reading_seconds,
                                                     state->page_summary, state->compute_progress,
                                                     state->chapter_offset);
    if (state->result != 0) {
        state->result = reader_report_progress_at_offset(&ctx, &state->doc, state->current_page,
                                                         state->total_pages, state->reading_seconds,
                                                         state->page_summary, state->compute_progress,
                                                         state->chapter_offset);
    }
    api_cleanup(&ctx);
    state->running = 0;
    return state->result;
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
    cJSON *books = shelf_books(nuxt);
    char covers_dir[1024];
    char file_name[256];
    int count;

    shelf_cover_cache_reset(cache);
    if (!ctx || !books || !cJSON_IsArray(books)) {
        return;
    }

    snprintf(covers_dir, sizeof(covers_dir), "%s/covers", ctx->state_dir);
    if (ensure_dir(covers_dir) != 0) {
        return;
    }

    count = cJSON_GetArraySize(books);
    cache->entries = calloc((size_t)count, sizeof(ShelfCoverEntry));
    if (!cache->entries) {
        cache->count = 0;
        return;
    }
    cache->count = count;

    for (int i = 0; i < count; i++) {
        cJSON *book = cJSON_GetArrayItem(books, i);
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

static void shelf_cover_download_maybe_start(ApiContext *ctx, ShelfCoverCache *cache,
                                             ShelfCoverDownloadState *state,
                                             SDL_Thread **thread_handle, int selected) {
    int radius = UI_SHELF_COVER_TEXTURE_KEEP_RADIUS;

    if (!ctx || !cache || !state || !thread_handle || *thread_handle || state->running ||
        !cache->entries || cache->count <= 0) {
        return;
    }

    for (int distance = 0; distance <= radius; distance++) {
        int candidates[2] = { selected - distance, selected + distance };
        int candidate_count = distance == 0 ? 1 : 2;

        for (int i = 0; i < candidate_count; i++) {
            int index = candidates[i];
            ShelfCoverEntry *entry;

            if (index < 0 || index >= cache->count) {
                continue;
            }
            entry = &cache->entries[index];
            if (!entry->cover_url || !entry->cover_url[0] ||
                !entry->cache_path[0] || entry->texture ||
                entry->attempted || entry->download_failed ||
                access(entry->cache_path, F_OK) == 0) {
                continue;
            }

            shelf_cover_download_state_reset(state);
            snprintf(state->data_dir, sizeof(state->data_dir), "%s", ctx->data_dir);
            snprintf(state->ca_file, sizeof(state->ca_file), "%s", ctx->ca_file);
            snprintf(state->cover_url, sizeof(state->cover_url), "%s", entry->cover_url);
            snprintf(state->cache_path, sizeof(state->cache_path), "%s", entry->cache_path);
            state->entry_index = index;
            state->running = 1;
            entry->attempted = 1;
            *thread_handle = SDL_CreateThread(shelf_cover_download_thread,
                                              "weread-cover-download", state);
            if (!*thread_handle) {
                state->running = 0;
                state->failed = 1;
                entry->attempted = 0;
                entry->download_failed = 1;
            }
            return;
        }
    }
}

static void shelf_cover_download_poll(ShelfCoverCache *cache,
                                      ShelfCoverDownloadState *state,
                                      SDL_Thread **thread_handle) {
    ShelfCoverEntry *entry = NULL;

    if (!cache || !state || !thread_handle || !*thread_handle || state->running) {
        return;
    }

    SDL_WaitThread(*thread_handle, NULL);
    *thread_handle = NULL;

    if (state->entry_index >= 0 && state->entry_index < cache->count) {
        entry = &cache->entries[state->entry_index];
    }
    if (entry) {
        if (state->ready) {
            entry->attempted = 0;
            entry->download_failed = 0;
        } else {
            entry->attempted = 0;
            entry->download_failed = 1;
        }
    }

    shelf_cover_download_state_reset(state);
}

static void shelf_cover_download_stop(ShelfCoverDownloadState *state,
                                      SDL_Thread **thread_handle) {
    if (!state || !thread_handle) {
        return;
    }
    if (*thread_handle) {
        SDL_WaitThread(*thread_handle, NULL);
        *thread_handle = NULL;
    }
    shelf_cover_download_state_reset(state);
}

static void draw_qr(SDL_Renderer *renderer, const char *path, const SDL_Rect *slot) {
    SDL_Surface *surface = IMG_Load(path);
    SDL_Texture *texture;
    SDL_Rect dst;
    int max_w;
    int max_h;
    float scale;

    if (!surface) {
        return;
    }
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }
    max_w = slot ? slot->w - 24 : surface->w * 2;
    max_h = slot ? slot->h - 24 : surface->h * 2;
    scale = 2.0f;
    if (surface->w > 0 && surface->h > 0) {
        float scale_w = (float)max_w / (float)surface->w;
        float scale_h = (float)max_h / (float)surface->h;
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
    dst.w = (int)(surface->w * scale);
    dst.h = (int)(surface->h * scale);
    if (slot) {
        dst.x = slot->x + (slot->w - dst.w) / 2;
        dst.y = slot->y + (slot->h - dst.h) / 2;
    } else {
        dst.x = 512 - dst.w / 2;
        dst.y = 180;
    }
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
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
                         const UiLayout *layout) {
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
        draw_qr(renderer, session->qr_png_path, &qr_slot);
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
    cJSON *books = shelf_books(nuxt);
    int count = books && cJSON_IsArray(books) ? cJSON_GetArraySize(books) : 0;
    int start_y;
    int content_top = header_h;
    int content_bottom = window_h - 56;
    int content_h = content_bottom - content_top;
    cJSON *selected_book = NULL;
    const char *selected_title = NULL;
    time_t now = time(NULL);
    struct tm *local_tm = localtime(&now);
    char time_buf[32] = "";
    char position_buf[32];
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

    if (count == 0) {
        int empty_w = 0;
        /* 书架为空 */
        const char *empty_text = "\xE4\xB9\xA6\xE6\x9E\xB6\xE4\xB8\xBA\xE7\xA9\xBA";
        render_header_status(renderer, body_font, battery_text, layout);
        TTF_SizeUTF8(title_font, empty_text, &empty_w, NULL);
        draw_text(renderer, title_font, window_x + window_w / 2 - empty_w / 2, window_h / 2 - 40, ink, empty_text);
        return;
    }

    shelf_cover_cache_trim(cover_cache, selected, UI_SHELF_COVER_TEXTURE_KEEP_RADIUS);
    selected_book = cJSON_GetArrayItem(books, selected);
    selected_title = json_get_string(selected_book, "title");
    /* 微信读书 */
    fit_text_ellipsis(title_font, selected_title ? selected_title : "\xE5\xBE\xAE\xE4\xBF\xA1\xE8\xAF\xBB\xE4\xB9\xA6",
                      window_w - margin * 2 - 140,
                      title_buf, sizeof(title_buf));
    draw_text(renderer, title_font, window_x + margin, title_y, ink, title_buf);
    render_header_status(renderer, body_font, battery_text, layout);

    for (int i = (int)floorf(selected_pos) - 3; i <= (int)ceilf(selected_pos) + 3; i++) {
        cJSON *book = cJSON_GetArrayItem(books, i);
        const char *title;
        float offset;
        float emphasis;
        SDL_Rect cover_rect = {
            0,
            start_y,
            cover_w,
            cover_h
        };

        if (i < 0 || i >= count || !book) {
            continue;
        }
        title = json_get_string(book, "title");
        offset = (float)i - selected_pos;
        emphasis = 1.0f - fabsf(offset);
        cover_rect.x = window_x + (window_w - cover_w) / 2 +
                       (int)lroundf(offset * (float)(cover_w + card_gap));

        render_shelf_cover(renderer, body_font, ink, cover_rect,
                           cover_cache && i < cover_cache->count ? &cover_cache->entries[i] : NULL,
                           title, emphasis);
    }

    if (selected > 0) {
        draw_text(renderer, title_font, window_x + 18, nav_y, ink, "<");
    }
    if (selected + 1 < count) {
        draw_text(renderer, title_font, window_x + window_w - 42, nav_y,
                  ink, ">");
    }

    snprintf(position_buf, sizeof(position_buf), "%d / %d", selected + 1, count);
    TTF_SizeUTF8(body_font, position_buf, &position_width, NULL);
    position_x = window_x + window_w - margin - position_width;
    footer_text_y = footer_line.y + (info_h - (body_font ? TTF_FontHeight(body_font) : 28)) / 2;
    draw_text(renderer, body_font, window_x + margin, footer_text_y, muted, time_buf[0] ? time_buf : "--:--");
    draw_text(renderer, body_font, position_x, footer_text_y, muted, position_buf);
}

static void reader_view_free(ReaderViewState *state) {
    if (!state) {
        return;
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
}

static int append_line(ReaderViewState *state, const char *text, size_t len, int start_offset) {
    char **tmp;
    int *offsets_tmp;
    char *line;
    int new_capacity;

    if (!state) {
        return -1;
    }

    if (state->line_count >= state->line_capacity) {
        new_capacity = state->line_capacity > 0 ? state->line_capacity * 2 : 128;
        tmp = realloc(state->lines, sizeof(char *) * (size_t)new_capacity);
        if (!tmp) {
            return -1;
        }
        state->lines = tmp;
        offsets_tmp = realloc(state->line_offsets, sizeof(int) * (size_t)new_capacity);
        if (!offsets_tmp) {
            return -1;
        }
        state->line_offsets = offsets_tmp;
        state->line_capacity = new_capacity;
    }
    line = malloc(len + 1);
    if (!line) {
        return -1;
    }
    memcpy(line, text, len);
    line[len] = '\0';
    state->lines[state->line_count++] = line;
    state->line_offsets[state->line_count - 1] = start_offset;
    return 0;
}

static void chapter_prefetch_reset(ChapterPrefetchState *state) {
    if (!state) {
        return;
    }
    reader_document_free(&state->doc);
    memset(state, 0, sizeof(*state));
}

static void shelf_cover_download_state_reset(ShelfCoverDownloadState *state) {
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->entry_index = -1;
}

static int shelf_cover_download_to_path(ApiContext *ctx, const char *url, const char *path) {
    Buffer buf = {0};
    FILE *fp = NULL;
    int rc = -1;

    if (!ctx || !url || !*url || !path || !*path) {
        return -1;
    }
    if (api_download(ctx, url, &buf) != 0) {
        goto cleanup;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        goto cleanup;
    }
    if (fwrite(buf.data, 1, buf.size, fp) != buf.size) {
        goto cleanup;
    }
    rc = 0;

cleanup:
    if (fp) {
        fclose(fp);
    }
    api_buffer_free(&buf);
    return rc;
}

static int shelf_cover_download_thread(void *userdata) {
    ShelfCoverDownloadState *state = (ShelfCoverDownloadState *)userdata;
    ApiContext ctx;

    if (!state) {
        return -1;
    }
    if (api_init(&ctx, state->data_dir) != 0) {
        state->failed = 1;
        state->running = 0;
        return -1;
    }
    snprintf(ctx.ca_file, sizeof(ctx.ca_file), "%s", state->ca_file);
    if (shelf_cover_download_to_path(&ctx, state->cover_url, state->cache_path) == 0) {
        state->ready = 1;
    } else {
        state->failed = 1;
    }
    api_cleanup(&ctx);
    state->running = 0;
    return state->ready ? 0 : -1;
}

static int startup_thread(void *userdata) {
    StartupState *state = (StartupState *)userdata;
    ApiContext ctx;
    int session_poor_network = 0;

    if (!state) {
        return -1;
    }
    if (api_init(&ctx, state->data_dir) != 0) {
        state->session_ok = -1;
        state->running = 0;
        state->completed = 1;
        return -1;
    }
    snprintf(ctx.ca_file, sizeof(ctx.ca_file), "%s", state->ca_file);
    state->session_ok = auth_check_session(&ctx, &state->shelf_nuxt);
    session_poor_network = ctx.poor_network;
    ctx.poor_network = 0;
    if (state->session_ok == 1) {
        (void)reader_warmup_font(&ctx);
    }
    state->poor_network = session_poor_network;
    api_cleanup(&ctx);
    state->running = 0;
    state->completed = 1;
    return state->session_ok == 1 ? 0 : -1;
}

static int reader_prepare_open_document(ApiContext *ctx, const char *source_target,
                                        const char *book_id_hint, int font_size,
                                        ReaderDocument *doc_out,
                                        char *resolved_source_target, size_t resolved_source_size,
                                        int *content_font_size_out,
                                        int *initial_page_out,
                                        int *initial_offset_out,
                                        int *honor_saved_position_out) {
    ReaderDocument doc = {0};
    ReaderDocument saved_doc = {0};
    char saved_target[2048];
    char saved_source_target[2048];
    int saved_page = 0;
    int saved_offset = 0;
    int saved_content_font_size = UI_READER_CONTENT_FONT_SIZE;
    int has_local_position = 0;
    int has_cloud_position = 0;
    int replace_with_saved_doc = 0;
    int initial_page = 0;
    int honor_saved_position = 1;
    int rc = -1;

    if (!ctx || !source_target || !*source_target || !doc_out) {
        return -1;
    }

    memset(doc_out, 0, sizeof(*doc_out));
    if (resolved_source_target && resolved_source_size > 0) {
        ui_copy_string(resolved_source_target, resolved_source_size, source_target);
    }

    if (book_id_hint && *book_id_hint &&
        state_load_reader_position(ctx, book_id_hint, source_target,
                                   saved_target, sizeof(saved_target),
                                   NULL, &saved_content_font_size,
                                   &saved_page, &saved_offset) == 0) {
        has_local_position = 1;
        if (resolved_source_target && resolved_source_size > 0) {
            ui_copy_string(resolved_source_target, resolved_source_size, source_target);
        }
    }

    if (reader_load(ctx, source_target, font_size, &doc) != 0) {
        goto cleanup;
    }

    {
        const char *progress_target = reader_find_progress_target((ReaderViewState *)&(ReaderViewState){ .doc = doc });
        char *fetched_target = NULL;
        if (!progress_target &&
            doc.book_id &&
            (doc.progress_chapter_idx > 0 ||
             (doc.progress_chapter_uid && strcmp(doc.progress_chapter_uid, "0") != 0))) {
            fetched_target = reader_find_chapter_target(ctx, doc.book_id,
                                                        doc.progress_chapter_uid,
                                                        doc.progress_chapter_idx);
            if (fetched_target && (!doc.target || strcmp(fetched_target, doc.target) != 0)) {
                progress_target = fetched_target;
            }
        }
        if (progress_target) {
            ReaderDocument progress_doc = {0};
            if (reader_load(ctx, progress_target, font_size, &progress_doc) != 0) {
                free(fetched_target);
                goto cleanup;
            }
            reader_document_free(&doc);
            doc = progress_doc;
        }
        free(fetched_target);
    }

    has_cloud_position = reader_has_cloud_position(&doc);

    if (!has_local_position && doc.book_id &&
        state_load_reader_position_by_book_id(ctx, doc.book_id,
                                              saved_source_target, sizeof(saved_source_target),
                                              saved_target, sizeof(saved_target),
                                              NULL, &saved_content_font_size,
                                              &saved_page, &saved_offset) == 0) {
        has_local_position = 1;
        if (resolved_source_target && resolved_source_size > 0) {
            ui_copy_string(resolved_source_target, resolved_source_size, saved_source_target);
        }
    }

    if (!has_cloud_position && has_local_position &&
        doc.target && strcmp(saved_target, doc.target) == 0) {
        initial_page = saved_page;
        honor_saved_position = 0;
    } else if (!has_cloud_position && has_local_position) {
        if (reader_load(ctx, saved_target, font_size, &saved_doc) == 0) {
            int same_chapter = 0;
            int saved_is_newer = 0;

            if (saved_doc.chapter_uid && doc.chapter_uid &&
                strcmp(saved_doc.chapter_uid, doc.chapter_uid) == 0) {
                same_chapter = 1;
            } else if (saved_doc.chapter_idx > 0 &&
                       saved_doc.chapter_idx == doc.chapter_idx) {
                same_chapter = 1;
            }

            if (saved_doc.chapter_idx > 0 &&
                doc.chapter_idx > 0 &&
                saved_doc.chapter_idx > doc.chapter_idx) {
                saved_is_newer = 1;
            } else if (saved_doc.chapter_idx > 0 && doc.chapter_idx <= 0) {
                saved_is_newer = 1;
            }

            if (same_chapter) {
                initial_page = saved_page;
                honor_saved_position = 0;
            } else if (saved_is_newer) {
                replace_with_saved_doc = 1;
                initial_page = saved_page;
                honor_saved_position = 0;
            }
        }
    }

    if (replace_with_saved_doc) {
        reader_document_free(&doc);
        doc = saved_doc;
        memset(&saved_doc, 0, sizeof(saved_doc));
    }

    *doc_out = doc;
    memset(&doc, 0, sizeof(doc));
    if (content_font_size_out) {
        *content_font_size_out = saved_content_font_size;
    }
    if (initial_page_out) {
        *initial_page_out = initial_page;
    }
    if (initial_offset_out) {
        *initial_offset_out = saved_offset;
    }
    if (honor_saved_position_out) {
        *honor_saved_position_out = honor_saved_position;
    }
    rc = 0;

cleanup:
    reader_document_free(&doc);
    reader_document_free(&saved_doc);
    return rc;
}

static int reader_open_thread(void *userdata) {
    ReaderOpenState *state = (ReaderOpenState *)userdata;
    ApiContext ctx;

    if (!state) {
        return -1;
    }
    if (api_init(&ctx, state->data_dir) != 0) {
        state->failed = 1;
        state->running = 0;
        return -1;
    }
    snprintf(ctx.ca_file, sizeof(ctx.ca_file), "%s", state->ca_file);

    if (reader_prepare_open_document(&ctx,
                                     state->source_target,
                                     state->book_id[0] ? state->book_id : NULL,
                                     state->font_size,
                                     &state->doc,
                                     state->source_target, sizeof(state->source_target),
                                     &state->content_font_size,
                                     &state->initial_page,
                                     &state->initial_offset,
                                     &state->honor_saved_position) == 0) {
        state->ready = 1;
    } else {
        state->failed = 1;
    }

    state->poor_network = ctx.poor_network;
    api_cleanup(&ctx);
    state->running = 0;
    return state->ready ? 0 : -1;
}

static int chapter_prefetch_thread(void *userdata) {
    ChapterPrefetchState *state = (ChapterPrefetchState *)userdata;
    ApiContext ctx;

    if (!state) {
        return -1;
    }
    if (api_init(&ctx, state->data_dir) != 0) {
        state->failed = 1;
        state->running = 0;
        return -1;
    }
    snprintf(ctx.ca_file, sizeof(ctx.ca_file), "%s", state->ca_file);

    if (reader_prefetch(&ctx, state->target, state->font_size, &state->doc) == 0) {
        state->ready = 1;
    } else {
        state->failed = 1;
    }

    api_cleanup(&ctx);
    state->running = 0;
    return state->ready ? 0 : -1;
}

static int append_blank_lines(ReaderViewState *state, int count) {
    for (int i = 0; i < count; i++) {
        int offset = state->line_count > 0 && state->line_offsets ?
            state->line_offsets[state->line_count - 1] : 0;
        if (append_line(state, "", 0, offset) != 0) {
            return -1;
        }
    }
    return 0;
}

static int reader_total_pages(ReaderViewState *state);

static int wrap_paragraph(TTF_Font *font, const char *text, int max_width, ReaderViewState *state) {
    const char *p = text;
    const char *line_start = text;
    const char *last_break = NULL;
    const char *last_word_start = NULL;
    int char_offset = 0;
    int line_start_offset = 0;
    int line_width = 0;
    int in_latin_word = 0;

    if (!font || !text || !state) {
        return -1;
    }

    while (*p) {
        int ch_len = utf8_char_len((unsigned char)*p);
        int ch_width = 0;

        /* Handle explicit newlines */
        if (*p == '\n') {
            if (p > line_start) {
                if (append_line(state, line_start, (size_t)(p - line_start), line_start_offset) != 0) {
                    return -1;
                }
                if (append_blank_lines(state, UI_READER_PARAGRAPH_GAP_LINES) != 0) {
                    return -1;
                }
            }
            p++;
            char_offset++;
            line_start = p;
            line_start_offset = char_offset;
            last_break = NULL;
            last_word_start = NULL;
            line_width = 0;
            in_latin_word = 0;
            continue;
        }

        /* Track break opportunities */
        if (isspace((unsigned char)*p)) {
            /* Space is always a valid break point */
            last_break = p;
            in_latin_word = 0;
            last_word_start = NULL;
        } else if (is_cjk_char(p)) {
            /* CJK characters can break before or after */
            if (p > line_start) {
                last_break = p;
            }
            in_latin_word = 0;
            last_word_start = NULL;
        } else if (is_latin_or_digit(p)) {
            /* Start of a Latin/digit word - track the word start */
            if (!in_latin_word) {
                in_latin_word = 1;
                last_word_start = p;
            }
        } else {
            /* Other characters (punctuation) - may break after */
            in_latin_word = 0;
            last_word_start = NULL;
        }

        /* Get character width using fast cached lookup */
        ch_width = get_char_width_fast(font, p, ch_len);

        /* Check if adding this character would exceed max width */
        if (line_width + ch_width > max_width && p > line_start) {
            const char *break_at;
            const char *next_start;

            /* Determine break point */
            if (last_break && last_break > line_start) {
                /* Break at last space or CJK boundary */
                break_at = last_break;
                if (isspace((unsigned char)*last_break)) {
                    next_start = last_break + 1;
                } else {
                    next_start = last_break;
                }
            } else if (last_word_start && last_word_start > line_start) {
                /* Break before current Latin word */
                break_at = last_word_start;
                next_start = last_word_start;
            } else {
                /* Force break at current position */
                break_at = p;
                next_start = p;
            }

            /* Trim trailing spaces from line */
            while (break_at > line_start && isspace((unsigned char)break_at[-1])) {
                break_at--;
            }

            /* Skip leading spaces for next line */
            while (*next_start && isspace((unsigned char)*next_start) && *next_start != '\n') {
                next_start++;
            }

            /* Handle forbidden line-start punctuation */
            {
                const char *punct = skip_line_start_spacing(next_start, next_start + strlen(next_start));
                while (*punct && *punct != '\n' && is_forbidden_line_start_punct(punct)) {
                    punct += utf8_char_len((unsigned char)*punct);
                }
                if (punct > next_start) {
                    break_at = punct;
                    next_start = punct;
                }
            }

            if (append_line(state, line_start, (size_t)(break_at - line_start), line_start_offset) != 0) {
                return -1;
            }

            /* Update position tracking */
            p = next_start;
            while (*p && isspace((unsigned char)*p) && *p != '\n') {
                p++;
                char_offset++;
            }
            line_start = p;
            line_start_offset = char_offset;
            last_break = NULL;
            last_word_start = NULL;
            line_width = 0;
            in_latin_word = 0;
            continue;
        }

        line_width += ch_width;
        p += ch_len;
        char_offset++;
    }

    /* Append remaining text */
    if (p > line_start) {
        if (append_line(state, line_start, (size_t)(p - line_start), line_start_offset) != 0) {
            return -1;
        }
    }
    return 0;
}

static int reader_view_init_from_document(TTF_Font *font, int content_width, int content_height,
                                          int honor_saved_position, ReaderViewState *state) {
    TTF_Font *render_font = font;

    if (!state) {
        return -1;
    }
    if (state->content_font_size <= 0) {
        state->content_font_size = UI_READER_CONTENT_FONT_SIZE;
    }
    if (reader_reset_content_font(font, state) != 0) {
        return -1;
    }
    if (state->content_font) {
        render_font = state->content_font;
    }
    if (wrap_paragraph(render_font, state->doc.content_text, content_width, state) != 0) {
        return -1;
    }

    state->line_height = reader_line_height_for_font_size(state->content_font_size);
    state->lines_per_page = state->line_height > 0 ? content_height / state->line_height : 18;
    if (state->lines_per_page < 1) {
        state->lines_per_page = 1;
    }
    state->current_page = 0;
    if (honor_saved_position && state->doc.saved_chapter_offset > 0) {
        state->current_page = reader_find_page_for_offset(state, state->doc.saved_chapter_offset);
    }
    reader_sync_catalog_selection(state);
    state->catalog_open = 0;
    return 0;
}

static int reader_view_adopt_document(TTF_Font *font, ReaderDocument *doc,
                                      int content_width, int content_height,
                                      int honor_saved_position, ReaderViewState *state) {
    int content_font_size = state ? state->content_font_size : UI_READER_CONTENT_FONT_SIZE;

    reader_view_free(state);
    if (!doc || !doc->content_text || !doc->target) {
        return -1;
    }
    state->doc = *doc;
    state->content_font_size = content_font_size;
    memset(doc, 0, sizeof(*doc));
    if (reader_view_init_from_document(font, content_width, content_height,
                                       honor_saved_position, state) != 0) {
        reader_view_free(state);
        return -1;
    }
    return 0;
}

static int reader_view_load(ApiContext *ctx, TTF_Font *font, const char *target, int font_size,
                            int content_width, int content_height, int honor_saved_position,
                            ReaderViewState *state) {
    int content_font_size = state ? state->content_font_size : UI_READER_CONTENT_FONT_SIZE;

    reader_view_free(state);
    if (reader_load(ctx, target, font_size, &state->doc) != 0) {
        return -1;
    }
    state->content_font_size = content_font_size;
    if (reader_view_init_from_document(font, content_width, content_height,
                                       honor_saved_position, state) != 0) {
        reader_view_free(state);
        return -1;
    }
    return 0;
}

static int reader_rewrap(TTF_Font *font, int content_width, int content_height,
                         ReaderViewState *state) {
    TTF_Font *render_font = state->content_font ? state->content_font : font;
    int saved_offset = 0;

    if (!state || !state->doc.content_text) {
        return -1;
    }

    /* Anchor around the visible portion of the current page so font-size
     * changes keep us near what we were actually reading. */
    saved_offset = reader_anchor_offset(state);

    /* Free old lines but keep doc */
    if (state->lines) {
        for (int i = 0; i < state->line_count; i++) {
            free(state->lines[i]);
        }
        free(state->lines);
        state->lines = NULL;
    }
    free(state->line_offsets);
    state->line_offsets = NULL;
    state->line_count = 0;
    state->line_capacity = 0;

    if (wrap_paragraph(render_font, state->doc.content_text, content_width, state) != 0) {
        return -1;
    }

    state->line_height = reader_line_height_for_font_size(state->content_font_size);
    state->lines_per_page = state->line_height > 0 ? content_height / state->line_height : 18;
    if (state->lines_per_page < 1) {
        state->lines_per_page = 1;
    }

    state->current_page = reader_find_page_for_offset(state, saved_offset);
    return 0;
}

static int reader_total_pages(ReaderViewState *state) {
    if (!state || state->line_count < 0) {
        return 1;
    }
    return state->line_count > 0 ?
        (state->line_count + state->lines_per_page - 1) / state->lines_per_page : 1;
}

static int reader_find_page_for_offset(const ReaderViewState *state, int target_offset) {
    int total_pages;

    if (!state || target_offset <= 0) {
        return 0;
    }

    total_pages = reader_total_pages((ReaderViewState *)state);
    if (total_pages <= 1) {
        return 0;
    }

    if (state->line_offsets && state->line_count > 0) {
        for (int page = 0; page < total_pages; page++) {
            int start_line = page * state->lines_per_page;
            int next_start_line = (page + 1) * state->lines_per_page;
            int page_start_offset;
            int next_page_offset;
            if (start_line >= state->line_count) {
                return total_pages - 1;
            }
            page_start_offset = state->line_offsets[start_line];
            if (target_offset <= page_start_offset) {
                return page;
            }
            if (next_start_line >= state->line_count) {
                return page;
            }
            next_page_offset = state->line_offsets[next_start_line];
            if (target_offset < next_page_offset) {
                return page;
            }
        }
        return total_pages - 1;
    }

    for (int page = 0; page < total_pages; page++) {
        int page_offset = reader_estimate_chapter_offset(&state->doc, page, total_pages);
        if (page_offset >= target_offset) {
            return page;
        }
    }

    return total_pages - 1;
}

static int reader_current_page_offset(const ReaderViewState *state) {
    int start_line;

    if (!state) {
        return 0;
    }
    start_line = state->current_page * state->lines_per_page;
    if (state->line_offsets && start_line >= 0 && start_line < state->line_count) {
        return state->line_offsets[start_line];
    }
    return reader_estimate_chapter_offset(&state->doc,
                                          state->current_page,
                                          reader_total_pages((ReaderViewState *)state));
}

static int reader_anchor_offset(const ReaderViewState *state) {
    int start_line;
    int end_line;
    int visible_lines;
    int anchor_line;

    if (!state || !state->line_offsets || state->line_count <= 0 || state->lines_per_page <= 0) {
        return 0;
    }

    start_line = state->current_page * state->lines_per_page;
    if (start_line < 0) {
        start_line = 0;
    }
    if (start_line >= state->line_count) {
        return state->line_offsets[state->line_count - 1];
    }

    end_line = start_line + state->lines_per_page;
    if (end_line > state->line_count) {
        end_line = state->line_count;
    }
    visible_lines = end_line - start_line;
    if (visible_lines <= 0) {
        return state->line_offsets[start_line];
    }

    anchor_line = start_line + visible_lines / 2;
    if (anchor_line >= end_line) {
        anchor_line = end_line - 1;
    }
    return state->line_offsets[anchor_line];
}

static void reader_set_source_target(ReaderViewState *state, const char *source_target) {
    if (!state) {
        return;
    }
    if (!source_target) {
        state->source_target[0] = '\0';
        return;
    }
    snprintf(state->source_target, sizeof(state->source_target), "%s", source_target);
}

static void reader_clamp_current_page(ReaderViewState *state) {
    int total_pages;

    if (!state) {
        return;
    }
    total_pages = reader_total_pages(state);
    if (state->current_page < 0) {
        state->current_page = 0;
    }
    if (state->current_page >= total_pages) {
        state->current_page = total_pages - 1;
    }
    if (state->current_page < 0) {
        state->current_page = 0;
    }
}

static int reader_current_catalog_index(ReaderViewState *state) {
    if (!state || !state->doc.catalog_items || state->doc.catalog_count <= 0) {
        return -1;
    }
    for (int i = 0; i < state->doc.catalog_count; i++) {
        if (state->doc.catalog_items[i].is_current) {
            return i;
        }
    }
    if (state->doc.chapter_uid) {
        for (int i = 0; i < state->doc.catalog_count; i++) {
            if (state->doc.catalog_items[i].chapter_uid &&
                strcmp(state->doc.catalog_items[i].chapter_uid, state->doc.chapter_uid) == 0) {
                return i;
            }
        }
    }
    if (state->doc.chapter_idx > 0) {
        for (int i = 0; i < state->doc.catalog_count; i++) {
            if (state->doc.catalog_items[i].chapter_idx == state->doc.chapter_idx) {
                return i;
            }
        }
    }
    return 0;
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

/* Returns the target URL of the server progress chapter if it differs from the
   currently loaded chapter (identified by is_current in the catalog), or NULL
   if the loaded chapter already matches the progress chapter. */
static const char *reader_find_progress_target(const ReaderViewState *state) {
    int i;

    if ((!state->doc.progress_chapter_uid && state->doc.progress_chapter_idx <= 0) ||
        !state->doc.catalog_items || state->doc.catalog_count <= 0) {
        return NULL;
    }
    /* Find the progress chapter in the catalog and compare its target URL
       against the actually loaded chapter URL (doc.target).  We avoid using
       the is_current catalog flag because the server may set it based on the
       cloud reading position rather than the chapter whose content was actually
       rendered on the page. */
    for (i = 0; i < state->doc.catalog_count; i++) {
        int matches_progress = 0;
        if (state->doc.progress_chapter_uid &&
            state->doc.catalog_items[i].chapter_uid &&
            strcmp(state->doc.catalog_items[i].chapter_uid, state->doc.progress_chapter_uid) == 0) {
            matches_progress = 1;
        } else if (state->doc.progress_chapter_idx > 0 &&
                   state->doc.catalog_items[i].chapter_idx == state->doc.progress_chapter_idx) {
            matches_progress = 1;
        }
        if (matches_progress) {
            if (state->doc.target && state->doc.catalog_items[i].target &&
                strcmp(state->doc.target, state->doc.catalog_items[i].target) == 0) {
                return NULL; /* already at the progress chapter */
            }
            return state->doc.catalog_items[i].target;
        }
    }
    return NULL;
}

static int reader_has_cloud_position(const ReaderDocument *doc) {
    if (!doc) {
        return 0;
    }
    if (doc->progress_chapter_idx > 0) {
        return 1;
    }
    if (doc->progress_chapter_uid &&
        doc->progress_chapter_uid[0] &&
        strcmp(doc->progress_chapter_uid, "0") != 0) {
        return 1;
    }
    if (doc->saved_chapter_offset > 0) {
        return 1;
    }
    return 0;
}

static void reader_sync_catalog_selection(ReaderViewState *state) {
    int index;

    if (!state) {
        return;
    }
    index = reader_current_catalog_index(state);
    state->catalog_selected = index >= 0 ? index : 0;
}

static void reader_open_catalog(ApiContext *ctx, ReaderViewState *state, char *status, size_t status_size) {
    if (!state || state->doc.catalog_count <= 0) {
        return;
    }

    if (ctx) {
        reader_focus_catalog(ctx, &state->doc);
    }
    if (status && status_size > 0) {
        status[0] = '\0';
    }

    reader_sync_catalog_selection(state);
    state->catalog_open = 1;
}

static void chapter_prefetch_maybe_start(ApiContext *ctx, ChapterPrefetchState *state,
                                         SDL_Thread **thread_handle,
                                         const char *target, int font_size) {
    if (!ctx || !state || !thread_handle || !target || !target[0]) {
        return;
    }
    if (state->running) {
        return;
    }
    if (state->ready && strcmp(state->target, target) == 0 && state->font_size == font_size) {
        return;
    }
    if (strlen(target) >= sizeof(state->target)) {
        return;
    }
    if (*thread_handle) {
        SDL_WaitThread(*thread_handle, NULL);
        *thread_handle = NULL;
    }
    chapter_prefetch_reset(state);
    snprintf(state->data_dir, sizeof(state->data_dir), "%s", ctx->data_dir);
    snprintf(state->ca_file, sizeof(state->ca_file), "%s", ctx->ca_file);
    snprintf(state->target, sizeof(state->target), "%s", target);
    state->font_size = font_size;
    state->running = 1;
    *thread_handle = SDL_CreateThread(chapter_prefetch_thread, "weread-prefetch", state);
    if (!*thread_handle) {
        state->running = 0;
        state->failed = 1;
    }
}

static void chapter_prefetch_poll(ChapterPrefetchState *state, SDL_Thread **thread_handle) {
    if (!state || !thread_handle || !*thread_handle || state->running) {
        return;
    }
    SDL_WaitThread(*thread_handle, NULL);
    *thread_handle = NULL;
    if (state->failed) {
        chapter_prefetch_reset(state);
    }
}

static void chapter_prefetch_slot_reset(ChapterPrefetchSlot *slot) {
    if (!slot) {
        return;
    }
    if (slot->thread) {
        SDL_WaitThread(slot->thread, NULL);
        slot->thread = NULL;
    }
    chapter_prefetch_reset(&slot->state);
}

static int chapter_prefetch_target_in_list(const char *target, char targets[][2048], int target_count) {
    if (!target || !target[0]) {
        return 0;
    }
    for (int i = 0; i < target_count; i++) {
        if (strcmp(targets[i], target) == 0) {
            return 1;
        }
    }
    return 0;
}

static void chapter_prefetch_cache_poll(ChapterPrefetchCache *cache) {
    if (!cache) {
        return;
    }
    for (int i = 0; i < (int)(sizeof(cache->slots) / sizeof(cache->slots[0])); i++) {
        chapter_prefetch_poll(&cache->slots[i].state, &cache->slots[i].thread);
    }
}

static int chapter_prefetch_cache_adopt(ChapterPrefetchCache *cache, const char *target,
                                        TTF_Font *body_font, ReaderViewState *reader_state,
                                        const UiLayout *current_layout) {
    if (!cache || !target || !target[0] || !body_font || !reader_state || !current_layout) {
        return -1;
    }
    for (int i = 0; i < (int)(sizeof(cache->slots) / sizeof(cache->slots[0])); i++) {
        ChapterPrefetchSlot *slot = &cache->slots[i];
        if (slot->state.ready &&
            strcmp(slot->state.target, target) == 0 &&
            reader_view_adopt_document(body_font, &slot->state.doc,
                                       current_layout->reader_content_w,
                                       current_layout->reader_content_h, 0,
                                       reader_state) == 0) {
            chapter_prefetch_slot_reset(slot);
            return 0;
        }
    }
    return -1;
}

static void chapter_prefetch_cache_request(ApiContext *ctx, ChapterPrefetchCache *cache,
                                           const char *target, int font_size) {
    ChapterPrefetchSlot *free_slot = NULL;

    if (!ctx || !cache || !target || !target[0]) {
        return;
    }

    for (int i = 0; i < (int)(sizeof(cache->slots) / sizeof(cache->slots[0])); i++) {
        ChapterPrefetchSlot *slot = &cache->slots[i];
        if ((slot->state.running || slot->state.ready) &&
            strcmp(slot->state.target, target) == 0 &&
            slot->state.font_size == font_size) {
            return;
        }
        if (!free_slot && !slot->state.running && !slot->thread && !slot->state.ready) {
            free_slot = slot;
        }
    }

    if (!free_slot) {
        for (int i = 0; i < (int)(sizeof(cache->slots) / sizeof(cache->slots[0])); i++) {
            ChapterPrefetchSlot *slot = &cache->slots[i];
            if (!slot->state.running && !slot->thread) {
                chapter_prefetch_reset(&slot->state);
                free_slot = slot;
                break;
            }
        }
    }

    if (!free_slot) {
        return;
    }

    chapter_prefetch_maybe_start(ctx, &free_slot->state, &free_slot->thread, target, font_size);
}

static void chapter_prefetch_cache_update(ApiContext *ctx, ChapterPrefetchCache *cache,
                                          ReaderViewState *reader_state) {
    char targets[UI_CHAPTER_PREFETCH_RADIUS * 2][2048];
    int target_count = 0;
    int current_index;

    if (!ctx || !cache || !reader_state) {
        return;
    }

    current_index = reader_current_catalog_index(reader_state);
    if (reader_state->doc.catalog_items && reader_state->doc.catalog_count > 0 && current_index >= 0) {
        for (int distance = 1; distance <= UI_CHAPTER_PREFETCH_RADIUS; distance++) {
            int indexes[2] = { current_index - distance, current_index + distance };

            for (int j = 0; j < 2; j++) {
                int index = indexes[j];
                ReaderCatalogItem *item;
                if (index < 0 || index >= reader_state->doc.catalog_count) {
                    continue;
                }
                item = &reader_state->doc.catalog_items[index];
                if (!item->target || !item->target[0] ||
                    chapter_prefetch_target_in_list(item->target, targets, target_count)) {
                    continue;
                }
                snprintf(targets[target_count++], sizeof(targets[0]), "%s", item->target);
            }
        }
    } else {
        if (reader_state->doc.prev_target && reader_state->doc.prev_target[0]) {
            snprintf(targets[target_count++], sizeof(targets[0]), "%s", reader_state->doc.prev_target);
        }
        if (reader_state->doc.next_target && reader_state->doc.next_target[0] &&
            !chapter_prefetch_target_in_list(reader_state->doc.next_target, targets, target_count)) {
            snprintf(targets[target_count++], sizeof(targets[0]), "%s", reader_state->doc.next_target);
        }
    }

    for (int i = 0; i < (int)(sizeof(cache->slots) / sizeof(cache->slots[0])); i++) {
        ChapterPrefetchSlot *slot = &cache->slots[i];
        if ((slot->state.ready || slot->state.running) &&
            !chapter_prefetch_target_in_list(slot->state.target, targets, target_count) &&
            !slot->state.running) {
            chapter_prefetch_slot_reset(slot);
        }
    }

    for (int i = 0; i < target_count; i++) {
        chapter_prefetch_cache_request(ctx, cache, targets[i], reader_state->doc.font_size);
    }
}

static void chapter_prefetch_cache_reset(ChapterPrefetchCache *cache) {
    if (!cache) {
        return;
    }
    for (int i = 0; i < (int)(sizeof(cache->slots) / sizeof(cache->slots[0])); i++) {
        chapter_prefetch_slot_reset(&cache->slots[i]);
    }
}

static int chapter_prefetch_cache_has_running_work(const ChapterPrefetchCache *cache) {
    if (!cache) {
        return 0;
    }
    for (int i = 0; i < (int)(sizeof(cache->slots) / sizeof(cache->slots[0])); i++) {
        if (cache->slots[i].thread || cache->slots[i].state.running) {
            return 1;
        }
    }
    return 0;
}

static int reader_expand_catalog_for_selection(ApiContext *ctx, ReaderViewState *state,
                                               int direction, char *status, size_t status_size) {
    int added_count = 0;

    if (!ctx || !state || !state->doc.catalog_items || state->doc.catalog_count <= 0) {
        return 0;
    }
    if (reader_expand_catalog(ctx, &state->doc, direction, &added_count) != 0) {
        if (status && status_size > 0) {
            /* 无法加载更多章节 */
            snprintf(status, status_size, "\xE6\x97\xA0\xE6\xB3\x95\xE5\x8A\xA0\xE8\xBD\xBD\xE6\x9B\xB4\xE5\xA4\x9A\xE7\xAB\xA0\xE8\x8A\x82");
        }
        return 0;
    }
    if (direction < 0 && added_count > 0) {
        state->catalog_selected += added_count;
    }
    return added_count;
}

static void reader_build_page_summary(ReaderViewState *state, char *out, size_t out_size) {
    int total_pages;
    int start_line;
    int end_line;
    size_t len = 0;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!state || !state->lines || state->line_count <= 0) {
        return;
    }

    total_pages = reader_total_pages(state);
    start_line = state->current_page * state->lines_per_page;
    end_line = start_line + state->lines_per_page;
    if (end_line > state->line_count) {
        end_line = state->line_count;
    }

    for (int i = start_line; i < end_line && len + 1 < out_size; i++) {
        const char *p = state->lines[i];
        while (p && *p && len + 1 < out_size) {
            int ch_len = utf8_char_len((unsigned char)*p);
            if (len + (size_t)ch_len >= out_size) {
                break;
            }
            memcpy(out + len, p, (size_t)ch_len);
            len += (size_t)ch_len;
            p += ch_len;
            if (len >= 20) {
                break;
            }
        }
        if (len >= 20 || state->current_page + 1 < total_pages) {
            break;
        }
    }
    out[len] = '\0';
}

static void reader_save_local_position(ApiContext *ctx, ReaderViewState *state) {
    int current_offset;

    if (!ctx || !state || !state->doc.book_id || !state->doc.target || !state->source_target[0]) {
        return;
    }
    reader_clamp_current_page(state);
    current_offset = reader_anchor_offset(state);
    state_save_reader_position(ctx, state->doc.book_id, state->source_target, state->doc.target,
                               state->doc.font_size, state->content_font_size,
                               state->current_page, current_offset);
    state_save_last_reader(ctx, state->source_target, state->doc.font_size, state->content_font_size);
}

static void reader_progress_begin(ReaderViewState *state, Uint32 now) {
    if (!state || state->progress_initialized) {
        return;
    }
    state->progress_initialized = 1;
    state->progress_paused = 0;
    state->progress_initial_report_pending = 1;
    state->progress_start_tick = now;
    state->progress_pause_tick = 0;
    state->progress_pause_deadline_tick = now + UI_PROGRESS_PAUSE_TIMEOUT_MS;
    state->progress_report_due_tick = now + UI_PROGRESS_REPORT_INTERVAL_MS;
}

static void reader_progress_note_activity(ReaderViewState *state, Uint32 now) {
    Uint32 paused_duration;

    if (!state) {
        return;
    }
    if (!state->progress_initialized) {
        reader_progress_begin(state, now);
        return;
    }

    state->progress_pause_deadline_tick = now + UI_PROGRESS_PAUSE_TIMEOUT_MS;
    if (!state->progress_paused) {
        return;
    }

    paused_duration = state->progress_pause_tick > 0 && now > state->progress_pause_tick ?
        (now - state->progress_pause_tick) : 0;
    if (paused_duration > 0 && state->progress_start_tick > 0) {
        state->progress_start_tick += paused_duration;
    }
    state->progress_paused = 0;
    state->progress_pause_tick = 0;
    if (now - state->progress_start_tick > UI_PROGRESS_REPORT_INTERVAL_MS) {
        state->progress_report_due_tick = now;
    } else if (state->progress_report_due_tick == 0) {
        state->progress_report_due_tick = state->progress_start_tick + UI_PROGRESS_REPORT_INTERVAL_MS;
    }
}

static int reader_progress_finalize_thread(ReaderViewState *state,
                                           ProgressReportState *report_state,
                                           SDL_Thread **report_thread) {
    Uint32 now;
    int result;

    if (!state || !report_state || !report_thread || !*report_thread || report_state->running) {
        return 0;
    }

    SDL_WaitThread(*report_thread, NULL);
    *report_thread = NULL;
    result = report_state->result;
    now = SDL_GetTicks();
    if (result == READER_REPORT_OK) {
        state->progress_start_tick = now;
        if (!state->progress_paused) {
            state->progress_report_due_tick = now + UI_PROGRESS_REPORT_INTERVAL_MS;
        }
    } else if (!state->progress_paused) {
        state->progress_report_due_tick = now + UI_PROGRESS_REPORT_INTERVAL_MS;
    }
    progress_report_state_reset(report_state);
    return result;
}

static void reader_progress_queue_report(ApiContext *ctx, ReaderViewState *state,
                                         ProgressReportState *report_state,
                                         SDL_Thread **report_thread, int reading_seconds,
                                         int compute_progress) {
    if (!ctx || !state || !report_state || !report_thread ||
        !state->doc.book_id || !state->doc.token || !state->doc.chapter_uid) {
        return;
    }

    reader_progress_finalize_thread(state, report_state, report_thread);
    if (*report_thread || report_state->running) {
        return;
    }

    memset(report_state, 0, sizeof(*report_state));
    snprintf(report_state->data_dir, sizeof(report_state->data_dir), "%s", ctx->data_dir);
    snprintf(report_state->ca_file, sizeof(report_state->ca_file), "%s", ctx->ca_file);
    if (copy_reader_report_document(&report_state->doc, &state->doc) != 0) {
        progress_report_state_reset(report_state);
        return;
    }
    report_state->current_page = state->current_page;
    report_state->total_pages = reader_total_pages(state);
    report_state->chapter_offset = reader_current_page_offset(state);
    report_state->reading_seconds = reading_seconds > 0 ? reading_seconds : 0;
    report_state->compute_progress = compute_progress;
    reader_build_page_summary(state, report_state->page_summary, sizeof(report_state->page_summary));
    report_state->running = 1;
    *report_thread = SDL_CreateThread(progress_report_thread, "weread-progress-report", report_state);
    if (!*report_thread) {
        progress_report_state_reset(report_state);
    }
}

static void reader_progress_flush_blocking(ApiContext *ctx, ReaderViewState *state,
                                          int compute_progress) {
    Uint32 now;
    Uint32 elapsed_ms;
    int elapsed_seconds;
    int rc;
    char page_summary[128];

    if (!ctx || !state || !state->doc.book_id || !state->doc.token || !state->doc.chapter_uid) {
        return;
    }

    now = SDL_GetTicks();
    reader_progress_note_activity(state, now);
    elapsed_ms = state->progress_start_tick > 0 && now > state->progress_start_tick ?
        (now - state->progress_start_tick) : 0;
    elapsed_seconds = (int)(elapsed_ms / 1000);
    if (elapsed_seconds < 0) {
        elapsed_seconds = 0;
    }
    reader_build_page_summary(state, page_summary, sizeof(page_summary));
    rc = reader_report_progress_at_offset(ctx, &state->doc, state->current_page,
                                          reader_total_pages(state), elapsed_seconds,
                                          page_summary, compute_progress,
                                          reader_current_page_offset(state));
    if (rc == READER_REPORT_OK) {
        state->progress_start_tick = now;
        if (!state->progress_paused) {
            state->progress_report_due_tick = now + UI_PROGRESS_REPORT_INTERVAL_MS;
        }
    } else if (!state->progress_paused) {
        state->progress_report_due_tick = now + UI_PROGRESS_REPORT_INTERVAL_MS;
    }
}

static int reader_progress_update(ApiContext *ctx, ReaderViewState *state,
                                  ProgressReportState *report_state,
                                  SDL_Thread **report_thread) {
    Uint32 now;
    Uint32 elapsed_ms;
    int elapsed_seconds;

    if (!ctx || !state || !report_state || !report_thread ||
        !state->doc.book_id || !state->doc.token || !state->doc.chapter_uid) {
        return 0;
    }

    now = SDL_GetTicks();
    reader_progress_begin(state, now);
    reader_progress_finalize_thread(state, report_state, report_thread);

    if (!state->progress_paused && now >= state->progress_pause_deadline_tick) {
        state->progress_paused = 1;
        state->progress_pause_tick = now;
        state->progress_report_due_tick = 0;
    }

    if (*report_thread || report_state->running || state->progress_paused) {
        return 0;
    }

    if (state->progress_initial_report_pending) {
        state->progress_initial_report_pending = 0;
        reader_progress_queue_report(ctx, state, report_state, report_thread, 0, 0);
        return 0;
    }

    if (state->progress_report_due_tick == 0 || now < state->progress_report_due_tick) {
        return 0;
    }

    elapsed_ms = state->progress_start_tick > 0 && now > state->progress_start_tick ?
        (now - state->progress_start_tick) : 0;
    elapsed_seconds = (int)(elapsed_ms / 1000);
    reader_progress_queue_report(ctx, state, report_state, report_thread, elapsed_seconds, 1);
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
    int start;
    int end;
    char title_buf[256];
    int header_title_y;
    float eased = ui_ease_out_cubic(progress);
    float row_eased = ui_ease_out_cubic(progress);
    float selected_clamped;
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
    start = (int)floorf(selected_clamped) - visible / 2;
    if (start < 0) {
        start = 0;
    }
    end = start + visible;
    if (end > state->doc.catalog_count) {
        end = state->doc.catalog_count;
        start = end - visible;
        if (start < 0) {
            start = 0;
        }
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
    selected_row.y = list_top + (int)lroundf((selected_clamped - (float)start) * (float)line_height);
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

    for (int i = start; i < end; i++) {
        ReaderCatalogItem *item = &state->doc.catalog_items[i];
        SDL_Rect row = { panel.x + 16, list_top + (i - start) * line_height, panel.w - 32, line_height - 4 };
        char row_buf[256];
        int indent = item->level > 1 ? (item->level - 1) * 20 : 0;
        int text_x_shift =
            (int)lroundf((1.0f - row_eased) *
                         (14.0f + fminf(fabsf((float)i - selected_clamped), 4.0f) * 5.0f));
        SDL_Color color = item->is_lock ? dim : ink;

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

static int login_start_thread(void *userdata) {
    LoginStartState *state = (LoginStartState *)userdata;
    ApiContext ctx;

    if (api_init(&ctx, state->data_dir) != 0) {
        state->failed = 1;
        state->running = 0;
        return -1;
    }
    snprintf(ctx.ca_file, sizeof(ctx.ca_file), "%s", state->ca_file);

    if (auth_start(&ctx, &state->session, state->qr_path) == 0) {
        state->success = 1;
    } else {
        state->failed = 1;
    }

    api_cleanup(&ctx);
    state->running = 0;
    return state->success ? 0 : -1;
}

static int login_poll_thread(void *userdata) {
    LoginPollState *state = (LoginPollState *)userdata;
    ApiContext ctx;

    if (api_init(&ctx, state->data_dir) != 0) {
        state->running = 0;
        return -1;
    }
    snprintf(ctx.ca_file, sizeof(ctx.ca_file), "%s", state->ca_file);

    while (!state->stop && !state->completed) {
        AuthPollStatus status = AUTH_POLL_WAITING;
        if (auth_poll_once(&ctx, &state->session, &status) == 0) {
            state->last_status = status;
            if (status == AUTH_POLL_CONFIRMED) {
                state->completed = 1;
                break;
            }
        } else {
            state->last_status = AUTH_POLL_ERROR;
        }
        SDL_Delay(700);
    }

    api_cleanup(&ctx);
    state->running = 0;
    return state->completed ? 0 : -1;
}

static void begin_login_flow(ApiContext *ctx, LoginStartState *login_start,
                             SDL_Thread **login_thread, UiView *view,
                             char *status, size_t status_size, const char *qr_path) {
    if (login_start->running || *login_thread) {
        return;
    }

    memset(login_start, 0, sizeof(*login_start));
    snprintf(login_start->data_dir, sizeof(login_start->data_dir), "%s", ctx->data_dir);
    snprintf(login_start->ca_file, sizeof(login_start->ca_file), "%s", ctx->ca_file);
    snprintf(login_start->qr_path, sizeof(login_start->qr_path), "%s", qr_path);
    login_start->running = 1;
    /* 正在生成二维码... */
    snprintf(status, status_size, "\xE6\xAD\xA3\xE5\x9C\xA8\xE7\x94\x9F\xE6\x88\x90\xE4\xBA\x8C\xE7\xBB\xB4\xE7\xA0\x81...");
    *view = VIEW_LOGIN;
    *login_thread = SDL_CreateThread(login_start_thread, "weread-login-start", login_start);
    if (!*login_thread) {
        login_start->running = 0;
        login_start->failed = 1;
        /* 无法创建登录线程 */
        snprintf(status, status_size, "\xE6\x97\xA0\xE6\xB3\x95\xE5\x88\x9B\xE5\xBB\xBA\xE7\x99\xBB\xE5\xBD\x95\xE7\xBA\xBF\xE7\xA8\x8B");
    }
}

static void begin_startup_refresh(ApiContext *ctx, StartupState *startup_state,
                                  SDL_Thread **startup_thread_handle) {
    if (!ctx || !startup_state || !startup_thread_handle || *startup_thread_handle ||
        startup_state->running) {
        return;
    }

    startup_state_reset(startup_state);
    snprintf(startup_state->data_dir, sizeof(startup_state->data_dir), "%s", ctx->data_dir);
    snprintf(startup_state->ca_file, sizeof(startup_state->ca_file), "%s", ctx->ca_file);
    startup_state->running = 1;
    *startup_thread_handle = SDL_CreateThread(startup_thread, "weread-startup", startup_state);
    if (!*startup_thread_handle) {
        startup_state->running = 0;
        startup_state->completed = 1;
        startup_state->session_ok = -1;
    }
}

static void begin_reader_open(ApiContext *ctx, ReaderOpenState *reader_open,
                              SDL_Thread **reader_open_thread_handle,
                              const char *source_target, const char *book_id, int font_size) {
    if (!ctx || !reader_open || !reader_open_thread_handle || !source_target || !*source_target ||
        *reader_open_thread_handle || reader_open->running) {
        return;
    }

    reader_open_state_reset(reader_open);
    snprintf(reader_open->data_dir, sizeof(reader_open->data_dir), "%s", ctx->data_dir);
    snprintf(reader_open->ca_file, sizeof(reader_open->ca_file), "%s", ctx->ca_file);
    snprintf(reader_open->source_target, sizeof(reader_open->source_target), "%s", source_target);
    if (book_id && *book_id) {
        snprintf(reader_open->book_id, sizeof(reader_open->book_id), "%s", book_id);
    }
    reader_open->font_size = font_size;
    reader_open->content_font_size = UI_READER_CONTENT_FONT_SIZE;
    reader_open->running = 1;
    *reader_open_thread_handle = SDL_CreateThread(reader_open_thread, "weread-reader-open", reader_open);
    if (!*reader_open_thread_handle) {
        reader_open->running = 0;
        reader_open->failed = 1;
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
    SDL_Thread *login_thread = NULL;
    SDL_Thread *startup_thread_handle = NULL;
    SDL_Thread *reader_open_thread_handle = NULL;
    SDL_Thread *shelf_cover_download_thread_handle = NULL;
    SDL_Thread *login_poll_thread_handle = NULL;
    SDL_Thread *progress_report_thread_handle = NULL;
    SDL_Texture *scene_texture = NULL;
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
    UiMotionState motion_state;
    UiHapticState haptic_state;
    UiBatteryState battery_state;
    Uint32 poor_network_toast_until = 0;
    Uint32 exit_confirm_until = 0;
    Uint32 reader_exit_confirm_until = 0;
    Uint32 lock_button_ignore_until = 0;
    Uint32 last_lock_trigger_tick = 0;
    time_t last_clock_minute = 0;
    int rc = -1;

    memset(&session, 0, sizeof(session));
    memset(&login_start, 0, sizeof(login_start));
    memset(&login_poll, 0, sizeof(login_poll));
    memset(&startup_state, 0, sizeof(startup_state));
    memset(&reader_open, 0, sizeof(reader_open));
    memset(&progress_report, 0, sizeof(progress_report));
    memset(&shelf_covers, 0, sizeof(shelf_covers));
    shelf_cover_download_state_reset(&shelf_cover_download);
    memset(&chapter_prefetch_cache, 0, sizeof(chapter_prefetch_cache));
    memset(&reader_state, 0, sizeof(reader_state));
    memset(&repeat_state, 0, sizeof(repeat_state));
    memset(&motion_state, 0, sizeof(motion_state));
    memset(&haptic_state, 0, sizeof(haptic_state));
    memset(&battery_state, 0, sizeof(battery_state));
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

    if (font_path && *font_path) {
        title_font = TTF_OpenFont(font_path, UI_TITLE_FONT_SIZE);
        body_font = TTF_OpenFont(font_path, UI_BODY_FONT_SIZE);
    }
    if (!title_font || !body_font) {
        fprintf(stderr, "Failed to open font: %s\n", font_path ? font_path : "(null)");
        goto cleanup;
    }
    (void)ui_platform_init_haptics(tg5040_input, &haptic_state);

    ui_dark_mode = state_load_dark_mode(ctx);
    if (state_load_brightness_level(ctx, &brightness_level) == 0) {
        ui_platform_apply_brightness_level(tg5040_input, brightness_level);
    }

    shelf_nuxt = state_read_json(ctx, "shelf.json");
    if (shelf_nuxt && shelf_books(shelf_nuxt) && cJSON_IsArray(shelf_books(shelf_nuxt))) {
        shelf_status[0] = '\0';
        shelf_cover_download_stop(&shelf_cover_download, &shelf_cover_download_thread_handle);
        shelf_cover_cache_build(ctx, shelf_nuxt, &shelf_covers);
        view = VIEW_SHELF;
    } else {
        cJSON_Delete(shelf_nuxt);
        shelf_nuxt = NULL;
        view = VIEW_BOOTSTRAP;
        /* 正在检查书架... */
        snprintf(status, sizeof(status), "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\xA3\x80\xE6\x9F\xA5\xE4\xB9\xA6\xE6\x9E\xB6...");
    }
    begin_startup_refresh(ctx, &startup_state, &startup_thread_handle);

    while (running) {
        SDL_Event event;
        Uint32 frame_now = SDL_GetTicks();
        time_t wall_now = time(NULL);
        float dt_seconds = motion_state.last_tick > 0 && frame_now > motion_state.last_tick ?
            (float)(frame_now - motion_state.last_tick) / 1000.0f : 0.0f;
        UiView frame_view_before_updates = view;
        int catalog_open_before_updates = reader_state.catalog_open;
        int reader_input_seen = 0;
        int render_requested = 0;
        time_t current_clock_minute = wall_now / 60;

        motion_state.last_tick = frame_now;
        if (last_clock_minute == 0) {
            last_clock_minute = current_clock_minute;
        } else if (current_clock_minute != last_clock_minute) {
            last_clock_minute = current_clock_minute;
            render_requested = 1;
        }
        ui_battery_state_update(&battery_state, frame_now);
        ui_platform_haptic_poll(&haptic_state, frame_now);
        while (SDL_PollEvent(&event)) {
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
            } else if (event.type == SDL_KEYDOWN ||
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
                if (ui_event_is_back(&event, tg5040_input)) {
                    if (view == VIEW_READER) {
                        if (reader_state.catalog_open) {
                            reader_state.catalog_open = 0;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 35);
                        } else if (reader_exit_confirm_until > frame_now) {
                            reader_progress_flush_blocking(ctx, &reader_state, 1);
                            reader_save_local_position(ctx, &reader_state);
                            reader_exit_confirm_until = 0;
                            view = VIEW_SHELF;
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
                } else if (ui_event_is_dark_mode_toggle(&event, tg5040_input,
                                                        tg5040_select_pressed)) {
                    ui_dark_mode = !ui_dark_mode;
                    state_save_dark_mode(ctx, ui_dark_mode);
                    ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 40);
                } else if (ui_event_is_brightness_up(&event, tg5040_input,
                                                     tg5040_start_pressed)) {
                    if (ui_platform_step_brightness(ctx, tg5040_input, 1,
                                                    &brightness_level) != 0) {
                        snprintf(shelf_status, sizeof(shelf_status),
                                 "\xE6\x97\xA0\xE6\xB3\x95\xE8\xB0\x83\xE6\x95\xB4\xE4\xBA\xAE\xE5\xBA\xA6");
                    } else {
                        snprintf(shelf_status, sizeof(shelf_status),
                                 "\xE4\xBA\xAE\xE5\xBA\xA6 %d/10", brightness_level);
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 35);
                    }
                } else if (ui_event_is_brightness_down(&event, tg5040_input,
                                                       tg5040_start_pressed)) {
                    if (ui_platform_step_brightness(ctx, tg5040_input, -1,
                                                    &brightness_level) != 0) {
                        snprintf(shelf_status, sizeof(shelf_status),
                                 "\xE6\x97\xA0\xE6\xB3\x95\xE8\xB0\x83\xE6\x95\xB4\xE4\xBA\xAE\xE5\xBA\xA6");
                    } else {
                        snprintf(shelf_status, sizeof(shelf_status),
                                 "\xE4\xBA\xAE\xE5\xBA\xA6 %d/10", brightness_level);
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 35);
                    }
                } else if (ui_event_is_lock_button(&event) &&
                           frame_now >= lock_button_ignore_until &&
                           (last_lock_trigger_tick == 0 ||
                            frame_now - last_lock_trigger_tick >= 1000)) {
                    last_lock_trigger_tick = frame_now;
                    if (view == VIEW_READER) {
                        reader_progress_flush_blocking(ctx, &reader_state, 1);
                        reader_save_local_position(ctx, &reader_state);
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
                } else if (ui_event_is_rotate_combo(&event, tg5040_input,
                                                    tg5040_select_pressed,
                                                    tg5040_start_pressed)) {
                    rotation = ui_rotation_next(rotation);
                    current_layout = ui_layout_for_rotation(rotation);
                    if (ui_recreate_scene_texture(renderer, &scene_texture, &current_layout) != 0) {
                        fprintf(stderr, "Failed to recreate scene texture: %s\n", SDL_GetError());
                        running = 0;
                        break;
                    }
                    if (view == VIEW_READER) {
                        reader_rewrap(body_font, current_layout.reader_content_w,
                                      current_layout.reader_content_h, &reader_state);
                    }
                    ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                } else if (view == VIEW_SHELF) {
                    cJSON *books = shelf_nuxt ? shelf_books(shelf_nuxt) : NULL;
                    int count = books && cJSON_IsArray(books) ? cJSON_GetArraySize(books) : 0;
                    if ((ui_event_is_down(&event, tg5040_input) || ui_event_is_right(&event, tg5040_input)) &&
                        count > 0 && selected + 1 < count) {
                        selected++;
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                    } else if ((ui_event_is_up(&event, tg5040_input) || ui_event_is_left(&event, tg5040_input)) &&
                               selected > 0) {
                        selected--;
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                    } else if (ui_event_is_shelf_resume(&event, tg5040_input)) {
                        char target[2048];
                        int font_size = 3;
                        if (state_load_last_reader(ctx, target, sizeof(target), &font_size, NULL) == 0) {
                            /* 正在打开 */
                            snprintf(loading_title, sizeof(loading_title), "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x89\x93\xE5\xBC\x80");
                            /* 正在恢复阅读位置... */
                            snprintf(status, sizeof(status), "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x81\xA2\xE5\xA4\x8D\xE9\x98\x85\xE8\xAF\xBB\xE4\xBD\x8D\xE7\xBD\xAE...");
                            begin_reader_open(ctx, &reader_open, &reader_open_thread_handle,
                                              target, NULL, font_size);
                            if (reader_open.running || reader_open_thread_handle) {
                                view = VIEW_OPENING;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                            }
                        }
                    } else if (ui_event_is_confirm(&event, tg5040_input) && count > 0) {
                        cJSON *book = cJSON_GetArrayItem(books, selected);
                        cJSON *urls = shelf_reader_urls(shelf_nuxt);
                        const char *target = shelf_reader_target(urls, selected);
                        const char *book_id = json_get_string(book, "bookId");
                        if (target) {
                            /* 正在打开 */
                            snprintf(loading_title, sizeof(loading_title), "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x89\x93\xE5\xBC\x80");
                            /* 正在加载章节... */
                            snprintf(status, sizeof(status), "\xE6\xAD\xA3\xE5\x9C\xA8\xE5\x8A\xA0\xE8\xBD\xBD\xE7\xAB\xA0\xE8\x8A\x82...");
                            begin_reader_open(ctx, &reader_open, &reader_open_thread_handle,
                                              target, book_id, 3);
                            if (reader_open.running || reader_open_thread_handle) {
                                view = VIEW_OPENING;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                            } else {
                                /* 无法启动加载任务 */
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE5\x90\xAF\xE5\x8A\xA8\xE5\x8A\xA0\xE8\xBD\xBD\xE4\xBB\xBB\xE5\x8A\xA1");
                            }
                        } else {
                            /* 无法打开所选书籍 */
                            snprintf(shelf_status, sizeof(shelf_status),
                                     "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE6\x89\x80\xE9\x80\x89\xE4\xB9\xA6\xE7\xB1\x8D");
                        }
                    }
                } else if (view == VIEW_LOGIN) {
                    if ((ui_event_is_confirm(&event, tg5040_input) ||
                         ui_event_is_keydown(&event, SDLK_l)) &&
                        !login_start.running && !login_active) {
                        begin_login_flow(ctx, &login_start, &login_thread, &view,
                                         status, sizeof(status), qr_path);
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                    }
                } else if (view == VIEW_BOOTSTRAP) {
                    if (ui_event_is_confirm(&event, tg5040_input) &&
                        !startup_state.running && !startup_thread_handle) {
                        /* 微信读书 */
                        snprintf(loading_title, sizeof(loading_title), "\xE5\xBE\xAE\xE4\xBF\xA1\xE8\xAF\xBB\xE4\xB9\xA6");
                        /* 正在重试... */
                        snprintf(status, sizeof(status), "\xE6\xAD\xA3\xE5\x9C\xA8\xE9\x87\x8D\xE8\xAF\x95...");
                        begin_startup_refresh(ctx, &startup_state, &startup_thread_handle);
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                    }
                } else if (view == VIEW_OPENING) {
                    if (ui_event_is_confirm(&event, tg5040_input) &&
                        !reader_open.running && !reader_open_thread_handle &&
                        reader_open.source_target[0]) {
                        /* 正在重试... */
                        snprintf(status, sizeof(status), "\xE6\xAD\xA3\xE5\x9C\xA8\xE9\x87\x8D\xE8\xAF\x95...");
                        begin_reader_open(ctx, &reader_open, &reader_open_thread_handle,
                                          reader_open.source_target,
                                          reader_open.book_id[0] ? reader_open.book_id : NULL,
                                          reader_open.font_size);
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                    }
                } else if (view == VIEW_READER) {
                    int total_pages = reader_total_pages(&reader_state);
                    reader_progress_note_activity(&reader_state, SDL_GetTicks());
                    if (reader_state.catalog_open) {
                        if (ui_event_is_back(&event, tg5040_input) ||
                            ui_event_is_catalog_toggle(&event, tg5040_input)) {
                            reader_state.catalog_open = 0;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 35);
                        } else if (ui_event_is_up(&event, tg5040_input)) {
                            if (reader_state.catalog_selected > 0) {
                                reader_state.catalog_selected--;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            } else if (reader_expand_catalog_for_selection(ctx, &reader_state, -1,
                                                                          shelf_status, sizeof(shelf_status)) > 0) {
                                reader_state.catalog_selected--;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            }
                        } else if (ui_event_is_down(&event, tg5040_input)) {
                            if (reader_state.catalog_selected + 1 < reader_state.doc.catalog_count) {
                                reader_state.catalog_selected++;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            } else if (reader_expand_catalog_for_selection(ctx, &reader_state, 1,
                                                                          shelf_status, sizeof(shelf_status)) > 0 &&
                                       reader_state.catalog_selected + 1 < reader_state.doc.catalog_count) {
                                reader_state.catalog_selected++;
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            }
                        } else if (ui_event_is_left(&event, tg5040_input)) {
                            if (reader_state.catalog_selected > 0) {
                                reader_state.catalog_selected -= 10;
                                if (reader_state.catalog_selected < 0) {
                                    reader_state.catalog_selected = 0;
                                }
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            } else if (reader_expand_catalog_for_selection(ctx, &reader_state, -1,
                                                                          shelf_status, sizeof(shelf_status)) > 0) {
                                reader_state.catalog_selected -= 10;
                                if (reader_state.catalog_selected < 0) {
                                    reader_state.catalog_selected = 0;
                                }
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            }
                        } else if (ui_event_is_right(&event, tg5040_input)) {
                            if (reader_state.catalog_selected + 1 < reader_state.doc.catalog_count) {
                                reader_state.catalog_selected += 10;
                                if (reader_state.catalog_selected >= reader_state.doc.catalog_count) {
                                    reader_state.catalog_selected = reader_state.doc.catalog_count - 1;
                                }
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            } else if (reader_expand_catalog_for_selection(ctx, &reader_state, 1,
                                                                          shelf_status, sizeof(shelf_status)) > 0) {
                                reader_state.catalog_selected += 10;
                                if (reader_state.catalog_selected >= reader_state.doc.catalog_count) {
                                    reader_state.catalog_selected = reader_state.doc.catalog_count - 1;
                                }
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 25);
                            }
                        } else if (ui_event_is_confirm(&event, tg5040_input) &&
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
                                reader_progress_flush_blocking(ctx, &reader_state, 1);
                                if (reader_view_load(ctx, body_font, item->target, font_size,
                                                     current_layout.reader_content_w,
                                                     current_layout.reader_content_h, 0,
                                                     &reader_state) == 0) {
                                    reader_set_source_target(&reader_state, source_target);
                                    reader_save_local_position(ctx, &reader_state);
                                    reader_state.catalog_open = 0;
                                    shelf_status[0] = '\0';
                                    ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 60);
                                } else {
                                    /* 无法打开所选章节 */
                                    snprintf(shelf_status, sizeof(shelf_status),
                                             "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE6\x89\x80\xE9\x80\x89\xE7\xAB\xA0\xE8\x8A\x82");
                                }
                            }
                        }
                    } else if (ui_event_is_catalog_toggle(&event, tg5040_input) &&
                               reader_state.doc.catalog_count > 0) {
                        reader_open_catalog(ctx, &reader_state, shelf_status, sizeof(shelf_status));
                        if (reader_state.catalog_open) {
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 50);
                        }
                    } else if (ui_event_is_page_next(&event, tg5040_input) &&
                        reader_state.current_page + 1 < total_pages) {
                        reader_state.current_page++;
                        reader_save_local_position(ctx, &reader_state);
                    } else if (ui_event_is_page_next(&event, tg5040_input) &&
                               reader_state.doc.next_target && reader_state.doc.next_target[0]) {
                        char *target = strdup(reader_state.doc.next_target);
                        char source_target[2048];
                        int font_size = reader_state.doc.font_size;
                        if (target) {
                            snprintf(source_target, sizeof(source_target), "%s", reader_state.source_target);
                            reader_progress_flush_blocking(ctx, &reader_state, 1);
                            if (chapter_prefetch_cache_adopt(&chapter_prefetch_cache, target, body_font,
                                                             &reader_state, &current_layout) == 0) {
                                reader_set_source_target(&reader_state, source_target);
                                reader_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
                            } else if (reader_view_load(ctx, body_font, target, font_size,
                                                        current_layout.reader_content_w,
                                                        current_layout.reader_content_h, 0,
                                                        &reader_state) == 0) {
                                    reader_set_source_target(&reader_state, source_target);
                                reader_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
                            } else {
                                /* 无法打开下一章 */
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8B\xE4\xB8\x80\xE7\xAB\xA0");
                            }
                            free(target);
                        }
                    } else if (ui_event_is_page_prev(&event, tg5040_input) && reader_state.current_page > 0) {
                        reader_state.current_page--;
                        reader_save_local_position(ctx, &reader_state);
                    } else if (ui_event_is_page_prev(&event, tg5040_input) &&
                               reader_state.doc.prev_target && reader_state.doc.prev_target[0]) {
                        char *target = strdup(reader_state.doc.prev_target);
                        char source_target[2048];
                        int font_size = reader_state.doc.font_size;
                        if (target) {
                            snprintf(source_target, sizeof(source_target), "%s", reader_state.source_target);
                            reader_progress_flush_blocking(ctx, &reader_state, 1);
                            if (chapter_prefetch_cache_adopt(&chapter_prefetch_cache, target, body_font,
                                                             &reader_state, &current_layout) == 0) {
                                reader_set_source_target(&reader_state, source_target);
                                int new_total_pages = reader_total_pages(&reader_state);
                                reader_state.current_page = new_total_pages > 0 ? new_total_pages - 1 : 0;
                                reader_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
                            } else if (reader_view_load(ctx, body_font, target, font_size,
                                                         current_layout.reader_content_w,
                                                         current_layout.reader_content_h, 0,
                                                         &reader_state) == 0) {
                                reader_set_source_target(&reader_state, source_target);
                                int new_total_pages = reader_total_pages(&reader_state);
                                reader_state.current_page = new_total_pages > 0 ? new_total_pages - 1 : 0;
                                reader_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
                            } else {
                                /* 无法打开上一章 */
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8A\xE4\xB8\x80\xE7\xAB\xA0");
                            }
                            free(target);
                        }
                    } else if ((ui_event_is_chapter_prev(&event, tg5040_input) ||
                                ui_event_is_chapter_next(&event, tg5040_input)) &&
                               reader_state.current_page > 0) {
                        reader_state.current_page = 0;
                        reader_save_local_position(ctx, &reader_state);
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 50);
                    } else if (ui_event_is_chapter_prev(&event, tg5040_input) &&
                               reader_state.doc.prev_target && reader_state.doc.prev_target[0]) {
                        char *target = strdup(reader_state.doc.prev_target);
                        char source_target[2048];
                        int font_size = reader_state.doc.font_size;
                        if (target) {
                            snprintf(source_target, sizeof(source_target), "%s", reader_state.source_target);
                            reader_progress_flush_blocking(ctx, &reader_state, 1);
                            if (chapter_prefetch_cache_adopt(&chapter_prefetch_cache, target, body_font,
                                                             &reader_state, &current_layout) == 0) {
                                reader_set_source_target(&reader_state, source_target);
                                reader_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
                            } else if (reader_view_load(ctx, body_font, target, font_size,
                                                         current_layout.reader_content_w,
                                                         current_layout.reader_content_h, 0,
                                                         &reader_state) == 0) {
                                reader_set_source_target(&reader_state, source_target);
                                reader_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
                            } else {
                                /* 无法打开上一章 */
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8A\xE4\xB8\x80\xE7\xAB\xA0");
                            }
                            free(target);
                        }
                    } else if (ui_event_is_chapter_next(&event, tg5040_input) &&
                               reader_state.doc.next_target && reader_state.doc.next_target[0]) {
                        char *target = strdup(reader_state.doc.next_target);
                        char source_target[2048];
                        int font_size = reader_state.doc.font_size;
                        if (target) {
                            snprintf(source_target, sizeof(source_target), "%s", reader_state.source_target);
                            reader_progress_flush_blocking(ctx, &reader_state, 1);
                            if (chapter_prefetch_cache_adopt(&chapter_prefetch_cache, target, body_font,
                                                             &reader_state, &current_layout) == 0) {
                                reader_set_source_target(&reader_state, source_target);
                                reader_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
                            } else if (reader_view_load(ctx, body_font, target, font_size,
                                                        current_layout.reader_content_w,
                                                        current_layout.reader_content_h, 0,
                                                        &reader_state) == 0) {
                                    reader_set_source_target(&reader_state, source_target);
                                reader_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                                ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_EMPHASIS_MS, 80);
                            } else {
                                /* 无法打开下一章 */
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE4\xB8\x8B\xE4\xB8\x80\xE7\xAB\xA0");
                            }
                            free(target);
                        }
                    } else if (ui_event_is_tg5040_button_down(&event, tg5040_input, TG5040_JOY_Y) ||
                               ui_event_is_keydown(&event, SDLK_EQUALS) ||
                               ui_event_is_keydown(&event, SDLK_MINUS)) {
                        /* Y button cycles font size: 28 -> 32 -> 36 -> 40 -> 44 -> 28 */
                        /* +/- keys increase/decrease */
                        if (reader_state.content_font_size <= 0) {
                            reader_state.content_font_size = UI_READER_CONTENT_FONT_SIZE;
                        }
                        if (ui_event_is_keydown(&event, SDLK_MINUS)) {
                            reader_state.content_font_size -= 4;
                            if (reader_state.content_font_size < 24) reader_state.content_font_size = 44;
                        } else {
                            reader_state.content_font_size += 4;
                            if (reader_state.content_font_size > 44) reader_state.content_font_size = 24;
                        }
                        if (reader_reset_content_font(body_font, &reader_state) != 0) {
                            /* 无法应用字体大小 */
                            snprintf(shelf_status, sizeof(shelf_status),
                                     "\xE6\x97\xA0\xE6\xB3\x95\xE5\xBA\x94\xE7\x94\xA8\xE5\xAD\x97\xE4\xBD\x93\xE5\xA4\xA7\xE5\xB0\x8F");
                            continue;
                        }
                        reader_rewrap(body_font, current_layout.reader_content_w,
                                      current_layout.reader_content_h, &reader_state);
                        reader_save_local_position(ctx, &reader_state);
                        ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 45);
                    } else if (ui_event_is_keydown(&event, SDLK_b)) {
                        if (reader_exit_confirm_until > frame_now) {
                            reader_save_local_position(ctx, &reader_state);
                            reader_exit_confirm_until = 0;
                            view = VIEW_SHELF;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_CONFIRM_MS, 50);
                        } else {
                            reader_exit_confirm_until = frame_now + UI_EXIT_CONFIRM_DURATION_MS;
                            render_requested = 1;
                            ui_platform_haptic_pulse(&haptic_state, UI_HAPTIC_NAV_MS, 35);
                        }
                    }
                }
            }
        }

        {
            UiRepeatAction repeat_action =
                ui_repeat_action_current(view, &reader_state, tg5040_input,
                                         joysticks, joystick_count);
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
                ui_apply_repeat_action(repeat_action, ctx, body_font, &reader_state,
                                       shelf_nuxt, &selected, shelf_status,
                                       sizeof(shelf_status), &current_layout,
                                       &chapter_prefetch_cache);
                if (view == VIEW_READER) {
                    render_requested = 1;
                }
                repeat_state.next_tick = now + UI_INPUT_REPEAT_INTERVAL_MS;
            }
        }

        if (view == VIEW_READER) {
            reader_progress_update(ctx, &reader_state,
                                   &progress_report, &progress_report_thread_handle);
            chapter_prefetch_cache_poll(&chapter_prefetch_cache);
            chapter_prefetch_cache_update(ctx, &chapter_prefetch_cache, &reader_state);
        } else {
            chapter_prefetch_cache_poll(&chapter_prefetch_cache);
        }

        shelf_cover_download_poll(&shelf_covers, &shelf_cover_download,
                                  &shelf_cover_download_thread_handle);
        if (view == VIEW_SHELF) {
            shelf_cover_download_maybe_start(ctx, &shelf_covers, &shelf_cover_download,
                                             &shelf_cover_download_thread_handle, selected);
            if (shelf_cover_prepare_nearest(ctx, renderer, &shelf_covers,
                                            motion_state.shelf_selected_visual)) {
                render_requested = 1;
            }
        }

        if (startup_thread_handle && !startup_state.running) {
            SDL_WaitThread(startup_thread_handle, NULL);
            startup_thread_handle = NULL;
            render_requested = 1;
            if (startup_state.session_ok == 1 && startup_state.shelf_nuxt) {
                cJSON_Delete(shelf_nuxt);
                shelf_nuxt = startup_state.shelf_nuxt;
                startup_state.shelf_nuxt = NULL;
                shelf_cover_download_stop(&shelf_cover_download, &shelf_cover_download_thread_handle);
                shelf_cover_cache_build(ctx, shelf_nuxt, &shelf_covers);
                if (shelf_books(shelf_nuxt) && cJSON_IsArray(shelf_books(shelf_nuxt))) {
                    int count = cJSON_GetArraySize(shelf_books(shelf_nuxt));
                    if (count <= 0) {
                        selected = 0;
                    } else if (selected >= count) {
                        selected = count - 1;
                    }
                    if (view != VIEW_READER && view != VIEW_LOGIN && view != VIEW_OPENING) {
                        shelf_status[0] = '\0';
                    } else if (view == VIEW_BOOTSTRAP) {
                        view = VIEW_SHELF;
                    }
                }
            } else if (startup_state.session_ok == 0) {
                view = VIEW_LOGIN;
                /* 正在生成二维码... */
                snprintf(status, sizeof(status), "\xE6\xAD\xA3\xE5\x9C\xA8\xE7\x94\x9F\xE6\x88\x90\xE4\xBA\x8C\xE7\xBB\xB4\xE7\xA0\x81...");
                begin_login_flow(ctx, &login_start, &login_thread, &view, status, sizeof(status), qr_path);
            } else if (!shelf_nuxt && view != VIEW_READER && view != VIEW_LOGIN) {
                /* 网络错误，按 A 重试 */
                snprintf(status, sizeof(status), "\xE7\xBD\x91\xE7\xBB\x9C\xE9\x94\x99\xE8\xAF\xAF\xEF\xBC\x8C\xE6\x8C\x89 A \xE9\x87\x8D\xE8\xAF\x95");
            }
            if (startup_state.poor_network) {
                poor_network_toast_until = SDL_GetTicks() + 3000;
            }
            startup_state_reset(&startup_state);
        }

        if (reader_open_thread_handle && !reader_open.running) {
            SDL_WaitThread(reader_open_thread_handle, NULL);
            reader_open_thread_handle = NULL;
            render_requested = 1;
            if (reader_open.poor_network) {
                poor_network_toast_until = SDL_GetTicks() + 3000;
            }
            reader_state.content_font_size = reader_open.content_font_size;
            if (reader_open.ready &&
                reader_view_adopt_document(body_font, &reader_open.doc,
                                           current_layout.reader_content_w,
                                           current_layout.reader_content_h,
                                           reader_open.honor_saved_position,
                                           &reader_state) == 0) {
                if (!reader_open.honor_saved_position) {
                    if (reader_open.initial_offset > 0) {
                        reader_state.current_page =
                            reader_find_page_for_offset(&reader_state, reader_open.initial_offset);
                    } else {
                        reader_state.current_page = reader_open.initial_page;
                    }
                    reader_clamp_current_page(&reader_state);
                }
                reader_set_source_target(&reader_state, reader_open.source_target);
                reader_save_local_position(ctx, &reader_state);
                shelf_status[0] = '\0';
                status[0] = '\0';
                view = VIEW_READER;
            } else if (reader_open.failed || !reader_open.ready) {
                if (shelf_nuxt) {
                    view = VIEW_SHELF;
                    /* 无法打开所选书籍 */
                    snprintf(shelf_status, sizeof(shelf_status),
                             "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE6\x89\x80\xE9\x80\x89\xE4\xB9\xA6\xE7\xB1\x8D");
                    reader_open_state_reset(&reader_open);
                } else {
                    char retry_target[2048];
                    char retry_book_id[256];
                    int retry_font_size = reader_open.font_size;
                    ui_copy_string(retry_target, sizeof(retry_target), reader_open.source_target);
                    ui_copy_string(retry_book_id, sizeof(retry_book_id), reader_open.book_id);
                    reader_open_state_reset(&reader_open);
                    ui_copy_string(reader_open.source_target, sizeof(reader_open.source_target), retry_target);
                    ui_copy_string(reader_open.book_id, sizeof(reader_open.book_id), retry_book_id);
                    reader_open.font_size = retry_font_size;
                    view = VIEW_OPENING;
                    /* 打开失败，按 A 重试 */
                    snprintf(status, sizeof(status),
                             "\xE6\x89\x93\xE5\xBC\x80\xE5\xA4\xB1\xE8\xB4\xA5\xEF\xBC\x8C\xE6\x8C\x89 A \xE9\x87\x8D\xE8\xAF\x95");
                }
            } else {
                reader_open_state_reset(&reader_open);
            }
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
        if (frame_view_before_updates != view ||
            (view == VIEW_READER && catalog_open_before_updates != reader_state.catalog_open)) {
            motion_state.view_fade_active = 1;
            motion_state.view_fade_start_tick = SDL_GetTicks();
            motion_state.view_fade_duration_ms = UI_VIEW_FADE_DURATION_MS;
        }

        if (view == VIEW_LOGIN && login_start.running == 0 && login_thread) {
            SDL_WaitThread(login_thread, NULL);
            login_thread = NULL;
            render_requested = 1;
            if (login_start.success) {
                session = login_start.session;
                /* 二维码已生成，等待扫码确认... */
                snprintf(status, sizeof(status), "\xE4\xBA\x8C\xE7\xBB\xB4\xE7\xA0\x81\xE5\xB7\xB2\xE7\x94\x9F\xE6\x88\x90\xEF\xBC\x8C\xE7\xAD\x89\xE5\xBE\x85\xE6\x89\xAB\xE7\xA0\x81\xE7\xA1\xAE\xE8\xAE\xA4...");
                login_active = 1;
                last_poll = SDL_GetTicks();
                memset(&login_poll, 0, sizeof(login_poll));
                snprintf(login_poll.data_dir, sizeof(login_poll.data_dir), "%s", ctx->data_dir);
                snprintf(login_poll.ca_file, sizeof(login_poll.ca_file), "%s", ctx->ca_file);
                login_poll.session = session;
                login_poll.running = 1;
                login_poll_thread_handle = SDL_CreateThread(login_poll_thread, "weread-login-poll", &login_poll);
                if (!login_poll_thread_handle) {
                    login_poll.running = 0;
                    login_active = 0;
                    /* 无法创建登录轮询线程 */
                    snprintf(status, sizeof(status), "\xE6\x97\xA0\xE6\xB3\x95\xE5\x88\x9B\xE5\xBB\xBA\xE7\x99\xBB\xE5\xBD\x95\xE8\xBD\xAE\xE8\xAF\xA2\xE7\xBA\xBF\xE7\xA8\x8B");
                }
            } else if (login_start.failed) {
                /* 二维码生成失败 */
                snprintf(status, sizeof(status), "\xE4\xBA\x8C\xE7\xBB\xB4\xE7\xA0\x81\xE7\x94\x9F\xE6\x88\x90\xE5\xA4\xB1\xE8\xB4\xA5");
            }
        }

        if (view == VIEW_LOGIN && login_active) {
            if (login_poll.running) {
                if (SDL_GetTicks() - last_poll > 1200) {
                    render_requested = 1;
                    if (login_poll.last_status == AUTH_POLL_SCANNED) {
                        /* 已扫码，等待确认... */
                        snprintf(status, sizeof(status), "\xE5\xB7\xB2\xE6\x89\xAB\xE7\xA0\x81\xEF\xBC\x8C\xE7\xAD\x89\xE5\xBE\x85\xE7\xA1\xAE\xE8\xAE\xA4...");
                    } else {
                        /* 等待扫码或确认... */
                        snprintf(status, sizeof(status), "\xE7\xAD\x89\xE5\xBE\x85\xE6\x89\xAB\xE7\xA0\x81\xE6\x88\x96\xE7\xA1\xAE\xE8\xAE\xA4...");
                    }
                    last_poll = SDL_GetTicks();
                }
            } else if (login_poll_thread_handle) {
                SDL_WaitThread(login_poll_thread_handle, NULL);
                login_poll_thread_handle = NULL;
                render_requested = 1;
                if (login_poll.completed) {
                    cJSON_Delete(shelf_nuxt);
                    shelf_nuxt = shelf_load(ctx, 1, NULL);
                    shelf_cover_download_stop(&shelf_cover_download, &shelf_cover_download_thread_handle);
                    shelf_cover_cache_build(ctx, shelf_nuxt, &shelf_covers);
                    selected = 0;
                    shelf_start = 0;
                    shelf_status[0] = '\0';
                    status[0] = '\0';
                    login_active = 0;
                    view = VIEW_SHELF;
                } else {
                    /* 登录等待已停止 */
                    snprintf(status, sizeof(status), "\xE7\x99\xBB\xE5\xBD\x95\xE7\xAD\x89\xE5\xBE\x85\xE5\xB7\xB2\xE5\x81\x9C\xE6\xAD\xA2");
                    login_active = 0;
                }
            }
        }

        {
            Uint32 now = SDL_GetTicks();
            int toast_visible = poor_network_toast_until > now;
            int catalog_animating = motion_state.catalog_animating_active;
            int should_render = 1;

            if (view == VIEW_READER &&
                !reader_input_seen &&
                !render_requested &&
                !motion_state.view_fade_active &&
                !catalog_animating &&
                !toast_visible) {
                should_render = 0;
            }

            if (should_render) {
                SDL_SetRenderTarget(renderer, scene_texture);

                if (view == VIEW_LOGIN) {
                    render_login(renderer, title_font, body_font, &session, status,
                                 battery_state.text, &current_layout);
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
                    chapter_prefetch_cache_has_running_work(&chapter_prefetch_cache);
                int reader_idle = view == VIEW_READER &&
                                  !reader_input_seen &&
                                  !render_requested;
                int wait_timeout_ms;
                SDL_Event waited_event;

                if (reader_idle) {
                    wait_timeout_ms = background_busy ? 50 : 250;
                } else if (!motion_state.view_fade_active &&
                           poor_network_toast_until <= now) {
                    wait_timeout_ms = background_busy ? 33 : 100;
                } else {
                    wait_timeout_ms = 0;
                }

                if (wait_timeout_ms > 0 && ms_until_next_minute > 0 &&
                    ms_until_next_minute < wait_timeout_ms) {
                    wait_timeout_ms = ms_until_next_minute;
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
    if (view == VIEW_READER) {
        reader_progress_flush_blocking(ctx, &reader_state, 1);
        reader_save_local_position(ctx, &reader_state);
    }
    if (login_thread) {
        SDL_WaitThread(login_thread, NULL);
    }
    if (startup_thread_handle) {
        SDL_WaitThread(startup_thread_handle, NULL);
    }
    if (reader_open_thread_handle) {
        SDL_WaitThread(reader_open_thread_handle, NULL);
    }
    if (shelf_cover_download_thread_handle) {
        SDL_WaitThread(shelf_cover_download_thread_handle, NULL);
    }
    if (login_poll_thread_handle) {
        login_poll.stop = 1;
        SDL_WaitThread(login_poll_thread_handle, NULL);
    }
    if (progress_report_thread_handle) {
        SDL_WaitThread(progress_report_thread_handle, NULL);
    }
    chapter_prefetch_cache_reset(&chapter_prefetch_cache);
    progress_report_state_reset(&progress_report);
    reader_view_free(&reader_state);
    reader_open_state_reset(&reader_open);
    startup_state_reset(&startup_state);
    shelf_cover_cache_reset(&shelf_covers);
    cJSON_Delete(shelf_nuxt);
    ui_platform_shutdown_haptics(&haptic_state);
    if (body_font) {
        TTF_CloseFont(body_font);
    }
    if (title_font) {
        TTF_CloseFont(title_font);
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
