#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "reader_internal.h"
#include "json.h"

int reader_estimate_chapter_offset(const ReaderDocument *doc, int current_page,
                                   int total_pages) {
    int total_pages_safe = total_pages > 0 ? total_pages : 1;

    if (!doc) {
        return 0;
    }

    if (current_page < 0) {
        current_page = 0;
    }
    if (current_page >= total_pages_safe) {
        current_page = total_pages_safe - 1;
    }

    if (doc->chapter_offset_count > 0 && doc->chapter_offsets) {
        int sample_index =
            (int)(((long long)doc->chapter_offset_count * current_page) /
                  total_pages_safe);
        if (sample_index < 0) {
            sample_index = 0;
        }
        if (sample_index >= doc->chapter_offset_count) {
            sample_index = doc->chapter_offset_count - 1;
        }
        return doc->chapter_offsets[sample_index];
    }

    if (doc->chapter_max_offset > 0) {
        return (int)(((long long)doc->chapter_max_offset * current_page) /
                     total_pages_safe);
    }

    return 0;
}

int reader_report_progress_at_offset(ApiContext *ctx, const ReaderDocument *doc,
                                     int current_page, int total_pages,
                                     int reading_seconds,
                                     const char *page_summary,
                                     int compute_progress,
                                     int chapter_offset_override) {
    char *url;
    Buffer buf = {0};
    char *body = NULL;
    cJSON *payload = NULL;
    cJSON *resp_json = NULL;
    cJSON *succ = NULL;
    cJSON *timeout = NULL;
    const char *summary = NULL;
    int offset = 0;
    int progress = 0;
    int total_pages_safe = total_pages > 0 ? total_pages : 1;
    long long now_ms = (long long)time(NULL) * 1000LL;
    long long now_seconds = (long long)time(NULL);
    int random_value = rand() % 1000;

    if (!ctx || !doc || !doc->book_id || !doc->token ||
        !doc->chapter_uid || doc->chapter_idx <= 0) {
        return READER_REPORT_ERROR;
    }

    if (reading_seconds < 0) {
        reading_seconds = 0;
    }
    if (reading_seconds > 60) {
        reading_seconds = 60;
    }

    offset = chapter_offset_override >= 0 ?
        chapter_offset_override :
        reader_estimate_chapter_offset(doc, current_page, total_pages_safe);

    if (!compute_progress) {
        progress = doc->last_reported_progress;
    } else if (doc->total_words > 0) {
        int chapter_progress_words = 0;

        if (doc->chapter_word_count > 0) {
            chapter_progress_words =
                (int)(((long long)doc->chapter_word_count * current_page) /
                      total_pages_safe);
        }
        progress = (int)(((long long)
                         (doc->prev_chapters_word_count + chapter_progress_words) * 100) /
                         doc->total_words);
        if (progress < 0) {
            progress = 0;
        }
        if (progress > 100) {
            progress = 100;
        }
    } else {
        progress = doc->last_reported_progress;
    }

    summary = reader_choose_summary(doc, page_summary);

    payload = cJSON_CreateObject();
    if (!payload) {
        return READER_REPORT_ERROR;
    }
    cJSON_AddStringToObject(payload, "b", doc->book_id);
    reader_add_chapter_uid_field(payload, doc->chapter_uid);
    cJSON_AddNumberToObject(payload, "ci", doc->chapter_idx);
    cJSON_AddNumberToObject(payload, "co", offset);
    cJSON_AddStringToObject(payload, "sm", summary);
    cJSON_AddNumberToObject(payload, "pr", progress);
    cJSON_AddNumberToObject(payload, "rt", reading_seconds);
    cJSON_AddNumberToObject(payload, "ts", (double)now_ms);
    cJSON_AddNumberToObject(payload, "rn", random_value);
    cJSON_AddStringToObject(payload, "tk", doc->token);
    cJSON_AddNumberToObject(payload, "ct", (double)now_seconds);
    body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    payload = NULL;
    if (!body) {
        return READER_REPORT_ERROR;
    }

    url = strdup(WEREAD_API_BASE_URL "/bookread");
    if (!url) {
        free(body);
        return READER_REPORT_ERROR;
    }

    if (api_post(ctx, url, body, &buf) != 0) {
        fprintf(stderr, "Failed to report reading progress.\n");
        free(url);
        free(body);
        return READER_REPORT_ERROR;
    }

    resp_json = buf.data ? cJSON_Parse(buf.data) : NULL;
    if (!resp_json) {
        fprintf(stderr, "Reading progress response was invalid.\n");
        api_buffer_free(&buf);
        free(url);
        free(body);
        return READER_REPORT_ERROR;
    }

    timeout = cJSON_GetObjectItem(resp_json, "sessionTimeout");
    if (timeout && cJSON_IsNumber(timeout) && timeout->valueint != 0) {
        cJSON_Delete(resp_json);
        api_buffer_free(&buf);
        free(url);
        free(body);
        return READER_REPORT_SESSION_EXPIRED;
    }

    succ = json_get_path(resp_json, "data.succ");
    if (!json_is_truthy(succ)) {
        cJSON *err = cJSON_GetObjectItem(resp_json, "errMsg");

        fprintf(stderr, "bookread: server rejected: %s\n",
                (err && cJSON_IsString(err)) ? err->valuestring : "(unknown)");
        cJSON_Delete(resp_json);
        api_buffer_free(&buf);
        free(url);
        free(body);
        return READER_REPORT_ERROR;
    }

    cJSON_Delete(resp_json);
    api_buffer_free(&buf);
    free(url);
    free(body);
    return READER_REPORT_OK;
}

int reader_report_progress(ApiContext *ctx, const ReaderDocument *doc, int current_page,
                           int total_pages, int reading_seconds,
                           const char *page_summary, int compute_progress) {
    return reader_report_progress_at_offset(ctx, doc, current_page, total_pages,
                                            reading_seconds, page_summary,
                                            compute_progress, -1);
}
