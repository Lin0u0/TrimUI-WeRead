#ifndef SHELF_SERVICE_H
#define SHELF_SERVICE_H

#include "shelf.h"

int shelf_service_print(ApiContext *ctx);
int shelf_service_print_cached(ApiContext *ctx);
cJSON *shelf_service_load(ApiContext *ctx, int allow_cache, int *from_cache);
int shelf_service_load_resume(ApiContext *ctx, char *target, size_t target_size,
                              int *font_size, int *content_font_size);
const char *shelf_service_reader_target(cJSON *urls, int index);
int shelf_service_prepare_resume(ApiContext *ctx, char *target, size_t target_size,
                                 int *font_size, char *loading_title,
                                 size_t loading_title_size, char *status,
                                 size_t status_size);
int shelf_service_prepare_selected_open(cJSON *shelf_nuxt, int selected,
                                        char *target, size_t target_size,
                                        char *book_id, size_t book_id_size,
                                        char *loading_title, size_t loading_title_size,
                                        char *status, size_t status_size,
                                        char *shelf_status, size_t shelf_status_size);
int shelf_service_download_cover_to_cache(const char *data_dir, const char *ca_file,
                                          const char *url, const char *path);

#endif
