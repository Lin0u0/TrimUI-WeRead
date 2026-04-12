/*
 * ui_reader_view.c - Internal reader-view ownership for pagination and progress
 */
#include "ui_internal.h"

#if HAVE_SDL

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reader_service.h"

static int ui_reader_view_append_line(ReaderViewState *state, const char *text,
                                      size_t len, int start_offset) {
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

static int ui_reader_view_append_blank_lines(ReaderViewState *state, int count) {
    int i;

    for (i = 0; i < count; i++) {
        int offset = state->line_count > 0 && state->line_offsets ?
            state->line_offsets[state->line_count - 1] : 0;
        if (ui_reader_view_append_line(state, "", 0, offset) != 0) {
            return -1;
        }
    }
    return 0;
}

static int ui_reader_view_line_height_for_font_size(int font_size) {
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

static void ui_reader_view_copy_fallback_font_path(char *dst, size_t dst_size,
                                                   const ReaderViewState *state) {
    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (!state || !state->fallback_font_path[0]) {
        return;
    }
    snprintf(dst, dst_size, "%s", state->fallback_font_path);
}

static void ui_reader_view_sync_catalog_selection(ReaderViewState *state) {
    int index;

    if (!state) {
        return;
    }
    index = ui_reader_view_current_catalog_index(state);
    state->catalog_selected = index >= 0 ? index : 0;
    state->catalog_scroll_top = state->catalog_selected > 2 ?
        state->catalog_selected - 2 : 0;
}

static int ui_reader_view_anchor_offset(const ReaderViewState *state) {
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

static int ui_reader_view_wrap_paragraph(TTF_Font *font, const char *text, int max_width,
                                         ReaderViewState *state) {
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

        if (*p == '\n') {
            if (p > line_start) {
                if (ui_reader_view_append_line(state, line_start,
                                               (size_t)(p - line_start),
                                               line_start_offset) != 0) {
                    return -1;
                }
                if (ui_reader_view_append_blank_lines(state,
                                                      UI_READER_PARAGRAPH_GAP_LINES) != 0) {
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

        if (isspace((unsigned char)*p)) {
            last_break = p;
            in_latin_word = 0;
            last_word_start = NULL;
        } else if (is_cjk_char(p)) {
            if (p > line_start) {
                last_break = p;
            }
            in_latin_word = 0;
            last_word_start = NULL;
        } else if (is_latin_or_digit(p)) {
            if (!in_latin_word) {
                in_latin_word = 1;
                last_word_start = p;
            }
        } else {
            in_latin_word = 0;
            last_word_start = NULL;
        }

        ch_width = get_char_width_fast(font, p, ch_len);
        if (line_width + ch_width > max_width && p > line_start) {
            const char *break_at;
            const char *next_start;

            if (last_break && last_break > line_start) {
                break_at = last_break;
                if (isspace((unsigned char)*last_break)) {
                    next_start = last_break + 1;
                } else {
                    next_start = last_break;
                }
            } else if (last_word_start && last_word_start > line_start) {
                break_at = last_word_start;
                next_start = last_word_start;
            } else {
                break_at = p;
                next_start = p;
            }

            while (break_at > line_start && isspace((unsigned char)break_at[-1])) {
                break_at--;
            }
            while (*next_start && isspace((unsigned char)*next_start) && *next_start != '\n') {
                next_start++;
            }

            {
                const char *punct = skip_line_start_spacing(next_start,
                                                            next_start + strlen(next_start));
                while (*punct && *punct != '\n' &&
                       is_forbidden_line_start_punct(punct)) {
                    punct += utf8_char_len((unsigned char)*punct);
                }
                if (punct > next_start) {
                    break_at = punct;
                    next_start = punct;
                }
            }

            if (ui_reader_view_append_line(state, line_start,
                                           (size_t)(break_at - line_start),
                                           line_start_offset) != 0) {
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
            last_word_start = NULL;
            line_width = 0;
            in_latin_word = 0;
            continue;
        }

        line_width += ch_width;
        p += ch_len;
        char_offset++;
    }

    if (p > line_start) {
        if (ui_reader_view_append_line(state, line_start, (size_t)(p - line_start),
                                       line_start_offset) != 0) {
            return -1;
        }
    }
    return 0;
}

static int ui_reader_view_init_from_document(TTF_Font *font, int content_width,
                                             int content_height,
                                             int honor_saved_position,
                                             ReaderViewState *state) {
    TTF_Font *render_font = font;

    if (!state) {
        return -1;
    }
    fprintf(stderr,
            "reader-view-init: begin target=%s kind=%s font=%d width=%d height=%d honorSaved=%d contentLen=%zu\n",
            state->doc.target ? state->doc.target : "(null)",
            state->doc.kind == READER_DOCUMENT_KIND_ARTICLE ? "article" : "book",
            state->content_font_size > 0 ? state->content_font_size : UI_READER_CONTENT_FONT_SIZE,
            content_width,
            content_height,
            honor_saved_position,
            state->doc.content_text ? strlen(state->doc.content_text) : 0u);
    if (!state->doc.content_text || !state->doc.content_text[0]) {
        fprintf(stderr,
                "reader-view-init: empty content target=%s\n",
                state->doc.target ? state->doc.target : "(null)");
        return -1;
    }
    if (state->content_font_size <= 0) {
        state->content_font_size = UI_READER_CONTENT_FONT_SIZE;
    }
    if (ui_reader_view_reset_content_font(font, state) != 0) {
        fprintf(stderr,
                "reader-view-init: content font reset failed target=%s fontPath=%s useContentFont=%d\n",
                state->doc.target ? state->doc.target : "(null)",
                state->doc.content_font_path,
                state->doc.use_content_font);
        return -1;
    }
    if (state->content_font) {
        render_font = state->content_font;
    }
    if (ui_reader_view_wrap_paragraph(render_font, state->doc.content_text,
                                      content_width, state) != 0) {
        fprintf(stderr,
                "reader-view-init: wrap failed target=%s\n",
                state->doc.target ? state->doc.target : "(null)");
        return -1;
    }

    state->line_height = ui_reader_view_line_height_for_font_size(state->content_font_size);
    state->lines_per_page = state->line_height > 0 ?
        content_height / state->line_height : 18;
    if (state->lines_per_page < 1) {
        state->lines_per_page = 1;
    }
    state->current_page = 0;
    if (honor_saved_position && state->doc.saved_chapter_offset > 0) {
        state->current_page = ui_reader_view_find_page_for_offset(
            state, state->doc.saved_chapter_offset);
    }
    ui_reader_view_sync_catalog_selection(state);
    state->catalog_open = 0;
    fprintf(stderr,
            "reader-view-init: ready target=%s lines=%d pages=%d currentPage=%d lineHeight=%d linesPerPage=%d\n",
            state->doc.target ? state->doc.target : "(null)",
            state->line_count,
            ui_reader_view_total_pages(state),
            state->current_page,
            state->line_height,
            state->lines_per_page);
    return 0;
}

static int ui_reader_view_prepare_state(TTF_Font *font, ReaderDocument *doc,
                                        int content_width, int content_height,
                                        int honor_saved_position,
                                        int content_font_size,
                                        const char *fallback_font_path,
                                        ReaderViewState *next_state) {
    if (!doc || !next_state) {
        return -1;
    }

    memset(next_state, 0, sizeof(*next_state));
    next_state->doc = *doc;
    memset(doc, 0, sizeof(*doc));
    next_state->content_font_size = content_font_size > 0 ?
        content_font_size : UI_READER_CONTENT_FONT_SIZE;
    if (fallback_font_path && fallback_font_path[0]) {
        snprintf(next_state->fallback_font_path, sizeof(next_state->fallback_font_path), "%s",
                 fallback_font_path);
    }
    if (ui_reader_view_init_from_document(font, content_width, content_height,
                                          honor_saved_position, next_state) != 0) {
        ui_reader_view_free(next_state);
        return -1;
    }
    return 0;
}

void ui_reader_view_build_page_summary(ReaderViewState *state, char *out,
                                       size_t out_size) {
    int total_pages;
    int start_line;
    int end_line;
    size_t len = 0;
    int i;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!state || !state->lines || state->line_count <= 0) {
        return;
    }

    total_pages = ui_reader_view_total_pages(state);
    start_line = state->current_page * state->lines_per_page;
    end_line = start_line + state->lines_per_page;
    if (end_line > state->line_count) {
        end_line = state->line_count;
    }

    for (i = start_line; i < end_line && len + 1 < out_size; i++) {
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

static void ui_reader_view_progress_begin(ReaderViewState *state, Uint32 now) {
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

void ui_reader_view_free(ReaderViewState *state) {
    int i;

    if (!state) {
        return;
    }
    if (state->lines) {
        for (i = 0; i < state->line_count; i++) {
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

int ui_reader_view_reset_content_font(TTF_Font *fallback_font, ReaderViewState *state) {
    const char *font_path = NULL;
    int font_size;

    if (!state) {
        return -1;
    }

    if (state->content_font) {
        TTF_CloseFont(state->content_font);
        state->content_font = NULL;
    }

    font_size = state->content_font_size > 0 ?
        state->content_font_size : UI_READER_CONTENT_FONT_SIZE;
    state->content_font_size = font_size;

    if (state->doc.use_content_font && state->doc.content_font_path[0]) {
        font_path = state->doc.content_font_path;
    } else if (state->fallback_font_path[0]) {
        font_path = state->fallback_font_path;
    }
    if (font_path && font_path[0]) {
        state->content_font = TTF_OpenFont(font_path, font_size);
        if (!state->content_font) {
            return fallback_font ? 0 : -1;
        }
    }

    char_width_cache_reset();
    return 0;
}

int ui_reader_view_adopt_document(TTF_Font *font, ReaderDocument *doc,
                                  int content_width, int content_height,
                                  int honor_saved_position, ReaderViewState *state) {
    int content_font_size = state ?
        state->content_font_size : UI_READER_CONTENT_FONT_SIZE;
    char fallback_font_path[512];
    ReaderViewState next_state;

    ui_reader_view_copy_fallback_font_path(fallback_font_path, sizeof(fallback_font_path), state);

    fprintf(stderr,
            "reader-view-adopt: begin docTarget=%s kind=%s contentLen=%zu preservedFont=%d existingLines=%d\n",
            doc && doc->target ? doc->target : "(null)",
            doc && doc->kind == READER_DOCUMENT_KIND_ARTICLE ? "article" : "book",
            doc && doc->content_text ? strlen(doc->content_text) : 0u,
            content_font_size,
            state ? state->line_count : -1);
    if (!doc || !doc->content_text || !doc->target) {
        fprintf(stderr,
                "reader-view-adopt: invalid document doc=%p target=%s content=%p\n",
                (void *)doc,
                doc && doc->target ? doc->target : "(null)",
                (void *)(doc ? doc->content_text : NULL));
        return -1;
    }
    if (ui_reader_view_prepare_state(font, doc, content_width, content_height,
                                     honor_saved_position, content_font_size,
                                     fallback_font_path, &next_state) != 0) {
        fprintf(stderr,
                "reader-view-adopt: init failed target=%s\n",
                doc && doc->target ? doc->target : "(null)");
        return -1;
    }
    ui_reader_view_free(state);
    *state = next_state;
    fprintf(stderr,
            "reader-view-adopt: success target=%s lines=%d currentPage=%d\n",
            state->doc.target ? state->doc.target : "(null)",
            state->line_count,
            state->current_page);
    return 0;
}

int ui_reader_view_load(ApiContext *ctx, TTF_Font *font, const char *target,
                        int font_size, int content_width, int content_height,
                        int honor_saved_position, ReaderViewState *state) {
    int content_font_size = state ?
        state->content_font_size : UI_READER_CONTENT_FONT_SIZE;
    char fallback_font_path[512];
    ReaderDocument doc = {0};
    ReaderViewState next_state;

    ui_reader_view_copy_fallback_font_path(fallback_font_path, sizeof(fallback_font_path), state);

    if (reader_load(ctx, target, font_size, &doc) != 0) {
        return -1;
    }
    if (ui_reader_view_prepare_state(font, &doc, content_width, content_height,
                                     honor_saved_position, content_font_size,
                                     fallback_font_path, &next_state) != 0) {
        reader_document_free(&doc);
        return -1;
    }
    ui_reader_view_free(state);
    *state = next_state;
    return 0;
}

int ui_reader_view_rewrap(TTF_Font *font, int content_width, int content_height,
                          ReaderViewState *state) {
    TTF_Font *render_font;
    int saved_offset = 0;
    int i;

    if (!state || !state->doc.content_text) {
        return -1;
    }

    render_font = state->content_font ? state->content_font : font;
    saved_offset = ui_reader_view_anchor_offset(state);

    if (state->lines) {
        for (i = 0; i < state->line_count; i++) {
            free(state->lines[i]);
        }
        free(state->lines);
        state->lines = NULL;
    }
    free(state->line_offsets);
    state->line_offsets = NULL;
    state->line_count = 0;
    state->line_capacity = 0;

    if (ui_reader_view_wrap_paragraph(render_font, state->doc.content_text,
                                      content_width, state) != 0) {
        return -1;
    }

    state->line_height = ui_reader_view_line_height_for_font_size(state->content_font_size);
    state->lines_per_page = state->line_height > 0 ?
        content_height / state->line_height : 18;
    if (state->lines_per_page < 1) {
        state->lines_per_page = 1;
    }

    state->current_page = ui_reader_view_find_page_for_offset(state, saved_offset);
    return 0;
}

int ui_reader_view_total_pages(ReaderViewState *state) {
    if (!state || state->line_count < 0) {
        return 1;
    }
    return state->line_count > 0 ?
        (state->line_count + state->lines_per_page - 1) / state->lines_per_page : 1;
}

int ui_reader_view_find_page_for_offset(const ReaderViewState *state, int target_offset) {
    int total_pages;
    int page;

    if (!state || target_offset <= 0) {
        return 0;
    }

    total_pages = ui_reader_view_total_pages((ReaderViewState *)state);
    if (total_pages <= 1) {
        return 0;
    }

    if (state->line_offsets && state->line_count > 0) {
        for (page = 0; page < total_pages; page++) {
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

    for (page = 0; page < total_pages; page++) {
        int page_offset = reader_estimate_chapter_offset(&state->doc, page, total_pages);
        if (page_offset >= target_offset) {
            return page;
        }
    }

    return total_pages - 1;
}

void ui_reader_view_set_source_target(ReaderViewState *state, const char *source_target) {
    if (!state) {
        return;
    }
    if (!source_target) {
        state->source_target[0] = '\0';
        return;
    }
    snprintf(state->source_target, sizeof(state->source_target), "%s", source_target);
}

void ui_reader_view_clamp_current_page(ReaderViewState *state) {
    int total_pages;

    if (!state) {
        return;
    }
    total_pages = ui_reader_view_total_pages(state);
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

int ui_reader_view_current_page_offset(const ReaderViewState *state) {
    int start_line;

    if (!state) {
        return 0;
    }
    start_line = state->current_page * state->lines_per_page;
    if (state->line_offsets && start_line >= 0 && start_line < state->line_count) {
        return state->line_offsets[start_line];
    }
    return reader_estimate_chapter_offset(&state->doc, state->current_page,
                                          ui_reader_view_total_pages((ReaderViewState *)state));
}

int ui_reader_view_current_catalog_index(ReaderViewState *state) {
    int i;

    if (!state || !state->doc.catalog_items || state->doc.catalog_count <= 0) {
        return -1;
    }
    for (i = 0; i < state->doc.catalog_count; i++) {
        if (state->doc.catalog_items[i].is_current) {
            return i;
        }
    }
    if (state->doc.chapter_uid) {
        for (i = 0; i < state->doc.catalog_count; i++) {
            if (state->doc.catalog_items[i].chapter_uid &&
                strcmp(state->doc.catalog_items[i].chapter_uid,
                       state->doc.chapter_uid) == 0) {
                return i;
            }
        }
    }
    if (state->doc.chapter_idx > 0) {
        for (i = 0; i < state->doc.catalog_count; i++) {
            if (state->doc.catalog_items[i].chapter_idx == state->doc.chapter_idx) {
                return i;
            }
        }
    }
    return 0;
}

void ui_reader_view_save_local_position(ApiContext *ctx, ReaderViewState *state) {
    int current_offset;

    if (!ctx || !state || !state->doc.book_id || !state->doc.target ||
        !state->source_target[0]) {
        return;
    }
    ui_reader_view_clamp_current_page(state);
    current_offset = ui_reader_view_anchor_offset(state);
    reader_service_save_local_position(ctx, &state->doc, state->source_target,
                                       state->content_font_size, state->current_page,
                                       current_offset);
}

void ui_reader_view_note_progress_activity(ReaderViewState *state, Uint32 now) {
    Uint32 paused_duration;

    if (!state) {
        return;
    }
    if (!state->progress_initialized) {
        ui_reader_view_progress_begin(state, now);
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
        state->progress_report_due_tick =
            state->progress_start_tick + UI_PROGRESS_REPORT_INTERVAL_MS;
    }
}

void ui_reader_view_flush_progress_blocking(ApiContext *ctx, ReaderViewState *state,
                                            int compute_progress) {
    Uint32 now;
    Uint32 elapsed_ms;
    int elapsed_seconds;
    int rc;
    char page_summary[128];

    if (!ctx || !state || !state->doc.book_id || !state->doc.token ||
        !state->doc.chapter_uid) {
        return;
    }

    now = SDL_GetTicks();
    ui_reader_view_note_progress_activity(state, now);
    elapsed_ms = state->progress_start_tick > 0 && now > state->progress_start_tick ?
        (now - state->progress_start_tick) : 0;
    elapsed_seconds = (int)(elapsed_ms / 1000);
    if (elapsed_seconds < 0) {
        elapsed_seconds = 0;
    }

    ui_reader_view_build_page_summary(state, page_summary, sizeof(page_summary));
    rc = reader_service_report_progress(ctx, &state->doc, state->current_page,
                                        ui_reader_view_total_pages(state),
                                        ui_reader_view_current_page_offset(state),
                                        elapsed_seconds, page_summary,
                                        compute_progress);
    if (rc == READER_REPORT_OK) {
        state->progress_start_tick = now;
        if (!state->progress_paused) {
            state->progress_report_due_tick = now + UI_PROGRESS_REPORT_INTERVAL_MS;
        }
    } else if (!state->progress_paused) {
        state->progress_report_due_tick = now + UI_PROGRESS_REPORT_INTERVAL_MS;
    }
}

void ui_reader_view_open_catalog(ApiContext *ctx, ReaderViewState *state,
                                 char *status, size_t status_size) {
    int catalog_complete = 0;

    if (!state || !state->doc.catalog_items || state->doc.catalog_count <= 0) {
        fprintf(stderr,
                "reader-view-catalog-open: unavailable target=%s kind=%s count=%d items=%p\n",
                state && state->doc.target ? state->doc.target : "(null)",
                state && state->doc.kind == READER_DOCUMENT_KIND_ARTICLE ? "article" : "book",
                state ? state->doc.catalog_count : -1,
                state ? (void *)state->doc.catalog_items : NULL);
        if (status && status_size > 0) {
            status[0] = '\0';
        }
        return;
    }

    if (ctx && state->doc.kind == READER_DOCUMENT_KIND_BOOK) {
        catalog_complete = state->doc.catalog_total_count <= 0 ||
            state->doc.catalog_count >= state->doc.catalog_total_count;
        fprintf(stderr,
                "reader-view-catalog-open: target=%s count=%d total=%d complete=%d chapterUid=%s chapterIdx=%d\n",
                state->doc.target ? state->doc.target : "(null)",
                state->doc.catalog_count,
                state->doc.catalog_total_count,
                catalog_complete,
                state->doc.chapter_uid ? state->doc.chapter_uid : "(null)",
                state->doc.chapter_idx);
    } else {
        catalog_complete = state->doc.catalog_total_count <= 0 ||
            state->doc.catalog_count >= state->doc.catalog_total_count;
        fprintf(stderr,
                "reader-view-catalog-open: target=%s kind=article count=%d total=%d complete=%d chapterUid=%s chapterIdx=%d\n",
                state->doc.target ? state->doc.target : "(null)",
                state->doc.catalog_count,
                state->doc.catalog_total_count,
                catalog_complete,
                state->doc.chapter_uid ? state->doc.chapter_uid : "(null)",
                state->doc.chapter_idx);
    }

    ui_reader_view_sync_catalog_selection(state);
    if (status && status_size > 0) {
        status[0] = '\0';
    }
    state->catalog_open = 1;
}

int ui_reader_view_expand_catalog_for_selection(ApiContext *ctx, ReaderViewState *state,
                                                int direction, char *status,
                                                size_t status_size) {
    int added_count = 0;

    if (!ctx || !state || !state->doc.catalog_items || state->doc.catalog_count <= 0) {
        return 0;
    }
    if (reader_expand_catalog(ctx, &state->doc, direction, &added_count) != 0) {
        if (status && status_size > 0) {
            snprintf(status, status_size,
                     "\xE6\x97\xA0\xE6\xB3\x95\xE5\x8A\xA0\xE8\xBD\xBD\xE6\x9B\xB4\xE5\xA4\x9A\xE7\xAB\xA0\xE8\x8A\x82");
        }
        return 0;
    }
    if (direction < 0 && added_count > 0) {
        state->catalog_selected += added_count;
    }
    return added_count;
}

#else

typedef int ui_reader_view_translation_unit_requires_semicolon;

#endif
