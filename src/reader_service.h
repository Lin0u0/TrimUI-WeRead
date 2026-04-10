#ifndef READER_SERVICE_H
#define READER_SERVICE_H

#include "reader.h"

typedef struct {
    ReaderDocument doc;
    char source_target[2048];
    int content_font_size;
    int initial_page;
    int initial_offset;
    int honor_saved_position;
} ReaderOpenResult;

typedef struct {
    int (*load)(ApiContext *ctx, const char *target, int font_size, ReaderDocument *doc);
    char *(*find_chapter_target)(ApiContext *ctx, const char *book_id,
                                 const char *chapter_uid, int chapter_idx);
} ReaderServiceOps;

void reader_service_set_test_ops(const ReaderServiceOps *ops);
int reader_service_prepare_open_document(ApiContext *ctx, const char *source_target,
                                         const char *book_id_hint, int font_size,
                                         ReaderOpenResult *result);
int reader_service_copy_report_document(ReaderDocument *dst, const ReaderDocument *src);
void reader_service_save_local_position(ApiContext *ctx, const ReaderDocument *doc,
                                        const char *source_target, int content_font_size,
                                        int current_page, int current_offset);
int reader_service_report_progress(ApiContext *ctx, const ReaderDocument *doc, int current_page,
                                   int total_pages, int chapter_offset,
                                   int reading_seconds, const char *page_summary,
                                   int compute_progress);
int reader_service_report_progress_with_retry(const char *data_dir, const char *ca_file,
                                              const ReaderDocument *doc, int current_page,
                                              int total_pages, int chapter_offset,
                                              int reading_seconds,
                                              const char *page_summary,
                                              int compute_progress);
int reader_service_print(ApiContext *ctx, const char *target, int font_size);
int reader_service_resume(ApiContext *ctx);

#endif
