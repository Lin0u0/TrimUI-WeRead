#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reader_internal.h"
#include "html_strip.h"

static const char *find_matching_div_close(const char *start) {
    const char *p = start;
    int depth = 1;

    while (*p) {
        const char *open_div = strstr(p, "<div");
        const char *close_div = strstr(p, "</div>");

        if (!close_div) {
            return NULL;
        }
        if (open_div && open_div < close_div) {
            depth++;
            p = open_div + 4;
            continue;
        }

        depth--;
        if (depth == 0) {
            return close_div;
        }
        p = close_div + 6;
    }

    return NULL;
}

static char *extract_div_inner_html(const char *div_open) {
    const char *start;
    const char *end;

    if (!div_open) {
        return NULL;
    }

    start = strchr(div_open, '>');
    if (!start) {
        return NULL;
    }
    start++;
    end = find_matching_div_close(start);
    if (!end || end < start) {
        return NULL;
    }

    return reader_dup_range(start, end);
}

static char *extract_obfuscated_content_html(const char *html) {
    const char *container_marker = "<div id=\"readerContentRenderContainer\"";
    const char *content_marker = "<div class=\"chapterContent_txt\">";
    const char *container = strstr(html, container_marker);
    const char *content = strstr(html, content_marker);
    const char *start;
    const char *end;

    if (container) {
        char *container_html = extract_div_inner_html(container);
        if (container_html) {
            return container_html;
        }
    }

    if (!content) {
        return NULL;
    }
    start = content + strlen(content_marker);
    end = strstr(start, "</div>");
    if (!end || end <= start) {
        return NULL;
    }
    return reader_dup_range(start, end);
}

static char *extract_reader_param_value(const char *html, const char *block_marker) {
    const char *block_start;
    const char *block_end;

    if (reader_find_named_object_block(html, block_marker, &block_start, &block_end) != 0) {
        return NULL;
    }
    return reader_extract_resolved_value_after_marker(html, block_start, block_end, "param:");
}

static int extract_int_after_marker(const char *html, const char *marker, int fallback) {
    char *value = reader_extract_resolved_value_after_global_marker(html, marker);
    int result = fallback;

    if (value) {
        result = atoi(value);
    }
    free(value);
    return result;
}

static void extract_book_progress(const char *html, ReaderDocument *doc) {
    const char *prog_start = NULL;
    const char *prog_end = NULL;
    char *book_block = NULL;

    if (reader_find_named_object_block(html, "progress:{bookId:", &prog_start, &prog_end) != 0) {
        return;
    }

    book_block = reader_extract_container_from_slice(prog_start, prog_end, "book:", '{', '}');
    if (!book_block) {
        return;
    }

    {
        const char *book_end = book_block + strlen(book_block);

        doc->progress_summary = reader_extract_resolved_from_slice(html, book_block, book_end,
                                                                   "summary:");
        doc->progress_chapter_uid = reader_extract_resolved_from_slice(html, book_block, book_end,
                                                                       "chapterUid:");
        doc->progress_chapter_idx = reader_extract_int_from_slice_resolved(
            html, book_block, book_end, "chapterIdx:", 0);
        doc->saved_chapter_offset = reader_extract_int_from_slice_resolved(
            html, book_block, book_end, "chapterOffset:", 0);
        doc->last_reported_progress = reader_extract_int_from_slice_resolved(
            html, book_block, book_end, "progress:", 0);
    }

    free(book_block);
}

static void extract_book_reader_state(const char *html, const char **reader_block_start_out,
                                      const char **reader_block_end_out,
                                      char **cur_chapter_block_out,
                                      ReaderDocument *doc) {
    const char *reader_block_start = NULL;
    const char *reader_block_end = NULL;

    doc->target = extract_reader_param_value(html, "curChapterUrlParam:{");
    doc->prev_target = extract_reader_param_value(html, "prevChapterUrlParam:{");
    doc->next_target = extract_reader_param_value(html, "nextChapterUrlParam:{");
    doc->book_id = reader_extract_resolved_value_in_object_block(html, "reader:{bookId:",
                                                                 "bookId:");
    doc->token = reader_extract_resolved_value_in_object_block(html, "reader:{bookId:",
                                                               "token:");
    doc->total_words = reader_extract_int_in_object_block(html, "bookInfo:{", "totalWords:", 0);
    doc->prev_chapters_word_count = extract_int_after_marker(html, "prevChaptersWordCount:", 0);

    if (reader_find_named_object_block(html, "reader:{bookId:",
                                       &reader_block_start, &reader_block_end) == 0) {
        doc->catalog_total_count = reader_extract_int_from_slice_resolved(
            html, reader_block_start, reader_block_end, "chapterInfoCount:", 0);
        {
            char *range_block = reader_extract_container_from_slice(reader_block_start,
                                                                    reader_block_end,
                                                                    "curCatalogRange:",
                                                                    '{', '}');
            if (range_block) {
                const char *range_end = range_block + strlen(range_block);

                doc->catalog_range_start = reader_extract_int_from_slice_resolved(
                    html, range_block, range_end, "start:", 0);
                doc->catalog_range_end = reader_extract_int_from_slice_resolved(
                    html, range_block, range_end, "end:", 0);
                free(range_block);
            }
        }

        if (cur_chapter_block_out) {
            *cur_chapter_block_out = reader_extract_container_from_slice(reader_block_start,
                                                                         reader_block_end,
                                                                         "curChapter:",
                                                                         '{', '}');
        }
    }

    if (reader_block_start_out) {
        *reader_block_start_out = reader_block_start;
    }
    if (reader_block_end_out) {
        *reader_block_end_out = reader_block_end;
    }
}

static void extract_current_chapter_from_block(const char *html, const char *cur_chapter_block,
                                               ReaderDocument *doc) {
    const char *cur_start;
    const char *cur_end;

    if (!cur_chapter_block) {
        return;
    }

    cur_start = cur_chapter_block;
    cur_end = cur_chapter_block + strlen(cur_chapter_block);
    free(doc->chapter_uid);
    doc->chapter_uid = reader_extract_resolved_from_slice(html, cur_start, cur_end,
                                                          "chapterUid:");
    doc->chapter_idx = reader_extract_int_from_slice_resolved(html, cur_start, cur_end,
                                                              "chapterIdx:",
                                                              doc->chapter_idx);
    doc->chapter_word_count = reader_extract_int_from_slice_resolved(
        html, cur_start, cur_end, "wordCount:", doc->chapter_word_count);
}

static void apply_progress_chapter_fallback(ReaderDocument *doc) {
    if ((!doc->chapter_uid || strcmp(doc->chapter_uid, "0") == 0) &&
        doc->progress_chapter_uid) {
        free(doc->chapter_uid);
        doc->chapter_uid = reader_dup_or_null(doc->progress_chapter_uid);
    }
    if (doc->chapter_idx <= 0 && doc->progress_chapter_idx > 0) {
        doc->chapter_idx = doc->progress_chapter_idx;
    }
}

static void reconcile_book_chapter_from_catalog(ApiContext *ctx, ReaderDocument *doc,
                                                const char *target) {
    const char *lookup_target = doc->target ? doc->target : target;
    int found_in_catalog = 0;

    if (lookup_target && doc->catalog_items && doc->catalog_count > 0) {
        for (int i = 0; i < doc->catalog_count; i++) {
            if (doc->catalog_items[i].target &&
                strcmp(doc->catalog_items[i].target, lookup_target) == 0) {
                if (doc->catalog_items[i].chapter_uid) {
                    free(doc->chapter_uid);
                    doc->chapter_uid = strdup(doc->catalog_items[i].chapter_uid);
                }
                if (doc->catalog_items[i].chapter_idx > 0) {
                    doc->chapter_idx = doc->catalog_items[i].chapter_idx;
                }
                if (doc->chapter_word_count <= 0 && doc->catalog_items[i].word_count > 0) {
                    doc->chapter_word_count = doc->catalog_items[i].word_count;
                }
                found_in_catalog = 1;
                break;
            }
        }
    }

    if (!found_in_catalog && lookup_target && doc->book_id && doc->book_id[0] &&
        doc->progress_chapter_idx > 0) {
        ReaderCatalogItem *chunk = NULL;
        int chunk_count = 0;
        int first_idx = 0;
        int last_idx = 0;
        int range = doc->progress_chapter_idx > 40 ? doc->progress_chapter_idx - 40 : 0;

        if (reader_fetch_catalog_chunk(ctx, doc->book_id, 2, range, range,
                                       NULL, &chunk, &chunk_count,
                                       &first_idx, &last_idx) == 0 && chunk_count > 0) {
            for (int i = 0; i < chunk_count; i++) {
                if (!found_in_catalog && chunk[i].target &&
                    strcmp(chunk[i].target, lookup_target) == 0) {
                    if (chunk[i].chapter_uid) {
                        free(doc->chapter_uid);
                        doc->chapter_uid = strdup(chunk[i].chapter_uid);
                    }
                    if (chunk[i].chapter_idx > 0) {
                        doc->chapter_idx = chunk[i].chapter_idx;
                    }
                    if (doc->chapter_word_count <= 0 && chunk[i].word_count > 0) {
                        doc->chapter_word_count = chunk[i].word_count;
                    }
                    found_in_catalog = 1;
                }
                free(chunk[i].chapter_uid);
                free(chunk[i].target);
                free(chunk[i].title);
            }
            free(chunk);
        }
    }
}

static int finalize_book_content(ApiContext *ctx, const char *target, int font_size,
                                 const char *html, ReaderDocument *doc) {
    char *content_html = extract_obfuscated_content_html(html);

    if (!content_html || !*content_html) {
        fprintf(stderr, "Kindle simplified reader content not found in page DOM.\n");
        free(content_html);
        return -1;
    }

    doc->content_text = html_strip_to_text(content_html);
    doc->book_title = reader_extract_attr_text(html, "<p class=\"readerCatalog_bookTitle\">");
    doc->chapter_title = reader_extract_attr_text(html, "<p class=\"name\">");
    if (!doc->target) {
        doc->target = strdup(target);
    }
    doc->chapter_max_offset = reader_extract_max_wco(content_html);
    if (reader_extract_wco_values(content_html, &doc->chapter_offsets,
                                  &doc->chapter_offset_count) != 0) {
        free(content_html);
        return -1;
    }
    doc->font_size = font_size;
    free(content_html);

    if (!doc->content_text || !doc->content_text[0] || !doc->target) {
        fprintf(stderr, "reader-load: content text missing target=%s\n", target);
        return -1;
    }

    reader_focus_catalog(ctx, doc);
    if (reader_cached_random_font_path(ctx, doc->content_font_path,
                                       sizeof(doc->content_font_path)) == 0) {
        doc->use_content_font = 1;
    }
    return 0;
}

int reader_load_book_document(ApiContext *ctx, const char *target, int font_size,
                              const char *html, ReaderDocument *doc) {
    const char *reader_block_start = NULL;
    const char *reader_block_end = NULL;
    char *cur_chapter_block = NULL;
    int rc = -1;

    if (!ctx || !target || !html || !doc) {
        return -1;
    }

    extract_book_progress(html, doc);
    extract_book_reader_state(html, &reader_block_start, &reader_block_end,
                              &cur_chapter_block, doc);
    extract_current_chapter_from_block(html, cur_chapter_block, doc);
    apply_progress_chapter_fallback(doc);

    if (reader_block_start && reader_block_end) {
        reader_parse_catalog(html, reader_block_start, reader_block_end,
                             &doc->catalog_items, &doc->catalog_count);
    }

    reconcile_book_chapter_from_catalog(ctx, doc, target);
    rc = finalize_book_content(ctx, target, font_size, html, doc);

    free(cur_chapter_block);
    return rc;
}
