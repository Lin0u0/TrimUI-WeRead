#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "preferences_state.h"
#include "reader_state.h"
#include "reader_service.h"

static ReaderServiceOps reader_service_test_ops = {0};

static char *reader_service_dup_or_null(const char *value) {
    if (!value) {
        return NULL;
    }
    return strdup(value);
}

static int reader_service_target_is_article(const char *target) {
    if (!target || !*target) {
        return 0;
    }
    return strstr(target, "mpdetail?") != NULL || strstr(target, "/mpdetail") != NULL;
}

static int reader_service_document_supports_local_position(const ReaderDocument *doc) {
    return doc &&
        doc->kind == READER_DOCUMENT_KIND_BOOK &&
        doc->book_id && doc->book_id[0];
}

static int reader_service_load_document(ApiContext *ctx, const char *target, int font_size,
                                        ReaderDocument *doc) {
    if (reader_service_test_ops.load) {
        return reader_service_test_ops.load(ctx, target, font_size, doc);
    }
    return reader_load(ctx, target, font_size, doc);
}

static char *reader_service_lookup_chapter_target(ApiContext *ctx, const char *book_id,
                                                  const char *chapter_uid, int chapter_idx) {
    if (reader_service_test_ops.find_chapter_target) {
        return reader_service_test_ops.find_chapter_target(ctx, book_id, chapter_uid, chapter_idx);
    }
    return reader_find_chapter_target(ctx, book_id, chapter_uid, chapter_idx);
}

void reader_service_set_test_ops(const ReaderServiceOps *ops) {
    if (ops) {
        reader_service_test_ops = *ops;
    } else {
        memset(&reader_service_test_ops, 0, sizeof(reader_service_test_ops));
    }
}

static const char *reader_service_find_progress_target(const ReaderDocument *doc) {
    int i;

    if (!doc || ((!doc->progress_chapter_uid && doc->progress_chapter_idx <= 0) ||
                 !doc->catalog_items || doc->catalog_count <= 0)) {
        return NULL;
    }
    for (i = 0; i < doc->catalog_count; i++) {
        int matches_progress = 0;

        if (doc->progress_chapter_uid &&
            doc->catalog_items[i].chapter_uid &&
            strcmp(doc->catalog_items[i].chapter_uid, doc->progress_chapter_uid) == 0) {
            matches_progress = 1;
        } else if (doc->progress_chapter_idx > 0 &&
                   doc->catalog_items[i].chapter_idx == doc->progress_chapter_idx) {
            matches_progress = 1;
        }
        if (matches_progress) {
            if (doc->target && doc->catalog_items[i].target &&
                strcmp(doc->target, doc->catalog_items[i].target) == 0) {
                return NULL;
            }
            return doc->catalog_items[i].target;
        }
    }
    return NULL;
}

static int reader_service_has_cloud_position(const ReaderDocument *doc) {
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
    return doc->saved_chapter_offset > 0;
}

/*
 * Phase 4 maintainer note: reader_service_prepare_open_document() decides
 * cloud progress versus local saved position precedence. Cloud progress wins
 * when present; local saved position is only applied when cloud progress is
 * absent or the saved chapter is newer than the freshly loaded document.
 */
int reader_service_prepare_open_document(ApiContext *ctx, const char *source_target,
                                         const char *book_id_hint, int font_size,
                                         ReaderOpenResult *result) {
    ReaderDocument doc = {0};
    ReaderDocument saved_doc = {0};
    char saved_target[2048];
    char saved_source_target[2048];
    int saved_page = 0;
    int saved_offset = 0;
    int saved_content_font_size = 36;
    int has_local_position = 0;
    int has_cloud_position = 0;
    int replace_with_saved_doc = 0;
    int initial_page = 0;
    int honor_saved_position = 1;
    int rc = -1;
    int source_is_article = reader_service_target_is_article(source_target);

    if (!ctx || !source_target || !*source_target || !result) {
        return -1;
    }

    memset(result, 0, sizeof(*result));
    snprintf(result->source_target, sizeof(result->source_target), "%s", source_target);
    fprintf(stderr,
            "reader-service-open: begin source=%s bookIdHint=%s font=%d article=%d\n",
            source_target, book_id_hint && *book_id_hint ? book_id_hint : "(null)",
            font_size, source_is_article);

    if (!source_is_article &&
        book_id_hint && *book_id_hint &&
        reader_state_load_position(ctx, book_id_hint, source_target,
                                   saved_target, sizeof(saved_target),
                                   NULL, &saved_content_font_size,
                                   &saved_page, &saved_offset) == 0) {
        has_local_position = 1;
        fprintf(stderr,
                "reader-service-open: local-position by source bookId=%s savedTarget=%s page=%d offset=%d font=%d\n",
                book_id_hint, saved_target, saved_page, saved_offset, saved_content_font_size);
    }
    if (!has_local_position) {
        (void)preferences_state_load_reader_font_size(ctx, &saved_content_font_size);
    }

    if (reader_service_load_document(ctx, source_target, font_size, &doc) != 0) {
        fprintf(stderr, "reader-service-open: load failed source=%s\n", source_target);
        goto cleanup;
    }
    fprintf(stderr,
            "reader-service-open: loaded kind=%s docTarget=%s bookId=%s chapterUid=%s chapterIdx=%d progressUid=%s progressIdx=%d savedOffset=%d\n",
            doc.kind == READER_DOCUMENT_KIND_ARTICLE ? "article" : "book",
            doc.target ? doc.target : "(null)",
            doc.book_id ? doc.book_id : "(null)",
            doc.chapter_uid ? doc.chapter_uid : "(null)",
            doc.chapter_idx,
            doc.progress_chapter_uid ? doc.progress_chapter_uid : "(null)",
            doc.progress_chapter_idx,
            doc.saved_chapter_offset);

    if (doc.kind == READER_DOCUMENT_KIND_ARTICLE) {
        has_local_position = 0;
        saved_page = 0;
        saved_offset = 0;
        saved_content_font_size = 36;
        (void)preferences_state_load_reader_font_size(ctx, &saved_content_font_size);
        source_is_article = 1;
        fprintf(stderr,
                "reader-service-open: article document disables book position source=%s docTarget=%s font=%d\n",
                source_target,
                doc.target ? doc.target : "(null)",
                saved_content_font_size);
    }

    {
        const char *progress_target = reader_service_find_progress_target(&doc);
        char *fetched_target = NULL;

        if (!progress_target &&
            doc.book_id &&
            (doc.progress_chapter_idx > 0 ||
             (doc.progress_chapter_uid && strcmp(doc.progress_chapter_uid, "0") != 0))) {
            fetched_target = reader_service_lookup_chapter_target(ctx, doc.book_id,
                                                                  doc.progress_chapter_uid,
                                                                  doc.progress_chapter_idx);
            if (fetched_target && (!doc.target || strcmp(fetched_target, doc.target) != 0)) {
                progress_target = fetched_target;
            }
        }
        if (progress_target) {
            ReaderDocument progress_doc = {0};

            fprintf(stderr, "reader-service-open: follow progress target=%s\n", progress_target);
            if (reader_service_load_document(ctx, progress_target, font_size, &progress_doc) != 0) {
                fprintf(stderr, "reader-service-open: progress load failed target=%s\n",
                        progress_target);
                free(fetched_target);
                goto cleanup;
            }
            reader_document_free(&doc);
            doc = progress_doc;
        }
        free(fetched_target);
    }

    has_cloud_position = reader_service_has_cloud_position(&doc);

    if (!has_cloud_position &&
        reader_service_document_supports_local_position(&doc) &&
        reader_state_load_position_by_book_id(ctx, doc.book_id,
                                              saved_source_target, sizeof(saved_source_target),
                                              saved_target, sizeof(saved_target),
                                              NULL, &saved_content_font_size,
                                              &saved_page, &saved_offset) == 0) {
        has_local_position = 1;
        snprintf(result->source_target, sizeof(result->source_target), "%s",
                 saved_source_target);
        fprintf(stderr,
                "reader-service-open: local-position by bookId bookId=%s savedSource=%s savedTarget=%s page=%d offset=%d font=%d\n",
                doc.book_id ? doc.book_id : "(null)",
                saved_source_target, saved_target, saved_page, saved_offset,
                saved_content_font_size);
    }

    if (!has_cloud_position && has_local_position &&
        doc.target && strcmp(saved_target, doc.target) == 0) {
        initial_page = saved_page;
        honor_saved_position = 0;
    } else if (!has_cloud_position && has_local_position) {
        if (reader_service_load_document(ctx, saved_target, font_size, &saved_doc) == 0) {
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

    result->doc = doc;
    memset(&doc, 0, sizeof(doc));
    result->content_font_size = saved_content_font_size;
    result->initial_page = initial_page;
    result->initial_offset = saved_offset;
    result->honor_saved_position = honor_saved_position;
    fprintf(stderr,
            "reader-service-open: ready source=%s finalSource=%s finalDocTarget=%s bookId=%s initialPage=%d initialOffset=%d honorSaved=%d contentFont=%d cloud=%d local=%d\n",
            source_target,
            result->source_target,
            result->doc.target ? result->doc.target : "(null)",
            result->doc.book_id ? result->doc.book_id : "(null)",
            result->initial_page,
            result->initial_offset,
            result->honor_saved_position,
            result->content_font_size,
            has_cloud_position,
            has_local_position);
    rc = 0;

cleanup:
    if (rc != 0) {
        fprintf(stderr,
                "reader-service-open: failed source=%s bookIdHint=%s\n",
                source_target,
                book_id_hint && *book_id_hint ? book_id_hint : "(null)");
    }
    reader_document_free(&doc);
    reader_document_free(&saved_doc);
    return rc;
}

int reader_service_copy_report_document(ReaderDocument *dst, const ReaderDocument *src) {
    memset(dst, 0, sizeof(*dst));
    dst->kind = src->kind;
    dst->book_id = reader_service_dup_or_null(src->book_id);
    dst->token = reader_service_dup_or_null(src->token);
    dst->chapter_uid = reader_service_dup_or_null(src->chapter_uid);
    dst->progress_chapter_uid = reader_service_dup_or_null(src->progress_chapter_uid);
    dst->progress_summary = reader_service_dup_or_null(src->progress_summary);
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
        memcpy(dst->chapter_offsets, src->chapter_offsets,
               sizeof(int) * src->chapter_offset_count);
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

void reader_service_save_local_position(ApiContext *ctx, const ReaderDocument *doc,
                                        const char *source_target, int content_font_size,
                                        int current_page, int current_offset) {
    if (!ctx || !reader_service_document_supports_local_position(doc) ||
        !doc->target || !source_target || !source_target[0]) {
        return;
    }

    reader_state_save_position(ctx, doc->book_id, source_target, doc->target,
                               doc->font_size, content_font_size, current_page,
                               current_offset);
    reader_state_save_last_reader(ctx, source_target, doc->font_size, content_font_size);
}

int reader_service_report_progress(ApiContext *ctx, const ReaderDocument *doc, int current_page,
                                   int total_pages, int chapter_offset,
                                   int reading_seconds, const char *page_summary,
                                   int compute_progress) {
    if (!ctx || !doc) {
        return READER_REPORT_ERROR;
    }
    if (doc->kind != READER_DOCUMENT_KIND_BOOK ||
        !doc->book_id || !doc->token || !doc->chapter_uid) {
        return READER_REPORT_OK;
    }
    return reader_report_progress_at_offset(ctx, doc, current_page, total_pages,
                                            reading_seconds, page_summary,
                                            compute_progress, chapter_offset);
}

int reader_service_report_progress_with_retry(const char *data_dir, const char *ca_file,
                                              const ReaderDocument *doc, int current_page,
                                              int total_pages, int chapter_offset,
                                              int reading_seconds,
                                              const char *page_summary,
                                              int compute_progress) {
    ApiContext ctx;
    int result;

    if (!data_dir || !doc) {
        return -1;
    }
    if (api_init(&ctx, data_dir) != 0) {
        return -1;
    }
    if (ca_file) {
        snprintf(ctx.ca_file, sizeof(ctx.ca_file), "%s", ca_file);
    }

    result = reader_service_report_progress(&ctx, doc, current_page, total_pages,
                                            chapter_offset, reading_seconds,
                                            page_summary, compute_progress);
    if (result != 0) {
        result = reader_service_report_progress(&ctx, doc, current_page, total_pages,
                                                chapter_offset, reading_seconds,
                                                page_summary, compute_progress);
    }
    api_cleanup(&ctx);
    return result;
}

int reader_service_print(ApiContext *ctx, const char *target, int font_size) {
#if !HAVE_SDL
    const char *override_output = getenv("WEREAD_TEST_READER_OUTPUT");

    if (override_output) {
        printf("%s\n", override_output);
        return 0;
    }
#endif
    ReaderDocument doc;

    if (reader_load(ctx, target, font_size, &doc) != 0) {
        return -1;
    }

    if (doc.book_title) {
        printf("%s\n", doc.book_title);
    }
    if (doc.chapter_title) {
        printf("%s\n\n", doc.chapter_title);
    }
    printf("%s", doc.content_text);
    reader_document_free(&doc);
    return 0;
}

int reader_service_resume(ApiContext *ctx) {
    char target[2048];
    int font_size = 3;

    if (reader_state_load_last_reader(ctx, target, sizeof(target), &font_size, NULL) != 0) {
        fprintf(stderr, "No saved reader state found in %s\n", ctx->state_dir);
        return -1;
    }
    return reader_service_print(ctx, target, font_size);
}
