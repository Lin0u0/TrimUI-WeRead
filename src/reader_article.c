#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reader_internal.h"
#include "html_strip.h"
#include "json.h"

static int tag_has_id(const char *tag_start, const char *tag_end, const char *id_value) {
    size_t id_len;
    const char *p;

    if (!tag_start || !tag_end || !id_value || tag_end <= tag_start) {
        return 0;
    }

    id_len = strlen(id_value);
    for (p = tag_start; p + 4 + id_len < tag_end; p++) {
        if (strncmp(p, "id=\"", 4) == 0 &&
            strncmp(p + 4, id_value, id_len) == 0 &&
            p[4 + id_len] == '"') {
            return 1;
        }
        if (strncmp(p, "id='", 4) == 0 &&
            strncmp(p + 4, id_value, id_len) == 0 &&
            p[4 + id_len] == '\'') {
            return 1;
        }
    }

    return 0;
}

static const char *find_div_with_id(const char *html, const char *id_value) {
    const char *p = html;

    if (!html || !id_value || !id_value[0]) {
        return NULL;
    }

    while ((p = strstr(p, "<div")) != NULL) {
        const char *tag_end = strchr(p, '>');

        if (!tag_end) {
            return NULL;
        }
        if (tag_has_id(p, tag_end, id_value)) {
            return p;
        }
        p = tag_end + 1;
    }

    return NULL;
}

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

static char *extract_article_content_html(const char *html) {
    const char *article_content = find_div_with_id(html, "js_content");

    if (!article_content) {
        article_content = find_div_with_id(html, "mpDetailContent");
    }

    return extract_div_inner_html(article_content);
}

static int reader_build_mpdetail_target(ApiContext *ctx, const char *review_id, int font_size,
                                        char **target_out) {
    char *escaped = NULL;
    char *target = NULL;

    if (!ctx || !review_id || !*review_id || !target_out) {
        return -1;
    }

    escaped = api_escape(ctx, review_id);
    if (!escaped) {
        return -1;
    }

    target = malloc(strlen(WEREAD_BASE_URL) + strlen("/mpdetail?reviewId=&fs=") +
                    strlen(escaped) + 12);
    if (!target) {
        free(escaped);
        return -1;
    }

    sprintf(target, "%s/mpdetail?reviewId=%s&fs=%d", WEREAD_BASE_URL, escaped, font_size);
    free(escaped);
    *target_out = target;
    return 0;
}

static void reader_log_article_page_markers(const char *target, const char *html) {
    const char *marker;
    const char *value_start;
    const char *value_end;
    char page_mid[128];
    int has_mpdetail = 0;

    if (!target || !html) {
        return;
    }

    marker = strstr(html, "window.PAGE_MID=");
    page_mid[0] = '\0';
    if (marker) {
        marker += strlen("window.PAGE_MID=");
        if (*marker == '"' || *marker == '\'') {
            char quote = *marker++;
            size_t len;

            value_start = marker;
            value_end = strchr(value_start, quote);
            if (value_end && value_end > value_start) {
                len = (size_t)(value_end - value_start);
                if (len >= sizeof(page_mid)) {
                    len = sizeof(page_mid) - 1;
                }
                memcpy(page_mid, value_start, len);
                page_mid[len] = '\0';
            }
        }
    }

    has_mpdetail = strstr(html, "mpDetailContent") != NULL;
    fprintf(stderr,
            "reader-article-page: target=%s pageMid=%s hasMpDetail=%d\n",
            target,
            page_mid[0] ? page_mid : "(missing)",
            has_mpdetail);
}

static int reader_parse_article_catalog(ApiContext *ctx, const char *html,
                                        const char *article_block_start,
                                        const char *article_block_end,
                                        int font_size,
                                        const char *current_review_id,
                                        ReaderCatalogItem **items_out,
                                        int *count_out,
                                        int *current_index_out) {
    char *catalog_array = NULL;
    ReaderCatalogItem *items = NULL;
    int count = 0;
    int cap = 0;
    int current_index = -1;
    int rc = 0;
    const char *cursor;
    const char *end;

    if (items_out) {
        *items_out = NULL;
    }
    if (count_out) {
        *count_out = 0;
    }
    if (current_index_out) {
        *current_index_out = -1;
    }
    if (!ctx || !html || !article_block_start || !article_block_end ||
        !items_out || !count_out) {
        return -1;
    }

    catalog_array = reader_extract_container_from_slice(article_block_start,
                                                        article_block_end,
                                                        "mpChaptersInfo:",
                                                        '[', ']');
    if (!catalog_array || strcmp(catalog_array, "[]") == 0) {
        rc = 0;
        goto cleanup;
    }

    cursor = catalog_array + 1;
    end = catalog_array + strlen(catalog_array) - 1;
    while (cursor < end) {
        const char *obj_end;
        ReaderCatalogItem item = {0};
        ReaderCatalogItem *tmp;
        char *review_id = NULL;
        char *mp_info = NULL;
        int raw_idx;

        cursor = reader_skip_ws(cursor, end);
        if (cursor >= end) {
            break;
        }
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor != '{') {
            break;
        }

        obj_end = reader_find_matching_pair(cursor, end, '{', '}');
        if (!obj_end) {
            rc = -1;
            goto cleanup;
        }

        review_id = reader_extract_resolved_from_slice(html, cursor, obj_end + 1,
                                                       "reviewId:");
        mp_info = reader_extract_container_from_slice(cursor, obj_end + 1,
                                                      "mpInfo:", '{', '}');
        if (mp_info) {
            item.title = reader_extract_resolved_from_slice(
                html, mp_info, mp_info + strlen(mp_info), "title:");
        }
        if (!item.title) {
            item.title = reader_extract_resolved_from_slice(html, cursor, obj_end + 1,
                                                            "title:");
        }

        raw_idx = reader_extract_int_from_slice_resolved(html, cursor, obj_end + 1,
                                                         "idx:", -1);
        item.chapter_idx = raw_idx >= 0 ? raw_idx + 1 : count + 1;
        item.is_current = reader_parse_bool_from_slice_resolved(html, cursor, obj_end + 1,
                                                                "isCurrent:", 0);
        if (!item.is_current && current_review_id && review_id &&
            strcmp(current_review_id, review_id) == 0) {
            item.is_current = 1;
        }
        item.chapter_uid = review_id;
        review_id = NULL;
        if (item.chapter_uid) {
            if (reader_build_mpdetail_target(ctx, item.chapter_uid, font_size,
                                             &item.target) != 0) {
                free(item.chapter_uid);
                free(item.title);
                item.chapter_uid = NULL;
                item.title = NULL;
            }
        }

        if (item.chapter_uid && item.target && item.title) {
            if (count >= cap) {
                int new_cap = cap > 0 ? cap * 2 : 16;

                tmp = realloc(items, sizeof(*items) * (size_t)new_cap);
                if (!tmp) {
                    free(item.chapter_uid);
                    free(item.target);
                    free(item.title);
                    rc = -1;
                    free(mp_info);
                    goto cleanup;
                }
                items = tmp;
                cap = new_cap;
            }
            if (item.is_current) {
                current_index = count;
            }
            items[count++] = item;
        } else {
            free(item.chapter_uid);
            free(item.target);
            free(item.title);
        }

        free(mp_info);
        cursor = obj_end + 1;
    }

cleanup:
    if (rc == 0) {
        *items_out = items;
        *count_out = count;
        if (current_index_out) {
            *current_index_out = current_index;
        }
        items = NULL;
        count = 0;
    }
    reader_catalog_items_free(items, count);
    free(catalog_array);
    return rc;
}

int reader_extract_article_review_id_from_reader_shell(const char *html,
                                                       char **review_id_out) {
    const char *prog_start = NULL;
    const char *prog_end = NULL;
    char *book_id = NULL;
    char *author = NULL;
    char *review_id = NULL;
    char *book_block = NULL;
    int book_type;
    int rc = 0;

    if (!html || !review_id_out) {
        return 0;
    }

    book_id = reader_extract_resolved_value_in_object_block(html, "reader:{bookId:",
                                                            "bookId:");
    author = reader_extract_resolved_value_in_object_block(html, "bookInfo:{",
                                                           "author:");
    book_type = reader_extract_int_in_object_block(html, "bookInfo:{", "type:", 0);

    if (!(book_type == 3 ||
          (book_id && strncmp(book_id, "MP", 2) == 0) ||
          (author && strcmp(author, "公众号") == 0))) {
        goto cleanup;
    }

    if (reader_find_named_object_block(html, "progress:{bookId:",
                                       &prog_start, &prog_end) != 0) {
        goto cleanup;
    }

    book_block = reader_extract_container_from_slice(prog_start, prog_end,
                                                     "book:", '{', '}');
    if (!book_block) {
        goto cleanup;
    }
    review_id = reader_extract_resolved_from_slice(html, book_block,
                                                   book_block + strlen(book_block),
                                                   "reviewId:");
    if (!review_id || !review_id[0]) {
        goto cleanup;
    }

    *review_id_out = review_id;
    review_id = NULL;
    rc = 1;

cleanup:
    free(book_id);
    free(author);
    free(review_id);
    free(book_block);
    return rc;
}

int reader_load_article_from_review_id(ApiContext *ctx, const char *review_id,
                                       int font_size, ReaderDocument *doc) {
    Buffer article_buf = {0};
    char *article_target = NULL;
    int rc = -1;

    if (!ctx || !review_id || !*review_id || !doc) {
        return -1;
    }

    if (reader_build_mpdetail_target(ctx, review_id, font_size, &article_target) != 0) {
        return -1;
    }

    if (reader_fetch_page(ctx, article_target, &article_buf) != 0) {
        fprintf(stderr,
                "reader-load: mpdetail fetch failed reviewId=%s target=%s\n",
                review_id, article_target);
        goto cleanup;
    }

    rc = reader_parse_article_document(ctx, article_target, font_size,
                                       article_buf.data, doc);
    if (rc == 0) {
        fprintf(stderr,
                "reader-load: article shell resolved reviewId=%s target=%s title=%s\n",
                review_id, article_target,
                doc->book_title ? doc->book_title : "(null)");
    } else {
        fprintf(stderr,
                "reader-load: article shell parse failed reviewId=%s target=%s\n",
                review_id, article_target);
    }

cleanup:
    api_buffer_free(&article_buf);
    free(article_target);
    return rc;
}

int reader_parse_article_document(ApiContext *ctx, const char *target, int font_size,
                                  const char *html, ReaderDocument *doc) {
    const char *article_block_start = NULL;
    const char *article_block_end = NULL;
    char *content_html = NULL;
    char *current_review_id = NULL;
    char *status_value = NULL;
    int current_catalog_index = -1;
    int has_mpdetail_content = 0;
    int rc = -1;

    if (!ctx || !target || !html || !doc) {
        return -1;
    }

    doc->kind = READER_DOCUMENT_KIND_ARTICLE;
    doc->catalog_total_count = 0;
    doc->catalog_range_start = 0;
    doc->catalog_range_end = 0;
    doc->catalog_count = 0;
    doc->catalog_items = NULL;
    reader_log_article_page_markers(target, html);
    has_mpdetail_content = strstr(html, "mpDetailContent") != NULL;
    if (!has_mpdetail_content &&
        (strstr(html, "PAGE_MID='mmbizwap:secitptpage/verify.html'") != NULL ||
         strstr(html, "PAGE_MID=\"mmbizwap:secitptpage/verify.html\"") != NULL)) {
        fprintf(stderr, "reader-article-verify-page: target=%s\n", target);
        goto cleanup;
    }
    if (reader_find_named_object_block(html, "mpdetail:{status:",
                                       &article_block_start, &article_block_end) != 0) {
        fprintf(stderr,
                "reader-article-parse: mpdetail block missing target=%s\n",
                target);
        goto cleanup;
    }

    status_value = reader_extract_resolved_from_slice(html, article_block_start,
                                                      article_block_end, "status:");
    if (!status_value || !status_value[0] ||
        strcmp(status_value, "-1") == 0 ||
        strcmp(status_value, "null") == 0) {
        fprintf(stderr,
                "reader-article-parse: invalid status target=%s status=%s\n",
                target, status_value ? status_value : "(null)");
        goto cleanup;
    }

    doc->book_title = reader_extract_resolved_from_slice(html, article_block_start,
                                                         article_block_end, "bookTitle:");
    if (!doc->book_title || !doc->book_title[0]) {
        free(doc->book_title);
        doc->book_title =
            reader_extract_attr_text(html, "<p class=\"mpCatalog_bookTitle\">");
    }
    if (doc->book_title && doc->book_title[0]) {
        doc->chapter_title = reader_dup_or_null(doc->book_title);
    }

    content_html = extract_article_content_html(html);
    if (!content_html || !content_html[0]) {
        fprintf(stderr,
                "reader-article-parse: content html missing target=%s\n",
                target);
        goto cleanup;
    }

    doc->content_text = html_strip_to_text(content_html);
    doc->target = strdup(target);
    doc->chapter_max_offset = reader_extract_max_wco(content_html);
    if (reader_extract_wco_values(content_html, &doc->chapter_offsets,
                                  &doc->chapter_offset_count) != 0) {
        goto cleanup;
    }
    doc->font_size = font_size;
    if (!doc->content_text || !doc->content_text[0] || !doc->target) {
        fprintf(stderr,
                "reader-article-parse: content text missing target=%s\n",
                target);
        goto cleanup;
    }
    doc->use_content_font = 0;
    doc->content_font_path[0] = '\0';
    current_review_id = reader_extract_resolved_from_slice(
        html, article_block_start, article_block_end, "reviewId:");
    if ((!current_review_id || !current_review_id[0]) && target) {
        free(current_review_id);
        current_review_id = reader_extract_query_value(target, "reviewId");
    }
    if (reader_parse_article_catalog(ctx, html, article_block_start,
                                     article_block_end, font_size,
                                     current_review_id, &doc->catalog_items,
                                     &doc->catalog_count,
                                     &current_catalog_index) != 0) {
        fprintf(stderr,
                "reader-article-parse: catalog parse failed target=%s\n",
                target);
        goto cleanup;
    }
    if (doc->catalog_count > 0) {
        ReaderCatalogItem *current_item = NULL;

        doc->catalog_total_count = doc->catalog_count;
        doc->catalog_range_start = 1;
        doc->catalog_range_end = doc->catalog_count;
        if (current_catalog_index < 0 && current_review_id) {
            for (int i = 0; i < doc->catalog_count; i++) {
                if (doc->catalog_items[i].chapter_uid &&
                    strcmp(doc->catalog_items[i].chapter_uid,
                           current_review_id) == 0) {
                    current_catalog_index = i;
                    doc->catalog_items[i].is_current = 1;
                    break;
                }
            }
        }
        if (current_catalog_index >= 0 &&
            current_catalog_index < doc->catalog_count) {
            current_item = &doc->catalog_items[current_catalog_index];
        }
        if (current_item) {
            free(doc->chapter_uid);
            doc->chapter_uid = reader_dup_or_null(current_item->chapter_uid);
            doc->chapter_idx = current_item->chapter_idx;
            if (current_item->title && current_item->title[0]) {
                free(doc->chapter_title);
                doc->chapter_title = reader_dup_or_null(current_item->title);
            }
            if (current_catalog_index > 0) {
                doc->prev_target = reader_dup_or_null(
                    doc->catalog_items[current_catalog_index - 1].target);
            }
            if (current_catalog_index + 1 < doc->catalog_count) {
                doc->next_target = reader_dup_or_null(
                    doc->catalog_items[current_catalog_index + 1].target);
            }
        }
    } else if (current_review_id && current_review_id[0]) {
        doc->chapter_uid = reader_dup_or_null(current_review_id);
    }

    rc = 0;

cleanup:
    free(content_html);
    free(current_review_id);
    free(status_value);
    return rc;
}
