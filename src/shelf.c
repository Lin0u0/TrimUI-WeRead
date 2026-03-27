#include <stdio.h>
#include <stdlib.h>
#include "shelf.h"
#include "json.h"
#include "state.h"

static int shelf_extend_from_api(ApiContext *ctx, cJSON *nuxt) {
    cJSON *shelf = json_get_path(nuxt, "state.shelf");
    cJSON *books = shelf_books(nuxt);
    cJSON *urls = shelf_reader_urls(nuxt);
    int total_count;
    int kk_idx;

    if (!shelf || !cJSON_IsObject(shelf) || !books || !cJSON_IsArray(books) ||
        !urls || !cJSON_IsArray(urls)) {
        return -1;
    }

    total_count = json_get_int(shelf, "totalCount", cJSON_GetArraySize(books));
    kk_idx = json_get_int(shelf, "kkIdx", -1);

    while (cJSON_GetArraySize(books) < total_count) {
        int loaded = cJSON_GetArraySize(books);
        char url[256];
        Buffer buf = {0};
        cJSON *page = NULL;
        cJSON *page_books;
        cJSON *page_urls;
        int page_count;

        snprintf(url, sizeof(url),
                 "https://weread.qq.com/wrwebsimplenjlogic/api/shelfloadmore?idx=%d&kkIdx=%d",
                 loaded, kk_idx);
        if (api_get(ctx, url, &buf) != 0) {
            api_buffer_free(&buf);
            break;
        }

        page = cJSON_Parse(buf.data);
        api_buffer_free(&buf);
        if (!page || !cJSON_IsObject(page)) {
            cJSON_Delete(page);
            break;
        }

        page_books = cJSON_GetObjectItemCaseSensitive(page, "books");
        page_urls = cJSON_GetObjectItemCaseSensitive(page, "bookReaderUrls");
        if (!page_books || !cJSON_IsArray(page_books) || !page_urls || !cJSON_IsArray(page_urls)) {
            cJSON_Delete(page);
            break;
        }

        page_count = cJSON_GetArraySize(page_books);
        if (page_count <= 0) {
            cJSON_Delete(page);
            break;
        }

        for (int i = 0; i < page_count; i++) {
            cJSON *book = cJSON_Duplicate(cJSON_GetArrayItem(page_books, i), 1);
            cJSON *reader = cJSON_Duplicate(cJSON_GetArrayItem(page_urls, i), 1);
            if (!book || !reader) {
                cJSON_Delete(book);
                cJSON_Delete(reader);
                cJSON_Delete(page);
                return 0;
            }
            cJSON_AddItemToArray(books, book);
            cJSON_AddItemToArray(urls, reader);
        }

        total_count = json_get_int(page, "totalCount", total_count);
        kk_idx = json_get_int(page, "kkIdx", kk_idx);
        cJSON_ReplaceItemInObjectCaseSensitive(shelf, "totalCount", cJSON_CreateNumber(total_count));
        cJSON_ReplaceItemInObjectCaseSensitive(shelf, "kkIdx", cJSON_CreateNumber(kk_idx));
        cJSON_Delete(page);
    }

    return 0;
}

const char *shelf_reader_target(cJSON *urls, int index) {
    cJSON *reader_item;

    if (!urls || !cJSON_IsArray(urls)) {
        return NULL;
    }
    reader_item = cJSON_GetArrayItem(urls, index);
    if (!reader_item) {
        return NULL;
    }
    if (cJSON_IsString(reader_item)) {
        return reader_item->valuestring;
    }
    if (cJSON_IsObject(reader_item)) {
        return json_get_string(reader_item, "param");
    }
    return NULL;
}

static int shelf_print_from_nuxt(cJSON *nuxt) {
    cJSON *books = shelf_books(nuxt);
    cJSON *urls = shelf_reader_urls(nuxt);
    int count;

    if (!nuxt) {
        return -1;
    }

    books = json_get_path(nuxt, "state.shelf.books");
    urls = json_get_path(nuxt, "state.shelf.bookReaderUrls");
    if (!books || !cJSON_IsArray(books)) {
        const char *route = json_get_string(nuxt, "routePath");
        fprintf(stderr, "Shelf data not available");
        if (route) {
            fprintf(stderr, " (route=%s)", route);
        }
        fprintf(stderr, ". Run login first.\n");
        return -1;
    }

    count = cJSON_GetArraySize(books);
    printf("Shelf contains %d books\n", count);
    for (int i = 0; i < count; i++) {
        cJSON *book = cJSON_GetArrayItem(books, i);
        const char *title = json_get_string(book, "title");
        const char *author = json_get_string(book, "author");
        const char *book_id = json_get_string(book, "bookId");
        const char *reader = shelf_reader_target(urls, i);

        printf("%2d. %s", i + 1, title ? title : "(untitled)");
        if (author) {
            printf(" - %s", author);
        }
        if (book_id) {
            printf(" [bookId=%s]", book_id);
        }
        printf("\n");
        if (reader) {
            printf("    reader=%s\n", reader);
        }
    }

    return 0;
}

cJSON *shelf_books(cJSON *nuxt) {
    return json_get_path(nuxt, "state.shelf.books");
}

cJSON *shelf_reader_urls(cJSON *nuxt) {
    return json_get_path(nuxt, "state.shelf.bookReaderUrls");
}

cJSON *shelf_load(ApiContext *ctx, int allow_cache, int *from_cache) {
    cJSON *nuxt = api_fetch_nuxt(ctx, WEREAD_BASE_URL "/shelf");
    if (from_cache) {
        *from_cache = 0;
    }
    if (nuxt) {
        shelf_extend_from_api(ctx, nuxt);
        state_write_json(ctx, "shelf.json", nuxt);
        return nuxt;
    }
    if (!allow_cache) {
        return NULL;
    }
    nuxt = state_read_json(ctx, "shelf.json");
    if (nuxt && from_cache) {
        *from_cache = 1;
    }
    return nuxt;
}

int shelf_print(ApiContext *ctx) {
    cJSON *nuxt = shelf_load(ctx, 0, NULL);
    int rc;

    if (!nuxt) {
        return -1;
    }
    rc = shelf_print_from_nuxt(nuxt);
    cJSON_Delete(nuxt);
    return rc;
}

int shelf_print_cached(ApiContext *ctx) {
    cJSON *nuxt = state_read_json(ctx, "shelf.json");
    int rc;

    if (!nuxt) {
        fprintf(stderr, "No cached shelf data found in %s\n", ctx->state_dir);
        return -1;
    }
    rc = shelf_print_from_nuxt(nuxt);
    cJSON_Delete(nuxt);
    return rc;
}
