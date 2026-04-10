#include <stdio.h>
#include <string.h>
#include "json.h"
#include "reader_state.h"
#include "shelf_service.h"
#include "shelf_state.h"

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

    if (!shelf_nuxt || !target || !target_size || !book_id || !book_id_size ||
        !loading_title || !status || !shelf_status) {
        return 0;
    }

    books = shelf_books(shelf_nuxt);
    if (!books || !cJSON_IsArray(books) || selected < 0 ||
        selected >= cJSON_GetArraySize(books)) {
        snprintf(shelf_status, shelf_status_size,
                 "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE6\x89\x80\xE9\x80\x89\xE4\xB9\xA6\xE7\xB1\x8D");
        return 0;
    }

    book = cJSON_GetArrayItem(books, selected);
    urls = shelf_reader_urls(shelf_nuxt);
    book_target = shelf_service_reader_target(urls, selected);
    selected_book_id = json_get_string(book, "bookId");
    if (!book_target) {
        snprintf(shelf_status, shelf_status_size,
                 "\xE6\x97\xA0\xE6\xB3\x95\xE6\x89\x93\xE5\xBC\x80\xE6\x89\x80\xE9\x80\x89\xE4\xB9\xA6\xE7\xB1\x8D");
        return 0;
    }

    snprintf(target, target_size, "%s", book_target);
    if (selected_book_id && *selected_book_id) {
        snprintf(book_id, book_id_size, "%s", selected_book_id);
    } else {
        book_id[0] = '\0';
    }
    snprintf(loading_title, loading_title_size,
             "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x89\x93\xE5\xBC\x80");
    snprintf(status, status_size,
             "\xE6\xAD\xA3\xE5\x9C\xA8\xE5\x8A\xA0\xE8\xBD\xBD\xE7\xAB\xA0\xE8\x8A\x82...");
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
