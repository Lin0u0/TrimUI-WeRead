#ifndef SHELF_H
#define SHELF_H

#include "api.h"

typedef struct {
    int available;
    int source_index;
    const char *entry_id;
    const char *title;
    const char *cover_url;
    const char *source_target;
    const char *review_id;
} ShelfArticleSlotInfo;

int shelf_print(ApiContext *ctx);
int shelf_print_cached(ApiContext *ctx);
cJSON *shelf_load(ApiContext *ctx, int allow_cache, int *from_cache);
cJSON *shelf_books(cJSON *nuxt);
cJSON *shelf_reader_urls(cJSON *nuxt);
const char *shelf_reader_target(cJSON *urls, int index);
cJSON *shelf_reader_item_for_entry_id(cJSON *urls, const char *entry_id);
const char *shelf_reader_target_for_entry_id(cJSON *urls, const char *entry_id);
int shelf_entry_is_article(cJSON *book, cJSON *reader_item);
int shelf_item_count(cJSON *nuxt);
cJSON *shelf_item_at(cJSON *nuxt, int index, int *source_index_out);
int shelf_article_count(cJSON *nuxt);
cJSON *shelf_article_at(cJSON *nuxt, int article_index, int *source_index_out);
int shelf_normal_book_count(cJSON *nuxt);
cJSON *shelf_normal_book_at(cJSON *nuxt, int normal_index, int *source_index_out);
int shelf_article_slot_info(cJSON *nuxt, ShelfArticleSlotInfo *info);

#endif
