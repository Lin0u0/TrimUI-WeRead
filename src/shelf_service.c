#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"
#include "api.h"
#include "reader_state.h"
#include "shelf_service.h"
#include "shelf_state.h"

static ShelfServiceResolveReviewIdFn shelf_service_review_id_override = NULL;

static int shelf_service_init_worker_context(ApiContext *ctx, const char *data_dir,
                                             const char *ca_file) {
    if (!ctx || !data_dir) {
        return -1;
    }
    if (api_init(ctx, data_dir) != 0) {
        return -1;
    }
    if (ca_file && *ca_file) {
        snprintf(ctx->ca_file, sizeof(ctx->ca_file), "%s", ca_file);
    }
    return 0;
}

int shelf_service_print(ApiContext *ctx) {
    return shelf_print(ctx);
}

int shelf_service_print_cached(ApiContext *ctx) {
    return shelf_print_cached(ctx);
}

cJSON *shelf_service_load(ApiContext *ctx, int allow_cache, int *from_cache) {
    return shelf_load(ctx, allow_cache, from_cache);
}

int shelf_service_load_resume(ApiContext *ctx, char *target, size_t target_size,
                              int *font_size, int *content_font_size) {
    return reader_state_load_last_reader(ctx, target, target_size, font_size, content_font_size);
}

const char *shelf_service_reader_target(cJSON *urls, int index) {
    return shelf_reader_target(urls, index);
}

void shelf_service_set_review_id_override(ShelfServiceResolveReviewIdFn fn) {
    shelf_service_review_id_override = fn;
}

static int shelf_service_copy_review_id_from_node(cJSON *node,
                                                  char *review_id,
                                                  size_t review_id_size) {
    if (!node || !review_id || review_id_size == 0) {
        return -1;
    }
    if (cJSON_IsString(node) && node->valuestring && node->valuestring[0]) {
        snprintf(review_id, review_id_size, "%s", node->valuestring);
        return 0;
    }
    if (cJSON_IsNumber(node)) {
        snprintf(review_id, review_id_size, "%d", node->valueint);
        return 0;
    }
    return -1;
}

static int shelf_service_extract_review_id(cJSON *json,
                                           char *review_id,
                                           size_t review_id_size) {
    cJSON *data;
    cJSON *item;

    if (!json) {
        return -1;
    }
    item = cJSON_GetObjectItemCaseSensitive(json, "reviewId");
    if (shelf_service_copy_review_id_from_node(item, review_id, review_id_size) == 0) {
        return 0;
    }
    item = cJSON_GetObjectItemCaseSensitive(json, "id");
    if (shelf_service_copy_review_id_from_node(item, review_id, review_id_size) == 0) {
        return 0;
    }

    data = cJSON_GetObjectItemCaseSensitive(json, "data");
    if (shelf_service_copy_review_id_from_node(data, review_id, review_id_size) == 0) {
        return 0;
    }
    item = cJSON_GetObjectItemCaseSensitive(data, "reviewId");
    if (shelf_service_copy_review_id_from_node(item, review_id, review_id_size) == 0) {
        return 0;
    }
    item = cJSON_GetObjectItemCaseSensitive(data, "id");
    if (shelf_service_copy_review_id_from_node(item, review_id, review_id_size) == 0) {
        return 0;
    }
    if (cJSON_IsArray(data) && cJSON_GetArraySize(data) > 0) {
        cJSON *first = cJSON_GetArrayItem(data, 0);
        item = cJSON_GetObjectItemCaseSensitive(first, "reviewId");
        if (shelf_service_copy_review_id_from_node(item, review_id, review_id_size) == 0) {
            return 0;
        }
        item = cJSON_GetObjectItemCaseSensitive(first, "id");
        if (shelf_service_copy_review_id_from_node(item, review_id, review_id_size) == 0) {
            return 0;
        }
    }

    return -1;
}

int shelf_service_extract_review_id_response(const char *response,
                                             char *review_id,
                                             size_t review_id_size) {
    const char *start;
    const char *end;
    size_t len;
    cJSON *json = NULL;
    int rc = -1;

    if (!response || !review_id || review_id_size == 0) {
        return -1;
    }

    start = response;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    len = (size_t)(end - start);
    if (len == 0) {
        return -1;
    }

    if (*start == '{' || *start == '[' || *start == '"') {
        json = cJSON_ParseWithLength(start, len);
        if (!json) {
            return -1;
        }
        if (cJSON_IsString(json) && json->valuestring && json->valuestring[0]) {
            snprintf(review_id, review_id_size, "%s", json->valuestring);
            rc = 0;
        } else {
            rc = shelf_service_extract_review_id(json, review_id, review_id_size);
        }
        cJSON_Delete(json);
        return rc;
    }

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)start[i];

        if (!(isalnum(ch) || ch == '_' || ch == '-')) {
            return -1;
        }
    }
    if (len >= review_id_size) {
        return -1;
    }

    memcpy(review_id, start, len);
    review_id[len] = '\0';
    return 0;
}

static int shelf_service_resolve_review_id(ApiContext *ctx, const char *entry_id,
                                           char *review_id, size_t review_id_size) {
    Buffer buf = {0};
    char *escaped = NULL;
    char *url = NULL;
    int rc = -1;

    if (!entry_id || !*entry_id || !review_id || review_id_size == 0) {
        return -1;
    }
    if (shelf_service_review_id_override) {
        return shelf_service_review_id_override(ctx, entry_id, review_id, review_id_size);
    }
    if (!ctx) {
        return -1;
    }

    escaped = api_escape(ctx, entry_id);
    if (!escaped) {
        goto cleanup;
    }

    url = malloc(strlen(WEREAD_API_BASE_URL) + strlen("/reviewId?bookId=") + strlen(escaped) + 1);
    if (!url) {
        goto cleanup;
    }
    sprintf(url, "%s/reviewId?bookId=%s", WEREAD_API_BASE_URL, escaped);
    if (api_get(ctx, url, &buf) != 0) {
        goto cleanup;
    }
    rc = shelf_service_extract_review_id_response(buf.data, review_id, review_id_size);

cleanup:
    api_buffer_free(&buf);
    free(url);
    free(escaped);
    return rc;
}

static int shelf_service_build_article_target(ApiContext *ctx, const char *review_id,
                                              int font_size, char *target,
                                              size_t target_size) {
    char *escaped = NULL;

    if (!ctx || !review_id || !*review_id || !target || target_size == 0) {
        return -1;
    }
    escaped = api_escape(ctx, review_id);
    if (!escaped) {
        return -1;
    }
    snprintf(target, target_size, "%s/mpdetail?reviewId=%s&fs=%d",
             WEREAD_BASE_URL, escaped, font_size);
    free(escaped);
    return 0;
}

static int shelf_service_normalize_article_target(ApiContext *ctx,
                                                  const ShelfArticleSlotInfo *article_slot,
                                                  int font_size,
                                                  char *target,
                                                  size_t target_size) {
    char review_id[128];

    if (!ctx || !article_slot || !target || target_size == 0) {
        return -1;
    }
    if (article_slot->source_target && article_slot->source_target[0]) {
        snprintf(target, target_size, "%s", article_slot->source_target);
        return 0;
    }
    if (article_slot->review_id && article_slot->review_id[0]) {
        return shelf_service_build_article_target(ctx, article_slot->review_id, font_size,
                                                  target, target_size);
    }
    if (article_slot->entry_id &&
        shelf_service_resolve_review_id(ctx, article_slot->entry_id,
                                        review_id, sizeof(review_id)) == 0) {
        return shelf_service_build_article_target(ctx, review_id, font_size,
                                                  target, target_size);
    }
    return -1;
}

/*
 * Phase 4 maintainer note: shelf_service_prepare_resume() and
 * shelf_service_prepare_selected_open() are the shelf-to-reader bridge.
 * Keep loading/status strings and reader-target preparation centralized here
 * instead of rebuilding them in ui_flow_shelf.c.
 */
int shelf_service_prepare_resume(ApiContext *ctx, char *target, size_t target_size,
                                 int *font_size, char *loading_title,
                                 size_t loading_title_size, char *status,
                                 size_t status_size) {
    int resume_font_size = 3;

    if (!ctx || !target || !target_size || !font_size || !loading_title || !status) {
        return 0;
    }
    if (shelf_service_load_resume(ctx, target, target_size, &resume_font_size, NULL) != 0) {
        return 0;
    }

    *font_size = resume_font_size;
    snprintf(loading_title, loading_title_size,
             "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x89\x93\xE5\xBC\x80");
    snprintf(status, status_size,
             "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x81\xA2\xE5\xA4\x8D\xE9\x98\x85\xE8\xAF\xBB\xE4\xBD\x8D\xE7\xBD\xAE...");
    return 1;
}

int shelf_service_prepare_selected_open(cJSON *shelf_nuxt, int selected,
                                        char *target, size_t target_size,
                                        char *book_id, size_t book_id_size,
                                        char *loading_title, size_t loading_title_size,
                                        char *status, size_t status_size,
                                        char *shelf_status, size_t shelf_status_size) {
    cJSON *books;
    cJSON *book;
    cJSON *urls;
    const char *book_target;
    const char *selected_book_id;
    int source_index = -1;

    if (!shelf_nuxt || !target || !target_size || !book_id || !book_id_size ||
        !loading_title || !status || !shelf_status) {
        return 0;
    }

    books = shelf_books(shelf_nuxt);
    book = shelf_normal_book_at(shelf_nuxt, selected, &source_index);
    if (!books || !cJSON_IsArray(books) || !book || source_index < 0) {
        snprintf(shelf_status, shelf_status_size,
                 "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE6\x89\x80\xE9\x80\x89\xE4\xB9\xA6\xE7\xB1\x8D");
        return 0;
    }

    urls = shelf_reader_urls(shelf_nuxt);
    selected_book_id = json_get_string(book, "bookId");
    book_target = selected_book_id ?
        shelf_reader_target_for_entry_id(urls, selected_book_id) : NULL;
    if (!book_target) {
        book_target = shelf_service_reader_target(urls, source_index);
    }
    if (!book_target) {
        snprintf(shelf_status, shelf_status_size,
                 "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE6\x89\x80\xE9\x80\x89\xE4\xB9\xA6\xE7\xB1\x8D");
        fprintf(stderr,
                "shelf-open-book: missing target selected=%d source_index=%d bookId=%s\n",
                selected, source_index, selected_book_id ? selected_book_id : "(null)");
        return 0;
    }

    snprintf(target, target_size, "%s", book_target);
    if (selected_book_id && *selected_book_id) {
        snprintf(book_id, book_id_size, "%s", selected_book_id);
    } else {
        book_id[0] = '\0';
    }
    fprintf(stderr,
            "shelf-open-book: selected=%d source_index=%d bookId=%s target=%s\n",
            selected, source_index, book_id[0] ? book_id : "(null)", target);
    snprintf(loading_title, loading_title_size,
             "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x89\x93\xE5\xBC\x80");
    snprintf(status, status_size,
             "\xE6\xAD\xA3\xE5\x9C\xA8\xE5\x8A\xA0\xE8\xBD\xBD\xE7\xAB\xA0\xE8\x8A\x82...");
    shelf_status[0] = '\0';
    return 1;
}

int shelf_service_prepare_article_open(ApiContext *ctx, cJSON *shelf_nuxt, int font_size,
                                       char *target, size_t target_size,
                                       char *loading_title, size_t loading_title_size,
                                       char *status, size_t status_size,
                                       char *shelf_status, size_t shelf_status_size) {
    ShelfArticleSlotInfo article_slot;

    if (!ctx || !shelf_nuxt || !target || target_size == 0 || !loading_title || !status ||
        !shelf_status) {
        return 0;
    }

    if (!shelf_article_slot_info(shelf_nuxt, &article_slot) ||
        !article_slot.entry_id || !article_slot.entry_id[0]) {
        snprintf(shelf_status, shelf_status_size, "文章入口暂不可用");
        fprintf(stderr, "shelf-open-article: article slot unavailable\n");
        return 0;
    }

    if (shelf_service_normalize_article_target(ctx, &article_slot, font_size,
                                               target, target_size) != 0) {
        snprintf(shelf_status, shelf_status_size, "无法解析文章入口");
        fprintf(stderr,
                "shelf-open-article: failed entryId=%s sourceTarget=%s inlineReviewId=%s\n",
                article_slot.entry_id ? article_slot.entry_id : "(null)",
                article_slot.source_target ? article_slot.source_target : "(null)",
                article_slot.review_id ? article_slot.review_id : "(null)");
        return 0;
    }

    fprintf(stderr,
            "shelf-open-article: entryId=%s sourceTarget=%s inlineReviewId=%s target=%s\n",
            article_slot.entry_id ? article_slot.entry_id : "(null)",
            article_slot.source_target ? article_slot.source_target : "(null)",
            article_slot.review_id ? article_slot.review_id : "(null)",
            target);
    snprintf(loading_title, loading_title_size,
             "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x89\x93\xE5\xBC\x80");
    snprintf(status, status_size, "正在加载文章...");
    shelf_status[0] = '\0';
    return 1;
}

int shelf_service_download_cover_to_cache(const char *data_dir, const char *ca_file,
                                          const char *url, const char *path) {
    ApiContext ctx;
    Buffer buf = {0};
    FILE *fp = NULL;
    int rc = -1;

    if (!url || !*url || !path || !*path) {
        return -1;
    }
    if (shelf_service_init_worker_context(&ctx, data_dir, ca_file) != 0) {
        return -1;
    }
    if (api_download(&ctx, url, &buf) != 0) {
        goto cleanup;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        goto cleanup;
    }
    if (fwrite(buf.data, 1, buf.size, fp) != buf.size) {
        goto cleanup;
    }
    rc = 0;

cleanup:
    if (fp) {
        fclose(fp);
    }
    api_buffer_free(&buf);
    api_cleanup(&ctx);
    return rc;
}
