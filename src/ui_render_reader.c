#include "ui_render_internal.h"

#if HAVE_SDL

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void reader_format_chapter_heading(const ReaderViewState *state,
                                          char *out, size_t out_size) {
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

static int reader_is_catalog_item_current(const ReaderViewState *state,
                                          const ReaderCatalogItem *item) {
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

void render_reader(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
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
    int line_h = state->line_height > 0 ? state->line_height :
        TTF_FontLineSkip(content_font);
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
    int footer_text_y = footer_line.y +
        (info_h - (body_font ? TTF_FontHeight(body_font) : 28)) / 2;
    int title_y = (header_h - (title_font ? TTF_FontHeight(title_font) : 36)) / 2;
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
        int fw = 0;

        TTF_SizeUTF8(body_font, footer, &fw, NULL);
        draw_text(renderer, body_font, cx + cw - margin - fw, footer_text_y, muted, footer);
    }
}

void render_catalog_overlay(SDL_Renderer *renderer, TTF_Font *title_font, TTF_Font *body_font,
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
    int list_bottom;
    int list_height;
    int first_visible;
    int last_visible;
    char title_buf[256];
    int header_title_y;
    float eased = ui_ease_out_cubic(progress);
    float row_eased = ui_ease_out_cubic(progress);
    float selected_clamped;
    float top_slots_visible;
    float bottom_slots_visible;
    int panel_offset;
    Uint8 backdrop_alpha;
    Uint8 panel_alpha;
    SDL_Rect selected_row;
    int selected_row_y;

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
    list_bottom = panel.y + panel.h - 20;
    list_height = list_bottom - list_top;
    if (list_height < line_height) {
        list_height = line_height;
    }
    selected_clamped = selected_pos;
    if (selected_clamped < 0.0f) {
        selected_clamped = 0.0f;
    }
    if (selected_clamped > (float)(state->doc.catalog_count - 1)) {
        selected_clamped = (float)(state->doc.catalog_count - 1);
    }
    selected_row_y = list_top + (list_height - (line_height - 4)) / 2;
    top_slots_visible = line_height > 0 ?
        (float)(selected_row_y - list_top) / (float)line_height : 0.0f;
    bottom_slots_visible = line_height > 0 ?
        (float)(list_bottom - (selected_row_y + (line_height - 4))) / (float)line_height : 0.0f;
    first_visible = (int)floorf(selected_clamped - top_slots_visible) - 2;
    last_visible = (int)ceilf(selected_clamped + bottom_slots_visible) + 3;
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

    fit_text_ellipsis(title_font,
                      state->doc.book_title ? state->doc.book_title : "目录",
                      header.w - 120, title_buf, sizeof(title_buf));
    header_title_y = header.y +
        (header.h - (title_font ? TTF_FontHeight(title_font) : 36)) / 2;
    draw_text(renderer, title_font, header.x + 24, header_title_y, ink, title_buf);

    selected_row.x = panel.x + 16;
    selected_row.y = selected_row_y;
    selected_row.w = panel.w - 32;
    selected_row.h = line_height - 4;
    SDL_SetRenderDrawColor(renderer, theme->catalog_highlight_r,
                           theme->catalog_highlight_g,
                           theme->catalog_highlight_b, 255);
    SDL_RenderFillRect(renderer, &selected_row);

    for (int i = first_visible; i < last_visible; i++) {
        ReaderCatalogItem *item = &state->doc.catalog_items[i];
        int row_y = selected_row_y +
            (int)lroundf(((float)i - selected_clamped) * (float)line_height);
        SDL_Rect row = { panel.x + 16, row_y, panel.w - 32, line_height - 4 };
        char row_buf[256];
        int indent = item->level > 1 ? (item->level - 1) * 20 : 0;
        int text_x_shift =
            (int)lroundf((1.0f - row_eased) *
                         (14.0f + fminf(fabsf((float)i - selected_clamped), 4.0f) * 5.0f));
        SDL_Color color = item->is_lock ? dim : ink;

        if (row.y + row.h < list_top || row.y > list_bottom) {
            continue;
        }

        if (reader_is_catalog_item_current(state, item) &&
            fabsf((float)i - selected_clamped) >= 0.45f) {
            SDL_SetRenderDrawColor(renderer, theme->catalog_current_r,
                                   theme->catalog_current_g,
                                   theme->catalog_current_b, 255);
            SDL_RenderFillRect(renderer, &row);
        }

        fit_text_ellipsis(body_font, item->title ? item->title : "(untitled)",
                          row.w - 72 - indent, row_buf, sizeof(row_buf));
        draw_text(renderer, body_font, row.x + 16 + indent + text_x_shift, row.y + 6,
                  color, row_buf);
    }
}

#endif
