#include "ui.h"

#if HAVE_SDL

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "auth.h"
#include "json.h"
#include "reader.h"
#include "shelf.h"
#include "state.h"

typedef enum {
    VIEW_SHELF = 0,
    VIEW_LOGIN = 1,
    VIEW_READER = 2
} UiView;

typedef struct {
    ReaderDocument doc;
    char source_target[2048];
    char **lines;
    int *line_offsets;
    int line_count;
    int lines_per_page;
    int line_height;
    int current_page;
    int catalog_open;
    int catalog_selected;
    TTF_Font *content_font;
    Uint32 progress_start_tick;
    Uint32 progress_pause_tick;
    Uint32 progress_pause_deadline_tick;
    Uint32 progress_report_due_tick;
    int progress_initialized;
    int progress_paused;
    int progress_initial_report_pending;
    int progress_session_expired;
} ReaderViewState;

typedef struct {
    char data_dir[512];
    char qr_path[1024];
    AuthSession session;
    int running;
    int success;
    int failed;
} LoginStartState;

typedef struct {
    char data_dir[512];
    AuthSession session;
    int running;
    int completed;
    int stop;
    AuthPollStatus last_status;
} LoginPollState;

typedef struct {
    char data_dir[512];
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
} ShelfCoverEntry;

typedef struct {
    ShelfCoverEntry *entries;
    int count;
} ShelfCoverCache;

static int reader_total_pages(ReaderViewState *state);
static int reader_find_page_for_offset(const ReaderViewState *state, int target_offset);

static int ui_join_path_checked(char *dst, size_t dst_size, const char *dir, const char *name) {
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

enum {
    UI_TITLE_FONT_SIZE = 36,
    UI_BODY_FONT_SIZE = 28,
    UI_READER_CONTENT_FONT_SIZE = 28,
    UI_READER_EXTRA_LEADING = 6,
    UI_READER_PARAGRAPH_GAP_LINES = 0,
    UI_READER_CONTENT_WIDTH = 912,
    UI_READER_CONTENT_HEIGHT = 640,
    UI_SHELF_COVER_TEXTURE_KEEP_RADIUS = 4,
    UI_PROGRESS_REPORT_INTERVAL_MS = 30000,
    UI_PROGRESS_PAUSE_TIMEOUT_MS = 120000
};

static void reader_sync_catalog_selection(ReaderViewState *state);
static void reader_open_catalog(ApiContext *ctx, ReaderViewState *state, char *status, size_t status_size);
static int reader_expand_catalog_for_selection(ApiContext *ctx, ReaderViewState *state,
                                               int direction, char *status, size_t status_size);

enum {
    TG5040_JOY_B = 0,
    TG5040_JOY_A = 1,
    TG5040_JOY_Y = 2,
    TG5040_JOY_X = 3,
    TG5040_JOY_L1 = 4,
    TG5040_JOY_R1 = 5,
    TG5040_JOY_SELECT = 6,
    TG5040_JOY_START = 7,
    TG5040_JOY_MENU = 8
};

static int ui_is_tg5040_platform(const char *platform) {
    return platform && strcmp(platform, "tg5040") == 0;
}

static int ui_event_is_keydown(const SDL_Event *event, SDL_Keycode key) {
    return event->type == SDL_KEYDOWN && event->key.keysym.sym == key;
}

static int ui_event_is_tg5040_button_down(const SDL_Event *event, int enabled, Uint8 button) {
    return enabled && event->type == SDL_JOYBUTTONDOWN && event->jbutton.button == button;
}

static int ui_event_is_tg5040_axis(const SDL_Event *event, int enabled, Uint8 axis, int negative) {
    const Sint16 threshold = 16000;

    if (!enabled || event->type != SDL_JOYAXISMOTION || event->jaxis.axis != axis) {
        return 0;
    }
    return negative ? event->jaxis.value <= -threshold : event->jaxis.value >= threshold;
}

static int ui_event_is_up(const SDL_Event *event, int tg5040_input) {
    if (ui_event_is_keydown(event, SDLK_UP) || ui_event_is_keydown(event, SDLK_w)) {
        return 1;
    }
    if (tg5040_input && event->type == SDL_JOYHATMOTION) {
        return (event->jhat.value & SDL_HAT_UP) != 0;
    }
    return ui_event_is_tg5040_axis(event, tg5040_input, 1, 1);
}

static int ui_event_is_down(const SDL_Event *event, int tg5040_input) {
    if (ui_event_is_keydown(event, SDLK_DOWN) || ui_event_is_keydown(event, SDLK_s)) {
        return 1;
    }
    if (tg5040_input && event->type == SDL_JOYHATMOTION) {
        return (event->jhat.value & SDL_HAT_DOWN) != 0;
    }
    return ui_event_is_tg5040_axis(event, tg5040_input, 1, 0);
}

static int ui_event_is_left(const SDL_Event *event, int tg5040_input) {
    if (ui_event_is_keydown(event, SDLK_LEFT) || ui_event_is_keydown(event, SDLK_a)) {
        return 1;
    }
    if (tg5040_input && event->type == SDL_JOYHATMOTION) {
        return (event->jhat.value & SDL_HAT_LEFT) != 0;
    }
    return ui_event_is_tg5040_axis(event, tg5040_input, 0, 1);
}

static int ui_event_is_right(const SDL_Event *event, int tg5040_input) {
    if (ui_event_is_keydown(event, SDLK_RIGHT) || ui_event_is_keydown(event, SDLK_d)) {
        return 1;
    }
    if (tg5040_input && event->type == SDL_JOYHATMOTION) {
        return (event->jhat.value & SDL_HAT_RIGHT) != 0;
    }
    return ui_event_is_tg5040_axis(event, tg5040_input, 0, 0);
}

static int ui_event_is_back(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_keydown(event, SDLK_ESCAPE) ||
           ui_event_is_keydown(event, SDLK_b) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_B) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_MENU);
}

static int ui_event_is_confirm(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_keydown(event, SDLK_RETURN) ||
           ui_event_is_keydown(event, SDLK_SPACE) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_A);
}

static int ui_event_is_shelf_resume(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_keydown(event, SDLK_r) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_X) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_SELECT);
}

static int ui_event_is_catalog_toggle(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_keydown(event, SDLK_c) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_X) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_SELECT);
}

static int ui_event_is_chapter_prev(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_up(event, tg5040_input) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_L1);
}

static int ui_event_is_chapter_next(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_down(event, tg5040_input) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_R1);
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

static void draw_text_clipped(SDL_Renderer *renderer, TTF_Font *font, const SDL_Rect *clip,
                              int x, int y, SDL_Color color, const char *text) {
    SDL_Surface *surface;
    SDL_Texture *texture;
    SDL_Rect dst;
    SDL_Rect previous_clip;
    SDL_bool had_clip = SDL_RenderIsClipEnabled(renderer);

    if (!renderer || !font || !clip || !text || !*text || clip->w <= 0 || clip->h <= 0) {
        return;
    }

    if (had_clip) {
        SDL_RenderGetClipRect(renderer, &previous_clip);
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

    SDL_RenderSetClipRect(renderer, clip);
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    if (had_clip) {
        SDL_RenderSetClipRect(renderer, &previous_clip);
    } else {
        SDL_RenderSetClipRect(renderer, NULL);
    }

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
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

static int shelf_cover_download(ApiContext *ctx, ShelfCoverEntry *entry) {
    Buffer buf = {0};
    FILE *fp = NULL;
    int rc = -1;

    if (!ctx || !entry || !entry->cover_url || !entry->cache_path[0]) {
        return -1;
    }
    if (api_download(ctx, entry->cover_url, &buf) != 0) {
        goto cleanup;
    }

    fp = fopen(entry->cache_path, "wb");
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
        if (entry->attempted) {
            return -1;
        }
        entry->attempted = 1;
        if (shelf_cover_download(ctx, entry) != 0) {
            return -1;
        }
    }

    if (access(entry->cache_path, F_OK) != 0) {
        return -1;
    }

    surface = IMG_Load(entry->cache_path);
    if (!surface) {
        return -1;
    }
    entry->texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return entry->texture ? 0 : -1;
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

static void draw_qr(SDL_Renderer *renderer, const char *path) {
    SDL_Surface *surface = IMG_Load(path);
    SDL_Texture *texture;
    SDL_Rect dst;
    if (!surface) {
        return;
    }
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }
    dst.w = surface->w * 2;
    dst.h = surface->h * 2;
    dst.x = 512 - dst.w / 2;
    dst.y = 180;
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

static void render_login(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
                         AuthSession *session, const char *status) {
    SDL_Color ink = { 32, 31, 28, 255 };
    SDL_Color line = { 221, 212, 190, 255 };
    SDL_Rect header_band = { 0, 0, 1024, 84 };
    SDL_Rect card = { 274, 128, 476, 520 };
    SDL_Rect qr_slot = { 350, 216, 324, 324 };

    (void)body_font;
    (void)status;

    SDL_SetRenderDrawColor(renderer, 244, 239, 226, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 248, 244, 234, 255);
    SDL_RenderFillRect(renderer, &header_band);
    SDL_SetRenderDrawColor(renderer, 252, 249, 242, 255);
    SDL_RenderFillRect(renderer, &card);
    draw_rect_outline(renderer, &card, line, 1);

    draw_text(renderer, title_font, 78, 30, ink, "WeRead");

    SDL_SetRenderDrawColor(renderer, 245, 240, 229, 255);
    SDL_RenderFillRect(renderer, &qr_slot);
    draw_rect_outline(renderer, &qr_slot, line, 1);

    if (session && session->qr_png_path[0]) {
        draw_qr(renderer, session->qr_png_path);
    }
}

static void render_shelf_cover(SDL_Renderer *renderer, TTF_Font *body_font, SDL_Color ink,
                               SDL_Rect cover_rect, ShelfCoverEntry *entry,
                               const char *title, int selected) {
    const float scale = selected ? 1.16f : 1.0f;
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

    SDL_SetRenderDrawColor(renderer, 201, 191, 166, 180);
    SDL_Rect shadow = {
        scaled_rect.x + 8,
        scaled_rect.y + 10,
        scaled_rect.w,
        scaled_rect.h
    };
    SDL_RenderFillRect(renderer, &shadow);

    if (selected) {
        SDL_SetRenderDrawColor(renderer, 214, 189, 121, 255);
        SDL_RenderFillRect(renderer, &border);
    }

    SDL_SetRenderDrawColor(renderer, 236, 228, 204, 255);
    SDL_RenderFillRect(renderer, &scaled_rect);

    if (entry && entry->texture) {
        SDL_RenderCopy(renderer, entry->texture, NULL, &scaled_rect);
    } else {
        SDL_SetRenderDrawColor(renderer, 222, 212, 188, 255);
        SDL_RenderFillRect(renderer, &scaled_rect);
        draw_text(renderer, body_font, scaled_rect.x + 22, scaled_rect.y + scaled_rect.h / 2 - 10,
                  ink, "No Cover");
    }
}

static void render_shelf(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
                         ApiContext *ctx, cJSON *nuxt, ShelfCoverCache *cover_cache,
                         int selected, int start, const char *status) {
    const int cover_w = 224;
    const int cover_h = 314;
    const int card_gap = 48;
    const int visible_cards = 3;
    const int window_w = 1024;
    const float selected_scale = 1.16f;
    SDL_Color ink = { 28, 28, 24, 255 };
    SDL_Color muted = { 116, 106, 88, 255 };
    SDL_Color line = { 221, 210, 188, 255 };
    cJSON *books = shelf_books(nuxt);
    int count = books && cJSON_IsArray(books) ? cJSON_GetArraySize(books) : 0;
    int end;
    int total_w;
    int start_x;
    int start_y;
    int layout_h;
    cJSON *selected_book = NULL;
    const char *selected_title = NULL;
    time_t now = time(NULL);
    struct tm *local_tm = localtime(&now);
    char time_buf[32] = "";
    char position_buf[32];
    SDL_Rect hero_panel = { 26, 72, 972, 594 };
    SDL_Rect info_panel = { 26, 674, 972, 84 };
    SDL_Rect title_clip;
    int position_width = 0;
    int position_x;
    int title_y;
    (void)status;

    if (start + visible_cards > count) {
        start = count - visible_cards;
    }
    if (start < 0) {
        start = 0;
    }
    end = start + visible_cards;
    if (end > count) {
        end = count;
    }
    total_w = visible_cards * cover_w + (visible_cards - 1) * card_gap;
    start_x = (window_w - total_w) / 2;
    layout_h = (int)(cover_h * selected_scale);
    start_y = hero_panel.y + (hero_panel.h - layout_h) / 2;

    SDL_SetRenderDrawColor(renderer, 246, 242, 230, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 248, 244, 234, 255);
    SDL_Rect header_band = { 0, 0, window_w, 72 };
    SDL_RenderFillRect(renderer, &header_band);
    SDL_SetRenderDrawColor(renderer, 241, 236, 223, 255);
    SDL_RenderFillRect(renderer, &hero_panel);
    SDL_SetRenderDrawColor(renderer, 252, 248, 239, 255);
    SDL_RenderFillRect(renderer, &info_panel);
    draw_rect_outline(renderer, &hero_panel, line, 1);
    draw_rect_outline(renderer, &info_panel, line, 1);

    draw_text(renderer, title_font, 30, 18, ink, "WeRead");
    if (local_tm) {
        strftime(time_buf, sizeof(time_buf), "%H:%M", local_tm);
    }
    draw_text(renderer, body_font, 924, 24, muted, time_buf[0] ? time_buf : "--:--");

    if (count == 0) {
        int empty_w = 0;
        TTF_SizeUTF8(title_font, "Shelf Empty", &empty_w, NULL);
        draw_text(renderer, title_font, 512 - empty_w / 2, 316, ink, "Shelf Empty");
        return;
    }

    shelf_cover_cache_trim(cover_cache, selected, UI_SHELF_COVER_TEXTURE_KEEP_RADIUS);
    selected_book = cJSON_GetArrayItem(books, selected);
    selected_title = json_get_string(selected_book, "title");

    for (int i = start; i < end; i++) {
        cJSON *book = cJSON_GetArrayItem(books, i);
        const char *title = json_get_string(book, "title");
        int card_index = i - start;
        SDL_Rect cover_rect = {
            start_x + card_index * (cover_w + card_gap),
            start_y,
            cover_w,
            cover_h
        };

        if (cover_cache && i < cover_cache->count) {
            shelf_cover_prepare(ctx, renderer, &cover_cache->entries[i]);
        }
        render_shelf_cover(renderer, body_font, ink, cover_rect,
                           cover_cache && i < cover_cache->count ? &cover_cache->entries[i] : NULL,
                           title, i == selected);
    }

    if (start > 0) {
        draw_text(renderer, title_font, 36, 320, ink, "<");
    }
    if (end < count) {
        draw_text(renderer, title_font, 956, 320, ink, ">");
    }

    snprintf(position_buf, sizeof(position_buf), "%d / %d", selected + 1, count);
    TTF_SizeUTF8(body_font, position_buf, &position_width, NULL);
    position_x = info_panel.x + info_panel.w - position_width - 22;
    title_y = info_panel.y + (info_panel.h - (title_font ? TTF_FontHeight(title_font) : 40)) / 2;
    title_clip.x = info_panel.x + 22;
    title_clip.y = title_y;
    title_clip.w = position_x - title_clip.x - 28;
    title_clip.h = title_font ? TTF_FontHeight(title_font) + 8 : 40;
    draw_text_clipped(renderer, title_font, &title_clip, title_clip.x, title_y,
                      ink, selected_title ? selected_title : "(untitled)");
    draw_text(renderer, body_font, position_x, info_panel.y + 26, muted, position_buf);
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

static int utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static int is_forbidden_line_start_punct(const char *text) {
    static const char *tokens[] = {
        ",", ".", "!", "?", ":", ";", ")", "]", "}", "%",
        "\xE3\x80\x81", /* 、 */
        "\xE3\x80\x82", /* 。 */
        "\xEF\xBC\x8C", /* ， */
        "\xEF\xBC\x8E", /* ． */
        "\xEF\xBC\x81", /* ！ */
        "\xEF\xBC\x9F", /* ？ */
        "\xEF\xBC\x9A", /* ： */
        "\xEF\xBC\x9B", /* ； */
        "\xEF\xBC\x89", /* ） */
        "\xE3\x80\x91", /* 】 */
        "\xE3\x80\x8D", /* 」 */
        "\xE3\x80\x8F", /* 』 */
        "\xE2\x80\x99", /* ’ */
        "\xE2\x80\x9D", /* ” */
        "\xE3\x80\x8B", /* 》 */
        "\xE3\x80\x89", /* 〉 */
        "\xE3\x80\xBE", /* 〾/fallback uncommon but safe */
        NULL
    };

    if (!text || !*text) {
        return 0;
    }
    for (int i = 0; tokens[i]; i++) {
        size_t len = strlen(tokens[i]);
        if (strncmp(text, tokens[i], len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int append_line(ReaderViewState *state, const char *text, size_t len, int start_offset) {
    char **tmp;
    int *offsets_tmp;
    char *line;

    tmp = realloc(state->lines, sizeof(char *) * (state->line_count + 1));
    if (!tmp) {
        return -1;
    }
    state->lines = tmp;
    offsets_tmp = realloc(state->line_offsets, sizeof(int) * (state->line_count + 1));
    if (!offsets_tmp) {
        return -1;
    }
    state->line_offsets = offsets_tmp;
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
    int char_offset = 0;
    int line_start_offset = 0;
    while (*p) {
        int ch_len = utf8_char_len((unsigned char)*p);
        int next_width = 0;
        char saved[8] = {0};

        if (*p == '\n') {
            if (append_line(state, line_start, (size_t)(p - line_start), line_start_offset) != 0) {
                return -1;
            }
            if (append_blank_lines(state, UI_READER_PARAGRAPH_GAP_LINES) != 0) {
                return -1;
            }
            p++;
            char_offset++;
            line_start = p;
            line_start_offset = char_offset;
            last_break = NULL;
            continue;
        }

        memcpy(saved, p, (size_t)ch_len);
        saved[ch_len] = '\0';
        if (isspace((unsigned char)*p)) {
            last_break = p;
        }

        {
            size_t len = (size_t)((p + ch_len) - line_start);
            char *candidate = malloc(len + 1);
            if (!candidate) {
                return -1;
            }
            memcpy(candidate, line_start, len);
            candidate[len] = '\0';
            TTF_SizeUTF8(font, candidate, &next_width, NULL);
            free(candidate);
        }

        if (next_width > max_width && p > line_start) {
            const char *break_at = last_break && last_break > line_start ? last_break : p;
            const char *next_start;
            while (break_at > line_start && isspace((unsigned char)break_at[-1])) {
                break_at--;
            }
            next_start = last_break && last_break > line_start ? last_break + 1 : p;
            while (*next_start && isspace((unsigned char)*next_start) && *next_start != '\n') {
                next_start++;
            }
            if (is_forbidden_line_start_punct(next_start)) {
                break_at = next_start + utf8_char_len((unsigned char)*next_start);
                next_start = break_at;
            }
            if (append_line(state, line_start, (size_t)(break_at - line_start), line_start_offset) != 0) {
                return -1;
            }
            p = next_start;
            while (*p && isspace((unsigned char)*p) && *p != '\n') {
                p++;
                char_offset++;
            }
            line_start = p;
            line_start_offset = char_offset;
            last_break = NULL;
            continue;
        }

        p += ch_len;
        char_offset++;
    }

    if (p > line_start) {
        if (append_line(state, line_start, (size_t)(p - line_start), line_start_offset) != 0) {
            return -1;
        }
    }
    return 0;
}

static int reader_view_load(ApiContext *ctx, TTF_Font *font, const char *target, int font_size,
                            int content_width, int content_height, int honor_saved_position,
                            ReaderViewState *state) {
    int line_skip;
    TTF_Font *render_font = font;

    reader_view_free(state);
    if (reader_load(ctx, target, font_size, &state->doc) != 0) {
        return -1;
    }
    if (state->doc.use_content_font && state->doc.content_font_path[0]) {
        state->content_font = TTF_OpenFont(state->doc.content_font_path, UI_READER_CONTENT_FONT_SIZE);
        if (state->content_font) {
            render_font = state->content_font;
        }
    }
    if (wrap_paragraph(render_font, state->doc.content_text, content_width, state) != 0) {
        reader_view_free(state);
        return -1;
    }

    line_skip = TTF_FontLineSkip(render_font);
    state->line_height = line_skip > 0 ? line_skip + UI_READER_EXTRA_LEADING :
                                          UI_READER_CONTENT_FONT_SIZE + UI_READER_EXTRA_LEADING;
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
            if (start_line >= state->line_count) {
                return total_pages - 1;
            }
            if (state->line_offsets[start_line] >= target_offset) {
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
    (void)ctx;
    (void)status;
    (void)status_size;
    if (!state || state->doc.catalog_count <= 0) {
        return;
    }

    reader_sync_catalog_selection(state);
    state->catalog_open = 1;
}

static int reader_expand_catalog_for_selection(ApiContext *ctx, ReaderViewState *state,
                                               int direction, char *status, size_t status_size) {
    int added_count = 0;

    if (!ctx || !state || !state->doc.catalog_items || state->doc.catalog_count <= 0) {
        return 0;
    }
    if (reader_expand_catalog(ctx, &state->doc, direction, &added_count) != 0) {
        if (status && status_size > 0) {
            snprintf(status, status_size, "Failed to load more catalog chapters.");
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
    if (!ctx || !state || !state->doc.book_id || !state->doc.target || !state->source_target[0]) {
        return;
    }
    reader_clamp_current_page(state);
    state_save_reader_position(ctx, state->doc.book_id, state->source_target, state->doc.target,
                               state->doc.font_size, state->current_page);
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
                          ReaderViewState *state) {
    static const int margin = 56;
    static const int footer_y = 708;
    SDL_Color ink = { 30, 29, 26, 255 };
    SDL_Color muted = { 118, 108, 90, 255 };
    SDL_Color line = { 223, 214, 194, 255 };
    TTF_Font *content_font = state->content_font ? state->content_font : body_font;
    int start_line = state->current_page * state->lines_per_page;
    int end_line = start_line + state->lines_per_page;
    int line_h = state->line_height > 0 ? state->line_height : TTF_FontLineSkip(content_font);
    int y = 84;
    int total_pages = state->line_count > 0 ?
        (state->line_count + state->lines_per_page - 1) / state->lines_per_page : 1;
    time_t now = time(NULL);
    struct tm *local_tm = localtime(&now);
    char time_buf[32];
    char footer[256];
    char title_buf[256];
    SDL_Rect header_band = { 0, 0, 1024, 68 };
    SDL_Rect footer_line = { margin, 690, 1024 - margin * 2, 1 };
    char chapter_heading[256];

    SDL_SetRenderDrawColor(renderer, 249, 246, 237, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 246, 241, 230, 255);
    SDL_RenderFillRect(renderer, &header_band);
    SDL_SetRenderDrawColor(renderer, line.r, line.g, line.b, line.a);
    SDL_RenderFillRect(renderer, &footer_line);

    reader_format_chapter_heading(state, chapter_heading, sizeof(chapter_heading));
    fit_text_ellipsis(body_font,
                      chapter_heading[0] ? chapter_heading :
                      (state->doc.book_title ? state->doc.book_title : ""),
                      1024 - 2 * margin - 140,
                      title_buf, sizeof(title_buf));
    draw_text(renderer, body_font, margin, 22, muted, title_buf);

    if (end_line > state->line_count) {
        end_line = state->line_count;
    }
    for (int i = start_line; i < end_line; i++) {
        if (y + line_h > footer_y) {
            break;
        }
        draw_text(renderer, content_font, margin, y, ink, state->lines[i]);
        y += line_h;
    }

    if (local_tm) {
        strftime(time_buf, sizeof(time_buf), "%H:%M", local_tm);
        draw_text(renderer, body_font, margin, footer_y, muted, time_buf);
    }
    snprintf(footer, sizeof(footer), "%d/%d", state->current_page + 1, total_pages);
    {
        int fw = 0, fh = 0;
        TTF_SizeUTF8(body_font, footer, &fw, &fh);
        draw_text(renderer, body_font, 1024 - margin - fw, footer_y, muted, footer);
    }
}

static void render_catalog_overlay(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
                                   ReaderViewState *state) {
    SDL_Color ink = { 28, 28, 24, 255 };
    SDL_Color dim = { 118, 108, 92, 255 };
    SDL_Color line = { 223, 214, 194, 255 };
    SDL_Rect backdrop = { 0, 0, 1024, 768 };
    SDL_Rect panel = { 230, 24, 760, 720 };
    SDL_Rect header = { 230, 24, 760, 92 };
    int line_height = body_font ? TTF_FontLineSkip(body_font) + 10 : 38;
    int list_top = 132;
    int visible = line_height > 0 ? 15 : 15;
    int start = state->catalog_selected - visible / 2;
    int end;
    char title_buf[256];

    if (!state || !state->doc.catalog_items || state->doc.catalog_count <= 0) {
        return;
    }
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

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 18, 16, 12, 108);
    SDL_RenderFillRect(renderer, &backdrop);
    SDL_SetRenderDrawColor(renderer, 250, 246, 237, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 244, 239, 228, 255);
    SDL_RenderFillRect(renderer, &header);
    draw_rect_outline(renderer, &panel, line, 1);

    fit_text_ellipsis(title_font, state->doc.book_title ? state->doc.book_title : "目录",
                      560, title_buf, sizeof(title_buf));
    draw_text(renderer, title_font, 262, 52, ink, title_buf);

    for (int i = start; i < end; i++) {
        ReaderCatalogItem *item = &state->doc.catalog_items[i];
        SDL_Rect row = { 254, list_top + (i - start) * line_height, 712, line_height - 4 };
        char row_buf[256];
        int indent = item->level > 1 ? (item->level - 1) * 20 : 0;
        SDL_Color color = item->is_lock ? dim : ink;

        if (i == state->catalog_selected) {
            SDL_SetRenderDrawColor(renderer, 228, 216, 187, 255);
            SDL_RenderFillRect(renderer, &row);
        } else if (reader_is_catalog_item_current(state, item)) {
            SDL_SetRenderDrawColor(renderer, 242, 236, 223, 255);
            SDL_RenderFillRect(renderer, &row);
        }

        fit_text_ellipsis(body_font, item->title ? item->title : "(untitled)",
                          640 - indent, row_buf, sizeof(row_buf));
        draw_text(renderer, body_font, row.x + 16 + indent, row.y + 6, color, row_buf);
    }
}

static int reader_open_with_saved_position(ApiContext *ctx, TTF_Font *body_font, const char *target,
                                           const char *book_id, int font_size,
                                           ReaderViewState *reader_state) {
    char saved_target[2048];
    char progress_target_buf[2048];
    ReaderViewState saved_state = {0};
    int saved_page = 0;
    int has_local_position = 0;
    int has_cloud_position = 0;
    int replace_with_saved_state = 0;

    if (book_id &&
        state_load_reader_position(ctx, book_id, target, saved_target, sizeof(saved_target),
                                   NULL, &saved_page) == 0) {
        has_local_position = 1;
    }

    if (reader_view_load(ctx, body_font, target, font_size,
                         UI_READER_CONTENT_WIDTH, UI_READER_CONTENT_HEIGHT, 1,
                         reader_state) != 0) {
        return -1;
    }
    reader_set_source_target(reader_state, target);

    {
        /* Always resolve against the server chapter first so startup follows
           the latest cloud progress instead of short-circuiting to stale local
           state from a previous session. */
        const char *progress_target = reader_find_progress_target(reader_state);
        char *fetched_target = NULL;
        if (!progress_target &&
            reader_state->doc.book_id &&
            (reader_state->doc.progress_chapter_idx > 0 ||
             (reader_state->doc.progress_chapter_uid &&
              strcmp(reader_state->doc.progress_chapter_uid, "0") != 0))) {
            /* The progress chapter was not found in the initially loaded catalog
               window (the server returns a non-contiguous catalog that may skip
               the middle of the book).  Do a targeted fetch to find it. */
            fetched_target = reader_find_chapter_target(
                ctx, reader_state->doc.book_id,
                reader_state->doc.progress_chapter_uid,
                reader_state->doc.progress_chapter_idx);
            if (fetched_target) {
                /* Only redirect if this target differs from what is loaded. */
                if (!reader_state->doc.target ||
                    strcmp(fetched_target, reader_state->doc.target) != 0) {
                    progress_target = fetched_target;
                }
            }
        }
        if (progress_target) {
            snprintf(progress_target_buf, sizeof(progress_target_buf), "%s", progress_target);
            if (reader_view_load(ctx, body_font, progress_target_buf, font_size,
                                 UI_READER_CONTENT_WIDTH, UI_READER_CONTENT_HEIGHT, 1,
                                 reader_state) != 0) {
                free(fetched_target);
                return -1;
            }
            reader_set_source_target(reader_state, target);
        }
        free(fetched_target);
    }
    has_cloud_position = reader_has_cloud_position(&reader_state->doc);

    /* When cloud progress is available, keep it authoritative. Local state is
       only used as a fallback when the server does not provide a position. */
    if (!has_cloud_position && has_local_position &&
        reader_state->doc.target && strcmp(saved_target, reader_state->doc.target) == 0) {
        reader_state->current_page = saved_page;
        reader_clamp_current_page(reader_state);
    } else if (!has_cloud_position && has_local_position &&
               reader_view_load(ctx, body_font, saved_target, font_size,
                                UI_READER_CONTENT_WIDTH, UI_READER_CONTENT_HEIGHT, 0,
                                &saved_state) == 0) {
        int same_chapter = 0;
        int saved_is_newer = 0;

        if (saved_state.doc.chapter_uid && reader_state->doc.chapter_uid &&
            strcmp(saved_state.doc.chapter_uid, reader_state->doc.chapter_uid) == 0) {
            same_chapter = 1;
        } else if (saved_state.doc.chapter_idx > 0 &&
                   saved_state.doc.chapter_idx == reader_state->doc.chapter_idx) {
            same_chapter = 1;
        }

        if (saved_state.doc.chapter_idx > 0 &&
            reader_state->doc.chapter_idx > 0 &&
            saved_state.doc.chapter_idx > reader_state->doc.chapter_idx) {
            saved_is_newer = 1;
        } else if (saved_state.doc.chapter_idx > 0 &&
                   reader_state->doc.chapter_idx <= 0) {
            saved_is_newer = 1;
        }

        if (same_chapter) {
            reader_state->current_page = saved_page;
            reader_clamp_current_page(reader_state);
        } else if (saved_is_newer) {
            replace_with_saved_state = 1;
        }
    }

    if (replace_with_saved_state) {
        reader_view_free(reader_state);
        *reader_state = saved_state;
        memset(&saved_state, 0, sizeof(saved_state));
        reader_set_source_target(reader_state, target);
        reader_state->current_page = saved_page;
        reader_clamp_current_page(reader_state);
    }
    reader_sync_catalog_selection(reader_state);
    reader_view_free(&saved_state);

    return 0;
}

static int login_start_thread(void *userdata) {
    LoginStartState *state = (LoginStartState *)userdata;
    ApiContext ctx;

    if (api_init(&ctx, state->data_dir) != 0) {
        state->failed = 1;
        state->running = 0;
        return -1;
    }

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
    snprintf(login_start->qr_path, sizeof(login_start->qr_path), "%s", qr_path);
    login_start->running = 1;
    snprintf(status, status_size, "Generating QR code...");
    *view = VIEW_LOGIN;
    *login_thread = SDL_CreateThread(login_start_thread, "weread-login-start", login_start);
    if (!*login_thread) {
        login_start->running = 0;
        login_start->failed = 1;
        snprintf(status, status_size, "Failed to create login worker thread.");
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
    SDL_Thread *login_thread = NULL;
    SDL_Thread *login_poll_thread_handle = NULL;
    SDL_Thread *progress_report_thread_handle = NULL;
    int login_active = 0;
    char status[256] = "";
    char shelf_status[256] = "";
    char qr_path[1024];
    int tg5040_input = ui_is_tg5040_platform(platform);
    int joystick_count = 0;
    int rc = -1;

    memset(&session, 0, sizeof(session));
    memset(&login_start, 0, sizeof(login_start));
    memset(&login_poll, 0, sizeof(login_poll));
    memset(&progress_report, 0, sizeof(progress_report));
    memset(&shelf_covers, 0, sizeof(shelf_covers));
    memset(&reader_state, 0, sizeof(reader_state));
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
                              1024, 768, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (window && !renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!window || !renderer) {
        fprintf(stderr, "SDL window setup failed: %s\n", SDL_GetError());
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

    {
        int session_ok = auth_check_session(ctx, &shelf_nuxt);
        if (session_ok == 1) {
            status[0] = '\0';
            shelf_status[0] = '\0';
            shelf_cover_cache_build(ctx, shelf_nuxt, &shelf_covers);
        } else {
            view = VIEW_LOGIN;
            snprintf(status, sizeof(status), "Generating QR code...");
            begin_login_flow(ctx, &login_start, &login_thread, &view, status, sizeof(status), qr_path);
        }
    }

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN ||
                       event.type == SDL_JOYBUTTONDOWN ||
                       event.type == SDL_JOYHATMOTION ||
                       event.type == SDL_JOYAXISMOTION) {
                if (ui_event_is_back(&event, tg5040_input)) {
                    if (view == VIEW_READER) {
                        if (reader_state.catalog_open) {
                            reader_state.catalog_open = 0;
                        } else {
                            reader_progress_flush_blocking(ctx, &reader_state, 1);
                            reader_save_local_position(ctx, &reader_state);
                            view = VIEW_SHELF;
                        }
                    } else {
                        running = 0;
                    }
                } else if (view == VIEW_SHELF) {
                    cJSON *books = shelf_nuxt ? shelf_books(shelf_nuxt) : NULL;
                    int count = books && cJSON_IsArray(books) ? cJSON_GetArraySize(books) : 0;
                    if ((ui_event_is_down(&event, tg5040_input) || ui_event_is_right(&event, tg5040_input)) &&
                        count > 0 && selected + 1 < count) {
                        selected++;
                        if (selected >= shelf_start + 3) {
                            shelf_start = selected - 2;
                        }
                    } else if ((ui_event_is_up(&event, tg5040_input) || ui_event_is_left(&event, tg5040_input)) &&
                               selected > 0) {
                        selected--;
                        if (selected < shelf_start) {
                            shelf_start = selected;
                        }
                    } else if (ui_event_is_shelf_resume(&event, tg5040_input)) {
                        char target[2048];
                        char source_target[2048];
                        int font_size = 3;
                        if (state_load_last_reader(ctx, target, sizeof(target), &font_size) == 0) {
                            snprintf(source_target, sizeof(source_target), "%s", target);
                            if (reader_view_load(ctx, body_font, target, font_size,
                                                 UI_READER_CONTENT_WIDTH, UI_READER_CONTENT_HEIGHT, 1,
                                                 &reader_state) == 0) {
                                char saved_source_target[2048];
                                char saved_target[2048];
                                int saved_font_size = font_size;
                                int saved_page = 0;
                                /* Sync to cloud progress if it points to a
                                   different chapter than what was loaded. */
                                {
                                    const char *pt = reader_find_progress_target(&reader_state);
                                    char *ft = NULL;
                                    int has_cloud_position = 0;
                                    if (!pt && reader_state.doc.book_id &&
                                        (reader_state.doc.progress_chapter_idx > 0 ||
                                         (reader_state.doc.progress_chapter_uid &&
                                          strcmp(reader_state.doc.progress_chapter_uid, "0") != 0))) {
                                        ft = reader_find_chapter_target(
                                            ctx, reader_state.doc.book_id,
                                            reader_state.doc.progress_chapter_uid,
                                            reader_state.doc.progress_chapter_idx);
                                        if (ft && (!reader_state.doc.target ||
                                                   strcmp(ft, reader_state.doc.target) != 0)) {
                                            pt = ft;
                                        }
                                    }
                                    if (pt) {
                                        char ptbuf[2048];
                                        snprintf(ptbuf, sizeof(ptbuf), "%s", pt);
                                        reader_view_load(ctx, body_font, ptbuf, font_size,
                                                         UI_READER_CONTENT_WIDTH, UI_READER_CONTENT_HEIGHT,
                                                         1, &reader_state);
                                    }
                                    free(ft);
                                    has_cloud_position = reader_has_cloud_position(&reader_state.doc);
                                    if (reader_state.doc.book_id &&
                                        state_load_reader_position_by_book_id(
                                            ctx, reader_state.doc.book_id,
                                            saved_source_target, sizeof(saved_source_target),
                                            saved_target, sizeof(saved_target),
                                            &saved_font_size, &saved_page) == 0) {
                                        reader_set_source_target(&reader_state, saved_source_target);
                                        if (!has_cloud_position &&
                                            reader_state.doc.target &&
                                            strcmp(saved_target, reader_state.doc.target) == 0) {
                                            reader_state.current_page = saved_page;
                                            reader_clamp_current_page(&reader_state);
                                        }
                                    } else {
                                        reader_set_source_target(&reader_state, source_target);
                                    }
                                }
                                view = VIEW_READER;
                                shelf_status[0] = '\0';
                            } else {
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "Failed to open saved reader target.");
                            }
                        }
                    } else if ((ui_event_is_confirm(&event, tg5040_input) ||
                                ui_event_is_keydown(&event, SDLK_RIGHT) ||
                                ui_event_is_tg5040_button_down(&event, tg5040_input, TG5040_JOY_START)) &&
                               count > 0) {
                        cJSON *book = cJSON_GetArrayItem(books, selected);
                        cJSON *urls = shelf_reader_urls(shelf_nuxt);
                        const char *target = shelf_reader_target(urls, selected);
                        const char *book_id = json_get_string(book, "bookId");
                        if (target && reader_open_with_saved_position(ctx, body_font, target, book_id,
                                                                      3, &reader_state) == 0) {
                            reader_save_local_position(ctx, &reader_state);
                            view = VIEW_READER;
                            shelf_status[0] = '\0';
                        } else if (target) {
                            snprintf(shelf_status, sizeof(shelf_status),
                                     "Failed to open the selected book.");
                        }
                    }
                } else if (view == VIEW_LOGIN) {
                    if ((ui_event_is_confirm(&event, tg5040_input) ||
                         ui_event_is_keydown(&event, SDLK_l) ||
                         ui_event_is_tg5040_button_down(&event, tg5040_input, TG5040_JOY_START)) &&
                        !login_start.running && !login_active) {
                        begin_login_flow(ctx, &login_start, &login_thread, &view,
                                         status, sizeof(status), qr_path);
                    }
                } else if (view == VIEW_READER) {
                    int total_pages = reader_total_pages(&reader_state);
                    reader_progress_note_activity(&reader_state, SDL_GetTicks());
                    if (reader_state.catalog_open) {
                        if (ui_event_is_back(&event, tg5040_input) ||
                            ui_event_is_catalog_toggle(&event, tg5040_input)) {
                            reader_state.catalog_open = 0;
                        } else if (ui_event_is_up(&event, tg5040_input)) {
                            if (reader_state.catalog_selected > 0) {
                                reader_state.catalog_selected--;
                            } else if (reader_expand_catalog_for_selection(ctx, &reader_state, -1,
                                                                          shelf_status, sizeof(shelf_status)) > 0) {
                                reader_state.catalog_selected--;
                            }
                        } else if (ui_event_is_down(&event, tg5040_input)) {
                            if (reader_state.catalog_selected + 1 < reader_state.doc.catalog_count) {
                                reader_state.catalog_selected++;
                            } else if (reader_expand_catalog_for_selection(ctx, &reader_state, 1,
                                                                          shelf_status, sizeof(shelf_status)) > 0 &&
                                       reader_state.catalog_selected + 1 < reader_state.doc.catalog_count) {
                                reader_state.catalog_selected++;
                            }
                        } else if (ui_event_is_left(&event, tg5040_input)) {
                            if (reader_state.catalog_selected > 0) {
                                reader_state.catalog_selected -= 10;
                                if (reader_state.catalog_selected < 0) {
                                    reader_state.catalog_selected = 0;
                                }
                            } else if (reader_expand_catalog_for_selection(ctx, &reader_state, -1,
                                                                          shelf_status, sizeof(shelf_status)) > 0) {
                                reader_state.catalog_selected -= 10;
                                if (reader_state.catalog_selected < 0) {
                                    reader_state.catalog_selected = 0;
                                }
                            }
                        } else if (ui_event_is_right(&event, tg5040_input)) {
                            if (reader_state.catalog_selected + 1 < reader_state.doc.catalog_count) {
                                reader_state.catalog_selected += 10;
                                if (reader_state.catalog_selected >= reader_state.doc.catalog_count) {
                                    reader_state.catalog_selected = reader_state.doc.catalog_count - 1;
                                }
                            } else if (reader_expand_catalog_for_selection(ctx, &reader_state, 1,
                                                                          shelf_status, sizeof(shelf_status)) > 0) {
                                reader_state.catalog_selected += 10;
                                if (reader_state.catalog_selected >= reader_state.doc.catalog_count) {
                                    reader_state.catalog_selected = reader_state.doc.catalog_count - 1;
                                }
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
                                                     UI_READER_CONTENT_WIDTH, UI_READER_CONTENT_HEIGHT, 0,
                                                     &reader_state) == 0) {
                                    reader_set_source_target(&reader_state, source_target);
                                    reader_save_local_position(ctx, &reader_state);
                                    reader_state.catalog_open = 0;
                                    shelf_status[0] = '\0';
                                } else {
                                    snprintf(shelf_status, sizeof(shelf_status),
                                             "Failed to open the selected chapter.");
                                }
                            }
                        }
                    } else if (ui_event_is_catalog_toggle(&event, tg5040_input) &&
                               reader_state.doc.catalog_count > 0) {
                        reader_open_catalog(ctx, &reader_state, shelf_status, sizeof(shelf_status));
                    } else if ((ui_event_is_right(&event, tg5040_input) || ui_event_is_confirm(&event, tg5040_input)) &&
                        reader_state.current_page + 1 < total_pages) {
                        reader_state.current_page++;
                        reader_save_local_position(ctx, &reader_state);
                    } else if ((ui_event_is_right(&event, tg5040_input) || ui_event_is_confirm(&event, tg5040_input)) &&
                               reader_state.doc.next_target && reader_state.doc.next_target[0]) {
                        char *target = strdup(reader_state.doc.next_target);
                        char source_target[2048];
                        int font_size = reader_state.doc.font_size;
                        if (target) {
                            snprintf(source_target, sizeof(source_target), "%s", reader_state.source_target);
                            reader_progress_flush_blocking(ctx, &reader_state, 1);
                            if (reader_view_load(ctx, body_font, target, font_size,
                                                 UI_READER_CONTENT_WIDTH, UI_READER_CONTENT_HEIGHT, 0,
                                                 &reader_state) == 0) {
                                    reader_set_source_target(&reader_state, source_target);
                                reader_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                            } else {
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "Failed to open the next chapter.");
                            }
                            free(target);
                        }
                    } else if (ui_event_is_left(&event, tg5040_input) && reader_state.current_page > 0) {
                        reader_state.current_page--;
                        reader_save_local_position(ctx, &reader_state);
                    } else if (ui_event_is_left(&event, tg5040_input) &&
                               reader_state.doc.prev_target && reader_state.doc.prev_target[0]) {
                        char *target = strdup(reader_state.doc.prev_target);
                        char source_target[2048];
                        int font_size = reader_state.doc.font_size;
                        if (target) {
                            snprintf(source_target, sizeof(source_target), "%s", reader_state.source_target);
                            reader_progress_flush_blocking(ctx, &reader_state, 1);
                            if (reader_view_load(ctx, body_font, target, font_size,
                                                 UI_READER_CONTENT_WIDTH, UI_READER_CONTENT_HEIGHT, 0,
                                                 &reader_state) == 0) {
                                    reader_set_source_target(&reader_state, source_target);
                                int new_total_pages = reader_total_pages(&reader_state);
                                reader_state.current_page = new_total_pages > 0 ? new_total_pages - 1 : 0;
                                reader_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                            } else {
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "Failed to open the previous chapter.");
                            }
                            free(target);
                        }
                    } else if (ui_event_is_chapter_prev(&event, tg5040_input) &&
                               reader_state.doc.prev_target && reader_state.doc.prev_target[0]) {
                        char *target = strdup(reader_state.doc.prev_target);
                        char source_target[2048];
                        int font_size = reader_state.doc.font_size;
                        if (target) {
                            snprintf(source_target, sizeof(source_target), "%s", reader_state.source_target);
                            reader_progress_flush_blocking(ctx, &reader_state, 1);
                            if (reader_view_load(ctx, body_font, target, font_size,
                                                 UI_READER_CONTENT_WIDTH, UI_READER_CONTENT_HEIGHT, 0,
                                                 &reader_state) == 0) {
                                    reader_set_source_target(&reader_state, source_target);
                                reader_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                            } else {
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "Failed to open the previous chapter.");
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
                            if (reader_view_load(ctx, body_font, target, font_size,
                                                 UI_READER_CONTENT_WIDTH, UI_READER_CONTENT_HEIGHT, 0,
                                                 &reader_state) == 0) {
                                    reader_set_source_target(&reader_state, source_target);
                                reader_save_local_position(ctx, &reader_state);
                                shelf_status[0] = '\0';
                            } else {
                                snprintf(shelf_status, sizeof(shelf_status),
                                         "Failed to open the next chapter.");
                            }
                            free(target);
                        }
                    } else if (ui_event_is_tg5040_button_down(&event, tg5040_input, TG5040_JOY_START)) {
                        if (reader_state.doc.catalog_count > 0) {
                            reader_open_catalog(ctx, &reader_state, shelf_status, sizeof(shelf_status));
                        }
                    } else if (ui_event_is_keydown(&event, SDLK_b)) {
                        reader_save_local_position(ctx, &reader_state);
                        view = VIEW_SHELF;
                    }
                }
            }
        }

        if (view == VIEW_READER) {
            reader_progress_update(ctx, &reader_state,
                                   &progress_report, &progress_report_thread_handle);
        }

        if (view == VIEW_LOGIN && login_start.running == 0 && login_thread) {
            SDL_WaitThread(login_thread, NULL);
            login_thread = NULL;
            if (login_start.success) {
                session = login_start.session;
                snprintf(status, sizeof(status), "QR ready. Waiting for scan confirmation...");
                login_active = 1;
                last_poll = SDL_GetTicks();
                memset(&login_poll, 0, sizeof(login_poll));
                snprintf(login_poll.data_dir, sizeof(login_poll.data_dir), "%s", ctx->data_dir);
                login_poll.session = session;
                login_poll.running = 1;
                login_poll_thread_handle = SDL_CreateThread(login_poll_thread, "weread-login-poll", &login_poll);
                if (!login_poll_thread_handle) {
                    login_poll.running = 0;
                    login_active = 0;
                    snprintf(status, sizeof(status), "Failed to create login poll thread.");
                }
            } else if (login_start.failed) {
                snprintf(status, sizeof(status), "Failed to generate QR code.");
            }
        }

        if (view == VIEW_LOGIN && login_active) {
            if (login_poll.running) {
                if (SDL_GetTicks() - last_poll > 1200) {
                    if (login_poll.last_status == AUTH_POLL_SCANNED) {
                        snprintf(status, sizeof(status), "QR scanned. Waiting for final confirmation...");
                    } else {
                        snprintf(status, sizeof(status), "Waiting for QR scan or confirmation...");
                    }
                    last_poll = SDL_GetTicks();
                }
            } else if (login_poll_thread_handle) {
                SDL_WaitThread(login_poll_thread_handle, NULL);
                login_poll_thread_handle = NULL;
                if (login_poll.completed) {
                    cJSON_Delete(shelf_nuxt);
                    shelf_nuxt = shelf_load(ctx, 0, NULL);
                    shelf_cover_cache_build(ctx, shelf_nuxt, &shelf_covers);
                    selected = 0;
                    shelf_start = 0;
                    shelf_status[0] = '\0';
                    status[0] = '\0';
                    login_active = 0;
                    view = VIEW_SHELF;
                } else {
                    snprintf(status, sizeof(status), "Login wait stopped.");
                    login_active = 0;
                }
            }
        }

        if (view == VIEW_LOGIN) {
            render_login(renderer, title_font, body_font, &session, status);
        } else if (view == VIEW_READER) {
            render_reader(renderer, title_font, body_font, &reader_state);
            if (reader_state.catalog_open) {
                render_catalog_overlay(renderer, title_font, body_font, &reader_state);
            }
        } else {
            render_shelf(renderer, title_font, body_font, ctx, shelf_nuxt, &shelf_covers,
                         selected, shelf_start, shelf_status);
        }
        SDL_RenderPresent(renderer);
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
    if (login_poll_thread_handle) {
        login_poll.stop = 1;
        SDL_WaitThread(login_poll_thread_handle, NULL);
    }
    if (progress_report_thread_handle) {
        SDL_WaitThread(progress_report_thread_handle, NULL);
    }
    progress_report_state_reset(&progress_report);
    reader_view_free(&reader_state);
    shelf_cover_cache_reset(&shelf_covers);
    cJSON_Delete(shelf_nuxt);
    if (body_font) {
        TTF_CloseFont(body_font);
    }
    if (title_font) {
        TTF_CloseFont(title_font);
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
