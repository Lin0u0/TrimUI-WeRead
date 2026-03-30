#ifndef STATE_H
#define STATE_H

#include "api.h"

int state_write_json(ApiContext *ctx, const char *name, cJSON *json);
cJSON *state_read_json(ApiContext *ctx, const char *name);
int state_save_last_reader(ApiContext *ctx, const char *target, int font_size, int content_font_size);
int state_load_last_reader(ApiContext *ctx, char *target, size_t target_size, int *font_size,
                           int *content_font_size);
int state_save_reader_position(ApiContext *ctx, const char *book_id, const char *source_target,
                               const char *target, int font_size, int content_font_size,
                               int current_page);
int state_load_reader_position(ApiContext *ctx, const char *book_id, const char *source_target,
                               char *target, size_t target_size, int *font_size,
                               int *content_font_size,
                               int *current_page);
int state_load_reader_position_by_book_id(ApiContext *ctx, const char *book_id,
                                          char *source_target, size_t source_target_size,
                                          char *target, size_t target_size,
                                          int *font_size, int *content_font_size,
                                          int *current_page);
int state_save_dark_mode(ApiContext *ctx, int dark_mode);
int state_load_dark_mode(ApiContext *ctx);

#endif
