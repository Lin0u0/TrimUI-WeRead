#include "ui_render_internal.h"

#if HAVE_SDL

#include <math.h>
#include <stdio.h>
#include <time.h>
#include "json.h"
#include "shelf.h"

static int shelf_ui_normal_count(cJSON *nuxt) {
    return shelf_normal_book_count(nuxt);
}

static int shelf_ui_article_count(cJSON *nuxt) {
    return shelf_article_count(nuxt);
}

int shelf_ui_default_selection(cJSON *nuxt) {
    (void)nuxt;
    return 0;
}

int shelf_ui_clamp_selection(cJSON *nuxt, int selected) {
    int article_count = shelf_ui_article_count(nuxt);
    int book_count = shelf_ui_normal_count(nuxt);
    int min_selected = article_count > 0 ? -article_count : 0;
    int max_selected = book_count - 1;

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

static cJSON *shelf_ui_selected_entry(cJSON *nuxt, int selected, int *source_index_out) {
    if (selected < 0) {
        int article_index = -selected - 1;

        return shelf_article_at(nuxt, article_index, source_index_out);
    }
    return shelf_normal_book_at(nuxt, selected, source_index_out);
}

int shelf_ui_cover_cache_index_with_counts(int article_count, int book_count, int selected) {
    if (selected < 0) {
        if (selected < -article_count) {
            return -1;
        }
        return -selected - 1;
    }
    if (selected >= book_count) {
        return -1;
    }
    return article_count + selected;
}

int shelf_ui_cover_cache_index(cJSON *nuxt, int selected) {
    return shelf_ui_cover_cache_index_with_counts(
        shelf_ui_article_count(nuxt), shelf_ui_normal_count(nuxt), selected);
}

static cJSON *shelf_ui_selected_book(cJSON *nuxt, int selected) {
    return shelf_ui_selected_entry(nuxt, selected, NULL);
}

static void shelf_cover_cache_trim(ShelfCoverCache *cache, int selected, float selected_pos,
                                   int article_count, int book_count, int keep_radius) {
    int visible_start = (int)floorf(selected_pos) - 3;
    int visible_end = (int)ceilf(selected_pos) + 3;

    if (!cache) {
        return;
    }
    if (cache->last_trim_selected == selected &&
        cache->last_trim_visible_start == visible_start &&
        cache->last_trim_visible_end == visible_end &&
        cache->last_trim_keep_radius == keep_radius) {
        return;
    }

    if (cache->entries) {
        for (int i = 0; i < cache->count; i++) {
            int keep = 0;

            for (int v = selected - keep_radius; v <= selected + keep_radius; v++) {
                int ci = shelf_ui_cover_cache_index_with_counts(
                    article_count, book_count, v);

                if (ci == i) {
                    keep = 1;
                    break;
                }
            }
            if (!keep) {
                for (int v = visible_start; v <= visible_end; v++) {
                    int ci = shelf_ui_cover_cache_index_with_counts(
                        article_count, book_count, v);

                    if (ci == i) {
                        keep = 1;
                        break;
                    }
                }
            }
            if (!keep && cache->entries[i].texture) {
                SDL_DestroyTexture(cache->entries[i].texture);
                cache->entries[i].texture = NULL;
            }
        }
    }
    cache->last_trim_selected = selected;
    cache->last_trim_visible_start = visible_start;
    cache->last_trim_visible_end = visible_end;
    cache->last_trim_keep_radius = keep_radius;
}

static SDL_Rect ui_shelf_cover_content_rect(const SDL_Rect *slot_rect,
                                            ShelfCoverEntry *entry) {
    SDL_Rect rect;
    int tex_w = 0;
    int tex_h = 0;

    if (!slot_rect) {
        return (SDL_Rect){0, 0, 0, 0};
    }
    rect = *slot_rect;
    if (!entry || !entry->texture ||
        SDL_QueryTexture(entry->texture, NULL, NULL, &tex_w, &tex_h) != 0 ||
        tex_w <= 0 || tex_h <= 0) {
        return rect;
    }

    rect.w = slot_rect->w;
    rect.h = (int)lroundf((float)slot_rect->w * (float)tex_h / (float)tex_w);
    if (rect.h > slot_rect->h) {
        rect.h = slot_rect->h;
    }
    rect.x = slot_rect->x + (slot_rect->w - rect.w) / 2;
    rect.y = slot_rect->y + (slot_rect->h - rect.h) / 2;
    return rect;
}

static void render_shelf_cover(SDL_Renderer *renderer, TTF_Font *body_font, SDL_Color ink,
                               SDL_Rect cover_rect, ShelfCoverEntry *entry,
                               const char *title, float emphasis) {
    SDL_Rect content_rect = ui_shelf_cover_content_rect(&cover_rect, entry);
    const float clamped_emphasis = ui_clamp01f(emphasis);
    const float scale = 1.0f + 0.18f * clamped_emphasis;
    const int border_pad = content_rect.w / 22 > 6 ? content_rect.w / 22 : 6;
    const int shadow_dx = content_rect.w / 34 > 6 ? content_rect.w / 34 : 6;
    const int shadow_dy = content_rect.h / 32 > 8 ? content_rect.h / 32 : 8;
    const int empty_pad_x = content_rect.w / 12 > 18 ? content_rect.w / 12 : 18;
    int empty_w = 0;
    int empty_h = body_font ? TTF_FontHeight(body_font) : 28;
    SDL_Rect scaled_rect = {
        content_rect.x - (int)((content_rect.w * scale - content_rect.w) / 2.0f),
        content_rect.y - (int)((content_rect.h * scale - content_rect.h) / 2.0f),
        (int)(content_rect.w * scale),
        (int)(content_rect.h * scale)
    };
    SDL_Rect border = {
        scaled_rect.x - border_pad,
        scaled_rect.y - border_pad,
        scaled_rect.w + border_pad * 2,
        scaled_rect.h + border_pad * 2
    };
    const UiTheme *theme = ui_current_theme();
    Uint8 border_alpha = (Uint8)(255.0f * clamped_emphasis);
    Uint8 shadow_alpha = (Uint8)(theme->shadow_a * (0.7f + clamped_emphasis * 0.3f));

    (void)title;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, theme->shadow_r, theme->shadow_g,
                           theme->shadow_b, shadow_alpha);
    {
        SDL_Rect shadow = {
            scaled_rect.x + shadow_dx,
            scaled_rect.y + shadow_dy,
            scaled_rect.w,
            scaled_rect.h
        };

        SDL_RenderFillRect(renderer, &shadow);
    }

    if (border_alpha > 0) {
        SDL_SetRenderDrawColor(renderer, theme->selection_border_r,
                               theme->selection_border_g,
                               theme->selection_border_b, border_alpha);
        SDL_RenderFillRect(renderer, &border);
    }

    SDL_SetRenderDrawColor(renderer, theme->cover_bg_r, theme->cover_bg_g,
                           theme->cover_bg_b, 255);
    SDL_RenderFillRect(renderer, &scaled_rect);

    if (entry && entry->texture) {
        int tex_w = 0;
        int tex_h = 0;

        SDL_QueryTexture(entry->texture, NULL, NULL, &tex_w, &tex_h);
        ui_draw_texture_contain(renderer, entry->texture, tex_w, tex_h, &scaled_rect);
    } else {
        SDL_SetRenderDrawColor(renderer, theme->cover_empty_r, theme->cover_empty_g,
                               theme->cover_empty_b, 255);
        SDL_RenderFillRect(renderer, &scaled_rect);
        TTF_SizeUTF8(body_font, "\xE6\x97\xA0\xE5\xB0\x81\xE9\x9D\xA2", &empty_w, NULL);
        if (empty_w > scaled_rect.w - empty_pad_x * 2) {
            empty_w = scaled_rect.w - empty_pad_x * 2;
        }
        draw_text(renderer, body_font,
                  scaled_rect.x + (scaled_rect.w - empty_w) / 2,
                  scaled_rect.y + (scaled_rect.h - empty_h) / 2,
                  ink, "\xE6\x97\xA0\xE5\xB0\x81\xE9\x9D\xA2");
    }
}

void render_shelf(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
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
    int article_count = shelf_ui_article_count(nuxt);
    int book_count = shelf_ui_normal_count(nuxt);
    int total_count = article_count + book_count;
    int min_selected = article_count > 0 ? -article_count : 0;
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

    (void)ctx;
    (void)status;
    (void)start;

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

    if (total_count == 0) {
        int empty_w = 0;
        const char *empty_text = "\xE4\xB9\xA6\xE6\x9E\xB6\xE4\xB8\xBA\xE7\xA9\xBA";

        render_header_status(renderer, body_font, battery_text, layout);
        TTF_SizeUTF8(title_font, empty_text, &empty_w, NULL);
        draw_text(renderer, title_font, window_x + window_w / 2 - empty_w / 2,
                  window_h / 2 - 40, ink, empty_text);
        return;
    }

    shelf_cover_cache_trim(cover_cache, selected, selected_pos, article_count, book_count,
                           UI_SHELF_COVER_TEXTURE_KEEP_RADIUS);
    selected_book = shelf_ui_selected_book(nuxt, selected);
    selected_title = json_get_string(selected_book, "title");
    fit_text_ellipsis(title_font,
                      selected_title ? selected_title :
                      "\xE5\xBE\xAE\xE4\xBF\xA1\xE8\xAF\xBB\xE4\xB9\xA6",
                      window_w - margin * 2 - 140,
                      title_buf, sizeof(title_buf));
    draw_text(renderer, title_font, window_x + margin, title_y, ink, title_buf);
    render_header_status(renderer, body_font, battery_text, layout);

    for (int i = (int)floorf(selected_pos) - 3;
         i <= (int)ceilf(selected_pos) + 3; i++) {
        float offset;
        float emphasis;
        int is_article;
        int slot_h;
        SDL_Rect cover_rect = { 0, 0, cover_w, 0 };

        if (i < min_selected || i >= book_count) {
            continue;
        }
        is_article = i < 0;
        slot_h = is_article ? cover_w : cover_h;
        cover_rect.h = slot_h;
        cover_rect.y = content_top + (content_h - slot_h) / 2;
        offset = (float)i - selected_pos;
        emphasis = 1.0f - fabsf(offset);
        cover_rect.x = window_x + (window_w - cover_w) / 2 +
                       (int)lroundf(offset * (float)(cover_w + card_gap));

        {
            int cache_index = shelf_ui_cover_cache_index_with_counts(
                article_count, book_count, i);
            cJSON *book = shelf_ui_selected_entry(nuxt, i, NULL);
            const char *title = json_get_string(book, "title");

            if (!book) {
                continue;
            }
            render_shelf_cover(
                renderer, body_font, ink, cover_rect,
                cover_cache && cache_index >= 0 && cache_index < cover_cache->count ?
                &cover_cache->entries[cache_index] : NULL,
                title, emphasis);
        }
    }

    if (selected > min_selected) {
        draw_text(renderer, title_font, window_x + 18, nav_y, ink, "<");
    }
    if (selected + 1 < book_count) {
        draw_text(renderer, title_font, window_x + window_w - 42, nav_y, ink, ">");
    }

    if (selected < 0) {
        snprintf(position_buf, sizeof(position_buf),
                 "\xE5\x85\xAC\xE4\xBC\x97\xE5\x8F\xB7 %d / %d",
                 -selected, article_count);
    } else {
        snprintf(position_buf, sizeof(position_buf),
                 "\xE4\xB9\xA6 %d / %d", selected + 1, book_count);
    }
    TTF_SizeUTF8(body_font, position_buf, &position_width, NULL);
    position_x = window_x + window_w - margin - position_width;
    footer_text_y = footer_line.y +
        (info_h - (body_font ? TTF_FontHeight(body_font) : 28)) / 2;
    draw_text(renderer, body_font, window_x + margin, footer_text_y, muted,
              time_buf[0] ? time_buf : "--:--");
    draw_text(renderer, body_font, position_x, footer_text_y, muted, position_buf);
}

#endif
