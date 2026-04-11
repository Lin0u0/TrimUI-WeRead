#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shelf.h"
#include "json.h"
#include "shelf_state.h"

static cJSON *shelf_reader_item(cJSON *urls, int index) {
    if (!urls || !cJSON_IsArray(urls)) {
        return NULL;
    }
    return cJSON_GetArrayItem(urls, index);
}

static const char *shelf_reader_item_entry_id(cJSON *reader_item) {
    const char *entry_id;

    if (!reader_item || !cJSON_IsObject(reader_item)) {
        return NULL;
    }

    entry_id = json_get_string(reader_item, "bId");
    if (entry_id && *entry_id) {
        return entry_id;
    }
    entry_id = json_get_string(reader_item, "bookId");
    if (entry_id && *entry_id) {
        return entry_id;
    }
    entry_id = json_get_string(reader_item, "id");
    if (entry_id && *entry_id) {
        return entry_id;
    }
    return NULL;
}

static const char *shelf_entry_id(cJSON *book, cJSON *reader_item) {
    const char *book_id = json_get_string(book, "bookId");
    const char *reader_book_id = json_get_string(reader_item, "bId");

    if (book_id && *book_id) {
        return book_id;
    }
    if (reader_book_id && *reader_book_id) {
        return reader_book_id;
    }
    return NULL;
}

static const char *shelf_entry_review_id(cJSON *book, cJSON *reader_item) {
    const char *review_id = json_get_string(book, "reviewId");

    if (review_id && *review_id) {
        return review_id;
    }
    review_id = json_get_string(reader_item, "reviewId");
    if (review_id && *review_id) {
        return review_id;
    }
    review_id = json_get_string(book, "id");
    if (review_id && *review_id) {
        return review_id;
    }
    review_id = json_get_string(reader_item, "id");
    if (review_id && *review_id) {
        return review_id;
    }
    return NULL;
}

static const char *shelf_entry_cover_url(cJSON *book) {
    static const char *paths[] = {
        "cover",
        "coverUrl",
        "cover_url",
        "coverMiddle",
        "coverMid",
        "coverLarge",
        "coverSmall",
        "bookCover",
        "bookInfo.cover",
        "bookInfo.coverUrl",
        "bookInfo.coverLarge",
        "bookInfo.coverMiddle",
        "bookInfo.coverSmall",
        "bookInfo.bookCover",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        const char *value = json_get_string(book, paths[i]);
        if (value && *value) {
            return value;
        }
    }
    return NULL;
}

static int shelf_entry_id_is_article(const char *entry_id) {
    return entry_id && strncmp(entry_id, "MP", 2) == 0;
}

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

cJSON *shelf_reader_item_for_entry_id(cJSON *urls, const char *entry_id) {
    int count;

    if (!urls || !cJSON_IsArray(urls) || !entry_id || !*entry_id) {
        return NULL;
    }

    count = cJSON_GetArraySize(urls);
    for (int i = 0; i < count; i++) {
        cJSON *reader_item = shelf_reader_item(urls, i);
        const char *reader_entry_id = shelf_reader_item_entry_id(reader_item);

        if (reader_entry_id && strcmp(reader_entry_id, entry_id) == 0) {
            return reader_item;
        }
    }

    return NULL;
}

const char *shelf_reader_target_for_entry_id(cJSON *urls, const char *entry_id) {
    cJSON *reader_item = shelf_reader_item_for_entry_id(urls, entry_id);

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

int shelf_entry_is_article(cJSON *book, cJSON *reader_item) {
    return shelf_entry_id_is_article(shelf_entry_id(book, reader_item));
}

int shelf_normal_book_count(cJSON *nuxt) {
    cJSON *books = shelf_books(nuxt);
    cJSON *urls = shelf_reader_urls(nuxt);
    int count = 0;
    int total;

    if (!books || !cJSON_IsArray(books)) {
        return 0;
    }

    total = cJSON_GetArraySize(books);
    for (int i = 0; i < total; i++) {
        cJSON *book = cJSON_GetArrayItem(books, i);
        cJSON *reader_item = shelf_reader_item(urls, i);

        if (!shelf_entry_is_article(book, reader_item)) {
            count++;
        }
    }
    return count;
}

cJSON *shelf_normal_book_at(cJSON *nuxt, int normal_index, int *source_index_out) {
    cJSON *books = shelf_books(nuxt);
    cJSON *urls = shelf_reader_urls(nuxt);
    int visible_index = 0;
    int total;

    if (source_index_out) {
        *source_index_out = -1;
    }
    if (!books || !cJSON_IsArray(books) || normal_index < 0) {
        return NULL;
    }

    total = cJSON_GetArraySize(books);
    for (int i = 0; i < total; i++) {
        cJSON *book = cJSON_GetArrayItem(books, i);
        cJSON *reader_item = shelf_reader_item(urls, i);

        if (shelf_entry_is_article(book, reader_item)) {
            continue;
        }
        if (visible_index == normal_index) {
            if (source_index_out) {
                *source_index_out = i;
            }
            return book;
        }
        visible_index++;
    }

    return NULL;
}

int shelf_article_slot_info(cJSON *nuxt, ShelfArticleSlotInfo *info) {
    cJSON *books = shelf_books(nuxt);
    cJSON *urls = shelf_reader_urls(nuxt);
    int total;

    if (info) {
        memset(info, 0, sizeof(*info));
        info->source_index = -1;
    }
    if (!books || !cJSON_IsArray(books)) {
        return 0;
    }

    total = cJSON_GetArraySize(books);
    for (int i = 0; i < total; i++) {
        cJSON *book = cJSON_GetArrayItem(books, i);
        cJSON *reader_item = shelf_reader_item(urls, i);
        const char *entry_id = shelf_entry_id(book, reader_item);
        cJSON *matched_reader_item = shelf_reader_item_for_entry_id(urls, entry_id);

        if (!shelf_entry_is_article(book, reader_item)) {
            continue;
        }
        if (info) {
            info->available = 1;
            info->source_index = i;
            info->entry_id = entry_id;
            info->title = json_get_string(book, "title");
            info->cover_url = shelf_entry_cover_url(book);
            info->source_target = matched_reader_item ?
                shelf_reader_target_for_entry_id(urls, entry_id) :
                shelf_reader_target(urls, i);
            info->review_id = shelf_entry_review_id(book,
                                                    matched_reader_item ?
                                                    matched_reader_item :
                                                    reader_item);
        }
        return 1;
    }

    return 0;
}

static int shelf_print_from_nuxt(cJSON *nuxt) {
    cJSON *books = shelf_books(nuxt);
    cJSON *urls = shelf_reader_urls(nuxt);
    ShelfArticleSlotInfo article_slot;
    int count;
    int normal_count;
    int printed_index = 0;

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
    normal_count = shelf_normal_book_count(nuxt);
    printf("Shelf contains %d books\n", normal_count);
    if (shelf_article_slot_info(nuxt, &article_slot)) {
        printf("Article slot: %s", article_slot.title ? article_slot.title : "(untitled article)");
        if (article_slot.entry_id) {
            printf(" [entryId=%s]", article_slot.entry_id);
        }
        printf("\n");
    }
    for (int i = 0; i < count; i++) {
        cJSON *book = cJSON_GetArrayItem(books, i);
        const char *title = json_get_string(book, "title");
        const char *author = json_get_string(book, "author");
        const char *book_id = json_get_string(book, "bookId");
        const char *reader = shelf_reader_target(urls, i);
        cJSON *reader_item = shelf_reader_item(urls, i);

        if (shelf_entry_is_article(book, reader_item)) {
            continue;
        }
        printed_index++;

        printf("%2d. %s", printed_index, title ? title : "(untitled)");
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
        shelf_state_save_cache(ctx, nuxt);
        return nuxt;
    }
    if (!allow_cache) {
        return NULL;
    }
    nuxt = shelf_state_load_cache(ctx);
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
    cJSON *nuxt = shelf_state_load_cache(ctx);
    int rc;

    if (!nuxt) {
        fprintf(stderr, "No cached shelf data found in %s\n", ctx->state_dir);
        return -1;
    }
    rc = shelf_print_from_nuxt(nuxt);
    cJSON_Delete(nuxt);
    return rc;
}
