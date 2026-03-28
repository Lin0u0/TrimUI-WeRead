#ifndef READER_H
#define READER_H

#include "api.h"

typedef struct {
    char *chapter_uid;
    char *target;
    char *title;
    int chapter_idx;
    int word_count;
    int level;
    int is_current;
    int is_lock;
} ReaderCatalogItem;

typedef struct {
    char *target;
    char *prev_target;
    char *next_target;
    char *book_id;
    char *token;
    char *chapter_uid;
    char *progress_chapter_uid;
    char *progress_summary;
    char *book_title;
    char *chapter_title;
    char *content_text;
    int chapter_idx;
    int progress_chapter_idx;
    int total_words;
    int chapter_word_count;
    int prev_chapters_word_count;
    int saved_chapter_offset;
    int chapter_max_offset;
    int last_reported_progress;
    int *chapter_offsets;
    int chapter_offset_count;
    int catalog_range_start;
    int catalog_range_end;
    int catalog_total_count;
    int font_size;
    int use_content_font;
    char content_font_path[512];
    ReaderCatalogItem *catalog_items;
    int catalog_count;
} ReaderDocument;

#define READER_REPORT_OK 0
#define READER_REPORT_ERROR (-1)
#define READER_REPORT_SESSION_EXPIRED (-2)

int reader_load(ApiContext *ctx, const char *target, int font_size, ReaderDocument *doc);
int reader_ensure_full_catalog(ApiContext *ctx, ReaderDocument *doc);
int reader_expand_catalog(ApiContext *ctx, ReaderDocument *doc, int direction, int *added_count);
int reader_estimate_chapter_offset(const ReaderDocument *doc, int current_page, int total_pages);
int reader_report_progress_at_offset(ApiContext *ctx, const ReaderDocument *doc, int current_page,
                                     int total_pages, int reading_seconds,
                                     const char *page_summary, int compute_progress,
                                     int chapter_offset_override);
int reader_report_progress(ApiContext *ctx, const ReaderDocument *doc, int current_page,
                           int total_pages, int reading_seconds, const char *page_summary,
                           int compute_progress);
char *reader_find_chapter_target(ApiContext *ctx, const char *book_id,
                                const char *chapter_uid, int chapter_idx);
void reader_document_free(ReaderDocument *doc);
int reader_print(ApiContext *ctx, const char *target, int font_size);
int reader_resume(ApiContext *ctx);

#endif
