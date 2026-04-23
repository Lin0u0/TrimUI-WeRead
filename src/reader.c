#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reader.h"
#include "reader_internal.h"
#include "reader_state.h"

void reader_document_free(ReaderDocument *doc) {
    if (!doc) {
        return;
    }
    free(doc->target);
    free(doc->prev_target);
    free(doc->next_target);
    free(doc->book_id);
    free(doc->token);
    free(doc->chapter_uid);
    free(doc->progress_chapter_uid);
    free(doc->progress_summary);
    free(doc->book_title);
    free(doc->chapter_title);
    free(doc->content_text);
    free(doc->chapter_offsets);
    reader_catalog_items_free(doc->catalog_items, doc->catalog_count);
    memset(doc, 0, sizeof(*doc));
}

static int reader_load_internal(ApiContext *ctx, const char *target, int font_size,
                                ReaderDocument *doc, int save_last_reader) {
    Buffer buf = {0};
    char *url = reader_build_url(ctx, target, font_size);
    char *article_review_id = NULL;
    int rc = -1;
    int is_article_target = reader_target_is_article(target);

    if (!url || !doc) {
        free(url);
        return -1;
    }

    fprintf(stderr,
            "reader-load: target=%s url=%s font=%d saveLast=%d article=%d\n",
            target, url, font_size, save_last_reader, is_article_target);
    memset(doc, 0, sizeof(*doc));
    if (reader_fetch_page(ctx, url, &buf) != 0) {
        fprintf(stderr, "reader-load: fetch failed url=%s\n", url);
        goto cleanup;
    }

    doc->kind = is_article_target ? READER_DOCUMENT_KIND_ARTICLE
                                  : READER_DOCUMENT_KIND_BOOK;
    if (is_article_target) {
        rc = reader_parse_article_document(ctx, target, font_size, buf.data, doc);
        if (rc == 0) {
            fprintf(stderr,
                    "reader-load: article parsed target=%s docTarget=%s title=%s\n",
                    target,
                    doc->target ? doc->target : "(null)",
                    doc->book_title ? doc->book_title : "(null)");
        } else {
            fprintf(stderr, "reader-load: article parse failed target=%s\n", target);
        }
        goto cleanup;
    }

    if (reader_extract_article_review_id_from_reader_shell(buf.data, &article_review_id) == 1) {
        fprintf(stderr,
                "reader-load: article reader shell detected target=%s reviewId=%s\n",
                target, article_review_id);
        rc = reader_load_article_from_review_id(ctx, article_review_id, font_size, doc);
        free(article_review_id);
        article_review_id = NULL;
        goto cleanup;
    }

    rc = reader_load_book_document(ctx, target, font_size, buf.data, doc);
    if (rc != 0) {
        goto cleanup;
    }

    rc = 0;
    if (save_last_reader && doc->kind == READER_DOCUMENT_KIND_BOOK) {
        reader_state_save_last_reader(ctx, doc->target, font_size, 36);
    }

cleanup:
    if (rc != 0) {
        reader_document_free(doc);
    }
    api_buffer_free(&buf);
    free(article_review_id);
    free(url);
    return rc;
}

int reader_load(ApiContext *ctx, const char *target, int font_size, ReaderDocument *doc) {
    return reader_load_internal(ctx, target, font_size, doc, 1);
}

int reader_prefetch(ApiContext *ctx, const char *target, int font_size, ReaderDocument *doc) {
    return reader_load_internal(ctx, target, font_size, doc, 0);
}
